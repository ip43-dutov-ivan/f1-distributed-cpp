#pragma once

#include "ClientShell.h"

#include <functional>
#include <string>
#include <utility>

class MetricsClient : public ClientShell {
public:
    using Collector = std::function<std::wstring(HWND)>;

    MetricsClient(std::wstring host, ClientProfile profile, Collector collector)
        : ClientShell(std::move(host), profile), collector_(std::move(collector)) {}

protected:
    std::wstring PayloadToSend() override {
        return collector_(Window());
    }

    std::wstring OpenHint() const override {
        return L"Client → Send Metrics, щоб надіслати дані";
    }

private:
    Collector collector_;
};
