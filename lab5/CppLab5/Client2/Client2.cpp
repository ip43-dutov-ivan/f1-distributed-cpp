#include "../Common/Mailslot.h"
#include "../Common/MetricsClient.h"

#include <format>

namespace {

constexpr const wchar_t* kWindowClass = L"Lab5Client2";
constexpr const wchar_t* kWindowTitle = L"Client 2 (Mailslot)";
constexpr const wchar_t* kCaptionSend = L"Send Metrics\tCtrl+Enter";
constexpr int kWindowOffsetX = 470;
constexpr int kWindowWidth = 460;
constexpr int kWindowHeight = 340;

std::wstring CollectMetrics(HWND window) {
    ScreenContext screen(window);
    return std::format(
        // WinUser.h defines SM_CYMENUSIZE (height of menu bar buttons); the lab PDF calls it SM_CYBUTTON.
        L"Клієнт 2 | висота кнопок у меню (SM_CYMENUSIZE): {} px | "
        L"ширина кнопки «Пуск» / мін. ширина (SM_CXMINTRACK): {} px | "
        L"кількість рядків екрана (VERTRES): {} px",
        GetSystemMetrics(SM_CYMENUSIZE), GetSystemMetrics(SM_CXMINTRACK), screen.Caps(VERTRES));
}

}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int showCommand) {
    MetricsClient app(HostFromCommandLine(commandLine),
        {.className = kWindowClass,
            .title = kWindowTitle,
            .windowOffsetX = kWindowOffsetX,
            .sendMenuCaption = kCaptionSend,
            .windowWidth = kWindowWidth,
            .windowHeight = kWindowHeight},
        CollectMetrics);
    return RunClient(instance, showCommand, app);
}
