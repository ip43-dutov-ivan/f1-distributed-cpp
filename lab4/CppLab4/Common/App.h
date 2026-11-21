#pragma once

#include "Ui.h"
#include "Win.h"

#include <cstdlib>
#include <span>

inline constexpr int kExitSuccess = EXIT_SUCCESS;
inline constexpr int kExitFailure = EXIT_FAILURE;

// Window callbacks invoked by RunApp's window procedure.
class AppWindow {
public:
    virtual ~AppWindow() = default;
    // Returning false aborts window creation.
    virtual bool OnCreate(HWND window) = 0;
    virtual void OnSize(int width, int height) = 0;
    virtual void OnCommand(WPARAM wParam, LPARAM lParam) = 0;
    // Receives kSocketMessage; forward it to the owning Connection or Listener.
    virtual void OnSocket(WPARAM wParam, LPARAM lParam) = 0;
    virtual void OnDestroy() = 0;
    virtual void OnFocus() {}
};

struct WindowSpec {
    const wchar_t* className;
    const wchar_t* title;
    int x, y, width, height;
    int minWidth, minHeight;
};

struct Shortcut {
    WORD key;
    BYTE modifiers;
    CommandId command;
};

struct MenuItem {
    CommandId command;
    const wchar_t* caption;
};

struct MenuPopup {
    const wchar_t* caption;
    std::span<const MenuItem> items;
};

Shortcut SendShortcut(CommandId command);
RECT WorkArea();
bool InstallMenuBar(HWND window, std::span<const MenuPopup> popups);
void SetCommandEnabled(HWND window, CommandId command, bool enabled);
int RunApp(HINSTANCE instance, int showCommand, const WindowSpec& spec, AppWindow& app,
    std::span<const Shortcut> shortcuts);
