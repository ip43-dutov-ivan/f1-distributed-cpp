#include "Protocol.h"

namespace {

constexpr unsigned kByteBits = 8;
constexpr uint32_t kByteMask = 0xFF;
constexpr size_t kFirstByte = 0;
constexpr size_t kLastByte = kFrameHeaderSize - 1;

}

std::optional<std::string> EncodeFrame(const std::string& payload) {
    if (payload.size() > kMaxPayloadBytes) return std::nullopt;

    const uint32_t length = static_cast<uint32_t>(payload.size());
    std::string frame;
    frame.reserve(kFrameHeaderSize + payload.size());
    for (size_t i = kFirstByte; i <= kLastByte; ++i) {
        const unsigned shift = static_cast<unsigned>(kLastByte - i) * kByteBits;
        frame.push_back(static_cast<char>((length >> shift) & kByteMask));
    }
    frame += payload;
    return frame;
}

DecodeResult DecodeFrame(std::string_view buffer) {
    DecodeResult result;
    if (buffer.size() < kFrameHeaderSize) return result;

    uint32_t length = 0;
    for (size_t i = kFirstByte; i <= kLastByte; ++i) {
        length = (length << kByteBits) | static_cast<unsigned char>(buffer[i]);
    }
    if (length > kMaxPayloadBytes) {
        result.status = DecodeStatus::TooLarge;
        return result;
    }
    if (buffer.size() - kFrameHeaderSize < length) return result;

    result.payload.assign(buffer.substr(kFrameHeaderSize, length));
    result.consumed = kFrameHeaderSize + length;
    result.status = DecodeStatus::Frame;
    return result;
}
