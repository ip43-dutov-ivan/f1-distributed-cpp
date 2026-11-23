#pragma once

#include "framework.h"
#include <string>
#include <vector>

constexpr UINT WM_TIME_UPDATED = WM_APP + 1;

enum class WorkerKind
{
    Reader,
    Display,
};

void WorkersInit(HWND notifyWnd);
void WorkersShutdown();

void WorkersStart();
void WorkersStopAll();
bool WorkersAdd(WorkerKind kind);
bool WorkersRemove(WorkerKind kind);

void WorkersHandleTime(DWORD threadId);
int WorkersCount(WorkerKind kind);
std::vector<std::wstring> WorkersDisplayLines();
