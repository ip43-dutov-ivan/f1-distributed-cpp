#include "../Common/MetricsClient.h"
#include "../Common/Pipe.h"

#include <format>

namespace {

constexpr const wchar_t* kWindowClass = L"Lab7Client2";
constexpr const wchar_t* kWindowTitle = L"Client 2 (Named Pipe)";
constexpr const wchar_t* kCaptionSend = L"Send Metrics\tCtrl+Enter";
constexpr int kWindowOffsetX = 470;
constexpr int kWindowWidth = 460;
constexpr int kWindowHeight = 340;

std::wstring DescribeColors(int numColors) {
    if (numColors < 0) return std::format(L"{} (True Color, >8 bpp)", numColors);
    return std::format(L"{}", numColors);
}

std::wstring CollectMetrics(HWND window) {
    ScreenContext screen(window);
    return std::format(
        L"Клієнт 2 | висота малого заголовка / шрифт меню (SM_CYSMCAPTION): {} px | "
        L"товщина рамки вікна (SM_CXFIXEDFRAME): {} px | "
        L"кількість кольорів (NUMCOLORS): {}",
        GetSystemMetrics(SM_CYSMCAPTION), GetSystemMetrics(SM_CXFIXEDFRAME),
        DescribeColors(screen.Caps(NUMCOLORS)));
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
