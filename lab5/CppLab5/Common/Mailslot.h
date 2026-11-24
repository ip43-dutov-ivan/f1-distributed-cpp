#pragma once

#include "Win.h"

#include <string>
#include <vector>

inline constexpr const wchar_t* kDefaultHost = L".";
inline constexpr const wchar_t* kDefaultSlotName = L"lab5";

std::wstring MailslotPath(const std::wstring& host, const std::wstring& slotName = kDefaultSlotName);
std::wstring HostFromCommandLine(const wchar_t* commandLine);
std::wstring DescribeWin32Error(DWORD code);

class MailslotServer {
public:
    struct PollResult {
        std::vector<std::wstring> messages;
        DWORD error = ERROR_SUCCESS;
    };

    MailslotServer() = default;
    ~MailslotServer();
    MailslotServer(const MailslotServer&) = delete;
    MailslotServer& operator=(const MailslotServer&) = delete;

    DWORD Open(const std::wstring& slotName = kDefaultSlotName);
    void Close();
    bool IsOpen() const;
    const std::wstring& Path() const;
    PollResult ReadAll();

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring path_;
};

class MailslotClient {
public:
    static DWORD Send(const std::wstring& path, const std::wstring& text);
};
