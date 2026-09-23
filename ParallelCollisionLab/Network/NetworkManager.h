#pragma once

#include "NetworkProtocol.h"
#include "NetworkServer.h"
#include "NetworkClient.h"
#include "../Core/Sphere.h"
#include <vector>

namespace Network
{
    enum class ENetworkRole
    {
        Standalone,
        Server,
        Client
    };

    class FNetworkManager
    {
    public:
        FNetworkManager();
        ~FNetworkManager();

        void Shutdown();

        bool StartServer(uint16_t port = DEFAULT_SERVER_PORT);
        bool StartClient(const char* serverIp = "127.0.0.1", uint16_t serverPort = DEFAULT_SERVER_PORT);

        void UpdateServer(uint32_t currentTick, const std::vector<FSphere>& spheres, float boxHalfSize, float dt = 0.016f);
        void UpdateClient(std::vector<FSphere>& spheres, float boxHalfSize, float dt);

        ENetworkRole GetRole() const { return m_Role; }
        const wchar_t* GetRoleString() const;

        bool IsConnected() const;
        size_t GetClientCount() const;
        uint32_t GetPacketsSent() const;
        uint32_t GetPacketsReceived() const;
        uint32_t GetLastSnapshotTick() const;

    private:
        FWinsockScope   m_WinsockScope;
        ENetworkRole    m_Role = ENetworkRole::Standalone;
        FNetworkServer  m_Server;
        FNetworkClient  m_Client;
    };
}
