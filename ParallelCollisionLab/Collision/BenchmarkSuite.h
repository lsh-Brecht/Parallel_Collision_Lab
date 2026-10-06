#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <functional>
#include <thread>
#include <windows.h>

#include "../Core/Sphere.h"
#include "../Core/CPUInfo.h"
#include "ICollisionSolver.h"
#include "NestedLoop/NestedLoopSolver.h"
#include "NestedLoop/NestedLoopMTSolver.h"
#include "UniformGrid/UniformGridSolver.h"
#include "UniformGrid/UniformGridMTSolver.h"
#include "BVH/BVHSolver.h"
#include "BVH/BVHMTSolver.h"

namespace Benchmark
{
    struct FStudyRow
    {
        std::string Scenario;
        int         Seed                    = 1;
        std::string SolverName;
        int         Threads                 = 1;
        int         N                       = 0;
        float       SizeRatio               = 1.0f;
        int         LargeCount              = 0;
        double      BroadAvg                = 0.0;
        double      NarrowAvg               = 0.0;
        double      TotalAvg                = 0.0;
        double      TotalMedian             = 0.0;
        double      TotalP95                = 0.0;
        uint64_t    CandidatePairsAvg       = 0;
        uint64_t    CollisionsAvg           = 0;
        int         GridDim                 = 0;
        float       GridCellSize            = 0.0f;
        float       AvgSpheresPerActiveCell = 0.0f;
        int         BVHDepth                = 0;

        std::string ToCsvLine() const
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(4);
            ss << Scenario << ","
               << Seed << ","
               << SolverName << ","
               << Threads << ","
               << N << ","
               << std::setprecision(2) << SizeRatio << ","
               << LargeCount << ","
               << std::setprecision(4) << BroadAvg << ","
               << NarrowAvg << ","
               << TotalAvg << ","
               << TotalMedian << ","
               << TotalP95 << ","
               << CandidatePairsAvg << ","
               << CollisionsAvg << ",";

            if (GridDim > 0)
            {
                ss << GridDim << "," << std::setprecision(4) << GridCellSize << "," << std::setprecision(2) << AvgSpheresPerActiveCell;
            }
            else
            {
                ss << ",,";
            }
            ss << ",";

            if (BVHDepth > 0)
            {
                ss << BVHDepth;
            }

            ss << "\n";
            return ss.str();
        }
    };

    inline FStudyRow RunSingleEvaluation(
        const std::string& scenario,
        int seed,
        ICollisionSolver* solver,
        const std::string& solverName,
        int threadCount,
        int N,
        float sizeRatio,
        int largeCount,
        float boxHalfSize,
        int warmupRuns,
        int sampleRuns,
        const std::vector<std::vector<FSphere>>& warmupSnapshots,
        const std::vector<std::vector<FSphere>>& sampleSnapshots)
    {
        solver->SetThreadCount(threadCount);

        for (int w = 0; w < warmupRuns; ++w)
        {
            std::vector<FSphere> workSpheres = warmupSnapshots[w];
            solver->Solve(workSpheres);
        }

        double   totalBroadMs          = 0.0;
        double   totalNarrowMs         = 0.0;
        double   totalTimeMs           = 0.0;
        uint64_t totalCandidatePairs   = 0;
        uint64_t totalActualCollisions = 0;

        std::vector<double> sampleTotals;
        sampleTotals.reserve(sampleRuns);

        for (int r = 0; r < sampleRuns; ++r)
        {
            std::vector<FSphere> workSpheres = sampleSnapshots[r];
            solver->Solve(workSpheres);

            const FCollisionStats& stats = solver->GetLastStats();
            totalBroadMs          += stats.BroadPhaseTimeMs;
            totalNarrowMs         += stats.NarrowPhaseTimeMs;
            totalTimeMs           += stats.TotalSolveTimeMs;
            totalCandidatePairs   += stats.CandidatePairCount;
            totalActualCollisions += stats.ActualCollisionCount;
            sampleTotals.push_back(stats.TotalSolveTimeMs);
        }

        std::sort(sampleTotals.begin(), sampleTotals.end());
        double medianTotal = sampleTotals[sampleRuns / 2];
        size_t p95Index    = static_cast<size_t>(floor(0.95 * (sampleRuns - 1)));
        double p95Total    = sampleTotals[p95Index];

        FStudyRow row;
        row.Scenario          = scenario;
        row.Seed              = seed;
        row.SolverName        = solverName;
        row.Threads           = threadCount;
        row.N                 = N;
        row.SizeRatio         = sizeRatio;
        row.LargeCount        = largeCount;
        row.BroadAvg          = totalBroadMs / static_cast<double>(sampleRuns);
        row.NarrowAvg         = totalNarrowMs / static_cast<double>(sampleRuns);
        row.TotalAvg          = totalTimeMs / static_cast<double>(sampleRuns);
        row.TotalMedian       = medianTotal;
        row.TotalP95          = p95Total;
        row.CandidatePairsAvg = totalCandidatePairs / sampleRuns;
        row.CollisionsAvg     = totalActualCollisions / sampleRuns;

        if (auto* grid = dynamic_cast<FUniformGridBase*>(solver))
        {
            int dx = 0, dy = 0, dz = 0;
            grid->GetGridDims(dx, dy, dz);
            row.GridDim      = dx;
            row.GridCellSize = grid->GetCellSize();
            int activeCells  = grid->GetActiveCellCount();
            row.AvgSpheresPerActiveCell = (activeCells > 0) ? (static_cast<float>(N) / static_cast<float>(activeCells)) : 0.0f;
        }

        if (auto* bvh = dynamic_cast<IBVHVisualizer*>(solver))
        {
            row.BVHDepth = bvh->GetMaxTreeDepth();
        }

        return row;
    }

    inline std::string RunCompleteStudy(
        float boxHalfSize = 2.0f,
        const std::string& outputCsvPath = "BenchmarkStudy_Results.csv",
        std::function<void(int current, int total, const std::string& description)> progressCallback = nullptr)
    {
        std::ofstream csv(outputCsvPath);
        if (!csv.is_open())
        {
            return "";
        }

        csv << "Scenario,Seed,Solver,Threads,N,SizeRatio,LargeCount,BroadAvg,NarrowAvg,TotalAvg,TotalMedian,TotalP95,CandidatePairsAvg,CollisionsAvg,GridDim,GridCellSize,AvgSpheresPerActiveCell,BVHDepth\n";
        csv.flush();

        unsigned int hwThreads = std::thread::hardware_concurrency();
        int defaultMtThreads = (hwThreads > 0) ? (std::min)(12, static_cast<int>(hwThreads)) : 8;

        NestedLoopSolver   nlSt;
        NestedLoopMTSolver nlMt(defaultMtThreads);
        UniformGridSolver  gridSt(boxHalfSize);
        UniformGridMTSolver gridMt(defaultMtThreads, boxHalfSize);
        BVHSolver          bvhSt;
        BVHMTSolver        bvhMt(defaultMtThreads);

        struct FSolverEntry
        {
            ICollisionSolver* Solver;
            std::string       Name;
            bool              bIsMT;
        };

        std::vector<FSolverEntry> allSolvers = {
            { &nlSt,   "NestedLoop_ST",  false },
            { &nlMt,   "NestedLoop_MT",  true  },
            { &gridSt, "UniformGrid_ST", false },
            { &gridMt, "UniformGrid_MT", true  },
            { &bvhSt,  "BVH_ST",         false },
            { &bvhMt,  "BVH_MT",         true  }
        };

        const int seeds[] = { 1, 2, 3 };
        const float fixedDt = 0.016f;

        auto PreRecordSnapshots = [&](int N, float sizeRatio, int largeCount, int seed, int warmupRuns, int sampleRuns,
                                      std::vector<std::vector<FSphere>>& outWarmup,
                                      std::vector<std::vector<FSphere>>& outSample)
        {
            outWarmup.clear();
            outSample.clear();
            outWarmup.reserve(warmupRuns);
            outSample.reserve(sampleRuns);

            std::vector<FSphere> sim = CreateSpheresScaled(N, boxHalfSize, sizeRatio, largeCount, seed);

            for (int w = 0; w < warmupRuns; ++w)
            {
                for (FSphere& s : sim)
                {
                    s.Update(fixedDt, false);
                    s.BoxCollisionCheck(boxHalfSize);
                }
                outWarmup.push_back(sim);
                nlSt.Solve(sim);
                for (FSphere& s : sim)
                {
                    s.BoxCollisionCheck(boxHalfSize);
                }
            }

            for (int r = 0; r < sampleRuns; ++r)
            {
                for (FSphere& s : sim)
                {
                    s.Update(fixedDt, false);
                    s.BoxCollisionCheck(boxHalfSize);
                }
                outSample.push_back(sim);
                nlSt.Solve(sim);
                for (FSphere& s : sim)
                {
                    s.BoxCollisionCheck(boxHalfSize);
                }
            }
        };

        int step = 0;
        int totalEstimatedSteps = 3 * (6 * 6 + 5 * 5 + 4 * 5 + 6 * 3); // Approximate progress counter

        auto ReportProgress = [&](const std::string& desc)
        {
            step++;
            if (progressCallback)
            {
                progressCallback(step, totalEstimatedSteps, desc);
            }
        };

        //=====================================================================
        // Experiment A: N-Scaling (Uniform size, 256 to 8192)
        //=====================================================================
        const int nScalingValues[] = { 256, 512, 1024, 2048, 4096, 8192 };
        for (int seed : seeds)
        {
            for (int N : nScalingValues)
            {
                int warmupRuns = 10;
                int sampleRuns = (N <= 1024) ? 30 : ((N <= 2048) ? 20 : 10);

                std::vector<std::vector<FSphere>> warmupSnapshots;
                std::vector<std::vector<FSphere>> sampleSnapshots;
                PreRecordSnapshots(N, 1.0f, 0, seed, warmupRuns, sampleRuns, warmupSnapshots, sampleSnapshots);

                for (const auto& entry : allSolvers)
                {
                    if (N >= 4096 && entry.Name == "NestedLoop_ST")
                    {
                        // Skip or use small run for O(N^2) ST at extreme scale to keep suite responsive
                        int smallSamples = 3;
                        std::vector<std::vector<FSphere>> smallSample(sampleSnapshots.begin(), sampleSnapshots.begin() + smallSamples);
                        FStudyRow row = RunSingleEvaluation("A_NScaling", seed, entry.Solver, entry.Name,
                            entry.bIsMT ? defaultMtThreads : 1, N, 1.0f, 0, boxHalfSize,
                            5, smallSamples, warmupSnapshots, smallSample);
                        csv << row.ToCsvLine();
                    }
                    else
                    {
                        FStudyRow row = RunSingleEvaluation("A_NScaling", seed, entry.Solver, entry.Name,
                            entry.bIsMT ? defaultMtThreads : 1, N, 1.0f, 0, boxHalfSize,
                            warmupRuns, sampleRuns, warmupSnapshots, sampleSnapshots);
                        csv << row.ToCsvLine();
                    }
                    csv.flush();
                    ReportProgress("Scenario A: N=" + std::to_string(N) + " (" + entry.Name + ")");
                }
            }
        }

        //=====================================================================
        // Experiment B1: Size-Ratio Scaling (N=2048, LargeCount=2, Ratio 1 to 16)
        //=====================================================================
        const float sizeRatios[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
        for (int seed : seeds)
        {
            for (float ratio : sizeRatios)
            {
                int N = 2048;
                int largeCount = 2;
                int warmupRuns = 10;
                int sampleRuns = 20;

                std::vector<std::vector<FSphere>> warmupSnapshots;
                std::vector<std::vector<FSphere>> sampleSnapshots;
                PreRecordSnapshots(N, ratio, largeCount, seed, warmupRuns, sampleRuns, warmupSnapshots, sampleSnapshots);

                for (const auto& entry : allSolvers)
                {
                    if (entry.Name == "NestedLoop_MT") continue; // Baseline ST is sufficient

                    FStudyRow row = RunSingleEvaluation("B1_SizeRatio", seed, entry.Solver, entry.Name,
                        entry.bIsMT ? defaultMtThreads : 1, N, ratio, largeCount, boxHalfSize,
                        warmupRuns, sampleRuns, warmupSnapshots, sampleSnapshots);
                    csv << row.ToCsvLine();
                    csv.flush();
                    ReportProgress("Scenario B1: Ratio=" + std::to_string(static_cast<int>(ratio)) + " (" + entry.Name + ")");
                }
            }
        }

        //=====================================================================
        // Experiment B2: Large-Count Scaling (N=2048, Ratio=8.0, Count 0 to 100)
        //=====================================================================
        const int largeCounts[] = { 0, 2, 20, 100 };
        for (int seed : seeds)
        {
            for (int count : largeCounts)
            {
                int N = 2048;
                float ratio = 8.0f;
                int warmupRuns = 10;
                int sampleRuns = 20;

                std::vector<std::vector<FSphere>> warmupSnapshots;
                std::vector<std::vector<FSphere>> sampleSnapshots;
                PreRecordSnapshots(N, ratio, count, seed, warmupRuns, sampleRuns, warmupSnapshots, sampleSnapshots);

                for (const auto& entry : allSolvers)
                {
                    if (entry.Name == "NestedLoop_MT") continue;

                    FStudyRow row = RunSingleEvaluation("B2_LargeCount", seed, entry.Solver, entry.Name,
                        entry.bIsMT ? defaultMtThreads : 1, N, ratio, count, boxHalfSize,
                        warmupRuns, sampleRuns, warmupSnapshots, sampleSnapshots);
                    csv << row.ToCsvLine();
                    csv.flush();
                    ReportProgress("Scenario B2: LargeCount=" + std::to_string(count) + " (" + entry.Name + ")");
                }
            }
        }

        //=====================================================================
        // Experiment C: Thread Scalability & Amdahl Bottleneck Analysis (N=2048 & 256)
        //=====================================================================
        std::vector<int> threadCounts = { 1, 2, 3, 4, 6, 8 };
        if (hwThreads >= 10) threadCounts.push_back(10);
        if (hwThreads >= 12) threadCounts.push_back(12);
        if (hwThreads >= 16) threadCounts.push_back(16);

        std::vector<FSolverEntry> mtSolvers = {
            { &nlMt,   "NestedLoop_MT", true },
            { &gridMt, "UniformGrid_MT", true },
            { &bvhMt,  "BVH_MT",         true }
        };

        const int expCNValues[] = { 2048, 256 };
        for (int N : expCNValues)
        {
            for (int seed : seeds)
            {
                int warmupRuns = 10;
                int sampleRuns = 20;

                std::vector<std::vector<FSphere>> warmupSnapshots;
                std::vector<std::vector<FSphere>> sampleSnapshots;
                PreRecordSnapshots(N, 1.0f, 0, seed, warmupRuns, sampleRuns, warmupSnapshots, sampleSnapshots);

                for (int th : threadCounts)
                {
                    for (const auto& entry : mtSolvers)
                    {
                        FStudyRow row = RunSingleEvaluation("C_ThreadScaling", seed, entry.Solver, entry.Name,
                            th, N, 1.0f, 0, boxHalfSize,
                            warmupRuns, sampleRuns, warmupSnapshots, sampleSnapshots);
                        csv << row.ToCsvLine();
                        csv.flush();
                        ReportProgress("Scenario C: N=" + std::to_string(N) + ", Th=" + std::to_string(th) + " (" + entry.Name + ")");
                    }
                }
            }
        }

        csv.close();
        return outputCsvPath;
    }
}
