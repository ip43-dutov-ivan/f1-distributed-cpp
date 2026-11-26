#include "../Common/MetricsClient.h"
#include "../Common/Pipe.h"

#include <format>

namespace {

constexpr const wchar_t* kWindowClass = L"Lab7Client1";
constexpr const wchar_t* kWindowTitle = L"Client 1 (Named Pipe)";
constexpr const wchar_t* kCaptionSend = L"Send Metrics\tCtrl+Enter";
constexpr int kWindowOffsetX = 0;
constexpr int kWindowWidth = 460;
constexpr int kWindowHeight = 340;

std::wstring DescribeDigitizer(int flags) {
    if (flags == 0) return L"відсутній (0x00)";
    return std::format(L"наявний (0x{:02X})", static_cast<unsigned int>(flags));
}

std::wstring CollectMetrics(HWND window) {
    ScreenContext screen(window);
    const int digitizer = GetSystemMetrics(SM_DIGITIZER);
    // WinUser.h defines SM_NETWORK (63) for network presence; the lab PDF calls it SM_NETCAPTURE.
    const int network = GetSystemMetrics(SM_NETWORK);
    return std::format(
        L"Клієнт 1 | дигітайзер / сенсорний ввід (SM_DIGITIZER): {} | "
        L"стан мережі (SM_NETWORK): {} ({}) | "
        L"ширина екрана (HORZRES): {} px",
        DescribeDigitizer(digitizer), (network & 1) != 0 ? L"присутня" : L"відсутня", network,
        screen.Caps(HORZRES));
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
