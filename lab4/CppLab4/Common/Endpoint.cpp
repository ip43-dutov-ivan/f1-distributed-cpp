#include "Endpoint.h"

#include <string_view>

namespace {

constexpr std::wstring_view kTrimmedCharacters = L" \t\"";

}

std::wstring FormatEndpoint(const std::wstring& host, unsigned short port) {
    return host + L":" + std::to_wstring(port);
}

std::wstring HostFromCommandLine(const wchar_t* commandLine) {
    if (commandLine == nullptr) return kDefaultHost;

    const std::wstring_view text(commandLine);
    const size_t first = text.find_first_not_of(kTrimmedCharacters);
    if (first == std::wstring_view::npos) return kDefaultHost;

    const size_t last = text.find_last_not_of(kTrimmedCharacters);
    return std::wstring(text.substr(first, last - first + 1));
}
