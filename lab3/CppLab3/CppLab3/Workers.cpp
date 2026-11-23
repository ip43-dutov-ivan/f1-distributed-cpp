#include "pch.h"
#include "Workers.h"
#include <algorithm>
#include <atomic>
#include <format>
#include <memory>

namespace
{
    constexpr DWORD ReaderPeriodMs = 1000;
    constexpr DWORD DisplayPeriodMs = 1000;
    constexpr DWORD StopTimeoutMs = 3000;
    constexpr LPARAM NoTime = -1;
    constexpr size_t MaxWorkers = MAXIMUM_WAIT_OBJECTS - 1;

    struct SharedTime
    {
        bool valid;
        WORD hour;
        WORD minute;
        WORD second;
    };

    SharedTime g_time{};
    CRITICAL_SECTION g_timeLock;
    HANDLE g_logMutex = nullptr;
    HANDLE g_runEvent = nullptr;
    HANDLE g_console = INVALID_HANDLE_VALUE;
    HWND g_notifyWnd = nullptr;
    std::atomic<bool> g_paused{false};
    std::wstring g_statusLine;

    HANDLE g_monitorThread = nullptr;
    HANDLE g_monitorStopEvent = nullptr;

    class TimeLock
    {
    public:
        TimeLock() { EnterCriticalSection(&g_timeLock); }
        ~TimeLock() { LeaveCriticalSection(&g_timeLock); }
        TimeLock(const TimeLock&) = delete;
        TimeLock& operator=(const TimeLock&) = delete;
    };

    class MutexLock
    {
    public:
        explicit MutexLock(HANDLE mutex) : m_mutex(mutex)
        {
            if (m_mutex) WaitForSingleObject(m_mutex, INFINITE);
        }
        ~MutexLock()
        {
            if (m_mutex) ReleaseMutex(m_mutex);
        }
        MutexLock(const MutexLock&) = delete;
        MutexLock& operator=(const MutexLock&) = delete;

    private:
        HANDLE m_mutex;
    };

    struct Worker
    {
        WorkerKind kind;
        HANDLE thread = nullptr;
        DWORD threadId = 0;
        HANDLE stopEvent = nullptr;
        HANDLE crashEvent = nullptr;
        std::wstring text = L"--:--:--";
        std::atomic<LPARAM> latest{NoTime};
        std::atomic<bool> updatePending{false};

        explicit Worker(WorkerKind k) : kind(k) {}
        Worker(const Worker&) = delete;
        Worker& operator=(const Worker&) = delete;
        ~Worker()
        {
            if (thread) CloseHandle(thread);
            if (stopEvent) CloseHandle(stopEvent);
            if (crashEvent) CloseHandle(crashEvent);
        }
    };

    std::vector<std::shared_ptr<Worker>> g_workers;

    void Log(const std::string& message)
    {
        if (g_console == INVALID_HANDLE_VALUE || !g_logMutex) return;

        const auto line = std::format("[thread {}] {}\n", GetCurrentThreadId(), message);
        MutexLock lock(g_logMutex);
        DWORD written = 0;
        WriteConsoleA(g_console, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }

    void OpenConsole()
    {
        if (!AllocConsole()) return;

        g_console = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_EXTENDED_FLAGS);
        SetConsoleTitleW(L"Thread log");

        if (HWND consoleWnd = GetConsoleWindow())
        {
            DeleteMenu(GetSystemMenu(consoleWnd, FALSE), SC_CLOSE, MF_BYCOMMAND);
            DrawMenuBar(consoleWnd);
        }
    }

    enum class WaitOutcome
    {
        Continue,
        Stop,
        Crash,
    };

    WaitOutcome WaitForNextCycle(const Worker& worker, DWORD periodMs)
    {
        HANDLE cycleHandles[2] = { worker.stopEvent, worker.crashEvent };
        const DWORD cycleResult = WaitForMultipleObjects(2, cycleHandles, FALSE, periodMs);
        if (cycleResult == WAIT_OBJECT_0) return WaitOutcome::Stop;
        if (cycleResult == WAIT_OBJECT_0 + 1) return WaitOutcome::Crash;

        HANDLE runHandles[3] = { worker.stopEvent, worker.crashEvent, g_runEvent };
        const DWORD runResult = WaitForMultipleObjects(3, runHandles, FALSE, INFINITE);
        if (runResult == WAIT_OBJECT_0) return WaitOutcome::Stop;
        if (runResult == WAIT_OBJECT_0 + 1) return WaitOutcome::Crash;
        return WaitOutcome::Continue;
    }

    DWORD RunReader(const Worker& worker)
    {
        Log("reader started");
        WaitOutcome outcome = WaitOutcome::Continue;
        do
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            {
                TimeLock lock;
                g_time = { true, st.wHour, st.wMinute, st.wSecond };
            }
            Log(std::format("reader wrote {:02}:{:02}:{:02}", st.wHour, st.wMinute, st.wSecond));
            outcome = WaitForNextCycle(worker, ReaderPeriodMs);
        } while (outcome == WaitOutcome::Continue);

        if (outcome == WaitOutcome::Crash)
        {
            Log("reader abnormal exit (code 1)");
            return 1;
        }
        Log("reader finished");
        return 0;
    }

    DWORD RunDisplay(Worker& worker)
    {
        Log("display started");
        WaitOutcome outcome = WaitOutcome::Continue;
        do
        {
            SharedTime snapshot;
            {
                TimeLock lock;
                snapshot = g_time;
            }

            LPARAM packed = NoTime;
            if (snapshot.valid)
            {
                packed = (snapshot.hour << 16) | (snapshot.minute << 8) | snapshot.second;
                Log(std::format("display read {:02}:{:02}:{:02}", snapshot.hour, snapshot.minute, snapshot.second));
            }
            else
            {
                Log("display read nothing yet");
            }

            worker.latest.store(packed);
            if (!worker.updatePending.exchange(true))
            {
                if (!PostMessageW(g_notifyWnd, WM_TIME_UPDATED, GetCurrentThreadId(), 0))
                {
                    worker.updatePending.store(false);
                    Log(std::format("display post failed, error {}", GetLastError()));
                }
            }
            outcome = WaitForNextCycle(worker, DisplayPeriodMs);
        } while (outcome == WaitOutcome::Continue);

        if (outcome == WaitOutcome::Crash)
        {
            Log("display abnormal exit (code 1)");
            return 1;
        }
        Log("display finished");
        return 0;
    }

    DWORD WINAPI WorkerProc(LPVOID param)
    {
        DWORD exitCode = 0;
        {
            const std::unique_ptr<std::shared_ptr<Worker>> self(static_cast<std::shared_ptr<Worker>*>(param));
            if ((*self)->kind == WorkerKind::Reader)
                exitCode = RunReader(**self);
            else
                exitCode = RunDisplay(**self);
        }
        ExitThread(exitCode);
    }

    struct MonitorSnapshot
    {
        std::vector<HANDLE> handles;
        std::vector<DWORD> threadIds;
    };

    DWORD WINAPI MonitorProc(LPVOID param)
    {
        const std::unique_ptr<MonitorSnapshot> snapshot(static_cast<MonitorSnapshot*>(param));
        if (snapshot->handles.empty()) ExitThread(0);

        std::vector<HANDLE> waitHandles;
        waitHandles.reserve(1 + snapshot->handles.size());
        waitHandles.push_back(g_monitorStopEvent);
        waitHandles.insert(waitHandles.end(), snapshot->handles.begin(), snapshot->handles.end());

        const DWORD res = WaitForMultipleObjects(static_cast<DWORD>(waitHandles.size()), waitHandles.data(), FALSE, INFINITE);
        if (res >= WAIT_OBJECT_0 + 1 && res < WAIT_OBJECT_0 + waitHandles.size())
        {
            const size_t idx = static_cast<size_t>(res - (WAIT_OBJECT_0 + 1));
            DWORD exitCode = 0;
            GetExitCodeThread(snapshot->handles[idx], &exitCode);
            const DWORD tid = snapshot->threadIds[idx];
            if (exitCode != 0)
            {
                Log(std::format("monitor detected abnormal termination of thread {} (exit code {})", tid, exitCode));
                PostMessageW(g_notifyWnd, WM_WORKER_FAULT, tid, static_cast<LPARAM>(exitCode));
            }
        }
        ExitThread(0);
    }

    void StopMonitor()
    {
        if (!g_monitorThread) return;

        SetEvent(g_monitorStopEvent);
        WaitForSingleObject(g_monitorThread, INFINITE);
        CloseHandle(g_monitorThread);
        g_monitorThread = nullptr;
    }

    void StartMonitor()
    {
        StopMonitor();
        if (g_workers.empty() || !g_monitorStopEvent) return;

        ResetEvent(g_monitorStopEvent);
        auto snapshot = std::make_unique<MonitorSnapshot>();
        for (const auto& worker : g_workers)
        {
            snapshot->handles.push_back(worker->thread);
            snapshot->threadIds.push_back(worker->threadId);
        }

        g_monitorThread = CreateThread(nullptr, 0, MonitorProc, snapshot.get(), 0, nullptr);
        if (g_monitorThread)
            snapshot.release();
    }

    bool StopAll()
    {
        StopMonitor();
        if (g_workers.empty()) return true;

        for (const auto& worker : g_workers)
            SetEvent(worker->stopEvent);

        std::vector<HANDLE> handles;
        handles.reserve(g_workers.size());
        for (const auto& worker : g_workers)
            handles.push_back(worker->thread);

        const ULONGLONG deadline = GetTickCount64() + StopTimeoutMs;
        bool allStopped = true;
        for (size_t offset = 0; offset < handles.size(); offset += MAXIMUM_WAIT_OBJECTS)
        {
            const DWORD count = static_cast<DWORD>(std::min<size_t>(MAXIMUM_WAIT_OBJECTS, handles.size() - offset));
            const ULONGLONG now = GetTickCount64();
            const DWORD remaining = deadline > now ? static_cast<DWORD>(deadline - now) : 0;
            const DWORD res = WaitForMultipleObjects(count, handles.data() + offset, TRUE, remaining);
            if (res == WAIT_TIMEOUT || res == WAIT_FAILED)
            {
                Log("worker(s) did not stop in time, abandoned");
                allStopped = false;
            }
        }

        g_workers.clear();
        return allStopped;
    }
}

void WorkersInit(HWND notifyWnd)
{
    g_notifyWnd = notifyWnd;
    InitializeCriticalSection(&g_timeLock);
    g_logMutex = CreateMutexW(nullptr, FALSE, nullptr);
    g_runEvent = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    g_monitorStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OpenConsole();
}

void WorkersShutdown()
{
    if (!StopAll()) return;

    DeleteCriticalSection(&g_timeLock);
    if (g_logMutex) { CloseHandle(g_logMutex); g_logMutex = nullptr; }
    if (g_runEvent) { CloseHandle(g_runEvent); g_runEvent = nullptr; }
    if (g_monitorStopEvent) { CloseHandle(g_monitorStopEvent); g_monitorStopEvent = nullptr; }
}

void WorkersStart()
{
    if (!g_workers.empty()) return;

    g_statusLine.clear();
    WorkersResume();
    WorkersAdd(WorkerKind::Reader);
    WorkersAdd(WorkerKind::Display);
}

void WorkersStopAll()
{
    StopAll();
    g_statusLine.clear();
}

bool WorkersAdd(WorkerKind kind)
{
    if (g_workers.size() >= MaxWorkers) return false;

    StopMonitor();

    auto worker = std::make_shared<Worker>(kind);
    worker->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    worker->crashEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!worker->stopEvent || !worker->crashEvent)
    {
        StartMonitor();
        return false;
    }

    auto self = std::make_unique<std::shared_ptr<Worker>>(worker);
    worker->thread = CreateThread(nullptr, 0, WorkerProc, self.get(), 0, &worker->threadId);
    if (!worker->thread)
    {
        StartMonitor();
        return false;
    }
    self.release();

    g_workers.push_back(std::move(worker));
    StartMonitor();
    return true;
}

bool WorkersRemove(WorkerKind kind)
{
    for (auto it = g_workers.rbegin(); it != g_workers.rend(); ++it)
    {
        if ((*it)->kind != kind) continue;

        StopMonitor();
        SetEvent((*it)->stopEvent);
        if (WaitForSingleObject((*it)->thread, StopTimeoutMs) != WAIT_OBJECT_0)
            Log("worker did not stop in time, abandoned");
        g_workers.erase(std::next(it).base());
        StartMonitor();
        return true;
    }
    return false;
}

void WorkersPause()
{
    if (!g_runEvent || g_paused.load()) return;

    ResetEvent(g_runEvent);
    g_paused.store(true);
    Log("workers paused (ResetEvent)");
}

void WorkersResume()
{
    if (!g_runEvent || !g_paused.load()) return;

    SetEvent(g_runEvent);
    g_paused.store(false);
    Log("workers resumed (SetEvent)");
}

bool WorkersIsPaused()
{
    return g_paused.load();
}

bool WorkersSimulateCrash()
{
    if (g_workers.empty()) return false;

    const auto& victim = g_workers.back();
    Log(std::format("simulating abnormal termination for thread {}", victim->threadId));
    SetEvent(victim->crashEvent);
    return true;
}

void WorkersHandleFault(DWORD threadId, DWORD exitCode)
{
    StopAll();
    g_statusLine = std::format(L"Fault: worker {} exited abnormally (code {}). All workers stopped.", threadId, exitCode);
}

void WorkersHandleTime(DWORD threadId)
{
    for (const auto& worker : g_workers)
    {
        if (worker->threadId != threadId) continue;

        worker->updatePending.store(false);
        const LPARAM packedTime = worker->latest.load();
        if (packedTime == NoTime)
            worker->text = L"--:--:--";
        else
            worker->text = std::format(L"{:02}:{:02}:{:02}",
                                       (packedTime >> 16) & 0xFF, (packedTime >> 8) & 0xFF, packedTime & 0xFF);
        return;
    }
}

int WorkersCount(WorkerKind kind)
{
    int count = 0;
    for (const auto& worker : g_workers)
        if (worker->kind == kind) ++count;
    return count;
}

std::vector<std::wstring> WorkersDisplayLines()
{
    std::vector<std::wstring> lines;
    for (const auto& worker : g_workers)
        if (worker->kind == WorkerKind::Display)
            lines.push_back(std::format(L"Display thread {}: {}", worker->threadId, worker->text));
    return lines;
}

std::wstring WorkersStatusLine()
{
    return g_statusLine;
}

void WorkersLog(const std::string& message)
{
    Log(message);
}
