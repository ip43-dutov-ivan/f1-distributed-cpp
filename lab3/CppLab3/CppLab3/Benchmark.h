#pragma once

#include "framework.h"
#include <string>
#include <vector>

constexpr UINT WM_BENCHMARK_DONE = WM_APP + 3;

void BenchmarkStart(HWND notifyWnd);
bool BenchmarkIsRunning();
std::vector<std::wstring> BenchmarkResultLines();
