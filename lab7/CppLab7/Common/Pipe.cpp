#include "Pipe.h"

#include "Text.h"

#include <algorithm>
#include <cwctype>
#include <format>
#include <memory>
#include <string_view>
#include <utility>

namespace {

constexpr DWORD kPipeBufferBytes = 4096;
constexpr DWORD kPipeTimeoutMs = 5000;
constexpr DWORD kStopTimeoutMs = 3000;
constexpr DWORD kBusyWaitMs = 2000;
constexpr int kMaxConnectAttempts = 3;

std::wstring TrimWhitespace(std::wstring_view text) {
    size_t start = 0;
    while (start < text.size() && std::iswspace(text[start]) != 0) ++start;
    size_t end = text.size();
    while (end > start && std::iswspace(text[end - 1]) != 0) --end;
    return std::wstring(text.substr(start, end - start));
}

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](wchar_t ca, wchar_t cb) {
        return std::towlower(ca) == std::towlower(cb);
    });
}

HANDLE CreatePipeInstance(const std::wstring& path) {
    return CreateNamedPipeW(
        path.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        kPipeBufferBytes,
        kPipeBufferBytes,
        kPipeTimeoutMs,
        nullptr);
}

enum class OverlappedWait { Completed, Stopped, Failed };

OverlappedWait WaitOverlappedIo(HANDLE pipe, HANDLE stopEvent, HANDLE ovEvent, OVERLAPPED& ov,
    DWORD& bytesTransferred, DWORD& error) {
    HANDLE waits[2] = {stopEvent, ovEvent};
    const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (waitResult == WAIT_OBJECT_0) {
        CancelIoEx(pipe, &ov);
        return OverlappedWait::Stopped;
    }
    if (waitResult != WAIT_OBJECT_0 + 1) {
        error = GetLastError();
        return OverlappedWait::Failed;
    }
    if (!GetOverlappedResult(pipe, &ov, &bytesTransferred, FALSE)) {
        error = GetLastError();
        return error == ERROR_OPERATION_ABORTED ? OverlappedWait::Stopped : OverlappedWait::Failed;
    }
    return OverlappedWait::Completed;
}

bool ReadOverlappedMessage(HANDLE pipe, HANDLE stopEvent, HANDLE ovEvent, std::string& out,
    DWORD& outError, bool& stopped) {
    out.clear();
    outError = ERROR_SUCCESS;
    stopped = false;

    for (;;) {
        ResetEvent(ovEvent);
        OVERLAPPED ov{};
        ov.hEvent = ovEvent;

        char chunk[kPipeBufferBytes];
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(pipe, chunk, sizeof(chunk), &bytesRead, &ov);
        DWORD err = ok ? ERROR_SUCCESS : GetLastError();

        if (!ok && err == ERROR_IO_PENDING) {
            const OverlappedWait wait = WaitOverlappedIo(pipe, stopEvent, ovEvent, ov, bytesRead, err);
            if (wait == OverlappedWait::Stopped) {
                stopped = true;
                return false;
            }
            ok = (wait == OverlappedWait::Completed) ? TRUE : FALSE;
        }

        if (bytesRead > 0) {
            out.append(chunk, bytesRead);
        }
        if (ok) {
            return true;
        }
        if (err == ERROR_MORE_DATA) {
            continue;
        }
        if (err == ERROR_BROKEN_PIPE) {
            return !out.empty();
        }
        outError = err;
        return false;
    }
}

bool WriteOverlappedMessage(HANDLE pipe, HANDLE stopEvent, HANDLE ovEvent, const std::string& data,
    DWORD& outError, bool& stopped) {
    outError = ERROR_SUCCESS;
    stopped = false;

    ResetEvent(ovEvent);
    OVERLAPPED ov{};
    ov.hEvent = ovEvent;

    DWORD bytesWritten = 0;
    const DWORD toWrite = static_cast<DWORD>(data.size());
    BOOL ok = WriteFile(pipe, data.data(), toWrite, &bytesWritten, &ov);
    DWORD err = ok ? ERROR_SUCCESS : GetLastError();

    if (!ok && err == ERROR_IO_PENDING) {
        const OverlappedWait wait = WaitOverlappedIo(pipe, stopEvent, ovEvent, ov, bytesWritten, err);
        if (wait == OverlappedWait::Stopped) {
            stopped = true;
            return false;
        }
        ok = (wait == OverlappedWait::Completed) ? TRUE : FALSE;
    }

    if (!ok) {
        outError = err;
        return false;
    }
    if (bytesWritten != toWrite) {
        outError = ERROR_WRITE_FAULT;
        return false;
    }
    return true;
}

}

struct PipeServer::StartContext {
    PipeServer* server;
    HANDLE firstPipe;
};

std::wstring PipePath(const std::wstring& host, const std::wstring& pipeName) {
    return L"\\\\" + host + L"\\pipe\\" + pipeName;
}

std::wstring HostFromCommandLine(const wchar_t* commandLine) {
    if (!commandLine) return kDefaultHost;
    std::wstring host = TrimWhitespace(commandLine);
    if (host.empty() || EqualsIgnoreCase(host, L"localhost")) return kDefaultHost;
    return host;
}

std::wstring DescribeWin32Error(DWORD code) {
    if (code == ERROR_FILE_NOT_FOUND) {
        return std::format(L"Канал не знайдено (сервер не запущено) [код {}]", code);
    }
    if (code == ERROR_PIPE_BUSY) {
        return std::format(L"Канал зайнятий, спробуйте ще раз [код {}]", code);
    }

    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || !buffer) {
        return std::format(L"Помилка Win32 [код {}]", code);
    }

    std::wstring text = TrimWhitespace(std::wstring_view(buffer, length));
    LocalFree(buffer);
    return std::format(L"{} [код {}]", text, code);
}

PipeServer::~PipeServer() {
    Stop();
}

DWORD PipeServer::Start(HWND notifyWnd, const std::wstring& pipeName) {
    Stop();

    const std::wstring path = PipePath(kDefaultHost, pipeName);
    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent) {
        return GetLastError();
    }

    HANDLE firstPipe = CreatePipeInstance(path);
    if (firstPipe == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        CloseHandle(stopEvent);
        return err;
    }

    notifyWnd_ = notifyWnd;
    path_ = path;
    stopEvent_ = stopEvent;

    auto context = std::make_unique<StartContext>(StartContext{this, firstPipe});
    acceptThread_ = CreateThread(nullptr, 0, &PipeServer::ThreadEntry, context.get(), 0, nullptr);
    if (!acceptThread_) {
        const DWORD err = GetLastError();
        CloseHandle(firstPipe);
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        path_.clear();
        return err;
    }
    context.release();
    return ERROR_SUCCESS;
}

void PipeServer::Stop() {
    if (stopEvent_) {
        SetEvent(stopEvent_);
    }
    if (acceptThread_) {
        WaitForSingleObject(acceptThread_, kStopTimeoutMs);
        CloseHandle(acceptThread_);
        acceptThread_ = nullptr;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    path_.clear();
}

bool PipeServer::IsRunning() const {
    return acceptThread_ != nullptr;
}

const std::wstring& PipeServer::Path() const {
    return path_;
}

std::vector<PipeServer::Event> PipeServer::DrainEvents() {
    std::lock_guard lock(eventsMutex_);
    std::vector<Event> drained;
    drained.swap(events_);
    return drained;
}

DWORD WINAPI PipeServer::ThreadEntry(LPVOID param) {
    std::unique_ptr<StartContext> context(static_cast<StartContext*>(param));
    context->server->RunLoop(context->firstPipe);
    return 0;
}

void PipeServer::PushEvent(bool isError, std::wstring text) {
    {
        std::lock_guard lock(eventsMutex_);
        events_.push_back(Event{isError, std::move(text)});
    }
    if (notifyWnd_) {
        PostMessageW(notifyWnd_, kPipeMessage, 0, 0);
    }
}

void PipeServer::RunLoop(HANDLE firstPipe) {
    HANDLE pendingPipe = firstPipe;

    while (WaitForSingleObject(stopEvent_, 0) == WAIT_TIMEOUT) {
        HANDLE hPipe = pendingPipe != INVALID_HANDLE_VALUE
            ? std::exchange(pendingPipe, INVALID_HANDLE_VALUE)
            : CreatePipeInstance(path_);
        if (hPipe == INVALID_HANDLE_VALUE) {
            PushEvent(true, DescribeWin32Error(GetLastError()));
            break;
        }

        HANDLE ovEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ovEvent) {
            PushEvent(true, DescribeWin32Error(GetLastError()));
            CloseHandle(hPipe);
            break;
        }

        OVERLAPPED ov{};
        ov.hEvent = ovEvent;
        BOOL connected = ConnectNamedPipe(hPipe, &ov);
        DWORD connectErr = connected ? ERROR_SUCCESS : GetLastError();

        if (!connected && connectErr == ERROR_IO_PENDING) {
            DWORD dummy = 0;
            const OverlappedWait wait = WaitOverlappedIo(hPipe, stopEvent_, ovEvent, ov, dummy, connectErr);
            if (wait == OverlappedWait::Stopped) {
                CloseHandle(ovEvent);
                CloseHandle(hPipe);
                break;
            }
            connected = (wait == OverlappedWait::Completed) ? TRUE : FALSE;
        } else if (!connected && connectErr == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }

        if (!connected) {
            PushEvent(true, DescribeWin32Error(connectErr));
            CloseHandle(ovEvent);
            CloseHandle(hPipe);
            continue;
        }

        std::string payload;
        DWORD ioErr = ERROR_SUCCESS;
        bool stopped = false;
        const bool readOk = ReadOverlappedMessage(hPipe, stopEvent_, ovEvent, payload, ioErr, stopped);
        if (stopped) {
            DisconnectNamedPipe(hPipe);
            CloseHandle(ovEvent);
            CloseHandle(hPipe);
            break;
        }

        if (readOk) {
            while (!payload.empty() && payload.back() == '\0') {
                payload.pop_back();
            }
            if (!payload.empty()) {
                PushEvent(false, Widen(payload));
                const std::string reply = Narrow(std::format(L"Прийнято ({} байт)", payload.size()));
                if (WriteOverlappedMessage(hPipe, stopEvent_, ovEvent, reply, ioErr, stopped)) {
                    FlushFileBuffers(hPipe);
                } else if (!stopped && ioErr != ERROR_SUCCESS) {
                    PushEvent(true, DescribeWin32Error(ioErr));
                }
            }
        } else if (ioErr != ERROR_SUCCESS) {
            PushEvent(true, DescribeWin32Error(ioErr));
        }

        DisconnectNamedPipe(hPipe);
        CloseHandle(ovEvent);
        CloseHandle(hPipe);

        if (stopped) break;
    }
}

PipeClient::SendResult PipeClient::Send(const std::wstring& path, const std::wstring& text) {
    SendResult result;
    HANDLE pipe = INVALID_HANDLE_VALUE;

    for (int attempt = 0; attempt < kMaxConnectAttempts; ++attempt) {
        pipe = CreateFileW(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;

        result.error = GetLastError();
        if (result.error != ERROR_PIPE_BUSY || attempt + 1 >= kMaxConnectAttempts) {
            return result;
        }
        WaitNamedPipeW(path.c_str(), kBusyWaitMs);
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
        result.error = GetLastError();
        CloseHandle(pipe);
        return result;
    }

    const std::string utf8 = Narrow(text);
    DWORD bytesWritten = 0;
    const DWORD toWrite = static_cast<DWORD>(utf8.size());
    if (!WriteFile(pipe, utf8.data(), toWrite, &bytesWritten, nullptr) || bytesWritten != toWrite) {
        result.error = GetLastError();
        if (result.error == ERROR_SUCCESS) result.error = ERROR_WRITE_FAULT;
        CloseHandle(pipe);
        return result;
    }

    std::string replyUtf8;
    for (;;) {
        char chunk[kPipeBufferBytes];
        DWORD bytesRead = 0;
        const BOOL ok = ReadFile(pipe, chunk, sizeof(chunk), &bytesRead, nullptr);
        if (bytesRead > 0) {
            replyUtf8.append(chunk, bytesRead);
        }
        if (ok) break;

        const DWORD err = GetLastError();
        if (err == ERROR_MORE_DATA) continue;
        if (err == ERROR_BROKEN_PIPE && !replyUtf8.empty()) break;
        if (err != ERROR_BROKEN_PIPE) {
            result.error = err;
            CloseHandle(pipe);
            return result;
        }
        break;
    }

    CloseHandle(pipe);
    result.error = ERROR_SUCCESS;
    result.reply = Widen(replyUtf8);
    return result;
}
