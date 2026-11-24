#include "../Common/Mailslot.h"
#include "../Common/MetricsClient.h"

#include <format>

namespace {

constexpr const wchar_t* kWindowClass = L"Lab5Client1";
constexpr const wchar_t* kWindowTitle = L"Client 1 (Mailslot)";
constexpr const wchar_t* kCaptionSend = L"Send Metrics\tCtrl+Enter";
constexpr int kWindowOffsetX = 0;
constexpr int kWindowWidth = 460;
constexpr int kWindowHeight = 340;

const wchar_t* DescribeCleanBoot(int mode) {
    switch (mode) {
    case 0: return L"звичайний (0)";
    case 1: return L"безпечний (1)";
    case 2: return L"безпечний з мережею (2)";
    default: return L"невідомий";
    }
}

std::wstring CollectMetrics(HWND window) {
    ScreenContext screen(window);
    const int cleanBoot = GetSystemMetrics(SM_CLEANBOOT);
    return std::format(
        L"Клієнт 1 | режим завантаження / час системи (SM_CLEANBOOT): {} | "
        L"мін. ширина вікна / стан антивірусу (SM_CXMIN): {} px | "
        L"ширина екрана (HORZRES): {} px",
        DescribeCleanBoot(cleanBoot), GetSystemMetrics(SM_CXMIN), screen.Caps(HORZRES));
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
