#pragma once

#include "ConnectionTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

inline constexpr UINT kSocketMessage = WM_APP + 1;

// Handlers run synchronously on the window thread and may call Close() or destroy the
// Connection; delivery stops once they do. Incoming data is not read while a handler runs,
// and a peer close that arrives meanwhile is processed after it returns.
// closed receives the socket that ended, because Socket() is already invalid by then.
struct ConnectionHandlers {
    std::function<void(const std::wstring&)> message;
    std::function<void(ConnectionFault, int)> fault;
    std::function<void(SOCKET)> closed;
    std::function<void(int)> connectResult;
};

class WinsockSession {
public:
    WinsockSession();
    ~WinsockSession();
    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;

    int Error() const;

private:
    int error_ = kNoError;
};

// One TCP link carrying length-prefixed text frames. Socket events are delivered to the
// owner's window as `message` and must be forwarded to HandleSocketEvent.
class Connection {
public:
    Connection();
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void SetHandlers(ConnectionHandlers handlers);
    // Returns once the attempt has started; the outcome arrives in connectResult.
    // Each resolved IPv4 address is tried in turn.
    int Connect(const std::wstring& host, unsigned short port, HWND window, UINT message);
    void HandleSocketEvent(WPARAM socket, LPARAM event);
    // Queues the frame and sends what the socket accepts now. A send failure invokes the
    // fault and closed handlers before Send returns kSendTerminated.
    int Send(const std::wstring& text);
    // Half-close: sends queued data, then SD_SEND. Further Send calls are rejected.
    int Shutdown();
    // Drops the socket and buffers without invoking any handler.
    void Close();

    bool IsOpen() const;
    bool IsConnecting() const;
    bool IsShuttingDown() const;
    SOCKET Socket() const;
    const std::wstring& Peer() const;

private:
    friend class Listener;

    enum class Phase { Idle, Connecting, Open };
    enum class ReadStop { WouldBlock, More, Backlog, PeerClosed, Failed, Ended };
    enum class Delivery { Drained, Exhausted, Ended };
    enum class Trigger { Readable, Closing };
    enum class Standing { Intact, Superseded, Destroyed };

    struct ReadResult {
        ReadStop stop = ReadStop::WouldBlock;
        int error = kNoError;
    };

    int Adopt(SOCKET socket, HWND window, UINT message, std::wstring peer);
    template <typename Call>
    Standing Invoke(Call&& call);
    int StartNextAttempt(int lastError);
    void OnConnectEvent(int error);
    void AdvanceAttempt(int error);
    bool ConnectFailureStands() const;
    bool IsSocketConnected() const;
    void ResumeInbound();
    void PostToSelf(long event, int error) const;
    void DiscardSocket();
    void OnWritable(int error);
    void OnReadable(int error);
    void OnCloseNotice(int error);
    void Drain(Trigger trigger, int closeError);
    bool Conclude(const ReadResult& result, Trigger trigger, int closeError);
    ReadResult ReadInbound();
    Delivery DeliverFrames(size_t& budget);
    int Flush();
    void Terminate();
    void Terminate(ConnectionFault fault, int code);
    void NotifyClosed(SOCKET ended);

    SOCKET sock_ = INVALID_SOCKET;
    HWND window_ = nullptr;
    UINT message_ = 0;
    Phase phase_ = Phase::Idle;
    std::wstring peer_;
    std::vector<sockaddr_in> candidates_;
    size_t nextCandidate_ = 0;
    std::string inbound_;
    size_t inboundOffset_ = 0;
    std::string outbound_;
    size_t outboundOffset_ = 0;
    std::shared_ptr<ConnectionHandlers> handlers_;
    std::shared_ptr<bool> alive_;
    uint64_t epoch_ = 0;
    bool dispatching_ = false;
    bool ending_ = false;
    bool shuttingDown_ = false;
    bool sendClosed_ = false;
    bool pendingClose_ = false;
    int pendingCloseError_ = kNoError;
};

class Listener {
public:
    Listener() = default;
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    int Start(unsigned short port, HWND window, UINT message);
    void Stop();
    bool IsListening() const;
    SOCKET Socket() const;
    // Returns a Connection that is already open and bound to the same window and message.
    std::unique_ptr<Connection> Accept(HWND window, UINT message, int& error);

private:
    SOCKET sock_ = INVALID_SOCKET;
    HWND window_ = nullptr;
};
