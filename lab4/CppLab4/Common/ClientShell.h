#pragma once

#include "App.h"
#include "Connection.h"
#include "Ui.h"

#include <optional>
#include <span>
#include <string>

inline constexpr CommandId kClientSendCommand = 102;
inline constexpr CommandId kFirstSubclassCommand = 200;
inline constexpr int kDefaultClientWidth = 520;
inline constexpr int kDefaultClientHeight = 460;

struct ClientProfile {
    const wchar_t* className;
    const wchar_t* title;
    int windowOffsetX;
    const wchar_t* sendMenuCaption;
    int windowWidth = kDefaultClientWidth;
    int windowHeight = kDefaultClientHeight;
};

// Template method for client windows: the shell owns the window, menu, log, status bar and
// connection lifecycle; subclasses supply the payload and any extra controls through the
// virtual hooks below. All hooks run on the window thread.
class ClientShell : public AppWindow {
public:
    ClientShell(std::wstring host, ClientProfile profile);
    ~ClientShell() override = default;

    bool OnCreate(HWND window) final;
    void OnSize(int width, int height) final;
    void OnCommand(WPARAM wParam, LPARAM lParam) final;
    void OnSocket(WPARAM wParam, LPARAM lParam) final;
    void OnDestroy() final;
    void OnFocus() final;

    WindowSpec Spec() const;

protected:
    enum class Phase { Idle, Connecting, Open, Closing };

    virtual bool CreateControls(HWND window, HINSTANCE instance);
    virtual RECT LayoutControls(RECT content);
    virtual bool HandleControlCommand(WPARAM wParam, LPARAM lParam);
    virtual std::span<const MenuItem> ExtraClientItems() const;
    virtual bool OnExtraCommand(CommandId command);
    virtual void SyncControls(Phase phase);
    virtual void FocusPrimaryInput();
    // Called only when the connection is open and CanSend() is true.
    virtual bool CanSend() const;
    virtual std::wstring PayloadToSend() = 0;
    // Called after PayloadToSend() was handed to the connection successfully.
    virtual void OnSent();
    virtual std::wstring OpenHint() const;

    HWND Window() const;
    void Refresh();
    void Log(LogKind kind, const std::wstring& text);
    void Notice(const std::wstring& text);
    bool Send(const std::wstring& text);

private:
    ConnectionHandlers MakeHandlers();
    Phase CurrentPhase() const;
    std::wstring Target() const;
    std::wstring StateText(Phase phase) const;
    std::wstring HintText(Phase phase) const;
    void ShowHint(const std::wstring& hint);
    void SetConnection();
    void OnConnectResult(int code);
    void NoticeSendBlocked(Phase phase);
    void SendPayload();
    void Disconnect();
    void OnClosed();
    void StartTimer();
    void StopTimer();
    void OnDisconnectTimeout(UINT_PTR id);
    static void CALLBACK OnTimer(HWND window, UINT, UINT_PTR id, DWORD);

    HWND window_ = nullptr;
    std::wstring host_;
    ClientProfile profile_;
    LogView log_;
    StatusBar status_;
    Connection connection_;
    std::optional<std::wstring> shownHint_;
    bool created_ = false;
    bool disconnectRequested_ = false;
    UINT_PTR timerId_ = 0;
    UINT_PTR lastTimerId_ = 0;
};

int RunClient(HINSTANCE instance, int showCommand, ClientShell& shell);
