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
    FHUDTracker(const FCPUInfo& cpuInfo)
        : m_CPUInfo(cpuInfo)
    {
        swprintf_s(m_HudTopText, L"Initializing...");
        swprintf_s(m_HudBottomText, L"");
    }

    void Update(float dt, double updateTimeMs, double renderTimeMs,
                const FCollisionStats& stats, size_t ballCount,
                const ICollisionSolver* activeSolver,
                int configuredThreads, bool bShowGridVis)
    {
        m_TimeAccum      += dt;
        m_FrameAccum     += 1;
        m_UpdateAccumMs  += updateTimeMs;
        m_RenderAccumMs  += renderTimeMs;
        m_BroadAccumMs   += stats.BroadPhaseTimeMs;
        m_NarrowAccumMs  += stats.NarrowPhaseTimeMs;
        m_ResolveAccumMs += stats.ResolutionTimeMs;

        if (m_TimeAccum >= Config::HUD_REFRESH_INTERVAL && m_FrameAccum > 0)
        {
            double currentFPS   = static_cast<double>(m_FrameAccum) / m_TimeAccum;
            double frameTimeMs  = (m_TimeAccum / static_cast<double>(m_FrameAccum)) * 1000.0;
            double avgUpdateMs  = m_UpdateAccumMs / static_cast<double>(m_FrameAccum);
            double avgRenderMs  = m_RenderAccumMs / static_cast<double>(m_FrameAccum);
            double avgBroadMs   = m_BroadAccumMs / static_cast<double>(m_FrameAccum);
            double avgNarrowMs  = m_NarrowAccumMs / static_cast<double>(m_FrameAccum);
            double avgResolveMs = m_ResolveAccumMs / static_cast<double>(m_FrameAccum);

            std::wstring strBalls = FormatCommas(ballCount);
            m_CachedCandidates    = FormatCommas(stats.CandidatePairCount);
            m_CachedCollisions    = FormatCommas(stats.ActualCollisionCount);

            swprintf_s(m_HudTopText,
                       L"CPU         : %s\n"
                       L"Cache       : %s\n"
                       L"\n"
                       L"Balls       : %s | Threads: %d\n"
                       L"Algorithm   : %s [%s]\n"
                       L"\n"
                       L"Frame Time  : %.1f ms (%.1f FPS) | Render: %.2f ms\n"
                       L"Broad Phase : %.3f ms\n"
                       L"Narrow Phase: %.2f ms\n"
                       L"Resolution  : %.3f ms",
                       m_CPUInfo.GetSummaryString().c_str(),
                       m_CPUInfo.GetCacheString().c_str(),
                       strBalls.c_str(),
                       activeSolver ? activeSolver->GetThreadCount() : 1,
                       activeSolver ? activeSolver->GetAlgorithmName() : L"Unknown",
                       activeSolver ? activeSolver->GetExecutionMode() : L"Unknown",
                       frameTimeMs, currentFPS,
                       avgRenderMs,
                       avgBroadMs,
                       avgNarrowMs,
                       avgResolveMs);

            m_TimeAccum      = 0.0;
            m_FrameAccum     = 0;
            m_UpdateAccumMs  = 0.0;
            m_RenderAccumMs  = 0.0;
            m_BroadAccumMs   = 0.0;
            m_NarrowAccumMs  = 0.0;
            m_ResolveAccumMs = 0.0;
        }

        const IBVHVisualizer* bvhVis = dynamic_cast<const IBVHVisualizer*>(activeSolver);
        if (bvhVis && bShowGridVis)
        {
            swprintf_s(m_HudBottomText,
                       L"Candidate Pairs : %s | Collisions: %s | BVH Depth: %d/%d (PgUp/PgDn) | Mode: %s (V)\n"
                       L"[1] Naive ST  [2] Naive MT  [3] Grid ST  [4] Grid MT  [5] BVH ST  [B] Bench  [G] Vis: ON  [H] HUD  (Tab: Cycle)",
                       m_CachedCandidates.c_str(),
                       m_CachedCollisions.c_str(),
                       bvhVis->GetVisualizerDepth(),
                       bvhVis->GetMaxTreeDepth(),
                       bvhVis->GetVisualizerModeName());
        }
        else
        {
            swprintf_s(m_HudBottomText,
                       L"Candidate Pairs : %s | Collisions: %s | Threads: %d (Hotkeys: [ / ])\n"
                       L"[1] Naive ST  [2] Naive MT  [3] Grid ST  [4] Grid MT  [5] BVH ST  [B] Bench  [G] Grid: %s  [H] HUD  (Tab: Cycle)",
                       m_CachedCandidates.c_str(),
                       m_CachedCollisions.c_str(),
                       configuredThreads,
                       bShowGridVis ? L"ON" : L"OFF");
        }
    }

    void Draw(FTextRenderer& textRenderer, int clientW, int clientH)
    {
        float bottomY = static_cast<float>(clientH) - 55.0f;
        textRenderer.DrawTextOverlay(m_HudTopText, 10.0f, 10.0f, 700.0f, 220.0f);
        textRenderer.DrawTextOverlay(m_HudBottomText, 10.0f, bottomY, 980.0f, 50.0f);
    }

private:
    static std::wstring FormatCommas(uint64_t val)
    {
        std::wstring s = std::to_wstring(val);
        int insertPos = static_cast<int>(s.length()) - 3;
        while (insertPos > 0)
        {
            s.insert(insertPos, L",");
            insertPos -= 3;
        }
        return s;
    }

private:
    FCPUInfo     m_CPUInfo;
    double       m_TimeAccum        = 0.0;
    int          m_FrameAccum       = 0;
    double       m_UpdateAccumMs    = 0.0;
    double       m_RenderAccumMs    = 0.0;
    double       m_BroadAccumMs     = 0.0;
    double       m_NarrowAccumMs    = 0.0;
    double       m_ResolveAccumMs   = 0.0;

    std::wstring m_CachedCandidates = L"0";
    std::wstring m_CachedCollisions = L"0";

    wchar_t      m_HudTopText[512]    = {};
    wchar_t      m_HudBottomText[512] = {};
};
