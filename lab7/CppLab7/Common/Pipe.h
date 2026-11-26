#pragma once

#include "Win.h"

#include <mutex>
#include <string>
#include <vector>

inline constexpr const wchar_t* kDefaultHost = L".";
inline constexpr const wchar_t* kDefaultPipeName = L"lab7";
inline constexpr UINT kPipeMessage = WM_APP + 1;

std::wstring PipePath(const std::wstring& host, const std::wstring& pipeName = kDefaultPipeName);
std::wstring HostFromCommandLine(const wchar_t* commandLine);
std::wstring DescribeWin32Error(DWORD code);

class PipeServer {
public:
    struct Event {
        bool isError = false;
        std::wstring text;
    };

    PipeServer() = default;
    ~PipeServer();
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    DWORD Start(HWND notifyWnd, const std::wstring& pipeName = kDefaultPipeName);
    void Stop();
    bool IsRunning() const;
    const std::wstring& Path() const;
    std::vector<Event> DrainEvents();

private:
    struct StartContext;

    static DWORD WINAPI ThreadEntry(LPVOID param);
    void RunLoop(HANDLE firstPipe);
    void PushEvent(bool isError, std::wstring text);

    HWND notifyWnd_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE acceptThread_ = nullptr;
    std::wstring path_;
    std::mutex eventsMutex_;
    std::vector<Event> events_;
};

class PipeClient {
public:
    struct SendResult {
        DWORD error = ERROR_SUCCESS;
        std::wstring reply;
    };

    static SendResult Send(const std::wstring& path, const std::wstring& text);
};
