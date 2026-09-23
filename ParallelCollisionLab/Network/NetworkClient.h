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

        bool Connect(const char* serverIp = "127.0.0.1", uint16_t serverPort = DEFAULT_SERVER_PORT);
        void Disconnect();
        void Update(std::vector<FSphere>& spheres, float boxHalfSize, float dt);

        bool IsConnected() const { return m_bConnected; }
        uint32_t GetLastSnapshotTick() const { return m_LastSnapshotTick; }
        uint32_t GetPacketsSent() const { return m_PacketsSent; }
        uint32_t GetPacketsReceived() const { return m_PacketsReceived; }

    private:
        void SendHandshakeRequest();
        void SendHeartbeat();
        void SendDisconnect();

    private:
        SOCKET      m_Socket           = INVALID_SOCKET;
        sockaddr_in m_ServerAddr       = {};
        std::string m_ServerIp         = "127.0.0.1";
        uint16_t    m_ServerPort       = DEFAULT_SERVER_PORT;
        bool        m_bConnected       = false;
        float       m_HandshakeTimer   = 0.0f;
        uint32_t    m_LastSnapshotTick = 0;
        uint32_t    m_PacketsSent      = 0;
        uint32_t    m_PacketsReceived  = 0;
    };
}
