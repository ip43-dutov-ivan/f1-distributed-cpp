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
    constexpr std::array<int, 4> ThreadCounts = { 2, 4, 8, 16 };

    std::atomic<bool> g_running{false};
    CRITICAL_SECTION g_resultsLock;
    bool g_resultsLockInit = false;
    std::vector<std::wstring> g_resultLines;

    void EnsureResultsLock()
    {
        if (!g_resultsLockInit)
        {
            InitializeCriticalSection(&g_resultsLock);
            g_resultsLockInit = true;
        }
    }

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
        HANDLE resultMutex;
        uint64_t* sharedChecksum;
    };

    DWORD WINAPI BenchWorkerProc(LPVOID param)
    {
        {
            const std::unique_ptr<BenchSlice> slice(static_cast<BenchSlice*>(param));
            WorkersLog(std::format("benchmark worker started [{}, {})", slice->begin, slice->end));

            uint64_t localSum = 0;
            for (size_t i = slice->begin; i < slice->end; ++i)
                localSum ^= MixValue(slice->data[i]);

            WaitForSingleObject(slice->resultMutex, INFINITE);
            *slice->sharedChecksum ^= localSum;
            WorkersLog(std::format("benchmark worker merged partial={:#018x}", localSum));
            ReleaseMutex(slice->resultMutex);

            WorkersLog("benchmark worker finished");
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
        uint64_t sharedChecksum = 0;

        std::vector<HANDLE> handles;
        handles.reserve(threadCount);

        FILETIME ftCreate{}, ftExit{}, ftKernelStart{}, ftUserStart{};
        GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernelStart, &ftUserStart);

        LARGE_INTEGER tStart{};
        QueryPerformanceCounter(&tStart);

        const size_t chunk = data.size() / threadCount;
        for (int i = 0; i < threadCount; ++i)
        {
            const size_t begin = static_cast<size_t>(i) * chunk;
            const size_t end = (i == threadCount - 1) ? data.size() : begin + chunk;
            auto slice = std::make_unique<BenchSlice>(BenchSlice{ data.data(), begin, end, resultMutex, &sharedChecksum });
            HANDLE hThread = CreateThread(nullptr, 0, BenchWorkerProc, slice.get(), 0, nullptr);
            if (hThread)
            {
                slice.release();
                handles.push_back(hThread);
            }
        }

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

        const double elapsedMs = static_cast<double>(tEnd.QuadPart - tStart.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
        const uint64_t cpu100ns = (FileTimeToU64(ftKernelEnd) - FileTimeToU64(ftKernelStart))
                                + (FileTimeToU64(ftUserEnd) - FileTimeToU64(ftUserStart));
        const double cpuMs = static_cast<double>(cpu100ns) / 10000.0;
        const double cpuPercent = (elapsedMs > 0.0 && logicalCpus > 0)
            ? std::clamp((cpuMs / (elapsedMs * logicalCpus)) * 100.0, 0.0, 100.0)
            : 0.0;

        return { threadCount, elapsedMs, cpuPercent, sharedChecksum };
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

        std::vector<std::wstring> lines;
        lines.push_back(std::format(L"Benchmark ({}M uint64, {} logical CPUs):", ArrayElements / 1'000'000, logicalCpus));
        lines.push_back(L"Threads | Time (ms) | Avg CPU (%) | Checksum");

        WorkersLog("Threads | Time (ms) | Avg CPU (%) | Checksum");
        for (int n : ThreadCounts)
        {
            const BenchRow row = RunSinglePass(data, n, logicalCpus, freq);
            lines.push_back(std::format(L"{:7} | {:9.2f} | {:11.1f} | {:#018x}",
                                        row.threads, row.elapsedMs, row.cpuPercent, row.checksum));
            WorkersLog(std::format("{:7} | {:9.2f} | {:11.1f} | {:#018x}",
                                   row.threads, row.elapsedMs, row.cpuPercent, row.checksum));
        }

        EnsureResultsLock();
        EnterCriticalSection(&g_resultsLock);
        g_resultLines = std::move(lines);
        LeaveCriticalSection(&g_resultsLock);

        g_running.store(false);
        PostMessageW(notifyWnd, WM_BENCHMARK_DONE, 0, 0);
        ExitThread(0);
    }
}

void BenchmarkStart(HWND notifyWnd)
{
    if (g_running.exchange(true)) return;

    EnsureResultsLock();
    EnterCriticalSection(&g_resultsLock);
    g_resultLines = { L"Benchmark running (2, 4, 8, 16 threads)..." };
    LeaveCriticalSection(&g_resultsLock);

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
    if (!g_resultsLockInit) return {};

    EnterCriticalSection(&g_resultsLock);
    auto copy = g_resultLines;
    LeaveCriticalSection(&g_resultsLock);
    return copy;
}
