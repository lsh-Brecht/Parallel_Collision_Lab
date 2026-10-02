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
        FNetworkSimulator();

        void SetLatencyMs(int InLatencyMs);
        void SetLossPercent(float InLossPercent);

        int   GetLatencyMs()   const { return LatencyMs; }
        int   GetJitterMs()    const { return (LatencyMs > 0) ? SIMULATED_JITTER_MS : 0; }
        float GetLossPercent() const { return LossPercent; }
        bool  IsEnabled()      const { return LatencyMs > 0 || LossPercent > 0.0f; }

        void CycleLatency();
        void CycleLoss();
        void ResetStats();
        void Clear();

        bool EnqueuePacket(const void* Data, size_t Size, const sockaddr_in& Endpoint, double CurrentTimeSec = -1.0);
        bool PopReadyPacket(std::vector<uint8_t>& OutData, sockaddr_in& OutEndpoint, double CurrentTimeSec = -1.0);

        size_t   GetQueuedPacketCount() const { return PacketQueue.size(); }
        uint32_t GetTotalProcessed()    const { return TotalProcessed; }
        uint32_t GetTotalDropped()      const { return TotalDropped; }
        double   GetActualDropPercent() const;

        std::wstring GetStatusString()      const;
        std::wstring GetShortStatusString() const;

    private:
        double GetCurrentTimeSec() const;

        int           LatencyMs      = 0;
        float         LossPercent    = 0.0f;

        uint32_t      TotalProcessed = 0;
        uint32_t      TotalDropped   = 0;
        uint32_t      TotalDelayed   = 0;

        LARGE_INTEGER TimerFreq      = {};
        std::deque<FPendingNetPacket> PacketQueue;
    };
}
