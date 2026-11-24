#include "App.h"

#include "Connection.h"
#include "NetError.h"

#include <commctrl.h>

#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr LRESULT kContinueCreate = 0;
constexpr LRESULT kAbortCreate = -1;
constexpr LRESULT kHandled = 0;
constexpr LONG_PTR kNoContext = 0;
constexpr BYTE kSendModifiers = FVIRTKEY | FCONTROL;
constexpr UINT kNoMsgFilter = 0;
constexpr BOOL kQuitReceived = 0;
constexpr BOOL kReceiveFailed = -1;
constexpr DWORD kMainWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr int kNoClassExtra = 0;
constexpr UINT kNoClassStyle = 0;
constexpr const wchar_t* kErrorTitle = L"Помилка";
constexpr const wchar_t* kStartupFailedText = L"Не вдалося ініціалізувати Winsock: ";
constexpr const wchar_t* kCreateFailedText = L"Не вдалося створити вікно";

struct ShellContext {
    AppWindow* app;
    const WindowSpec* spec;
};

class AcceleratorTable {
public:
    explicit AcceleratorTable(std::span<const Shortcut> shortcuts) {
        std::vector<ACCEL> entries;
        entries.reserve(shortcuts.size());
        for (const Shortcut& shortcut : shortcuts) {
            ACCEL entry{};
            entry.fVirt = shortcut.modifiers;
            entry.key = shortcut.key;
            entry.cmd = static_cast<WORD>(shortcut.command);
            entries.push_back(entry);
        }
        if (entries.empty()) return;
        handle_ = CreateAcceleratorTableW(entries.data(), static_cast<int>(entries.size()));
    }

    ~AcceleratorTable() {
        if (handle_) DestroyAcceleratorTable(handle_);
    }

    AcceleratorTable(const AcceleratorTable&) = delete;
    AcceleratorTable& operator=(const AcceleratorTable&) = delete;

    HACCEL Handle() const {
        return handle_;
    }

private:
    HACCEL handle_ = nullptr;
};

void InitUi() {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);
}

void ShowError(const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), kErrorTitle, MB_OK | MB_ICONERROR);
}

bool IsKeyDown(const MSG& msg, WPARAM key) {
    return msg.message == WM_KEYDOWN && msg.wParam == key;
}

bool RoutesToDialogManager(const MSG& msg) {
    if (!IsKeyDown(msg, VK_RETURN)) return true;
    LRESULT code = SendMessageW(msg.hwnd, WM_GETDLGCODE, msg.wParam, reinterpret_cast<LPARAM>(&msg));
    return (code & DLGC_BUTTON) != 0;
}

int RunMessageLoop(HWND window, HACCEL accelerators) {
    MSG msg;
    for (;;) {
        BOOL status = GetMessageW(&msg, nullptr, kNoMsgFilter, kNoMsgFilter);
        if (status == kQuitReceived) return static_cast<int>(msg.wParam);
        if (status == kReceiveFailed) return kExitFailure;
        if (TranslateAcceleratorW(window, accelerators, &msg)) continue;
        if (IsKeyDown(msg, VK_ESCAPE)) continue;
        if (RoutesToDialogManager(msg) && IsDialogMessageW(window, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

ShellContext* ContextOf(HWND window) {
    return reinterpret_cast<ShellContext*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT CALLBACK ShellProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(window, message, wParam, lParam);
    }
    ShellContext* context = ContextOf(window);
    if (!context) return DefWindowProcW(window, message, wParam, lParam);
    AppWindow& app = *context->app;
    switch (message) {
    case WM_CREATE:
        return app.OnCreate(window) ? kContinueCreate : kAbortCreate;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) app.OnSize(LOWORD(lParam), HIWORD(lParam));
        return kHandled;
    case WM_COMMAND:
        app.OnCommand(wParam, lParam);
        return kHandled;
    case kSocketMessage:
        app.OnSocket(wParam, lParam);
        return kHandled;
    case WM_SETFOCUS:
        app.OnFocus();
        return kHandled;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = context->spec->minWidth;
        info->ptMinTrackSize.y = context->spec->minHeight;
        return kHandled;
    }
    case WM_CTLCOLORSTATIC: {
        HBRUSH brush = PaintReadOnlyEdit(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
        if (brush) return reinterpret_cast<LRESULT>(brush);
        break;
    }
    case WM_DESTROY:
        app.OnDestroy();
        PostQuitMessage(kExitSuccess);
        return kHandled;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, kNoContext);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateShellWindow(HINSTANCE instance, const WindowSpec& spec, ShellContext& context) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = kNoClassStyle;
    windowClass.lpfnWndProc = ShellProc;
    windowClass.cbClsExtra = kNoClassExtra;
    windowClass.cbWndExtra = kNoClassExtra;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    windowClass.lpszClassName = spec.className;
    if (!RegisterClassExW(&windowClass)) return nullptr;
    return CreateWindowExW(kNoExStyle, spec.className, spec.title, kMainWindowStyle,
        spec.x, spec.y, spec.width, spec.height, nullptr, nullptr, instance, &context);
}

HMENU BuildPopup(std::span<const MenuItem> items) {
    HMENU popup = CreatePopupMenu();
    if (!popup) return nullptr;
    for (const MenuItem& item : items) {
        if (!AppendMenuW(popup, MF_STRING, static_cast<UINT_PTR>(item.command), item.caption)) {
            DestroyMenu(popup);
            return nullptr;
        }
    }
    return popup;
}

}

Shortcut SendShortcut(CommandId command) {
    return Shortcut{VK_RETURN, kSendModifiers, command};
}

RECT WorkArea() {
    RECT area{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, kNoFlags, &area, kNoFlags)) {
        area.right = GetSystemMetrics(SM_CXSCREEN);
        area.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return area;
}

bool InstallMenuBar(HWND window, std::span<const MenuPopup> popups) {
    HMENU bar = CreateMenu();
    if (!bar) return false;
    for (const MenuPopup& entry : popups) {
        HMENU popup = BuildPopup(entry.items);
        if (!popup) {
            DestroyMenu(bar);
            return false;
        }
        if (!AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), entry.caption)) {
            DestroyMenu(popup);
            DestroyMenu(bar);
            return false;
        }
    }
    if (!SetMenu(window, bar)) {
        DestroyMenu(bar);
        return false;
    }
    return true;
}

void SetCommandEnabled(HWND window, CommandId command, bool enabled) {
    EnableMenuItem(GetMenu(window), static_cast<UINT>(command), MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
}

int RunApp(HINSTANCE instance, int showCommand, const WindowSpec& spec, AppWindow& app,
    std::span<const Shortcut> shortcuts) {
    WinsockSession session;
    if (session.Error() != kNoError) {
        ShowError(kStartupFailedText + DescribeWinsockError(session.Error()));
        return kExitFailure;
    }
    InitUi();

    AcceleratorTable accelerators(shortcuts);
    ShellContext context{&app, &spec};
    HWND window = CreateShellWindow(instance, spec, context);
    if (!window) {
        ShowError(kCreateFailedText);
        return kExitFailure;
    }
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    return RunMessageLoop(window, accelerators.Handle());
}
