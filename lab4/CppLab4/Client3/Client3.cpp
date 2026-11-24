#include "../Common/ClientShell.h"
#include "../Common/Endpoint.h"

#include <string>
#include <utility>

namespace {

constexpr const wchar_t* kWindowClass = L"Lab4Client3";
constexpr const wchar_t* kWindowTitle = L"Client 3";
constexpr const wchar_t* kCaptionSendMessage = L"Send Message\tCtrl+Enter";
constexpr const wchar_t* kCaptionSendButton = L"Send\nCtrl+Enter";
constexpr const wchar_t* kTextHintCompose = L"Введіть текст і натисніть Ctrl+Enter";
constexpr int kWindowOffsetX = 840;

class TextClient : public ClientShell {
public:
    explicit TextClient(std::wstring host)
        : ClientShell(std::move(host), {
              .className = kWindowClass,
              .title = kWindowTitle,
              .windowOffsetX = kWindowOffsetX,
              .sendMenuCaption = kCaptionSendMessage,
          }) {}

protected:
    bool CreateControls(HWND window, HINSTANCE instance) override {
        return compose_.Create(window, instance, kClientSendCommand, kCaptionSendButton);
    }

    RECT LayoutControls(RECT content) override {
        return LayoutComposeBelow(compose_, content);
    }

    bool HandleControlCommand(WPARAM wParam, LPARAM lParam) override {
        return compose_.HandleCommand(wParam, lParam);
    }

    void SyncControls(Phase phase) override {
        compose_.SetSendEnabled(phase == Phase::Open);
    }

    void FocusPrimaryInput() override {
        compose_.Focus();
    }

    bool CanSend() const override {
        return !compose_.IsEmpty();
    }

    std::wstring PayloadToSend() override {
        return compose_.Text();
    }

    void OnSent() override {
        compose_.Clear();
        compose_.Focus();
    }

    std::wstring OpenHint() const override {
        return compose_.IsEmpty() ? kTextHintCompose : L"";
    }

private:
    ComposePanel compose_;
};

}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int showCommand) {
    TextClient app(HostFromCommandLine(commandLine));
    return RunClient(instance, showCommand, app);
}
