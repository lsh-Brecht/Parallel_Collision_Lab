#pragma once

#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <windows.h>
#include "../Core/Sphere.h"
#include "../Core/CPUInfo.h"
#include "ICollisionSolver.h"

//=============================================================================
// FBenchmarkItem - Individual solver performance statistics
//=============================================================================
struct FBenchmarkItem
{
    std::wstring Name;
    int          ThreadCount = 1;
    double       AvgNarrowMs = 0.0;
    double       MinNarrowMs = 0.0;
    double       MaxNarrowMs = 0.0;
    double       AvgTotalMs  = 0.0;
    double       Speedup     = 1.0;
};

//=============================================================================
// FBenchmarkReport - Complete benchmark suite report
//=============================================================================
struct FBenchmarkReport
{
    bool                        bValid     = false;
    int                         BallCount  = 0;
    int                         WarmupRuns = 10;
    int                         SampleRuns = 50;
    std::vector<FBenchmarkItem> Items;
    std::wstring                DisplayText;

    std::wstring GenerateDetailedReport(const FCPUInfo& cpuInfo) const
    {
        SYSTEMTIME st;
        GetLocalTime(&st);

        wchar_t headerBuf[1024];
        uint64_t candidatePairs = static_cast<uint64_t>(BallCount) * (BallCount - 1) / 2;

        swprintf_s(headerBuf,
            L"================================================================================\r\n"
            L"                   PARALLEL COLLISION LAB - BENCHMARK REPORT                    \r\n"
            L"================================================================================\r\n"
            L"Date / Time     : %04d-%02d-%02d %02d:%02d:%02d\r\n"
            L"CPU             : %s\r\n"
            L"Cache           : %s\r\n"
            L"Ball Count      : %d Balls (Candidate Pairs: %llu)\r\n"
            L"Iterations      : %d Runs (Warm-up: %d Runs)\r\n"
            L"================================================================================\r\n\r\n"
            L"[SOLVER PERFORMANCE COMPARISON]\r\n"
            L"--------------------------------------------------------------------------------\r\n"
            L"No.  Solver Name       Threads   Narrow Phase (Avg / Min / Max)      Speedup    \r\n"
            L"--------------------------------------------------------------------------------\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            cpuInfo.GetSummaryString().c_str(),
            cpuInfo.GetCacheString().c_str(),
            BallCount, candidatePairs,
            SampleRuns, WarmupRuns);

        std::wstring out = headerBuf;

        for (size_t i = 0; i < Items.size(); ++i)
        {
            const auto& it = Items[i];
            wchar_t rowBuf[256];
            if (i == 0)
            {
                swprintf_s(rowBuf,
                    L"%02zu   %-17s %4d    %6.2f ms  (Min: %5.2f, Max: %5.2f)   [Baseline]\r\n",
                    i + 1, it.Name.c_str(), it.ThreadCount,
                    it.AvgNarrowMs, it.MinNarrowMs, it.MaxNarrowMs);
            }
            else
            {
                swprintf_s(rowBuf,
                    L"%02zu   %-17s %4d    %6.2f ms  (Min: %5.2f, Max: %5.2f)   %6.2fx\r\n",
                    i + 1, it.Name.c_str(), it.ThreadCount,
                    it.AvgNarrowMs, it.MinNarrowMs, it.MaxNarrowMs, it.Speedup);
            }
            out += rowBuf;
        }

        out += L"--------------------------------------------------------------------------------\r\n\r\n";
        out += L"[SUMMARY & ARCHITECTURE NOTES]\r\n";
        if (Items.size() >= 2 && Items[1].Speedup > 1.0)
        {
            wchar_t noteBuf[256];
            double eff = (Items[1].Speedup / static_cast<double>(Items[1].ThreadCount)) * 100.0;
            swprintf_s(noteBuf,
                L"- Naive MT Speedup: %.2fx on %d threads (Scaling Efficiency: %.1f%%)\r\n",
                Items[1].Speedup, Items[1].ThreadCount, eff);
            out += noteBuf;
        }
        if (Items.size() >= 4 && Items[3].AvgNarrowMs > 1e-6 && Items[2].AvgNarrowMs > 1e-6)
        {
            wchar_t gridNoteBuf[256];
            double gridMtSpeedup = Items[2].AvgNarrowMs / Items[3].AvgNarrowMs;
            double gridMtEff = (gridMtSpeedup / static_cast<double>(Items[3].ThreadCount)) * 100.0;
            swprintf_s(gridNoteBuf,
                L"- Grid MT vs Grid ST Speedup: %.2fx on %d threads (Scaling Efficiency: %.1f%%)\r\n"
                L"- Overall Maximum Speedup (Grid MT vs Naive ST): %.2fx\r\n",
                gridMtSpeedup, Items[3].ThreadCount, gridMtEff, Items[3].Speedup);
            out += gridNoteBuf;
        }
        if (Items.size() >= 5 && Items[4].AvgNarrowMs > 1e-6)
        {
            wchar_t bvhNoteBuf[256];
            swprintf_s(bvhNoteBuf,
                L"- BVH (ST) vs Naive ST Speedup: %.2fx (Narrow Phase: %.2f ms vs %.2f ms)\r\n",
                Items[4].Speedup, Items[4].AvgNarrowMs, Items[0].AvgNarrowMs);
            out += bvhNoteBuf;
        }
        out += L"================================================================================\r\n";

        return out;
    }
};

//=============================================================================
// RunBenchmark - Runs warm-up and timed sample iterations across all solvers
//=============================================================================
inline FBenchmarkReport RunBenchmark(
    const std::vector<std::unique_ptr<ICollisionSolver>>& solvers,
    const std::vector<FSphere>& baseSpheres,
    float boxHalfSize,
    float fixedDt = 0.016f)
{
    FBenchmarkReport report;
    report.BallCount = static_cast<int>(baseSpheres.size());
    if (report.BallCount < 2 || solvers.empty())
    {
        return report;
    }

    report.WarmupRuns = 10;
    report.SampleRuns = (report.BallCount <= 256) ? 100 : 50;

    double baselineAvgNarrow = 0.0;

    for (size_t s = 0; s < solvers.size(); ++s)
    {
        ICollisionSolver* solver = solvers[s].get();
        if (!solver) continue;

        std::vector<FSphere> testSpheres = baseSpheres;
        for (int w = 0; w < report.WarmupRuns; ++w)
        {
            for (FSphere& sphere : testSpheres)
            {
                sphere.Update(fixedDt);
                sphere.BoxCollisionCheck(boxHalfSize);
            }
            solver->Solve(testSpheres);
        }

        testSpheres = baseSpheres;

        double totalNarrowMs = 0.0;
        double minNarrowMs   = 1e9;
        double maxNarrowMs   = 0.0;
        double totalTimeMs   = 0.0;

        for (int run = 0; run < report.SampleRuns; ++run)
        {
            for (FSphere& sphere : testSpheres)
            {
                sphere.Update(fixedDt);
                sphere.BoxCollisionCheck(boxHalfSize);
            }

            solver->Solve(testSpheres);

            const FCollisionStats& stats = solver->GetLastStats();
            double narrow = stats.NarrowPhaseTimeMs;
            double total  = stats.TotalSolveTimeMs;

            totalNarrowMs += narrow;
            totalTimeMs   += total;
            if (narrow < minNarrowMs) minNarrowMs = narrow;
            if (narrow > maxNarrowMs) maxNarrowMs = narrow;
        }

        double avgNarrowMs = totalNarrowMs / static_cast<double>(report.SampleRuns);
        double avgTotalMs  = totalTimeMs / static_cast<double>(report.SampleRuns);

        if (s == 0 || baselineAvgNarrow <= 0.0)
        {
            baselineAvgNarrow = avgNarrowMs;
        }

        double speedup = (avgNarrowMs > 1e-6) ? (baselineAvgNarrow / avgNarrowMs) : 1.0;

        FBenchmarkItem item;
        item.Name        = solver->GetName();
        item.ThreadCount = solver->GetThreadCount();
        item.AvgNarrowMs = avgNarrowMs;
        item.MinNarrowMs = minNarrowMs;
        item.MaxNarrowMs = maxNarrowMs;
        item.AvgTotalMs  = avgTotalMs;
        item.Speedup     = speedup;

        report.Items.push_back(item);
    }

    report.bValid = true;

    wchar_t buf[2048];
    int offset = swprintf_s(buf,
        L"=== BENCHMARK (%d Balls, %d Runs) ===\n",
        report.BallCount, report.SampleRuns);

    for (size_t i = 0; i < report.Items.size(); ++i)
    {
        const auto& it = report.Items[i];
        if (i == 0)
        {
            offset += swprintf_s(buf + offset, sizeof(buf)/sizeof(wchar_t) - offset,
                L"[%zu] %-16s : %5.2f ms (Min:%5.2f, Max:%5.2f) [Base]\n",
                i + 1, it.Name.c_str(), it.AvgNarrowMs, it.MinNarrowMs, it.MaxNarrowMs);
        }
        else
        {
            offset += swprintf_s(buf + offset, sizeof(buf)/sizeof(wchar_t) - offset,
                L"[%zu] %-16s : %5.2f ms (Min:%5.2f, Max:%5.2f) -> %4.2fx\n",
                i + 1, it.Name.c_str(), it.AvgNarrowMs, it.MinNarrowMs, it.MaxNarrowMs, it.Speedup);
        }
    }
    swprintf_s(buf + offset, sizeof(buf)/sizeof(wchar_t) - offset, L"\n");

    report.DisplayText = buf;

    OutputDebugStringW(L"\n============================================================\n");
    OutputDebugStringW(report.DisplayText.c_str());
    OutputDebugStringW(L"\n============================================================\n\n");

    return report;
}
