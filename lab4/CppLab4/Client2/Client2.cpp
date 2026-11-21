#include "../Common/Endpoint.h"
#include "../Common/MetricsClient.h"

#include <format>

namespace {

constexpr const wchar_t* kWindowClass = L"Lab4Client2";
constexpr const wchar_t* kWindowTitle = L"Client 2";
constexpr const wchar_t* kCaptionSend = L"Send Metrics\tCtrl+Enter";
constexpr int kWindowOffsetX = 420;
constexpr int kWindowWidth = 420;
constexpr int kWindowHeight = 320;

class ScreenContext {
public:
    explicit ScreenContext(HWND window) : window_(window), dc_(GetDC(window)) {}
    ~ScreenContext() {
        if (dc_) ReleaseDC(window_, dc_);
    }
    ScreenContext(const ScreenContext&) = delete;
    ScreenContext& operator=(const ScreenContext&) = delete;

    int Caps(int index) const {
        return dc_ ? GetDeviceCaps(dc_, index) : 0;
    }

private:
    HWND window_;
    HDC dc_;
};

std::wstring CollectMetrics(HWND window) {
    ScreenContext screen(window);
    return std::format(
        L"Клієнт 2 | ширина робочої області на весь екран (SM_CXFULLSCREEN): {} px | "
        L"кількість моніторів (SM_CMONITORS): {} | "
        L"DPI по горизонталі (LOGPIXELSX): {}",
        GetSystemMetrics(SM_CXFULLSCREEN), GetSystemMetrics(SM_CMONITORS), screen.Caps(LOGPIXELSX));
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
