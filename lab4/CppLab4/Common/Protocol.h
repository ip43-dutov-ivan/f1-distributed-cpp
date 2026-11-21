#pragma once

#include "Units.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

inline constexpr size_t kFrameHeaderSize = 4;
inline constexpr uint32_t kMaxPayloadMiB = 16;
inline constexpr uint32_t kMaxPayloadBytes = kMaxPayloadMiB * kBytesPerMiB;

enum class DecodeStatus { NeedMore, Frame, TooLarge };

struct DecodeResult {
    DecodeStatus status = DecodeStatus::NeedMore;
    std::string payload;
    size_t consumed = 0;
};

std::optional<std::string> EncodeFrame(const std::string& payload);
DecodeResult DecodeFrame(std::string_view buffer);
