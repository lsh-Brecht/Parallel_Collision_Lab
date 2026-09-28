#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <deque>
#include <string>
#include <cstdlib>
#include <algorithm>

namespace Network
{
    // Fixed jitter variation range (±20ms) when latency is active
    static const int SIMULATED_JITTER_MS = 20;

    struct FPendingNetPacket
    {
        std::vector<uint8_t> Buffer;
        sockaddr_in          Endpoint;
        double               ReleaseTimeSec = 0.0;
    };

    class FNetworkSimulator
    {
    public:
        FNetworkSimulator()
        {
            QueryPerformanceFrequency(&TimerFreq);
        }

        void SetLatencyMs(int InLatencyMs)
        {
            LatencyMs = (std::max)(0, InLatencyMs);
        }

        void SetLossPercent(float InLossPercent)
        {
            LossPercent = (std::max)(0.0f, (std::min)(100.0f, InLossPercent));
        }

        int   GetLatencyMs()   const { return LatencyMs; }
        int   GetJitterMs()    const { return (LatencyMs > 0) ? SIMULATED_JITTER_MS : 0; }
        float GetLossPercent() const { return LossPercent; }
        bool  IsEnabled()      const { return LatencyMs > 0 || LossPercent > 0.0f; }

        void CycleLatency()
        {
            if (LatencyMs == 0)        LatencyMs = 50;
            else if (LatencyMs == 50)  LatencyMs = 100;
            else if (LatencyMs == 100) LatencyMs = 200;
            else                       LatencyMs = 0;
            ResetStats();
        }

        void CycleLoss()
        {
            if (LossPercent < 1.0f)         LossPercent = 5.0f;
            else if (LossPercent < 7.0f)    LossPercent = 10.0f;
            else if (LossPercent < 15.0f)   LossPercent = 20.0f;
            else                            LossPercent = 0.0f;
            ResetStats();
        }

        void ResetStats()
        {
            TotalProcessed = 0;
            TotalDropped   = 0;
            TotalDelayed   = 0;
        }

        void Clear()
        {
            PacketQueue.clear();
            ResetStats();
        }

        bool EnqueuePacket(const void* Data, size_t Size, const sockaddr_in& Endpoint, double CurrentTimeSec = -1.0)
        {
            if (Data == nullptr || Size == 0)
                return false;

            TotalProcessed++;

            if (LossPercent > 0.0f)
            {
                float roll = static_cast<float>(rand() % 10000) / 100.0f;
                if (roll < LossPercent)
                {
                    TotalDropped++;
                    return false;
                }
            }

            if (LatencyMs == 0)
            {
                return true;
            }

            if (CurrentTimeSec < 0.0)
            {
                CurrentTimeSec = GetCurrentTimeSec();
            }

            int range = SIMULATED_JITTER_MS * 2 + 1;
            float jitterOffsetMs = static_cast<float>((rand() % range) - SIMULATED_JITTER_MS);
            float finalDelayMs = (std::max)(0.0f, static_cast<float>(LatencyMs) + jitterOffsetMs);
            double releaseTime = CurrentTimeSec + (finalDelayMs / 1000.0);

            FPendingNetPacket packet;
            packet.Buffer.assign(reinterpret_cast<const uint8_t*>(Data), reinterpret_cast<const uint8_t*>(Data) + Size);
            packet.Endpoint       = Endpoint;
            packet.ReleaseTimeSec = releaseTime;

            auto it = std::upper_bound(PacketQueue.begin(), PacketQueue.end(), packet,
                [](const FPendingNetPacket& a, const FPendingNetPacket& b) {
                    return a.ReleaseTimeSec < b.ReleaseTimeSec;
                });
            PacketQueue.insert(it, std::move(packet));

            TotalDelayed++;
            return true;
        }

        bool PopReadyPacket(std::vector<uint8_t>& OutData, sockaddr_in& OutEndpoint, double CurrentTimeSec = -1.0)
        {
            if (PacketQueue.empty())
                return false;

            if (CurrentTimeSec < 0.0)
            {
                CurrentTimeSec = GetCurrentTimeSec();
            }

            if (PacketQueue.front().ReleaseTimeSec <= CurrentTimeSec)
            {
                OutData     = std::move(PacketQueue.front().Buffer);
                OutEndpoint = PacketQueue.front().Endpoint;
                PacketQueue.pop_front();
                return true;
            }

            return false;
        }

        size_t GetQueuedPacketCount() const { return PacketQueue.size(); }
        uint32_t GetTotalProcessed()   const { return TotalProcessed; }
        uint32_t GetTotalDropped()     const { return TotalDropped; }

        double GetActualDropPercent() const
        {
            return (TotalProcessed > 0) ? (static_cast<double>(TotalDropped) / static_cast<double>(TotalProcessed) * 100.0) : 0.0;
        }

        std::wstring GetStatusString() const
        {
            if (!IsEnabled())
            {
                return L"Off";
            }

            wchar_t buf[256];
            swprintf_s(buf,
                L"Latency: %dms (±%dms) | Loss: %.1f%% (Drop: %u/%u, %.1f%%) | Q: %zu",
                LatencyMs, (LatencyMs > 0 ? SIMULATED_JITTER_MS : 0), LossPercent,
                TotalDropped, TotalProcessed, GetActualDropPercent(),
                PacketQueue.size());
            return buf;
        }

        std::wstring GetShortStatusString() const
        {
            if (!IsEnabled())
            {
                return L"Sim: Off";
            }

            wchar_t buf[128];
            swprintf_s(buf, L"Sim: %dms (±%d) | Loss %.0f%%", LatencyMs, (LatencyMs > 0 ? SIMULATED_JITTER_MS : 0), LossPercent);
            return buf;
        }

    private:
        double GetCurrentTimeSec() const
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            return static_cast<double>(now.QuadPart) / static_cast<double>(TimerFreq.QuadPart);
        }

        int           LatencyMs      = 0;
        float         LossPercent    = 0.0f;

        uint32_t      TotalProcessed = 0;
        uint32_t      TotalDropped   = 0;
        uint32_t      TotalDelayed   = 0;

        LARGE_INTEGER TimerFreq      = {};
        std::deque<FPendingNetPacket> PacketQueue;
    };
}
