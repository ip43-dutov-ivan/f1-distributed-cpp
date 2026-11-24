#pragma once

#include "App.h"
#include "Mailslot.h"
#include "Ui.h"

#include <optional>
#include <span>
#include <string>

inline constexpr CommandId kClientSendCommand = 102;
inline constexpr CommandId kFirstSubclassCommand = 200;
inline constexpr int kDefaultClientWidth = 500;
inline constexpr int kDefaultClientHeight = 380;

struct ClientProfile {
    const wchar_t* className;
    const wchar_t* title;
    int windowOffsetX;
    const wchar_t* sendMenuCaption;
    int windowWidth = kDefaultClientWidth;
    int windowHeight = kDefaultClientHeight;
};

// Template method for mailslot client windows: the shell owns the window, menu, log and
// status bar; subclasses supply the payload and any extra controls through the virtual hooks.
class ClientShell : public AppWindow {
public:
    ClientShell(std::wstring host, ClientProfile profile);
    ~ClientShell() override = default;

    bool OnCreate(HWND window) final;
    void OnSize(int width, int height) final;
    void OnCommand(WPARAM wParam, LPARAM lParam) final;
    void OnDestroy() final;
    void OnFocus() final;

    WindowSpec Spec() const;

protected:
    virtual bool CreateControls(HWND window, HINSTANCE instance);
    virtual RECT LayoutControls(RECT content);
    virtual bool HandleControlCommand(WPARAM wParam, LPARAM lParam);
    virtual std::span<const MenuItem> ExtraClientItems() const;
    virtual bool OnExtraCommand(CommandId command);
    virtual void SyncControls();
    virtual void FocusPrimaryInput();
    virtual bool CanSend() const;
    virtual std::wstring PayloadToSend() = 0;
    virtual void OnSent();
    virtual std::wstring OpenHint() const;

    HWND Window() const;
    void Refresh();
    void Log(LogKind kind, const std::wstring& text);
    void Notice(const std::wstring& text);
    bool Send(const std::wstring& text);

private:
    std::wstring Target() const;
    void ShowHint(const std::wstring& hint);
    void SendPayload();

    HWND window_ = nullptr;
    std::wstring host_;
    ClientProfile profile_;
    LogView log_;
    StatusBar status_;
    std::optional<std::wstring> shownHint_;
    bool created_ = false;
};

int RunClient(HINSTANCE instance, int showCommand, ClientShell& shell);
