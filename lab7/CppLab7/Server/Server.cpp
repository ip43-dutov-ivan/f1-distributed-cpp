#include "../Common/App.h"
#include "../Common/Pipe.h"
#include "../Common/Ui.h"
#include "../Common/Win.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace {

constexpr wchar_t kWindowClass[] = L"Lab7Server";
constexpr wchar_t kWindowTitle[] = L"Server (Named Pipe)";

constexpr wchar_t kCaptionMenuFile[] = L"File";
constexpr wchar_t kCaptionMenuPipe[] = L"Pipe";
constexpr wchar_t kCaptionExit[] = L"Exit";
constexpr wchar_t kCaptionCreate[] = L"Create";
constexpr wchar_t kCaptionClose[] = L"Close";

constexpr wchar_t kTextStateOpen[] = L"Відкрито · {} · повідомлень: {}";
constexpr wchar_t kTextStateClosed[] = L"Закрито · повідомлень: {}";
constexpr wchar_t kTextHintClosed[] = L"Pipe → Create, щоб створити іменований канал";
constexpr wchar_t kTextHintWaiting[] = L"Очікування підключень від клієнтів…";
constexpr wchar_t kTextCreated[] = L"Іменований канал створено: {}";
constexpr wchar_t kTextClosed[] = L"Іменований канал закрито";

constexpr CommandId kCommandExit = 101;
constexpr CommandId kCommandCreate = 102;
constexpr CommandId kCommandClose = 103;
constexpr CommandId kControlStatusBar = 104;

constexpr int kWindowWidth = 720;
constexpr int kWindowHeight = 460;
constexpr int kMinWindowWidth = 500;
constexpr int kMinWindowHeight = 340;
constexpr int kNoSize = 0;

constexpr MenuItem kFileItems[] = {{kCommandExit, kCaptionExit}};
constexpr MenuItem kPipeItems[] = {{kCommandCreate, kCaptionCreate}, {kCommandClose, kCaptionClose}};
constexpr MenuPopup kMenus[] = {
    {kCaptionMenuFile, kFileItems},
    {kCaptionMenuPipe, kPipeItems},
};

class ServerApp : public AppWindow {
public:
    bool OnCreate(HWND window) override;
    void OnSize(int width, int height) override;
    void OnCommand(WPARAM wParam, LPARAM lParam) override;
    void OnAppMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnDestroy() override;

private:
    void CreatePipeChannel();
    void ClosePipeChannel();
    void Refresh();

    HWND window_ = nullptr;
    LogView log_;
    StatusBar status_;
    PipeServer server_;
    size_t messageCount_ = 0;
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
        CreatePipeChannel();
        break;
    case kCommandClose:
        ClosePipeChannel();
        break;
    default:
        break;
    }
}

void ServerApp::OnAppMessage(UINT message, WPARAM, LPARAM) {
    if (message != kPipeMessage) return;

    const auto events = server_.DrainEvents();
    bool receivedAny = false;
    for (const PipeServer::Event& event : events) {
        if (event.isError) {
            log_.Append(LogKind::Error, event.text);
            status_.SetNotice(event.text);
        } else {
            ++messageCount_;
            log_.Append(LogKind::Received, LogPreview(event.text));
            receivedAny = true;
        }
    }
    if (receivedAny) {
        Refresh();
    }
}

void ServerApp::OnDestroy() {
    server_.Stop();
}

void ServerApp::CreatePipeChannel() {
    if (server_.IsRunning()) return;
    const DWORD err = server_.Start(window_, kDefaultPipeName);
    if (err != ERROR_SUCCESS) {
        const std::wstring msg = DescribeWin32Error(err);
        log_.Append(LogKind::Error, msg);
        status_.SetNotice(msg);
        return;
    }
    log_.Append(LogKind::Info, std::format(kTextCreated, server_.Path()));
    Refresh();
}

void ServerApp::ClosePipeChannel() {
    if (!server_.IsRunning()) return;
    server_.Stop();
    log_.Append(LogKind::Info, kTextClosed);
    Refresh();
}

void ServerApp::Refresh() {
    if (!created_) return;
    const bool open = server_.IsRunning();
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
