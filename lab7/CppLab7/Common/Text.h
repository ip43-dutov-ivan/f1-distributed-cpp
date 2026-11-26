#pragma once

#include "Win.h"

#include <string>

namespace detail {

inline constexpr int kQuerySize = 0;

}

inline std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, kNoFlags, utf8.data(), static_cast<int>(utf8.size()), nullptr,
        detail::kQuerySize);
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, kNoFlags, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

inline std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    int length = WideCharToMultiByte(CP_UTF8, kNoFlags, wide.data(), static_cast<int>(wide.size()), nullptr,
        detail::kQuerySize, nullptr, nullptr);
    std::string utf8(length, '\0');
    WideCharToMultiByte(CP_UTF8, kNoFlags, wide.data(), static_cast<int>(wide.size()), utf8.data(), length,
        nullptr, nullptr);
    return utf8;
}
