#pragma once

#include <string>
#include <cstdio>
#include "TextRenderer.h"
#include "../Core/CPUInfo.h"
#include "../Core/AppConfig.h"
#include "../Collision/ICollisionSolver.h"
#include "../Collision/BVH/IBVHVisualizer.h"

//=============================================================================
// FHUDTracker - Real-time performance statistics accumulator & HUD renderer
//=============================================================================
class FHUDTracker
{
public:
    FHUDTracker(const FCPUInfo& InCPUInfo)
        : CPUInfo(InCPUInfo)
    {
        swprintf_s(HudTopText, L"Initializing...");
        swprintf_s(HudBottomText, L"");
    }

    void Update(float DeltaTime, double UpdateTimeMs, double RenderTimeMs,
                const FCollisionStats& Stats, size_t BallCount,
                const ICollisionSolver* ActiveSolver,
                bool bShowGridVis,
                bool bMultiScale = false,
                const wchar_t* NetStatus = nullptr,
                bool bDamping = false,
                size_t SleepingCount = 0,
                const wchar_t* ControlPrompt = nullptr,
                bool bIsClient = false)
    {
        TimeAccumulator        += DeltaTime;
        FrameCountAccumulator  += 1;
        UpdateAccumMs          += UpdateTimeMs;
        RenderAccumMs          += RenderTimeMs;
        BroadAccumMs           += Stats.BroadPhaseTimeMs;
        NarrowAccumMs          += Stats.NarrowPhaseTimeMs;

        if (TimeAccumulator >= Config::HUD_REFRESH_INTERVAL && FrameCountAccumulator > 0)
        {
            double currentFPS   = static_cast<double>(FrameCountAccumulator) / TimeAccumulator;
            double frameTimeMs  = (TimeAccumulator / static_cast<double>(FrameCountAccumulator)) * 1000.0;
            double avgUpdateMs  = UpdateAccumMs / static_cast<double>(FrameCountAccumulator);
            double avgRenderMs  = RenderAccumMs / static_cast<double>(FrameCountAccumulator);
            double avgBroadMs   = BroadAccumMs / static_cast<double>(FrameCountAccumulator);
            double avgNarrowMs  = NarrowAccumMs / static_cast<double>(FrameCountAccumulator);

            CachedBallCount       = FormatCommas(BallCount);
            CachedCandidates      = FormatCommas(Stats.CandidatePairCount);
            CachedCollisions      = FormatCommas(Stats.ActualCollisionCount);

            if (bIsClient)
            {
                swprintf_s(HudTopText,
                           L"CPU         : %s\n"
                           L"Cache       : %s\n"
                           L"Network     : %s\n"
                           L"\n"
                           L"Frame Time  : %.1f ms (%.1f FPS) | Render: %.2f ms",
                           CPUInfo.GetSummaryString().c_str(),
                           CPUInfo.GetCacheString().c_str(),
                           NetStatus ? NetStatus : L"Client",
                           frameTimeMs, currentFPS,
                           avgRenderMs);
            }
            else
            {
                swprintf_s(HudTopText,
                           L"CPU         : %s\n"
                           L"Cache       : %s\n"
                           L"Network     : %s\n"
                           L"\n"
                           L"Algorithm   : %s [%s] | Threads: %d\n"
                           L"\n"
                           L"Frame Time  : %.1f ms (%.1f FPS) | Render: %.2f ms\n"
                           L"Broad Phase : %.3f ms\n"
                           L"Narrow Phase: %.3f ms",
                           CPUInfo.GetSummaryString().c_str(),
                           CPUInfo.GetCacheString().c_str(),
                           NetStatus ? NetStatus : L"Standalone",
                           ActiveSolver ? ActiveSolver->GetAlgorithmName() : L"Unknown",
                           ActiveSolver ? ActiveSolver->GetExecutionMode() : L"Unknown",
                           ActiveSolver ? ActiveSolver->GetThreadCount() : 1,
                           frameTimeMs, currentFPS,
                           avgRenderMs,
                           avgBroadMs,
                           avgNarrowMs);
            }

            TimeAccumulator       = 0.0;
            FrameCountAccumulator = 0;
            UpdateAccumMs         = 0.0;
            RenderAccumMs         = 0.0;
            BroadAccumMs          = 0.0;
            NarrowAccumMs         = 0.0;
        }

        if (CachedBallCount == L"0" && BallCount > 0)
        {
            CachedBallCount = FormatCommas(BallCount);
        }

        wchar_t dampStr[32];
        if (bDamping)
            swprintf_s(dampStr, L"ON (%zu asleep)", SleepingCount);
        else
            swprintf_s(dampStr, L"OFF");

        const wchar_t* prompt = ControlPrompt;

        if (bIsClient)
        {
            swprintf_s(HudBottomText,
                       L"Balls: %s\n"
                       L"%s\n"
                       L"[H] Toggle HUD",
                       CachedBallCount.c_str(),
                       prompt ? prompt : L"");
        }
        else
        {
            const IBVHVisualizer* bvhVis = dynamic_cast<const IBVHVisualizer*>(ActiveSolver);
            const wchar_t* visLabel = (bvhVis && bShowGridVis) ? L"Vis: ON" : (bShowGridVis ? L"Grid: ON" : L"Grid: OFF");

            swprintf_s(HudBottomText,
                       L"Balls: %s\n"
                       L"Pairs: %s | Collisions: %s\n"
                       L"[1] Naive (2 MT) | [3] Grid (4 MT) | [5] BVH (6 MT)\n"
                       L"[D] Damp: %s  [M] %s  [B] Bench  [G] %s  [H] HUD",
                       CachedBallCount.c_str(),
                       CachedCandidates.c_str(),
                       CachedCollisions.c_str(),
                       dampStr,
                       bMultiScale ? L"Multi" : L"Uniform",
                       visLabel);
        }
    }

    void Draw(FTextRenderer& TextRenderer, int ClientWidth, int ClientHeight)
    {
        float bottomY = static_cast<float>(ClientHeight) - 90.0f;
        TextRenderer.DrawTextOverlay(HudTopText, 10.0f, 10.0f, 700.0f, 240.0f);
        TextRenderer.DrawTextOverlay(HudBottomText, 10.0f, bottomY, static_cast<float>(ClientWidth) - 20.0f, 88.0f);
    }

private:
    static std::wstring FormatCommas(uint64_t Value)
    {
        std::wstring s = std::to_wstring(Value);
        int insertPos = static_cast<int>(s.length()) - 3;
        while (insertPos > 0)
        {
            s.insert(insertPos, L",");
            insertPos -= 3;
        }
        return s;
    }

private:
    FCPUInfo     CPUInfo;
    double       TimeAccumulator        = 0.0;
    int          FrameCountAccumulator  = 0;
    double       UpdateAccumMs          = 0.0;
    double       RenderAccumMs          = 0.0;
    double       BroadAccumMs           = 0.0;
    double       NarrowAccumMs          = 0.0;

    std::wstring CachedBallCount        = L"0";
    std::wstring CachedCandidates       = L"0";
    std::wstring CachedCollisions       = L"0";

    wchar_t      HudTopText[512]        = {};
    wchar_t      HudBottomText[512]     = {};
};
