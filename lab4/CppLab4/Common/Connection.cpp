#include "Connection.h"
#include "Endpoint.h"
#include "Protocol.h"
#include "Text.h"

#include <ws2tcpip.h>
#include <mswsock.h>

#include <algorithm>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr size_t kReceiveChunkKiB = 64;
constexpr size_t kReceiveChunkBytes = kReceiveChunkKiB * kBytesPerKiB;
constexpr size_t kMaxChunksPerEvent = 16;
constexpr size_t kMaxFramesPerEvent = 256;
constexpr size_t kMaxSendChunkBytes = kBytesPerMiB;
constexpr size_t kMaxOutboundMiB = 64;
constexpr size_t kMaxOutboundBytes = kMaxOutboundMiB * kBytesPerMiB;
constexpr size_t kCompactThresholdBytes = kBytesPerMiB;
constexpr long kConnectionEvents = FD_READ | FD_WRITE | FD_CLOSE;
constexpr long kConnectingEvents = FD_CONNECT | kConnectionEvents;
constexpr long kListenerEvents = FD_ACCEPT;
constexpr UINT kDetachMessage = 0;
constexpr long kDetachEvents = 0;
constexpr WORD kRequestedWinsock = MAKEWORD(2, 2);
constexpr int kPeerClosed = 0;
constexpr size_t kNothingQueued = 0;
constexpr size_t kBufferStart = 0;
constexpr size_t kFirstCandidate = 0;
constexpr DWORD kNotConnectedTime = 0xFFFFFFFF;

using AddrInfoPtr = std::unique_ptr<ADDRINFOW, decltype(&FreeAddrInfoW)>;

int AbortSocket(SOCKET socket) {
    const int error = WSAGetLastError();
    closesocket(socket);
    return error;
}

}

WinsockSession::WinsockSession() {
    WSADATA data{};
    error_ = WSAStartup(kRequestedWinsock, &data);
}

WinsockSession::~WinsockSession() {
    if (error_ == kNoError) WSACleanup();
}

int WinsockSession::Error() const {
    return error_;
}

Connection::Connection()
    : handlers_(std::make_shared<ConnectionHandlers>()), alive_(std::make_shared<bool>(true)) {}

Connection::~Connection() {
    *alive_ = false;
    Close();
}

void Connection::SetHandlers(ConnectionHandlers handlers) {
    handlers_ = std::make_shared<ConnectionHandlers>(std::move(handlers));
}

template <typename Call>
Connection::Standing Connection::Invoke(Call&& call) {
    const auto handlers = handlers_;
    const auto alive = alive_;
    const uint64_t epoch = epoch_;
    const bool outer = dispatching_;
    dispatching_ = true;
    call(*handlers);
    if (!*alive) return Standing::Destroyed;
    dispatching_ = outer;
    return epoch_ == epoch ? Standing::Intact : Standing::Superseded;
}

int Connection::Connect(const std::wstring& host, unsigned short port, HWND window, UINT message) {
    if (sock_ != INVALID_SOCKET) return WSAEISCONN;

    const std::wstring service = std::to_wstring(port);
    ADDRINFOW hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ADDRINFOW* raw = nullptr;
    const int resolveError = GetAddrInfoW(host.c_str(), service.c_str(), &hints, &raw);
    if (resolveError != kNoError) return resolveError;
    AddrInfoPtr addresses(raw, FreeAddrInfoW);

    Close();
    for (const ADDRINFOW* address = addresses.get(); address != nullptr; address = address->ai_next) {
        candidates_.push_back(*reinterpret_cast<const sockaddr_in*>(address->ai_addr));
    }
    window_ = window;
    message_ = message;
    peer_ = FormatEndpoint(host, port);
    nextCandidate_ = kFirstCandidate;

    const int error = StartNextAttempt(WSAEHOSTUNREACH);
    if (error != kNoError) {
        candidates_.clear();
        return error;
    }
    phase_ = Phase::Connecting;
    return kNoError;
}

int Connection::StartNextAttempt(int lastError) {
    while (nextCandidate_ < candidates_.size()) {
        const sockaddr_in& address = candidates_[nextCandidate_++];
        SOCKET candidate = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (candidate == INVALID_SOCKET) {
            lastError = WSAGetLastError();
            continue;
        }
        if (WSAAsyncSelect(candidate, window_, message_, kConnectingEvents) == SOCKET_ERROR) {
            lastError = AbortSocket(candidate);
            continue;
        }
        const int connected = connect(candidate, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        if (connected == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
            lastError = AbortSocket(candidate);
            continue;
        }
        sock_ = candidate;
        ++epoch_;
        return kNoError;
    }
    return lastError;
}

int Connection::Adopt(SOCKET socket, HWND window, UINT message, std::wstring peer) {
    Close();
    sock_ = socket;
    window_ = window;
    message_ = message;
    phase_ = Phase::Open;
    peer_ = std::move(peer);
    ++epoch_;
    if (WSAAsyncSelect(sock_, window_, message_, kConnectionEvents) == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        Close();
        return error;
    }
    return kNoError;
}

void Connection::HandleSocketEvent(WPARAM socket, LPARAM event) {
    if (sock_ == INVALID_SOCKET || ending_ || socket != sock_) return;

    const int error = WSAGETSELECTERROR(event);
    const WORD kind = WSAGETSELECTEVENT(event);
    if (phase_ == Phase::Connecting) {
        if (kind == FD_CONNECT) OnConnectEvent(error);
        return;
    }
    switch (kind) {
    case FD_READ:
        OnReadable(error);
        return;
    case FD_WRITE:
        OnWritable(error);
        return;
    case FD_CLOSE:
        OnCloseNotice(error);
        return;
    default:
        return;
    }
}

void Connection::OnConnectEvent(int error) {
    if (error != kNoError) {
        if (ConnectFailureStands()) AdvanceAttempt(error);
        return;
    }
    if (!IsSocketConnected()) return;

    phase_ = Phase::Open;
    candidates_.clear();
    nextCandidate_ = kFirstCandidate;
    const Standing standing = Invoke([](const ConnectionHandlers& handlers) {
        if (handlers.connectResult) handlers.connectResult(kNoError);
    });
    if (standing == Standing::Intact) ResumeInbound();
}

void Connection::AdvanceAttempt(int error) {
    DiscardSocket();
    const int outcome = StartNextAttempt(error);
    if (outcome == kNoError) return;

    Close();
    Invoke([outcome](const ConnectionHandlers& handlers) {
        if (handlers.connectResult) handlers.connectResult(outcome);
    });
}

bool Connection::ConnectFailureStands() const {
    int pending = kNoError;
    int size = sizeof(pending);
    if (getsockopt(sock_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&pending), &size) == SOCKET_ERROR) return false;
    return pending != kNoError;
}

bool Connection::IsSocketConnected() const {
    DWORD seconds = kNotConnectedTime;
    int size = sizeof(seconds);
    if (getsockopt(sock_, SOL_SOCKET, SO_CONNECT_TIME, reinterpret_cast<char*>(&seconds), &size) == SOCKET_ERROR) return false;
    return seconds != kNotConnectedTime;
}

void Connection::ResumeInbound() {
    if (pendingClose_) {
        pendingClose_ = false;
        PostToSelf(FD_CLOSE, pendingCloseError_);
        return;
    }
    PostToSelf(FD_READ, kNoError);
}

void Connection::PostToSelf(long event, int error) const {
    PostMessageW(window_, message_, sock_, WSAMAKESELECTREPLY(event, error));
}

void Connection::DiscardSocket() {
    WSAAsyncSelect(sock_, window_, kDetachMessage, kDetachEvents);
    closesocket(sock_);
    sock_ = INVALID_SOCKET;
    ++epoch_;
}

void Connection::OnWritable(int error) {
    if (error != kNoError) {
        OnCloseNotice(error);
        return;
    }
    Flush();
}

void Connection::OnReadable(int error) {
    if (error != kNoError) {
        OnCloseNotice(error);
        return;
    }
    if (dispatching_) return;
    Drain(Trigger::Readable, kNoError);
}

void Connection::OnCloseNotice(int error) {
    if (dispatching_) {
        pendingClose_ = true;
        pendingCloseError_ = error;
        return;
    }
    Drain(Trigger::Closing, error);
}

void Connection::Drain(Trigger trigger, int closeError) {
    if (!Conclude(ReadInbound(), trigger, closeError)) return;
    if (!pendingClose_) return;

    const int pendingError = pendingCloseError_;
    pendingClose_ = false;
    Drain(Trigger::Closing, pendingError);
}

bool Connection::Conclude(const ReadResult& result, Trigger trigger, int closeError) {
    const bool closing = trigger == Trigger::Closing;
    switch (result.stop) {
    case ReadStop::Ended:
        return false;
    case ReadStop::More:
    case ReadStop::Backlog:
        if (closing) PostToSelf(FD_CLOSE, closeError);
        else if (result.stop == ReadStop::Backlog) PostToSelf(FD_READ, kNoError);
        return true;
    case ReadStop::PeerClosed:
        if (closeError != kNoError) Terminate(ConnectionFault::ClosedWithError, closeError);
        else Terminate();
        return false;
    case ReadStop::Failed:
        if (closeError != kNoError) Terminate(ConnectionFault::ClosedWithError, closeError);
        else Terminate(ConnectionFault::Receive, result.error);
        return false;
    case ReadStop::WouldBlock:
        return true;
    }
    return true;
}

Connection::ReadResult Connection::ReadInbound() {
    size_t budget = kMaxFramesPerEvent;
    switch (DeliverFrames(budget)) {
    case Delivery::Ended: return {ReadStop::Ended, kNoError};
    case Delivery::Exhausted: return {ReadStop::Backlog, kNoError};
    case Delivery::Drained: break;
    }

    std::string chunk(kReceiveChunkBytes, '\0');
    for (size_t round = 0; round < kMaxChunksPerEvent; ++round) {
        const int received = recv(sock_, chunk.data(), static_cast<int>(chunk.size()), kNoFlags);
        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) return {ReadStop::WouldBlock, kNoError};
            return {ReadStop::Failed, error};
        }
        if (received == kPeerClosed) return {ReadStop::PeerClosed, kNoError};
        inbound_.append(chunk.data(), static_cast<size_t>(received));
        switch (DeliverFrames(budget)) {
        case Delivery::Ended: return {ReadStop::Ended, kNoError};
        case Delivery::Exhausted: return {ReadStop::Backlog, kNoError};
        case Delivery::Drained: break;
        }
    }
    return {ReadStop::More, kNoError};
}

Connection::Delivery Connection::DeliverFrames(size_t& budget) {
    Delivery outcome = Delivery::Drained;
    for (;;) {
        if (budget == 0) {
            outcome = Delivery::Exhausted;
            break;
        }
        const std::string_view pending = std::string_view(inbound_).substr(inboundOffset_);
        DecodeResult decoded = DecodeFrame(pending);
        if (decoded.status == DecodeStatus::NeedMore) break;
        if (decoded.status == DecodeStatus::TooLarge) {
            Terminate(ConnectionFault::FrameTooLarge, kNoError);
            return Delivery::Ended;
        }
        inboundOffset_ += decoded.consumed;
        --budget;
        if (!handlers_->message) continue;

        const std::wstring text = Widen(decoded.payload);
        const Standing standing = Invoke([&text](const ConnectionHandlers& handlers) { handlers.message(text); });
        if (standing != Standing::Intact) return Delivery::Ended;
    }
    inbound_.erase(kBufferStart, inboundOffset_);
    inboundOffset_ = kBufferStart;
    return outcome;
}

int Connection::Send(const std::wstring& text) {
    if (!IsOpen() || ending_ || shuttingDown_) return kSendNotConnected;

    const std::optional<std::string> frame = EncodeFrame(Narrow(text));
    if (!frame) return kSendPayloadTooLarge;

    const size_t queued = outbound_.size() - outboundOffset_;
    if (queued + frame->size() > kMaxOutboundBytes) return kSendQueueFull;

    outbound_ += *frame;
    return Flush();
}

int Connection::Shutdown() {
    if (!IsOpen() || ending_) return kSendNotConnected;
    if (shuttingDown_) return kNoError;

    shuttingDown_ = true;
    return Flush();
}

int Connection::Flush() {
    while (IsOpen() && outboundOffset_ < outbound_.size()) {
        const size_t remaining = outbound_.size() - outboundOffset_;
        const int chunk = static_cast<int>(std::min(remaining, kMaxSendChunkBytes));
        const int sent = send(sock_, outbound_.data() + outboundOffset_, chunk, kNoFlags);
        if (sent == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) break;
            Terminate(ConnectionFault::Send, error);
            return kSendTerminated;
        }
        outboundOffset_ += static_cast<size_t>(sent);
    }
    if (outboundOffset_ == outbound_.size()) {
        outbound_.clear();
        outboundOffset_ = kNothingQueued;
    } else if (outboundOffset_ >= kCompactThresholdBytes) {
        outbound_.erase(kBufferStart, outboundOffset_);
        outboundOffset_ = kNothingQueued;
    }
    if (shuttingDown_ && !sendClosed_ && outbound_.empty()) {
        if (shutdown(sock_, SD_SEND) == SOCKET_ERROR) {
            Terminate(ConnectionFault::Send, WSAGetLastError());
            return kSendTerminated;
        }
        sendClosed_ = true;
    }
    return kNoError;
}

void Connection::Terminate() {
    const SOCKET ended = sock_;
    Close();
    NotifyClosed(ended);
}

void Connection::Terminate(ConnectionFault fault, int code) {
    const SOCKET ended = sock_;
    ending_ = true;
    const Standing standing = Invoke([fault, code](const ConnectionHandlers& handlers) {
        if (handlers.fault) handlers.fault(fault, code);
    });
    if (standing == Standing::Destroyed) return;
    if (standing == Standing::Intact) Close();
    NotifyClosed(ended);
}

void Connection::NotifyClosed(SOCKET ended) {
    Invoke([ended](const ConnectionHandlers& handlers) {
        if (handlers.closed) handlers.closed(ended);
    });
}

void Connection::Close() {
    ending_ = false;
    shuttingDown_ = false;
    sendClosed_ = false;
    pendingClose_ = false;
    phase_ = Phase::Idle;
    candidates_.clear();
    nextCandidate_ = kFirstCandidate;
    if (sock_ == INVALID_SOCKET) return;
    DiscardSocket();
    inbound_.clear();
    inboundOffset_ = kBufferStart;
    outbound_.clear();
    outboundOffset_ = kNothingQueued;
}

bool Connection::IsOpen() const {
    return phase_ == Phase::Open;
}

bool Connection::IsConnecting() const {
    return phase_ == Phase::Connecting;
}

bool Connection::IsShuttingDown() const {
    return shuttingDown_;
}

SOCKET Connection::Socket() const {
    return sock_;
}

const std::wstring& Connection::Peer() const {
    return peer_;
}

Listener::~Listener() {
    Stop();
}

int Listener::Start(unsigned short port, HWND window, UINT message) {
    if (IsListening()) return WSAEISCONN;

    SOCKET candidate = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (candidate == INVALID_SOCKET) return WSAGetLastError();

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(candidate, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        return AbortSocket(candidate);
    }
    if (listen(candidate, SOMAXCONN) == SOCKET_ERROR) return AbortSocket(candidate);
    if (WSAAsyncSelect(candidate, window, message, kListenerEvents) == SOCKET_ERROR) {
        return AbortSocket(candidate);
    }

    sock_ = candidate;
    window_ = window;
    return kNoError;
}

void Listener::Stop() {
    if (sock_ == INVALID_SOCKET) return;
    WSAAsyncSelect(sock_, window_, kDetachMessage, kDetachEvents);
    closesocket(sock_);
    sock_ = INVALID_SOCKET;
}

bool Listener::IsListening() const {
    return sock_ != INVALID_SOCKET;
}

SOCKET Listener::Socket() const {
    return sock_;
}

std::unique_ptr<Connection> Listener::Accept(HWND window, UINT message, int& error) {
    error = kNoError;
    if (!IsListening()) {
        error = WSAENOTSOCK;
        return nullptr;
    }

    sockaddr_in peer{};
    int peerSize = sizeof(peer);
    SOCKET accepted = accept(sock_, reinterpret_cast<sockaddr*>(&peer), &peerSize);
    if (accepted == INVALID_SOCKET) {
        error = WSAGetLastError();
        return nullptr;
    }

    wchar_t ip[INET_ADDRSTRLEN] = {};
    InetNtopW(AF_INET, &peer.sin_addr, ip, std::size(ip));
    const std::wstring peerText = FormatEndpoint(ip, ntohs(peer.sin_port));

    auto connection = std::make_unique<Connection>();
    error = connection->Adopt(accepted, window, message, peerText);
    if (error != kNoError) return nullptr;
    return connection;
}
