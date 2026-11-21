#include "../Common/App.h"
#include "../Common/Connection.h"
#include "../Common/Endpoint.h"
#include "../Common/NetError.h"
#include "../Common/Ui.h"
#include "../Common/Win.h"

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"Lab4Server";
constexpr wchar_t kWindowTitle[] = L"Server";
constexpr wchar_t kAppProperty[] = L"Lab4ServerApp";

constexpr wchar_t kCaptionMenuFile[] = L"File";
constexpr wchar_t kCaptionMenuServer[] = L"Server";
constexpr wchar_t kCaptionMenuClient[] = L"Client";
constexpr wchar_t kCaptionExit[] = L"Exit";
constexpr wchar_t kCaptionStart[] = L"Start";
constexpr wchar_t kCaptionStop[] = L"Stop";
constexpr wchar_t kCaptionSendMenu[] = L"Send Message\tCtrl+Enter";
constexpr wchar_t kCaptionSendButton[] = L"Send\nCtrl+Enter";
constexpr wchar_t kCaptionDisconnectMenu[] = L"Disconnect Client";
constexpr wchar_t kCaptionDisconnectButton[] = L"Disconnect";

constexpr wchar_t kTextConfirmTitle[] = L"Підтвердження";
constexpr wchar_t kTextConfirmDisconnect[] = L"Відключити";
constexpr wchar_t kTextCancel[] = L"Скасувати";
constexpr wchar_t kTextDisconnectQuestion[] = L"Відключити {} ({})?";
constexpr wchar_t kTextDisconnectNotice[] = L"Відключено клієнта {}";
constexpr wchar_t kTextDisconnectedByOperator[] = L"{} відключено оператором";
constexpr wchar_t kTextStateListening[] = L"Слухає порт {} · клієнтів: {}";
constexpr wchar_t kTextStateStopped[] = L"Зупинено · клієнтів: {}";
constexpr wchar_t kTextHintStopped[] = L"Server → Start, щоб прийняти клієнтів";
constexpr wchar_t kTextHintWaiting[] = L"Очікування клієнтів…";
constexpr wchar_t kTextSelectClient[] = L"Оберіть клієнта у списку";
constexpr wchar_t kTextClientName[] = L"Клієнт {}";
constexpr wchar_t kTextClientRow[] = L"{} ({})";
constexpr wchar_t kTextClientConnected[] = L"{} підключився ({})";
constexpr wchar_t kTextClientFinished[] = L"{} завершив роботу";
constexpr wchar_t kTextConnectionClosed[] = L"З'єднання з {} закрито";
constexpr wchar_t kTextEntry[] = L"{}: {}";
constexpr wchar_t kTextServerStarted[] = L"Сервер запущено, порт {}";
constexpr wchar_t kTextServerStopped[] = L"Сервер зупинено, відключено клієнтів: {}";
constexpr wchar_t kTextStartFailed[] = L"Не вдалося запустити сервер";
constexpr wchar_t kTextAcceptFailed[] = L"Не вдалося прийняти клієнта";

constexpr CommandId kCommandExit = 101;
constexpr CommandId kCommandStart = 102;
constexpr CommandId kCommandStop = 103;
constexpr CommandId kCommandSend = 104;
constexpr CommandId kCommandDisconnect = 105;
constexpr CommandId kControlClientList = 106;
constexpr CommandId kControlStatusBar = 107;

constexpr int kWindowWidth = 820;
constexpr int kWindowHeight = 520;
constexpr int kMinWindowWidth = 600;
constexpr int kMinWindowHeight = 380;
constexpr int kListColumnWidth = 220;
constexpr int kNoSize = 0;

constexpr UINT_PTR kLingerTimerId = 1000;
constexpr UINT kLingerMilliseconds = 5000;

constexpr MenuItem kFileItems[] = {{kCommandExit, kCaptionExit}};
constexpr MenuItem kServerItems[] = {{kCommandStart, kCaptionStart}, {kCommandStop, kCaptionStop}};
constexpr MenuItem kClientItems[] = {{kCommandSend, kCaptionSendMenu},
    {kCommandDisconnect, kCaptionDisconnectMenu}};
constexpr MenuPopup kMenus[] = {{kCaptionMenuFile, kFileItems}, {kCaptionMenuServer, kServerItems},
    {kCaptionMenuClient, kClientItems}};

struct ClientSession {
    int id = 0;
    std::wstring name;
    std::unique_ptr<Connection> link;
    bool closing = false;
    bool faulted = false;
};

class SessionRegistry {
public:
    ClientSession& Add(std::unique_ptr<Connection> link) {
        SOCKET socket = link->Socket();
        ClientSession session;
        session.id = nextId_++;
        session.link = std::move(link);
        return sessions_.insert_or_assign(socket, std::move(session)).first->second;
    }

    ClientSession* FindBySocket(SOCKET socket) {
        auto found = sessions_.find(socket);
        return found == sessions_.end() ? nullptr : &found->second;
    }

    ClientSession* FindById(int id) {
        auto found = std::ranges::find_if(sessions_, [id](const auto& entry) { return entry.second.id == id; });
        return found == sessions_.end() ? nullptr : &found->second;
    }

    void Remove(SOCKET socket) {
        sessions_.erase(socket);
    }

    std::vector<int> CloseAll() {
        std::vector<int> ids;
        for (auto& entry : sessions_) {
            entry.second.link->Close();
            ids.push_back(entry.second.id);
        }
        sessions_.clear();
        return ids;
    }

    size_t ActiveCount() const {
        return static_cast<size_t>(std::ranges::count_if(sessions_,
            [](const auto& entry) { return !entry.second.closing; }));
    }

private:
    std::unordered_map<SOCKET, ClientSession> sessions_;
    int nextId_ = 1;
};

void CALLBACK LingerTimerProc(HWND window, UINT message, UINT_PTR timerId, DWORD time);

class ServerApp : public AppWindow {
public:
    bool OnCreate(HWND window) override;
    void OnSize(int width, int height) override;
    void OnCommand(WPARAM wParam, LPARAM lParam) override;
    void OnSocket(WPARAM wParam, LPARAM lParam) override;
    void OnDestroy() override;
    void OnFocus() override;
    void OnLingerExpired(int id);

private:
    void Refresh();
    std::wstring IdleHint(bool listening, size_t clients, bool selected) const;
    void RescueFocus(bool selected);
    void FocusAfterAction();
    void Log(LogKind kind, const std::wstring& text);
    void Notify(const std::wstring& text);
    void ReportError(const std::wstring& context, int code);
    void StartServer();
    void StopServer();
    void AcceptEvent(LPARAM event);
    void AcceptClient();
    void Wire(ClientSession& session);
    void OnMessage(SOCKET socket, const std::wstring& text);
    void OnFault(SOCKET socket, ConnectionFault fault, int code);
    void OnSessionClosed(SOCKET socket);
    ClientSession* SelectedSession();
    void SendToSelected();
    void DisconnectSelected();
    void BeginDisconnect(ClientSession& session);
    bool ArmLinger(int id);
    void DisarmLinger(int id);
    void EvictSession(int id);

    HWND window_ = nullptr;
    LogView log_;
    ComposePanel compose_;
    ClientList clients_;
    Button disconnect_;
    StatusBar status_;
    Listener listener_;
    SessionRegistry sessions_;
    bool noticeActive_ = false;
    bool created_ = false;
};

bool ServerApp::OnCreate(HWND window) {
    window_ = window;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!SetPropW(window_, kAppProperty, this)) return false;
    if (!InstallMenuBar(window_, kMenus)) return false;
    if (!status_.Create(window_, instance, kControlStatusBar)) return false;
    if (!log_.Create(window_, instance)) return false;
    if (!compose_.Create(window_, instance, kCommandSend, kCaptionSendButton)) return false;
    if (!clients_.Create(window_, instance, kControlClientList)) return false;
    if (!disconnect_.Create(window_, instance, kCommandDisconnect, kCaptionDisconnectButton)) return false;
    created_ = true;
    Refresh();
    return true;
}

void ServerApp::OnSize(int width, int height) {
    if (!created_) return;
    status_.Resize();
    int bodyWidth = std::max(width - 2 * kMargin, kNoSize);
    int bodyHeight = std::max(height - status_.Height() - 2 * kMargin, kNoSize);
    int listHeight = std::max(bodyHeight - kButtonHeight - kGap, kNoSize);
    clients_.Move(kMargin, kMargin, kListColumnWidth, listHeight);
    disconnect_.Move(kMargin, kMargin + listHeight + kGap, kListColumnWidth, kButtonHeight);
    int chatOffset = kListColumnWidth + kGap;
    LayoutChat(log_, compose_, kMargin + chatOffset, kMargin, std::max(bodyWidth - chatOffset, kNoSize),
        bodyHeight);
}

void ServerApp::OnFocus() {
    if (created_) FocusAfterAction();
}

void ServerApp::Refresh() {
    bool listening = listener_.IsListening();
    bool selected = clients_.Selected().has_value();
    size_t clients = sessions_.ActiveCount();
    SetCommandEnabled(window_, kCommandStart, !listening);
    SetCommandEnabled(window_, kCommandStop, listening);
    SetCommandEnabled(window_, kCommandSend, selected && !compose_.IsEmpty());
    SetCommandEnabled(window_, kCommandDisconnect, selected);
    compose_.SetSendEnabled(selected);
    disconnect_.SetEnabled(selected);
    status_.SetState(listening ? std::format(kTextStateListening, kDefaultPort, clients)
                               : std::format(kTextStateStopped, clients));
    if (!noticeActive_) status_.SetNotice(IdleHint(listening, clients, selected));
    RescueFocus(selected);
}

std::wstring ServerApp::IdleHint(bool listening, size_t clients, bool selected) const {
    if (!listening) return kTextHintStopped;
    if (clients == 0) return kTextHintWaiting;
    return selected ? std::wstring() : std::wstring(kTextSelectClient);
}

void ServerApp::RescueFocus(bool selected) {
    if (selected) return;
    HWND focus = GetFocus();
    if (focus == GetDlgItem(window_, kCommandDisconnect) || focus == GetDlgItem(window_, kCommandSend)) {
        FocusAfterAction();
    }
}

void ServerApp::FocusAfterAction() {
    if (sessions_.ActiveCount() > 0) {
        clients_.Focus();
    } else {
        compose_.Focus();
    }
}

void ServerApp::Log(LogKind kind, const std::wstring& text) {
    log_.Append(kind, text);
}

void ServerApp::Notify(const std::wstring& text) {
    noticeActive_ = true;
    status_.SetNotice(text);
}

void ServerApp::ReportError(const std::wstring& context, int code) {
    std::wstring description = DescribeWinsockError(code);
    Log(LogKind::Error, std::format(kTextEntry, context, description));
    Notify(description);
}

void ServerApp::OnCommand(WPARAM wParam, LPARAM lParam) {
    if (compose_.HandleCommand(wParam, lParam)) {
        Refresh();
        return;
    }
    if (clients_.IsSelectionChange(wParam, lParam)) {
        noticeActive_ = false;
        Refresh();
        return;
    }
    switch (LOWORD(wParam)) {
    case kCommandExit:
        DestroyWindow(window_);
        break;
    case kCommandStart:
        StartServer();
        break;
    case kCommandStop:
        StopServer();
        break;
    case kCommandSend:
        SendToSelected();
        break;
    case kCommandDisconnect:
        DisconnectSelected();
        break;
    }
}

void ServerApp::StartServer() {
    noticeActive_ = false;
    int error = listener_.Start(kDefaultPort, window_, kSocketMessage);
    if (error != kNoError) {
        ReportError(kTextStartFailed, error);
    } else {
        Log(LogKind::Info, std::format(kTextServerStarted, kDefaultPort));
    }
    Refresh();
}

void ServerApp::StopServer() {
    if (!listener_.IsListening()) return;
    size_t clients = sessions_.ActiveCount();
    for (int id : sessions_.CloseAll()) DisarmLinger(id);
    clients_.Clear();
    listener_.Stop();
    Log(LogKind::Info, std::format(kTextServerStopped, clients));
    noticeActive_ = false;
    Refresh();
}

void ServerApp::OnDestroy() {
    if (window_) RemovePropW(window_, kAppProperty);
    if (created_) StopServer();
}

void ServerApp::OnSocket(WPARAM wParam, LPARAM lParam) {
    SOCKET source = static_cast<SOCKET>(wParam);
    if (listener_.IsListening() && source == listener_.Socket()) {
        AcceptEvent(lParam);
        return;
    }
    ClientSession* session = sessions_.FindBySocket(source);
    if (session) session->link->HandleSocketEvent(wParam, lParam);
}

void ServerApp::AcceptEvent(LPARAM event) {
    if (WSAGETSELECTEVENT(event) != FD_ACCEPT) return;
    int error = WSAGETSELECTERROR(event);
    if (error != kNoError) {
        ReportError(kTextAcceptFailed, error);
        return;
    }
    AcceptClient();
}

void ServerApp::AcceptClient() {
    int error = kNoError;
    std::unique_ptr<Connection> link = listener_.Accept(window_, kSocketMessage, error);
    if (!link) {
        if (error != WSAEWOULDBLOCK) ReportError(kTextAcceptFailed, error);
        return;
    }
    ClientSession& session = sessions_.Add(std::move(link));
    session.name = std::format(kTextClientName, session.id);
    const std::wstring& peer = session.link->Peer();
    clients_.Add(session.id, std::format(kTextClientRow, session.name, peer));
    Log(LogKind::Info, std::format(kTextClientConnected, session.name, peer));
    Wire(session);
    clients_.SelectFirstIfNone();
    noticeActive_ = false;
    Refresh();
}

void ServerApp::Wire(ClientSession& session) {
    SOCKET socket = session.link->Socket();
    ConnectionHandlers handlers;
    handlers.message = [this, socket](const std::wstring& text) { OnMessage(socket, text); };
    handlers.fault = [this, socket](ConnectionFault fault, int code) { OnFault(socket, fault, code); };
    handlers.closed = [this](SOCKET ended) { OnSessionClosed(ended); };
    session.link->SetHandlers(std::move(handlers));
}

void ServerApp::OnMessage(SOCKET socket, const std::wstring& text) {
    ClientSession* session = sessions_.FindBySocket(socket);
    if (session) Log(LogKind::Received, std::format(kTextEntry, session->name, LogPreview(text)));
}

void ServerApp::OnFault(SOCKET socket, ConnectionFault fault, int code) {
    ClientSession* session = sessions_.FindBySocket(socket);
    if (!session) return;
    session->faulted = true;
    if (session->closing) return;
    Log(LogKind::Error, std::format(kTextEntry, session->name, DescribeFault(fault, code)));
}

void ServerApp::OnSessionClosed(SOCKET socket) {
    ClientSession* session = sessions_.FindBySocket(socket);
    if (!session) return;
    int id = session->id;
    if (!session->closing) {
        Log(LogKind::Info, session->faulted ? std::format(kTextConnectionClosed, session->name)
                                            : std::format(kTextClientFinished, session->name));
    }
    sessions_.Remove(socket);
    clients_.Remove(id);
    DisarmLinger(id);
    noticeActive_ = false;
    Refresh();
}

ClientSession* ServerApp::SelectedSession() {
    std::optional<int> id = clients_.Selected();
    if (!id) return nullptr;
    ClientSession* session = sessions_.FindById(*id);
    return session && !session->closing ? session : nullptr;
}

void ServerApp::SendToSelected() {
    ClientSession* session = SelectedSession();
    if (!session) {
        Notify(kTextSelectClient);
        return;
    }
    if (compose_.IsEmpty()) return;
    std::wstring text = compose_.Text();
    std::wstring name = session->name;
    int error = session->link->Send(text);
    if (error == kSendTerminated) return;
    if (error != kNoError) {
        ReportError(name, error);
        return;
    }
    Log(LogKind::Sent, std::format(kTextEntry, name, LogPreview(text)));
    compose_.Clear();
    compose_.Focus();
    noticeActive_ = false;
    Refresh();
}

void ServerApp::DisconnectSelected() {
    ClientSession* session = SelectedSession();
    if (!session) return;
    int id = session->id;
    std::wstring question = std::format(kTextDisconnectQuestion, session->name, session->link->Peer());
    if (!Confirm(window_, kTextConfirmTitle, question, kTextConfirmDisconnect, kTextCancel)) return;
    session = sessions_.FindById(id);
    if (!session || session->closing) return;
    BeginDisconnect(*session);
    noticeActive_ = false;
    Refresh();
    FocusAfterAction();
}

void ServerApp::BeginDisconnect(ClientSession& session) {
    int id = session.id;
    std::wstring name = session.name;
    int error = session.link->Send(std::format(kTextDisconnectNotice, id));
    if (error == kSendTerminated) return;
    if (error != kNoError) ReportError(name, error);
    session.closing = true;
    clients_.Remove(id);
    Log(LogKind::Info, std::format(kTextDisconnectedByOperator, name));
    if (!ArmLinger(id)) {
        EvictSession(id);
        return;
    }
    session.link->Shutdown();
}

bool ServerApp::ArmLinger(int id) {
    return SetTimer(window_, kLingerTimerId + static_cast<UINT_PTR>(id), kLingerMilliseconds,
        LingerTimerProc) != 0;
}

void ServerApp::DisarmLinger(int id) {
    KillTimer(window_, kLingerTimerId + static_cast<UINT_PTR>(id));
}

void ServerApp::EvictSession(int id) {
    ClientSession* session = sessions_.FindById(id);
    if (!session) return;
    SOCKET socket = session->link->Socket();
    session->link->Close();
    sessions_.Remove(socket);
    DisarmLinger(id);
}

void ServerApp::OnLingerExpired(int id) {
    EvictSession(id);
    Refresh();
}

void CALLBACK LingerTimerProc(HWND window, UINT, UINT_PTR timerId, DWORD) {
    KillTimer(window, timerId);
    auto* app = static_cast<ServerApp*>(GetPropW(window, kAppProperty));
    if (app) app->OnLingerExpired(static_cast<int>(timerId - kLingerTimerId));
}

}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    RECT area = WorkArea();
    WindowSpec spec{kWindowClass, kWindowTitle, area.left, area.top, kWindowWidth, kWindowHeight,
        kMinWindowWidth, kMinWindowHeight};
    const Shortcut shortcuts[] = {SendShortcut(kCommandSend)};
    ServerApp app;
    return RunApp(instance, showCommand, spec, app, shortcuts);
}
