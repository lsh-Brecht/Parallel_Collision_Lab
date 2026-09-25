#pragma once

#include "NetworkProtocol.h"
#include "../Core/Sphere.h"
#include <vector>

class FSimulationWorld;

namespace Network
{
    struct FConnectedClient
    {
        sockaddr_in Addr;
        float       TimeSinceLastSeen      = 0.0f;
        int32_t     AssignedSphereId       = -1;
        uint8_t     AssignedPlanet         = 0; // 0: None/Spectator, 1: Earth, 2: Mars, 3: UVMap
        uint32_t    FullSyncRemainingTicks = 120; // Full snapshot for first 120 ticks upon joining/reconnecting
    };

    class FNetworkServer
    {
    public:
        FNetworkServer();
        ~FNetworkServer();

        bool Start(uint16_t InPort = DEFAULT_SERVER_PORT);
        void Stop();

        // Process incoming client packets (handshakes, disconnects, heartbeats, inputs)
        void ProcessIncoming(FSimulationWorld& World, float DeltaTime = 0.016f);

        // Broadcast current physics snapshot to all connected clients
        void BroadcastSnapshot(uint32_t CurrentTick, const std::vector<FSphere>& Spheres, float BoxHalfSize);

        bool IsRunning() const { return ServerSocket != INVALID_SOCKET; }
        size_t GetClientCount() const { return ConnectedClients.size(); }
        uint32_t GetPacketsSent() const { return TotalPacketsSent; }
        uint32_t GetPacketsReceived() const { return TotalPacketsReceived; }

        bool IsPlanetActive(EPlanetType type) const;
        int32_t GetPlanetSphereId(EPlanetType type) const;
        void SyncPromotedPlanets(FSimulationWorld& World);

    private:
        void RegisterOrRefreshClient(const sockaddr_in& ClientAddr, FSimulationWorld& World);
        void RemoveClient(const sockaddr_in& ClientAddr, FSimulationWorld& World);
        void SendHandshakeResponse(const sockaddr_in& Target, uint32_t SphereCount, float BoxHalfSize, int32_t AssignedSphereId, uint8_t AssignedPlanet);

    private:
        SOCKET                        ServerSocket             = INVALID_SOCKET;
        uint16_t                      ListenPort               = DEFAULT_SERVER_PORT;
        std::vector<FConnectedClient> ConnectedClients;
        uint32_t                      TotalPacketsSent         = 0;
        uint32_t                      TotalPacketsReceived     = 0;
        uint32_t                      LastBroadcastSphereCount = 0;
    };
}
