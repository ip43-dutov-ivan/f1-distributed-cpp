#pragma once

#include "Win.h"

inline constexpr int kNoError = 0;
inline constexpr int kOwnErrorBase = 0x20000000;
inline constexpr int kSendNotConnected = WSAENOTCONN;
inline constexpr int kSendPayloadTooLarge = WSAEMSGSIZE;
inline constexpr int kSendQueueFull = WSAENOBUFS;
inline constexpr int kSendTerminated = kOwnErrorBase + 1;

enum class ConnectionFault { Receive, Send, ClosedWithError, FrameTooLarge };
