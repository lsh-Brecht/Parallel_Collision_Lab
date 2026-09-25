#pragma once

#include "NetworkProtocol.h"
#include "../Core/Sphere.h"
#include <vector>
#include <string>

namespace Network
{
    class FNetworkClient
    {
    public:
        FNetworkClient();
        ~FNetworkClient();

        bool Connect(const char* InServerIp = "127.0.0.1", uint16_t InServerPort = DEFAULT_SERVER_PORT);
        void Disconnect();
        void Update(std::vector<FSphere>& Spheres, float BoxHalfSize, float DeltaTime);
        void SendInput(float x, float y, float z);

        bool IsConnected() const { return bIsConnected; }
        uint32_t GetLastSnapshotTick() const { return LastReceivedSnapshotTick; }
        uint32_t GetPacketsSent() const { return TotalPacketsSent; }
        uint32_t GetPacketsReceived() const { return TotalPacketsReceived; }

        int32_t GetAssignedSphereId() const { return AssignedSphereId; }
        EPlanetType GetAssignedPlanet() const { return static_cast<EPlanetType>(AssignedPlanet); }

    private:
        void SendHandshakeRequest();
        void SendHeartbeat();
        void SendDisconnect();

    private:
        SOCKET      ClientSocket             = INVALID_SOCKET;
        sockaddr_in ServerEndpoint           = {};
        std::string ServerIpAddress          = "127.0.0.1";
        uint16_t    ServerPort               = DEFAULT_SERVER_PORT;
        bool        bIsConnected             = false;
        float       KeepAliveTimer           = 0.0f;
        uint32_t    LastReceivedSnapshotTick = 0;
        uint32_t    TotalPacketsSent         = 0;
        uint32_t    TotalPacketsReceived     = 0;

        int32_t     AssignedSphereId         = -1;
        uint8_t     AssignedPlanet           = 0;
        uint32_t    InputSeq                 = 0;
    };
}
