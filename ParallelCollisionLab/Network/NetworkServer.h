#pragma once

#include "NetworkProtocol.h"
#include "../Core/Sphere.h"
#include <vector>

namespace Network
{
    struct FConnectedClient
    {
        sockaddr_in Addr;
        float       TimeSinceLastSeen = 0.0f;
    };

    class FNetworkServer
    {
    public:
        FNetworkServer();
        ~FNetworkServer();

        bool Start(uint16_t InPort = DEFAULT_SERVER_PORT);
        void Stop();
        void Update(uint32_t CurrentTick, const std::vector<FSphere>& Spheres, float BoxHalfSize, float DeltaTime = 0.016f);

        bool IsRunning() const { return ServerSocket != INVALID_SOCKET; }
        size_t GetClientCount() const { return ConnectedClients.size(); }
        uint32_t GetPacketsSent() const { return TotalPacketsSent; }
        uint32_t GetPacketsReceived() const { return TotalPacketsReceived; }

    private:
        void RegisterOrRefreshClient(const sockaddr_in& ClientAddr);
        void RemoveClient(const sockaddr_in& ClientAddr);
        void SendHandshakeResponse(const sockaddr_in& Target, uint16_t SphereCount, float BoxHalfSize);

    private:
        SOCKET                        ServerSocket         = INVALID_SOCKET;
        uint16_t                      ListenPort           = DEFAULT_SERVER_PORT;
        std::vector<FConnectedClient> ConnectedClients;
        uint32_t                      TotalPacketsSent     = 0;
        uint32_t                      TotalPacketsReceived = 0;
    };
}
