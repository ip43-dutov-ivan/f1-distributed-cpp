#include "../Common/App.h"
#include "../Common/Mailslot.h"
#include "../Common/Ui.h"
#include "../Common/Win.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace {

constexpr wchar_t kWindowClass[] = L"Lab5Server";
constexpr wchar_t kWindowTitle[] = L"Server (Mailslot)";

constexpr wchar_t kCaptionMenuFile[] = L"File";
constexpr wchar_t kCaptionMenuMailslot[] = L"Mailslot";
constexpr wchar_t kCaptionExit[] = L"Exit";
constexpr wchar_t kCaptionCreate[] = L"Create";
constexpr wchar_t kCaptionClose[] = L"Close";

constexpr wchar_t kTextStateOpen[] = L"Відкрито · {} · повідомлень: {}";
constexpr wchar_t kTextStateClosed[] = L"Закрито · повідомлень: {}";
constexpr wchar_t kTextHintClosed[] = L"Mailslot → Create, щоб створити поштову скриньку";
constexpr wchar_t kTextHintWaiting[] = L"Очікування повідомлень від клієнтів…";
constexpr wchar_t kTextCreated[] = L"Поштову скриньку створено: {}";
constexpr wchar_t kTextClosed[] = L"Поштову скриньку закрито";

constexpr CommandId kCommandExit = 101;
constexpr CommandId kCommandCreate = 102;
constexpr CommandId kCommandClose = 103;
constexpr CommandId kControlStatusBar = 104;

constexpr UINT_PTR kPollTimerId = 1001;
constexpr UINT kPollIntervalMs = 500;

constexpr int kWindowWidth = 720;
constexpr int kWindowHeight = 460;
constexpr int kMinWindowWidth = 500;
constexpr int kMinWindowHeight = 340;
constexpr int kNoSize = 0;

constexpr MenuItem kFileItems[] = {{kCommandExit, kCaptionExit}};
constexpr MenuItem kMailslotItems[] = {{kCommandCreate, kCaptionCreate}, {kCommandClose, kCaptionClose}};
constexpr MenuPopup kMenus[] = {
    {kCaptionMenuFile, kFileItems},
    {kCaptionMenuMailslot, kMailslotItems},
};

class ServerApp : public AppWindow {
public:
    bool OnCreate(HWND window) override;
    void OnSize(int width, int height) override;
    void OnCommand(WPARAM wParam, LPARAM lParam) override;
    void OnTimer(UINT_PTR timerId) override;
    void OnDestroy() override;

private:
    void CreateMailslotChannel();
    void CloseMailslotChannel();
    void PollMessages();
    void Refresh();

    HWND window_ = nullptr;
    LogView log_;
    StatusBar status_;
    MailslotServer server_;
    size_t messageCount_ = 0;
    bool timerActive_ = false;
    bool created_ = false;
};

bool ServerApp::OnCreate(HWND window) {
    window_ = window;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!log_.Create(window, instance)) return false;
    if (!status_.Create(window, instance, kControlStatusBar)) return false;
    if (!InstallMenuBar(window, kMenus)) return false;
    created_ = true;
    Refresh();
    return true;
}

void ServerApp::OnSize(int width, int height) {
    status_.Resize();
    const int logHeight = std::max(height - status_.Height() - 2 * kMargin, kNoSize);
    const int logWidth = std::max(width - 2 * kMargin, kNoSize);
    log_.Move(kMargin, kMargin, logWidth, logHeight);
}

void ServerApp::OnCommand(WPARAM wParam, LPARAM) {
    switch (LOWORD(wParam)) {
    case kCommandExit:
        DestroyWindow(window_);
        break;
    case kCommandCreate:
        CreateMailslotChannel();
        break;
    case kCommandClose:
        CloseMailslotChannel();
        break;
    default:
        break;
    }
}

void ServerApp::OnTimer(UINT_PTR timerId) {
    if (timerId == kPollTimerId) {
        PollMessages();
    }
}

void ServerApp::OnDestroy() {
    if (timerActive_) {
        KillTimer(window_, kPollTimerId);
        timerActive_ = false;
    }
    server_.Close();
}

void ServerApp::CreateMailslotChannel() {
    if (server_.IsOpen()) return;
    const DWORD err = server_.Open(kDefaultSlotName);
    if (err != ERROR_SUCCESS) {
        const std::wstring msg = DescribeWin32Error(err);
        log_.Append(LogKind::Error, msg);
        status_.SetNotice(msg);
        return;
    }

    if (SetTimer(window_, kPollTimerId, kPollIntervalMs, nullptr) != 0) {
        timerActive_ = true;
    }
    log_.Append(LogKind::Info, std::format(kTextCreated, server_.Path()));
    Refresh();
}

void ServerApp::CloseMailslotChannel() {
    if (!server_.IsOpen()) return;
    if (timerActive_) {
        KillTimer(window_, kPollTimerId);
        timerActive_ = false;
    }
    PollMessages();
    server_.Close();
    log_.Append(LogKind::Info, kTextClosed);
    Refresh();
}

void ServerApp::PollMessages() {
    if (!server_.IsOpen()) return;
    const MailslotServer::PollResult result = server_.ReadAll();
    for (const std::wstring& message : result.messages) {
        ++messageCount_;
        log_.Append(LogKind::Received, LogPreview(message));
    }
    if (result.error != ERROR_SUCCESS) {
        const std::wstring msg = DescribeWin32Error(result.error);
        log_.Append(LogKind::Error, msg);
        status_.SetNotice(msg);
    }
    if (!result.messages.empty()) {
        Refresh();
    }
}

void ServerApp::Refresh() {
    if (!created_) return;
    const bool open = server_.IsOpen();
    SetCommandEnabled(window_, kCommandCreate, !open);
    SetCommandEnabled(window_, kCommandClose, open);
    if (open) {
        status_.SetState(std::format(kTextStateOpen, server_.Path(), messageCount_));
        status_.SetNotice(kTextHintWaiting);
    } else {
        status_.SetState(std::format(kTextStateClosed, messageCount_));
        status_.SetNotice(kTextHintClosed);
    }
}

}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    ServerApp app;
    const RECT area = WorkArea();
    const int x = std::max(static_cast<int>(area.left),
        static_cast<int>(area.left + (area.right - area.left - kWindowWidth) / 2));
    const int y = std::max(static_cast<int>(area.top),
        static_cast<int>(area.top + (area.bottom - area.top - kWindowHeight) / 2));
    const WindowSpec spec{
        kWindowClass, kWindowTitle, x, y, kWindowWidth, kWindowHeight, kMinWindowWidth, kMinWindowHeight};
    const std::array<Shortcut, 0> noShortcuts{};
    return RunApp(instance, showCommand, spec, app, noShortcuts);
}
