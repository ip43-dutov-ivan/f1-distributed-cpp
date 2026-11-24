#include "Mailslot.h"

#include "Text.h"

#include <algorithm>
#include <cwctype>
#include <format>
#include <string_view>

namespace {

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

}

std::wstring MailslotPath(const std::wstring& host, const std::wstring& slotName) {
    return L"\\\\" + host + L"\\mailslot\\" + slotName;
}

std::wstring HostFromCommandLine(const wchar_t* commandLine) {
    if (!commandLine) return kDefaultHost;
    std::wstring host = TrimWhitespace(commandLine);
    if (host.empty() || EqualsIgnoreCase(host, L"localhost")) return kDefaultHost;
    return host;
}

std::wstring DescribeWin32Error(DWORD code) {
    if (code == ERROR_FILE_NOT_FOUND) {
        return std::format(L"Поштова скринька не знайдена (сервер не запущено) [код {}]", code);
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

MailslotServer::~MailslotServer() {
    Close();
}

DWORD MailslotServer::Open(const std::wstring& slotName) {
    Close();
    const std::wstring path = MailslotPath(kDefaultHost, slotName);
    HANDLE created = CreateMailslotW(path.c_str(), 0, 0, nullptr);
    if (created == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    handle_ = created;
    path_ = path;
    return ERROR_SUCCESS;
}

void MailslotServer::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    path_.clear();
}

bool MailslotServer::IsOpen() const {
    return handle_ != INVALID_HANDLE_VALUE;
}

const std::wstring& MailslotServer::Path() const {
    return path_;
}

MailslotServer::PollResult MailslotServer::ReadAll() {
    PollResult result;
    while (IsOpen()) {
        DWORD nextSize = 0;
        DWORD messageCount = 0;
        if (!GetMailslotInfo(handle_, nullptr, &nextSize, &messageCount, nullptr)) {
            result.error = GetLastError();
            break;
        }
        if (nextSize == MAILSLOT_NO_MESSAGE || messageCount == 0) {
            break;
        }

        std::string buffer(nextSize, '\0');
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, buffer.data(), nextSize, &bytesRead, nullptr)) {
            result.error = GetLastError();
            break;
        }
        buffer.resize(bytesRead);
        while (!buffer.empty() && buffer.back() == '\0') {
            buffer.pop_back();
        }
        result.messages.push_back(Widen(buffer));
    }
    return result;
}

DWORD MailslotClient::Send(const std::wstring& path, const std::wstring& text) {
    HANDLE slot = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (slot == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }

    const std::string utf8 = Narrow(text);
    DWORD bytesWritten = 0;
    const DWORD toWrite = static_cast<DWORD>(utf8.size());
    const BOOL ok = WriteFile(slot, utf8.data(), toWrite, &bytesWritten, nullptr);
    const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(slot);
    if (err == ERROR_SUCCESS && bytesWritten != toWrite) {
        return ERROR_WRITE_FAULT;
    }
    return err;
}
