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

        bool Start(uint16_t port = DEFAULT_SERVER_PORT);
        void Stop();
        void Update(uint32_t currentTick, const std::vector<FSphere>& spheres, float boxHalfSize, float dt = 0.016f);

        bool IsRunning() const { return m_Socket != INVALID_SOCKET; }
        size_t GetClientCount() const { return m_Clients.size(); }
        uint32_t GetPacketsSent() const { return m_PacketsSent; }
        uint32_t GetPacketsReceived() const { return m_PacketsReceived; }

    private:
        void RegisterOrRefreshClient(const sockaddr_in& clientAddr);
        void RemoveClient(const sockaddr_in& clientAddr);
        void SendHandshakeResponse(const sockaddr_in& target, uint16_t sphereCount, float boxHalfSize);

    private:
        SOCKET                        m_Socket = INVALID_SOCKET;
        uint16_t                      m_Port   = DEFAULT_SERVER_PORT;
        std::vector<FConnectedClient> m_Clients;
        uint32_t                      m_PacketsSent     = 0;
        uint32_t                      m_PacketsReceived = 0;
    };
}
