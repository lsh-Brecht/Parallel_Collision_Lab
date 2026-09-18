#pragma once

#include <windows.h>
#include <cstdint>

//=============================================================================
// High-Resolution Performance Counter Timer
//=============================================================================
class FTimer
{
public:
    FTimer()
    {
        QueryPerformanceFrequency(&m_Frequency);
        QueryPerformanceCounter(&m_LastTime);
    }

    float Tick()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(now.QuadPart - m_LastTime.QuadPart) / static_cast<float>(m_Frequency.QuadPart);
        m_LastTime = now;

        // Clamp dt to ~30 FPS (0.033s) to prevent tunneling on severe frame hitches
        if (dt > 0.033f)
        {
            dt = 0.033f;
        }
        return dt;
    }

    int64_t GetFrequency() const { return m_Frequency.QuadPart; }

    static double GetElapsedMs(const LARGE_INTEGER& start, const LARGE_INTEGER& end, int64_t frequency)
    {
        return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency);
    }

private:
    LARGE_INTEGER m_Frequency = {};
    LARGE_INTEGER m_LastTime  = {};
};
