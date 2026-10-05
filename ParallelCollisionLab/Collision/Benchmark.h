#pragma once

#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <windows.h>
#include "../Core/Sphere.h"
#include "../Core/CPUInfo.h"
#include "ICollisionSolver.h"

struct FBenchmarkItem
{
    std::wstring Name;
    int          ThreadCount      = 1;
    double       AvgBroadMs       = 0.0;
    double       AvgNarrowMs      = 0.0;
    double       AvgTotalMs       = 0.0;
    double       MedianTotalMs    = 0.0;
    double       P95TotalMs       = 0.0;
    double       MinTotalMs       = 0.0;
    double       MaxTotalMs       = 0.0;
    double       Speedup          = 1.0; // Based on AvgTotalMs vs Baseline (Total solve time)
    double       NarrowSpeedup    = 1.0; // Narrow-phase only speedup for architectural reference
    uint64_t     CandidatePairs   = 0;
    uint64_t     ActualCollisions = 0;
    bool         bFrame0Match     = true;
};

struct FBenchmarkReport
{
    bool                        bValid         = false;
    int                         BallCount      = 0;
    int                         WarmupRuns     = 10;
    int                         SampleRuns     = 50;
    bool                        bAllMatch      = true;
    std::vector<FBenchmarkItem> Items;
    std::wstring                DisplayText;

    std::wstring GenerateDetailedReport(const FCPUInfo& CPUInfo) const
    {
        SYSTEMTIME st;
        GetLocalTime(&st);

        wchar_t headerBuf[1024];
        uint64_t MaxPossiblePairs = static_cast<uint64_t>(BallCount) * (BallCount - 1) / 2;

        swprintf_s(headerBuf,
            L"========================================================================================================================\r\n"
            L"                                       PARALLEL COLLISION LAB - BENCHMARK REPORT                                        \r\n"
            L"                                      (Snapshot Replay: 100%% Identical Trajectories)                                   \r\n"
            L"========================================================================================================================\r\n"
            L"Date / Time     : %04d-%02d-%02d %02d:%02d:%02d\r\n"
            L"CPU             : %s\r\n"
            L"Cache           : %s\r\n"
            L"Ball Count      : %d Balls (Max Pairs: %llu)\r\n"
            L"Replay Frames   : %d Sample Runs (Warm-up: %d Runs) [Pre-recorded via Ground Truth]\r\n"
            L"Validation      : %s (Frame 0 collision count across all solvers)\r\n"
            L"========================================================================================================================\r\n\r\n"
            L"[SOLVER PERFORMANCE COMPARISON - TOTAL TIME SPEEDUP & DISTRIBUTION]\r\n"
            L"------------------------------------------------------------------------------------------------------------------------\r\n"
            L"No.  Solver Name        Threads   Broad Phase   Narrow Phase   Total (Avg / Median / P95)        Speedup   Verified     \r\n"
            L"------------------------------------------------------------------------------------------------------------------------\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            CPUInfo.GetSummaryString().c_str(),
            CPUInfo.GetCacheString().c_str(),
            BallCount, MaxPossiblePairs,
            SampleRuns, WarmupRuns,
            bAllMatch ? L"ALL PASS (100% Consistent Collision Count)" : L"PARTIAL MISMATCH");

        std::wstring out = headerBuf;

        for (size_t i = 0; i < Items.size(); ++i)
        {
            const auto& it = Items[i];
            wchar_t rowBuf[256];
            const wchar_t* matchStr = it.bFrame0Match ? L"PASS" : L"DIFF";
            if (i == 0)
            {
                swprintf_s(rowBuf,
                    L"%02zu   %-18s %4d    %6.2f ms    %6.2f ms    %6.2f / %5.2f / %5.2f ms   [Baseline]      %s\r\n",
                    i + 1, it.Name.c_str(), it.ThreadCount,
                    it.AvgBroadMs, it.AvgNarrowMs, it.AvgTotalMs, it.MedianTotalMs, it.P95TotalMs, matchStr);
            }
            else
            {
                swprintf_s(rowBuf,
                    L"%02zu   %-18s %4d    %6.2f ms    %6.2f ms    %6.2f / %5.2f / %5.2f ms     %5.2fx        %s\r\n",
                    i + 1, it.Name.c_str(), it.ThreadCount,
                    it.AvgBroadMs, it.AvgNarrowMs, it.AvgTotalMs, it.MedianTotalMs, it.P95TotalMs, it.Speedup, matchStr);
            }
            out += rowBuf;
        }

        out += L"------------------------------------------------------------------------------------------------------------------------\r\n";
        out += L"* Speedup is calculated based on TOTAL solve time (Broad Phase + Narrow Phase).\r\n";
        out += L"* All solvers evaluated on the exact same pre-recorded snapshot stream to eliminate divergence.\r\n\r\n";

        out += L"[SUMMARY & ARCHITECTURE ANALYSIS]\r\n";
        if (Items.size() >= 2 && Items[1].Speedup > 1.0)
        {
            wchar_t noteBuf[256];
            double eff = (Items[1].Speedup / static_cast<double>(Items[1].ThreadCount)) * 100.0;
            swprintf_s(noteBuf,
                L"- Naive MT Total Speedup: %.2fx on %d threads (Scaling Efficiency: %.1f%%)\r\n",
                Items[1].Speedup, Items[1].ThreadCount, eff);
            out += noteBuf;
        }
        if (Items.size() >= 4 && Items[3].AvgTotalMs > 1e-6 && Items[2].AvgTotalMs > 1e-6)
        {
            wchar_t gridNoteBuf[256];
            double gridMtTotalSpeedup  = Items[2].AvgTotalMs / Items[3].AvgTotalMs;
            double gridMtNarrowSpeedup = (Items[3].AvgNarrowMs > 1e-6) ? (Items[2].AvgNarrowMs / Items[3].AvgNarrowMs) : 1.0;
            double gridMtEff = (gridMtTotalSpeedup / static_cast<double>(Items[3].ThreadCount)) * 100.0;
            swprintf_s(gridNoteBuf,
                L"- Grid MT vs Grid ST Total Speedup: %.2fx on %d threads (Narrow MT: %.2fx, Efficiency: %.1f%%)\r\n"
                L"  (Amdahl Bottleneck: Grid Build is single-threaded %.2f ms)\r\n"
                L"- Overall Grid MT vs Naive ST Speedup: %.2fx (Total: %.2f ms vs %.2f ms)\r\n",
                gridMtTotalSpeedup, Items[3].ThreadCount, gridMtNarrowSpeedup, gridMtEff,
                Items[3].AvgBroadMs,
                Items[3].Speedup, Items[3].AvgTotalMs, Items[0].AvgTotalMs);
            out += gridNoteBuf;
        }
        if (Items.size() >= 5 && Items[4].AvgTotalMs > 1e-6)
        {
            wchar_t bvhNoteBuf[256];
            swprintf_s(bvhNoteBuf,
                L"- BVH (ST) vs Naive ST Total Speedup: %.2fx (Total: %.2f ms [Broad: %.2f ms, Narrow: %.2f ms] vs %.2f ms)\r\n",
                Items[4].Speedup, Items[4].AvgTotalMs, Items[4].AvgBroadMs, Items[4].AvgNarrowMs, Items[0].AvgTotalMs);
            out += bvhNoteBuf;
        }
        if (Items.size() >= 6 && Items[5].AvgTotalMs > 1e-6 && Items[4].AvgTotalMs > 1e-6)
        {
            wchar_t bvhMtNoteBuf[256];
            double bvhMtTotalSpeedup  = Items[4].AvgTotalMs / Items[5].AvgTotalMs;
            double bvhMtNarrowSpeedup = (Items[5].AvgNarrowMs > 1e-6) ? (Items[4].AvgNarrowMs / Items[5].AvgNarrowMs) : 1.0;
            double bvhMtEff = (bvhMtTotalSpeedup / static_cast<double>(Items[5].ThreadCount)) * 100.0;
            swprintf_s(bvhMtNoteBuf,
                L"- BVH MT vs BVH ST Total Speedup: %.2fx on %d threads (Narrow MT: %.2fx, Efficiency: %.1f%%)\r\n"
                L"- Overall BVH MT vs Naive ST Speedup: %.2fx (Total: %.2f ms vs %.2f ms)\r\n",
                bvhMtTotalSpeedup, Items[5].ThreadCount, bvhMtNarrowSpeedup, bvhMtEff,
                Items[5].Speedup, Items[5].AvgTotalMs, Items[0].AvgTotalMs);
            out += bvhMtNoteBuf;
        }
        out += L"========================================================================================================================\r\n";

        return out;
    }
};

// Pre-records ground truth trajectories using Nested Loop ST,
// then plays back exact identical frames to all solvers to eliminate divergence.
inline FBenchmarkReport RunBenchmark(
    const std::vector<std::unique_ptr<ICollisionSolver>>& Solvers,
    const std::vector<FSphere>& BaseSpheres,
    float BoxHalfSize,
    float FixedDt = 0.016f)
{
    FBenchmarkReport report;
    report.BallCount = static_cast<int>(BaseSpheres.size());
    if (report.BallCount < 2 || Solvers.empty())
    {
        return report;
    }

    report.WarmupRuns = 10;
    report.SampleRuns = (report.BallCount <= 256) ? 100 : (report.BallCount <= 2048 ? 50 : 30);

    //-------------------------------------------------------------------------
    // 1. Pre-Record Ground Truth Snapshot Stream (Nested Loop ST)
    //-------------------------------------------------------------------------
    std::vector<std::vector<FSphere>> warmupSnapshots;
    std::vector<std::vector<FSphere>> sampleSnapshots;
    warmupSnapshots.reserve(report.WarmupRuns);
    sampleSnapshots.reserve(report.SampleRuns);

    std::vector<FSphere> simSpheres = BaseSpheres;
    ICollisionSolver* groundTruth = Solvers[0].get();

    for (int w = 0; w < report.WarmupRuns; ++w)
    {
        for (FSphere& s : simSpheres)
        {
            s.Update(FixedDt, false); // Damping and sleep disabled for uniform workload
            s.BoxCollisionCheck(BoxHalfSize);
        }
        warmupSnapshots.push_back(simSpheres); // Save pre-solve state
        groundTruth->Solve(simSpheres);
        for (FSphere& s : simSpheres)
        {
            s.BoxCollisionCheck(BoxHalfSize);
        }
    }

    uint64_t baselineCollisionsFrame0 = 0;

    for (int r = 0; r < report.SampleRuns; ++r)
    {
        for (FSphere& s : simSpheres)
        {
            s.Update(FixedDt, false);
            s.BoxCollisionCheck(BoxHalfSize);
        }
        sampleSnapshots.push_back(simSpheres); // Save pre-solve state
        groundTruth->Solve(simSpheres);
        for (FSphere& s : simSpheres)
        {
            s.BoxCollisionCheck(BoxHalfSize);
        }

        if (r == 0)
        {
            baselineCollisionsFrame0 = groundTruth->GetLastStats().ActualCollisionCount;
        }
    }

    //-------------------------------------------------------------------------
    // 2. Playback & Measure Each Solver on Identical Snapshot Streams
    //-------------------------------------------------------------------------
    double baselineAvgTotal  = 0.0;
    double baselineAvgNarrow = 0.0;
    bool   bAllMatch         = true;

    for (size_t s = 0; s < Solvers.size(); ++s)
    {
        ICollisionSolver* solver = Solvers[s].get();
        if (!solver) continue;

        // Warmup on recorded stream
        for (int w = 0; w < report.WarmupRuns; ++w)
        {
            std::vector<FSphere> workSpheres = warmupSnapshots[w];
            solver->Solve(workSpheres);
        }

        double   totalBroadMs           = 0.0;
        double   totalNarrowMs          = 0.0;
        double   totalTimeMs            = 0.0;
        uint64_t totalCandidatePairs    = 0;
        uint64_t totalActualCollisions  = 0;
        bool     bFrame0Match           = true;

        std::vector<double> sampleTotals;
        sampleTotals.reserve(report.SampleRuns);

        for (int r = 0; r < report.SampleRuns; ++r)
        {
            std::vector<FSphere> workSpheres = sampleSnapshots[r];
            solver->Solve(workSpheres);

            const FCollisionStats& stats = solver->GetLastStats();
            double broad  = stats.BroadPhaseTimeMs;
            double narrow = stats.NarrowPhaseTimeMs;
            double total  = stats.TotalSolveTimeMs;

            totalBroadMs           += broad;
            totalNarrowMs          += narrow;
            totalTimeMs            += total;
            totalCandidatePairs    += stats.CandidatePairCount;
            totalActualCollisions  += stats.ActualCollisionCount;
            sampleTotals.push_back(total);

            if (r == 0)
            {
                if (stats.ActualCollisionCount != baselineCollisionsFrame0)
                {
                    bFrame0Match = false;
                    bAllMatch    = false;
                }
            }
        }

        // Compute Statistics (Average, Min, Max, Median, P95)
        std::sort(sampleTotals.begin(), sampleTotals.end());
        double minTotal    = sampleTotals.front();
        double maxTotal    = sampleTotals.back();
        double medianTotal = sampleTotals[report.SampleRuns / 2];
        size_t p95Index    = static_cast<size_t>(floor(0.95 * (report.SampleRuns - 1)));
        double p95Total    = sampleTotals[p95Index];

        double avgBroadMs  = totalBroadMs           / static_cast<double>(report.SampleRuns);
        double avgNarrowMs = totalNarrowMs          / static_cast<double>(report.SampleRuns);
        double avgTotalMs  = totalTimeMs            / static_cast<double>(report.SampleRuns);
        uint64_t avgPairs  = totalCandidatePairs    / report.SampleRuns;
        uint64_t avgColl   = totalActualCollisions  / report.SampleRuns;

        if (s == 0 || baselineAvgTotal <= 0.0)
        {
            baselineAvgTotal  = avgTotalMs;
            baselineAvgNarrow = avgNarrowMs;
        }

        double speedup       = (avgTotalMs  > 1e-6) ? (baselineAvgTotal  / avgTotalMs)  : 1.0;
        double narrowSpeedup = (avgNarrowMs > 1e-6) ? (baselineAvgNarrow / avgNarrowMs) : 1.0;

        FBenchmarkItem item;
        item.Name             = solver->GetName();
        item.ThreadCount      = solver->GetThreadCount();
        item.AvgBroadMs       = avgBroadMs;
        item.AvgNarrowMs      = avgNarrowMs;
        item.AvgTotalMs       = avgTotalMs;
        item.MedianTotalMs    = medianTotal;
        item.P95TotalMs       = p95Total;
        item.MinTotalMs       = minTotal;
        item.MaxTotalMs       = maxTotal;
        item.Speedup          = speedup;
        item.NarrowSpeedup    = narrowSpeedup;
        item.CandidatePairs   = avgPairs;
        item.ActualCollisions = avgColl;
        item.bFrame0Match     = bFrame0Match;

        report.Items.push_back(item);
    }

    report.bValid    = true;
    report.bAllMatch = bAllMatch;

    wchar_t buf[2048];
    int offset = swprintf_s(buf,
        L"=== BENCHMARK (Snapshot Replay: %d Balls, %d Runs) ===\n",
        report.BallCount, report.SampleRuns);

    for (size_t i = 0; i < report.Items.size(); ++i)
    {
        const auto& it = report.Items[i];
        if (i == 0)
        {
            offset += swprintf_s(buf + offset, sizeof(buf)/sizeof(wchar_t) - offset,
                L"[%zu] %-16s : %5.2f ms (Median:%5.2f, P95:%5.2f) [Base]\n",
                i + 1, it.Name.c_str(), it.AvgTotalMs, it.MedianTotalMs, it.P95TotalMs);
        }
        else
        {
            offset += swprintf_s(buf + offset, sizeof(buf)/sizeof(wchar_t) - offset,
                L"[%zu] %-16s : %5.2f ms (Median:%5.2f, P95:%5.2f) -> %5.2fx\n",
                i + 1, it.Name.c_str(), it.AvgTotalMs, it.MedianTotalMs, it.P95TotalMs, it.Speedup);
        }
    }
    swprintf_s(buf + offset, sizeof(buf)/sizeof(wchar_t) - offset, L"\n");

    report.DisplayText = buf;

    OutputDebugStringW(L"\n============================================================\n");
    OutputDebugStringW(report.DisplayText.c_str());
    OutputDebugStringW(L"\n============================================================\n\n");

    return report;
}
