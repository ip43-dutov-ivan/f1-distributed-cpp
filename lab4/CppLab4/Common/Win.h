#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>

inline constexpr int kNoFlags = 0;
inline constexpr DWORD kNoExStyle = 0;
inline constexpr WPARAM kUnusedWParam = 0;
inline constexpr LPARAM kUnusedLParam = 0;
inline constexpr BOOL kRedraw = TRUE;
