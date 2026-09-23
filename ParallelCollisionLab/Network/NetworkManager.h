#pragma once

#include "NetworkServer.h"
#include "NetworkClient.h"

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

        bool StartServer(uint16_t InPort = DEFAULT_SERVER_PORT);
        bool StartClient(const char* InServerIp = "127.0.0.1", uint16_t InServerPort = DEFAULT_SERVER_PORT);

        void UpdateServer(uint32_t CurrentTick, const std::vector<FSphere>& Spheres, float BoxHalfSize, float DeltaTime = 0.016f);
        void UpdateClient(std::vector<FSphere>& Spheres, float BoxHalfSize, float DeltaTime);

        ENetworkRole GetRole() const { return CurrentRole; }
        const wchar_t* GetRoleString() const;

        bool IsConnected() const;
        size_t GetClientCount() const;
        uint32_t GetPacketsSent() const;
        uint32_t GetPacketsReceived() const;
        uint32_t GetLastSnapshotTick() const;

    private:
        FWinsockScope   WinsockScope;
        ENetworkRole    CurrentRole = ENetworkRole::Standalone;
        FNetworkServer  ServerInstance;
        FNetworkClient  ClientInstance;
    };
}
