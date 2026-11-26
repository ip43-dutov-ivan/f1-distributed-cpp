#include "ClientShell.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kShellProperty[] = L"Lab7ClientShell";

constexpr int kMinWindowWidth = 380;
constexpr int kMinWindowHeight = 300;

constexpr CommandId kCommandExit = 100;
constexpr CommandId kControlStatusBar = 104;

constexpr const wchar_t* kCaptionFile = L"File";
constexpr const wchar_t* kCaptionClient = L"Client";
constexpr const wchar_t* kCaptionExit = L"Exit";

constexpr const wchar_t* kTextTargetPrefix = L"Ціль: ";
constexpr const wchar_t* kTextSentOk = L"Повідомлення надіслано";

RECT ClampedArea(RECT area) {
    area.right = std::max(area.right, area.left);
    area.bottom = std::max(area.bottom, area.top);
    return area;
}

}

ClientShell::ClientShell(std::wstring host, ClientProfile profile)
    : host_(std::move(host)), profile_(profile) {}

bool ClientShell::OnCreate(HWND window) {
    window_ = window;
    if (!SetPropW(window_, kShellProperty, this)) return false;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!log_.Create(window, instance)) return false;
    if (!status_.Create(window, instance, kControlStatusBar)) return false;
    if (!CreateControls(window, instance)) return false;

    const std::array<MenuItem, 1> fileItems{{{kCommandExit, kCaptionExit}}};
    std::vector<MenuItem> clientItems{
        {kClientSendCommand, profile_.sendMenuCaption},
    };
    std::span<const MenuItem> extraItems = ExtraClientItems();
    clientItems.insert(clientItems.end(), extraItems.begin(), extraItems.end());
    const std::array<MenuPopup, 2> menu{{
        {kCaptionFile, fileItems},
        {kCaptionClient, clientItems},
    }};
    if (!InstallMenuBar(window, menu)) return false;

    created_ = true;
    Refresh();
    return true;
}

void ClientShell::OnSize(int width, int height) {
    status_.Resize();
    int bottom = height - status_.Height();
    RECT content{kMargin, kMargin, width - kMargin, bottom - kMargin};
    RECT logArea = ClampedArea(LayoutControls(ClampedArea(content)));
    log_.Move(logArea.left, logArea.top, logArea.right - logArea.left, logArea.bottom - logArea.top);
}

void ClientShell::OnCommand(WPARAM wParam, LPARAM lParam) {
    if (HandleControlCommand(wParam, lParam)) {
        Refresh();
        return;
    }
    switch (LOWORD(wParam)) {
    case kCommandExit:
        DestroyWindow(window_);
        break;
    case kClientSendCommand:
        SendPayload();
        break;
    default:
        OnExtraCommand(static_cast<CommandId>(LOWORD(wParam)));
        break;
    }
}

void ClientShell::OnDestroy() {
    RemovePropW(window_, kShellProperty);
}

void ClientShell::OnFocus() {
    FocusPrimaryInput();
}

WindowSpec ClientShell::Spec() const {
    RECT area = WorkArea();
    int rightmost = std::max(static_cast<int>(area.left), static_cast<int>(area.right) - profile_.windowWidth);
    int x = std::min(static_cast<int>(area.left) + profile_.windowOffsetX, rightmost);
    return {profile_.className, profile_.title, x, area.top, profile_.windowWidth, profile_.windowHeight,
        kMinWindowWidth, kMinWindowHeight};
}

bool ClientShell::CreateControls(HWND, HINSTANCE) {
    return true;
}

RECT ClientShell::LayoutControls(RECT content) {
    return content;
}

bool ClientShell::HandleControlCommand(WPARAM, LPARAM) {
    return false;
}

std::span<const MenuItem> ClientShell::ExtraClientItems() const {
    return {};
}

bool ClientShell::OnExtraCommand(CommandId) {
    return false;
}

void ClientShell::SyncControls() {}

void ClientShell::FocusPrimaryInput() {}

bool ClientShell::CanSend() const {
    return true;
}

void ClientShell::OnSent() {}

std::wstring ClientShell::OpenHint() const {
    return L"";
}

HWND ClientShell::Window() const {
    return window_;
}

void ClientShell::Refresh() {
    if (!created_) return;
    SetCommandEnabled(window_, kClientSendCommand, CanSend());
    SyncControls();
    status_.SetState(kTextTargetPrefix + Target());
    ShowHint(OpenHint());
}

void ClientShell::Log(LogKind kind, const std::wstring& text) {
    log_.Append(kind, text);
}

void ClientShell::Notice(const std::wstring& text) {
    status_.SetNotice(text);
    shownHint_.reset();
}

bool ClientShell::Send(const std::wstring& text) {
    if (text.empty()) return false;
    const PipeClient::SendResult result = PipeClient::Send(Target(), text);
    if (result.error != ERROR_SUCCESS) {
        const std::wstring msg = DescribeWin32Error(result.error);
        Log(LogKind::Error, msg);
        Notice(msg);
        return false;
    }
    Log(LogKind::Sent, LogPreview(text));
    if (!result.reply.empty()) {
        Log(LogKind::Received, result.reply);
    }
    Notice(kTextSentOk);
    return true;
}

std::wstring ClientShell::Target() const {
    return PipePath(host_);
}

void ClientShell::ShowHint(const std::wstring& hint) {
    if (shownHint_ == hint) return;
    shownHint_ = hint;
    status_.SetNotice(hint);
}

void ClientShell::SendPayload() {
    if (!CanSend()) return;
    const std::wstring payload = PayloadToSend();
    if (payload.empty()) return;
    if (!Send(payload)) return;
    OnSent();
    SetCommandEnabled(window_, kClientSendCommand, CanSend());
    SyncControls();
}

int RunClient(HINSTANCE instance, int showCommand, ClientShell& shell) {
    const WindowSpec spec = shell.Spec();
    const Shortcut shortcuts[] = {SendShortcut(kClientSendCommand)};
    return RunApp(instance, showCommand, spec, shell, shortcuts);
}
