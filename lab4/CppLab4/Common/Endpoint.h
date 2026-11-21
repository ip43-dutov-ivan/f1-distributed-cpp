#pragma once

#include <string>

inline constexpr unsigned short kDefaultPort = 5000;
inline constexpr const wchar_t* kDefaultHost = L"localhost";

std::wstring FormatEndpoint(const std::wstring& host, unsigned short port);
std::wstring HostFromCommandLine(const wchar_t* commandLine);
