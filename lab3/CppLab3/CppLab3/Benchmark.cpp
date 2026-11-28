#include "pch.h"
#include "Benchmark.h"
#include "Workers.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <vector>

namespace
{
    constexpr size_t ArrayElements = 16'000'000;
    constexpr std::array<int, 5> ThreadCounts = { 1, 2, 4, 8, 16 };
    constexpr int Rounds = 40;
    constexpr int Repeats = 5;

    std::atomic<bool> g_running{false};
    std::vector<std::wstring> g_resultLines;

    struct ResultsLock
    {
        CRITICAL_SECTION section;
        ResultsLock() { InitializeCriticalSection(&section); }
        ~ResultsLock() { DeleteCriticalSection(&section); }
        ResultsLock(const ResultsLock&) = delete;
        ResultsLock& operator=(const ResultsLock&) = delete;
    } g_resultsLock;

    uint64_t FileTimeToU64(const FILETIME& ft)
    {
        return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    uint64_t MixValue(uint64_t x)
    {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }

    struct BenchSlice
    {
        const uint64_t* data;
        size_t begin;
        size_t end;
        HANDLE startEvent;
        HANDLE resultMutex;
        uint64_t* sharedChecksum;
    };

    DWORD WINAPI BenchWorkerProc(LPVOID param)
    {
        {
            const std::unique_ptr<BenchSlice> slice(static_cast<BenchSlice*>(param));
            WaitForSingleObject(slice->startEvent, INFINITE);

            uint64_t localSum = 0;
            for (int round = 0; round < Rounds; ++round)
                for (size_t i = slice->begin; i < slice->end; ++i)
                    localSum ^= MixValue(slice->data[i] + round);

            WaitForSingleObject(slice->resultMutex, INFINITE);
            *slice->sharedChecksum ^= localSum;
            ReleaseMutex(slice->resultMutex);
        }
        ExitThread(0);
    }

    struct BenchRow
    {
        int threads;
        double elapsedMs;
        double cpuPercent;
        uint64_t checksum;
    };

    BenchRow RunSinglePass(const std::vector<uint64_t>& data, int threadCount, DWORD logicalCpus, LARGE_INTEGER freq)
    {
        HANDLE resultMutex = CreateMutexW(nullptr, FALSE, nullptr);
        HANDLE startEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        uint64_t sharedChecksum = 0;

        std::vector<HANDLE> handles;
        handles.reserve(threadCount);

        const size_t chunk = data.size() / threadCount;
        for (int i = 0; i < threadCount; ++i)
        {
            const size_t begin = static_cast<size_t>(i) * chunk;
            const size_t end = (i == threadCount - 1) ? data.size() : begin + chunk;
            auto slice = std::make_unique<BenchSlice>(BenchSlice{ data.data(), begin, end, startEvent, resultMutex, &sharedChecksum });
            HANDLE hThread = CreateThread(nullptr, 0, BenchWorkerProc, slice.get(), 0, nullptr);
            if (hThread)
            {
                slice.release();
                handles.push_back(hThread);
            }
        }

        FILETIME ftCreate{}, ftExit{}, ftKernelStart{}, ftUserStart{};
        GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernelStart, &ftUserStart);

        LARGE_INTEGER tStart{};
        QueryPerformanceCounter(&tStart);
        SetEvent(startEvent);

        if (!handles.empty())
            WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, INFINITE);

        LARGE_INTEGER tEnd{};
        QueryPerformanceCounter(&tEnd);

        FILETIME ftKernelEnd{}, ftUserEnd{};
        GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernelEnd, &ftUserEnd);

        for (HANDLE h : handles)
            CloseHandle(h);
        if (resultMutex)
            CloseHandle(resultMutex);
        if (startEvent)
            CloseHandle(startEvent);

        const double elapsedMs = static_cast<double>(tEnd.QuadPart - tStart.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
        const uint64_t cpu100ns = (FileTimeToU64(ftKernelEnd) - FileTimeToU64(ftKernelStart))
                                + (FileTimeToU64(ftUserEnd) - FileTimeToU64(ftUserStart));
        const double cpuMs = static_cast<double>(cpu100ns) / 10000.0;
        const double cpuPercent = (elapsedMs > 0.0 && logicalCpus > 0)
            ? (cpuMs / (elapsedMs * logicalCpus)) * 100.0
            : 0.0;

        return { threadCount, elapsedMs, cpuPercent, sharedChecksum };
    }

    BenchRow RunMedianPass(const std::vector<uint64_t>& data, int threadCount, DWORD logicalCpus, LARGE_INTEGER freq)
    {
        std::vector<BenchRow> rows;
        rows.reserve(Repeats);
        for (int i = 0; i < Repeats; ++i)
            rows.push_back(RunSinglePass(data, threadCount, logicalCpus, freq));

        std::sort(rows.begin(), rows.end(), [](const BenchRow& a, const BenchRow& b) { return a.elapsedMs < b.elapsedMs; });
        return rows[rows.size() / 2];
    }

    DWORD WINAPI BenchmarkCoordinatorProc(LPVOID param)
    {
        HWND notifyWnd = static_cast<HWND>(param);
        WorkersLog(std::format("benchmark starting ({} uint64 elements)...", ArrayElements));

        std::vector<uint64_t> data(ArrayElements);
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<uint64_t>(i) * 6364136223846793005ULL + 1442695040888963407ULL;

        SYSTEM_INFO sysInfo{};
        GetSystemInfo(&sysInfo);
        const DWORD logicalCpus = sysInfo.dwNumberOfProcessors > 0 ? sysInfo.dwNumberOfProcessors : 1;

        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);

        RunSinglePass(data, static_cast<int>(logicalCpus), logicalCpus, freq);

        std::vector<std::wstring> lines;
        lines.push_back(std::format(L"Benchmark ({}M uint64 x {} rounds, {} logical CPUs, median of {}):", ArrayElements / 1'000'000, Rounds, logicalCpus, Repeats));
        lines.push_back(L"Threads | Time (ms) | Speedup | Avg CPU (%) | Checksum");

        WorkersLog("Threads | Time (ms) | Speedup | Avg CPU (%) | Checksum");
        double baselineMs = 0.0;
        for (int n : ThreadCounts)
        {
            const BenchRow row = RunMedianPass(data, n, logicalCpus, freq);
            if (baselineMs == 0.0) baselineMs = row.elapsedMs;
            const double speedup = row.elapsedMs > 0.0 ? baselineMs / row.elapsedMs : 0.0;
            lines.push_back(std::format(L"{:7} | {:9.2f} | {:7.2f} | {:11.1f} | {:#018x}",
                                        row.threads, row.elapsedMs, speedup, row.cpuPercent, row.checksum));
            WorkersLog(std::format("{:7} | {:9.2f} | {:7.2f} | {:11.1f} | {:#018x}",
                                   row.threads, row.elapsedMs, speedup, row.cpuPercent, row.checksum));
        }

        EnterCriticalSection(&g_resultsLock.section);
        g_resultLines = std::move(lines);
        LeaveCriticalSection(&g_resultsLock.section);

        g_running.store(false);
        PostMessageW(notifyWnd, WM_BENCHMARK_DONE, 0, 0);
        ExitThread(0);
    }
}

void BenchmarkStart(HWND notifyWnd)
{
    if (g_running.exchange(true)) return;

    EnterCriticalSection(&g_resultsLock.section);
    g_resultLines = { L"Benchmark running (1, 2, 4, 8, 16 threads)..." };
    LeaveCriticalSection(&g_resultsLock.section);

    HANDLE hThread = CreateThread(nullptr, 0, BenchmarkCoordinatorProc, notifyWnd, 0, nullptr);
    if (hThread)
        CloseHandle(hThread);
    else
        g_running.store(false);
}

bool BenchmarkIsRunning()
{
    return g_running.load();
}

std::vector<std::wstring> BenchmarkResultLines()
{
    EnterCriticalSection(&g_resultsLock.section);
    auto copy = g_resultLines;
    LeaveCriticalSection(&g_resultsLock.section);
    return copy;
}
