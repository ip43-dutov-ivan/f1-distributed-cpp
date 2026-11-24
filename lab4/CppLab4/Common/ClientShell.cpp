#include "ClientShell.h"

#include "Endpoint.h"
#include "NetError.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kShellProperty[] = L"Lab4ClientShell";

constexpr int kMinWindowWidth = 380;
constexpr int kMinWindowHeight = 300;

constexpr UINT kDisconnectTimeoutMs = 5000;
constexpr UINT_PTR kNoTimer = 0;

constexpr CommandId kCommandExit = 100;
constexpr CommandId kCommandSetConnection = 101;
constexpr CommandId kCommandDisconnect = 103;
constexpr CommandId kControlStatusBar = 104;

constexpr const wchar_t* kCaptionFile = L"File";
constexpr const wchar_t* kCaptionClient = L"Client";
constexpr const wchar_t* kCaptionExit = L"Exit";
constexpr const wchar_t* kCaptionSetConnection = L"Set Connection";
constexpr const wchar_t* kCaptionDisconnect = L"Disconnect";

constexpr const wchar_t* kTextSeparator = L" · ";
constexpr const wchar_t* kTextNotConnected = L"Не з'єднано";
constexpr const wchar_t* kTextConnectingTo = L"Підключення до ";
constexpr const wchar_t* kTextConnectedTo = L"З'єднано з ";
constexpr const wchar_t* kTextDisconnectingFrom = L"Відключення від ";
constexpr const wchar_t* kTextEllipsis = L"…";
constexpr const wchar_t* kTextNoNotice = L"";
constexpr const wchar_t* kTextHintConnect = L"Client → Set Connection, щоб підключитись";
constexpr const wchar_t* kTextConnectFirst = L"Спочатку Client → Set Connection";
constexpr const wchar_t* kTextAlreadyConnected = L"З'єднання вже встановлено";
constexpr const wchar_t* kTextConnectingInProgress = L"Підключення триває";
constexpr const wchar_t* kTextDisconnectingInProgress = L"Відключення триває";
constexpr const wchar_t* kTextConnectCancelled = L"Підключення скасовано";
constexpr const wchar_t* kTextConnected = L"Зв'язок встановлено!";
constexpr const wchar_t* kTextConnectFailed = L"Помилка з'єднання";
constexpr const wchar_t* kTextServerClosed = L"Сервер закрив з'єднання";
constexpr const wchar_t* kTextDisconnected = L"Від'єднано від сервера";

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
        {kCommandSetConnection, kCaptionSetConnection},
        {kClientSendCommand, profile_.sendMenuCaption},
    };
    std::span<const MenuItem> extraItems = ExtraClientItems();
    clientItems.insert(clientItems.end(), extraItems.begin(), extraItems.end());
    clientItems.push_back({kCommandDisconnect, kCaptionDisconnect});
    const std::array<MenuPopup, 2> menu{{
        {kCaptionFile, fileItems},
        {kCaptionClient, clientItems},
    }};
    if (!InstallMenuBar(window, menu)) return false;

    connection_.SetHandlers(MakeHandlers());
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
    case kCommandSetConnection:
        SetConnection();
        break;
    case kClientSendCommand:
        SendPayload();
        break;
    case kCommandDisconnect:
        Disconnect();
        break;
    default:
        OnExtraCommand(static_cast<CommandId>(LOWORD(wParam)));
        break;
    }
}

void ClientShell::OnSocket(WPARAM wParam, LPARAM lParam) {
    connection_.HandleSocketEvent(wParam, lParam);
}

void ClientShell::OnDestroy() {
    StopTimer();
    connection_.Close();
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

void ClientShell::SyncControls(Phase) {}

void ClientShell::FocusPrimaryInput() {}

bool ClientShell::CanSend() const {
    return true;
}

void ClientShell::OnSent() {}

std::wstring ClientShell::OpenHint() const {
    return kTextNoNotice;
}

HWND ClientShell::Window() const {
    return window_;
}

void ClientShell::Refresh() {
    if (!created_) return;
    Phase phase = CurrentPhase();
    SetCommandEnabled(window_, kCommandSetConnection, phase == Phase::Idle);
    SetCommandEnabled(window_, kCommandDisconnect, phase != Phase::Idle);
    SetCommandEnabled(window_, kClientSendCommand, phase == Phase::Open && CanSend());
    SyncControls(phase);
    status_.SetState(StateText(phase));
    ShowHint(HintText(phase));
}

void ClientShell::Log(LogKind kind, const std::wstring& text) {
    log_.Append(kind, text);
}

void ClientShell::Notice(const std::wstring& text) {
    status_.SetNotice(text);
    shownHint_.reset();
}

bool ClientShell::Send(const std::wstring& text) {
    Phase phase = CurrentPhase();
    if (phase != Phase::Open) {
        NoticeSendBlocked(phase);
        return false;
    }
    if (text.empty()) return false;
    int error = connection_.Send(text);
    if (error == kSendTerminated) return false;
    if (error != kNoError) {
        Log(LogKind::Error, DescribeWinsockError(error));
        Notice(DescribeWinsockError(error));
        return false;
    }
    Log(LogKind::Sent, LogPreview(text));
    return true;
}

ConnectionHandlers ClientShell::MakeHandlers() {
    return {
        .message = [this](const std::wstring& text) { Log(LogKind::Received, LogPreview(text)); },
        .fault = [this](ConnectionFault fault, int code) { Log(LogKind::Error, DescribeFault(fault, code)); },
        .closed = [this](SOCKET) { OnClosed(); },
        .connectResult = [this](int code) { OnConnectResult(code); },
    };
}

ClientShell::Phase ClientShell::CurrentPhase() const {
    if (connection_.IsConnecting()) return Phase::Connecting;
    if (!connection_.IsOpen()) return Phase::Idle;
    return connection_.IsShuttingDown() ? Phase::Closing : Phase::Open;
}

std::wstring ClientShell::Target() const {
    return FormatEndpoint(host_, kDefaultPort);
}

std::wstring ClientShell::StateText(Phase phase) const {
    switch (phase) {
    case Phase::Connecting:
        return kTextConnectingTo + Target() + kTextEllipsis;
    case Phase::Open:
        return kTextConnectedTo + connection_.Peer();
    case Phase::Closing:
        return kTextDisconnectingFrom + connection_.Peer() + kTextEllipsis;
    case Phase::Idle:
        break;
    }
    return kTextNotConnected + std::wstring(kTextSeparator) + Target();
}

std::wstring ClientShell::HintText(Phase phase) const {
    if (phase == Phase::Idle) return kTextHintConnect;
    if (phase == Phase::Open) return OpenHint();
    return kTextNoNotice;
}

void ClientShell::ShowHint(const std::wstring& hint) {
    if (shownHint_ == hint) return;
    shownHint_ = hint;
    status_.SetNotice(hint);
}

void ClientShell::SetConnection() {
    Phase phase = CurrentPhase();
    if (phase == Phase::Connecting) {
        Log(LogKind::Info, kTextConnectingInProgress);
        return;
    }
    if (phase != Phase::Idle) {
        Log(LogKind::Info, kTextAlreadyConnected);
        return;
    }
    int error = connection_.Connect(host_, kDefaultPort, window_, kSocketMessage);
    if (error != kNoError) {
        Log(LogKind::Error, DescribeWinsockError(error));
        Refresh();
        Notice(DescribeWinsockError(error));
        return;
    }
    Refresh();
}

void ClientShell::OnConnectResult(int code) {
    if (code != kNoError) {
        Log(LogKind::Error, DescribeWinsockError(code));
        Refresh();
        Notice(kTextConnectFailed);
        return;
    }
    Log(LogKind::Info, kTextConnected);
    Refresh();
    if (GetActiveWindow() == window_) FocusPrimaryInput();
}

void ClientShell::NoticeSendBlocked(Phase phase) {
    if (phase == Phase::Connecting) Notice(kTextConnectingInProgress);
    else if (phase == Phase::Closing) Notice(kTextDisconnectingInProgress);
    else Notice(kTextConnectFirst);
}

void ClientShell::SendPayload() {
    Phase phase = CurrentPhase();
    if (phase != Phase::Open) {
        NoticeSendBlocked(phase);
        return;
    }
    if (!CanSend()) return;
    if (!Send(PayloadToSend())) return;
    OnSent();
    Refresh();
}

void ClientShell::Disconnect() {
    Phase phase = CurrentPhase();
    if (phase == Phase::Connecting) {
        connection_.Close();
        Log(LogKind::Info, kTextConnectCancelled);
        Refresh();
        return;
    }
    if (phase != Phase::Open) return;
    disconnectRequested_ = true;
    int error = connection_.Shutdown();
    if (error == kSendTerminated) return;
    if (error != kNoError) {
        disconnectRequested_ = false;
        Log(LogKind::Error, DescribeWinsockError(error));
        Notice(DescribeWinsockError(error));
        return;
    }
    StartTimer();
    Refresh();
}

void ClientShell::OnClosed() {
    StopTimer();
    bool requested = std::exchange(disconnectRequested_, false);
    Log(LogKind::Info, requested ? kTextDisconnected : kTextServerClosed);
    Refresh();
}

void ClientShell::StartTimer() {
    StopTimer();
    UINT_PTR id = ++lastTimerId_;
    if (SetTimer(window_, id, kDisconnectTimeoutMs, OnTimer) != kNoTimer) timerId_ = id;
}

void ClientShell::StopTimer() {
    if (timerId_ == kNoTimer) return;
    KillTimer(window_, timerId_);
    timerId_ = kNoTimer;
}

void ClientShell::OnDisconnectTimeout(UINT_PTR id) {
    if (id != timerId_) return;
    StopTimer();
    connection_.Close();
    disconnectRequested_ = false;
    Log(LogKind::Info, kTextDisconnected);
    Refresh();
}

void CALLBACK ClientShell::OnTimer(HWND window, UINT, UINT_PTR id, DWORD) {
    auto* shell = static_cast<ClientShell*>(GetPropW(window, kShellProperty));
    if (shell) shell->OnDisconnectTimeout(id);
}

int RunClient(HINSTANCE instance, int showCommand, ClientShell& shell) {
    const WindowSpec spec = shell.Spec();
    const Shortcut shortcuts[] = {SendShortcut(kClientSendCommand)};
    return RunApp(instance, showCommand, spec, shell, shortcuts);
}
