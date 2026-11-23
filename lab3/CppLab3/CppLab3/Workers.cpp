#include "pch.h"
#include "Workers.h"
#include <atomic>
#include <format>
#include <memory>

namespace
{
    constexpr DWORD ReaderPeriodMs = 1000;
    constexpr DWORD DisplayPeriodMs = 1000;
    constexpr DWORD StopTimeoutMs = 3000;
    constexpr LPARAM NoTime = -1;

    struct SharedTime
    {
        bool valid;
        WORD hour;
        WORD minute;
        WORD second;
    };

    SharedTime g_time{};
    CRITICAL_SECTION g_timeLock;
    CRITICAL_SECTION g_logLock;
    HANDLE g_console = INVALID_HANDLE_VALUE;
    HWND g_notifyWnd = nullptr;

    class TimeLock
    {
    public:
        TimeLock() { EnterCriticalSection(&g_timeLock); }
        ~TimeLock() { LeaveCriticalSection(&g_timeLock); }
        TimeLock(const TimeLock&) = delete;
        TimeLock& operator=(const TimeLock&) = delete;
    };

    struct Worker
    {
        WorkerKind kind;
        HANDLE thread = nullptr;
        DWORD threadId = 0;
        HANDLE stopEvent = nullptr;
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
        }
    };

    std::vector<std::shared_ptr<Worker>> g_workers;

    void Log(const std::string& message)
    {
        if (g_console == INVALID_HANDLE_VALUE) return;

        const auto line = std::format("[thread {}] {}\n", GetCurrentThreadId(), message);
        EnterCriticalSection(&g_logLock);
        DWORD written = 0;
        WriteConsoleA(g_console, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        LeaveCriticalSection(&g_logLock);
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

    void RunReader(const Worker& worker)
    {
        Log("reader started");
        do
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            {
                TimeLock lock;
                g_time = { true, st.wHour, st.wMinute, st.wSecond };
            }
            Log(std::format("reader wrote {:02}:{:02}:{:02}", st.wHour, st.wMinute, st.wSecond));
        } while (WaitForSingleObject(worker.stopEvent, ReaderPeriodMs) == WAIT_TIMEOUT);
        Log("reader finished");
    }

    void RunDisplay(Worker& worker)
    {
        Log("display started");
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
        } while (WaitForSingleObject(worker.stopEvent, DisplayPeriodMs) == WAIT_TIMEOUT);
        Log("display finished");
    }

    DWORD WINAPI WorkerProc(LPVOID param)
    {
        {
            const std::unique_ptr<std::shared_ptr<Worker>> self(static_cast<std::shared_ptr<Worker>*>(param));
            if ((*self)->kind == WorkerKind::Reader)
                RunReader(**self);
            else
                RunDisplay(**self);
        }
        ExitThread(0);
    }

    bool WaitForStop(const Worker& worker, ULONGLONG deadline)
    {
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = deadline > now ? static_cast<DWORD>(deadline - now) : 0;
        if (WaitForSingleObject(worker.thread, remaining) == WAIT_OBJECT_0) return true;

        Log("worker did not stop in time, abandoned");
        return false;
    }

    bool StopAll()
    {
        for (const auto& worker : g_workers)
            SetEvent(worker->stopEvent);

        const ULONGLONG deadline = GetTickCount64() + StopTimeoutMs;
        bool allStopped = true;
        for (const auto& worker : g_workers)
            allStopped = WaitForStop(*worker, deadline) && allStopped;

        g_workers.clear();
        return allStopped;
    }
}

void WorkersInit(HWND notifyWnd)
{
    g_notifyWnd = notifyWnd;
    InitializeCriticalSection(&g_timeLock);
    InitializeCriticalSection(&g_logLock);
    OpenConsole();
}

void WorkersShutdown()
{
    if (!StopAll()) return;

    DeleteCriticalSection(&g_timeLock);
    DeleteCriticalSection(&g_logLock);
}

void WorkersStart()
{
    if (!g_workers.empty()) return;

    WorkersAdd(WorkerKind::Reader);
    WorkersAdd(WorkerKind::Display);
}

void WorkersStopAll()
{
    StopAll();
}

bool WorkersAdd(WorkerKind kind)
{
    auto worker = std::make_shared<Worker>(kind);
    worker->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!worker->stopEvent) return false;

    auto self = std::make_unique<std::shared_ptr<Worker>>(worker);
    worker->thread = CreateThread(nullptr, 0, WorkerProc, self.get(), 0, &worker->threadId);
    if (!worker->thread) return false;
    self.release();

    g_workers.push_back(std::move(worker));
    return true;
}

bool WorkersRemove(WorkerKind kind)
{
    for (auto it = g_workers.rbegin(); it != g_workers.rend(); ++it)
    {
        if ((*it)->kind != kind) continue;

        SetEvent((*it)->stopEvent);
        WaitForStop(**it, GetTickCount64() + StopTimeoutMs);
        g_workers.erase(std::next(it).base());
        return true;
    }
    return false;
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
