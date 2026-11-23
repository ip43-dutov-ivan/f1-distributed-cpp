#pragma once

#include "framework.h"
#include <string>
#include <vector>

constexpr UINT WM_TIME_UPDATED = WM_APP + 1;
constexpr UINT WM_WORKER_FAULT = WM_APP + 2;

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

void WorkersPause();
void WorkersResume();
bool WorkersIsPaused();

bool WorkersSimulateCrash();
void WorkersHandleFault(DWORD threadId, DWORD exitCode);

void WorkersHandleTime(DWORD threadId);
int WorkersCount(WorkerKind kind);
std::vector<std::wstring> WorkersDisplayLines();
std::wstring WorkersStatusLine();

void WorkersLog(const std::string& message);
