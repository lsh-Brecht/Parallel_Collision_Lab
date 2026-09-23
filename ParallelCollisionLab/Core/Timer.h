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
        QueryPerformanceFrequency(&Frequency);
        QueryPerformanceCounter(&LastTime);
    }

    float Tick()
    {
        LARGE_INTEGER Now;
        QueryPerformanceCounter(&Now);
        float DeltaTime = static_cast<float>(Now.QuadPart - LastTime.QuadPart) / static_cast<float>(Frequency.QuadPart);
        LastTime = Now;

        // Clamp DeltaTime to ~30 FPS (0.033s) to prevent tunneling on severe frame hitches
        if (DeltaTime > 0.033f)
        {
            DeltaTime = 0.033f;
        }
        return DeltaTime;
    }

    int64_t GetFrequency() const { return Frequency.QuadPart; }

    static double GetElapsedMs(const LARGE_INTEGER& Start, const LARGE_INTEGER& End, int64_t InFrequency)
    {
        return static_cast<double>(End.QuadPart - Start.QuadPart) * 1000.0 / static_cast<double>(InFrequency);
    }

private:
    LARGE_INTEGER Frequency = {};
    LARGE_INTEGER LastTime  = {};
};
