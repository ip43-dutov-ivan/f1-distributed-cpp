#pragma once

#include "ClientShell.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

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

class MetricsClient : public ClientShell {
public:
    using Collector = std::function<std::wstring(HWND)>;

    MetricsClient(std::wstring host, ClientProfile profile, Collector collector)
        : ClientShell(std::move(host), profile), collector_(std::move(collector)) {}

protected:
    bool CreateControls(HWND window, HINSTANCE instance) override {
        return sendButton_.Create(window, instance, kClientSendCommand, L"Send Metrics (Ctrl+Enter)");
    }

    RECT LayoutControls(RECT content) override {
        const int width = std::max(static_cast<int>(content.right - content.left), 0);
        const int height = std::max(static_cast<int>(content.bottom - content.top), 0);
        const int buttonHeight = std::min(kButtonHeight, height);
        const int logHeight = std::max(height - buttonHeight - kGap, 0);
        sendButton_.Move(content.left, content.top + height - buttonHeight, width, buttonHeight);
        return RECT{content.left, content.top, content.left + width, content.top + logHeight};
    }

    std::wstring PayloadToSend() override {
        return collector_(Window());
    }

    std::wstring OpenHint() const override {
        return L"Натисніть Send Metrics або Ctrl+Enter, щоб надіслати дані";
    }

private:
    Collector collector_;
    Button sendButton_;
};
