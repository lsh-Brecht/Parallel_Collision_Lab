#include "NetworkManager.h"

namespace Network
{
    FNetworkManager::FNetworkManager()
        : CurrentRole(ENetworkRole::Standalone)
    {
    }

    FNetworkManager::~FNetworkManager()
    {
        Shutdown();
    }

    void FNetworkManager::Shutdown()
    {
        if (CurrentRole == ENetworkRole::Server)
        {
            ServerInstance.Stop();
        }
        else if (CurrentRole == ENetworkRole::Client)
        {
            ClientInstance.Disconnect();
        }
        CurrentRole = ENetworkRole::Standalone;
    }

    bool FNetworkManager::StartServer(uint16_t InPort)
    {
        Shutdown();

        if (!WinsockScope.IsValid())
            return false;

        if (ServerInstance.Start(InPort))
        {
            CurrentRole = ENetworkRole::Server;
            return true;
        }
        return false;
    }

    bool FNetworkManager::StartClient(const char* InServerIp, uint16_t InServerPort)
    {
        Shutdown();

        if (!WinsockScope.IsValid())
            return false;

        if (ClientInstance.Connect(InServerIp, InServerPort))
        {
            CurrentRole = ENetworkRole::Client;
            return true;
        }
        return false;
    }

    void FNetworkManager::UpdateServer(uint32_t CurrentTick, const std::vector<FSphere>& Spheres, float BoxHalfSize, float DeltaTime)
    {
        if (CurrentRole == ENetworkRole::Server)
        {
            ServerInstance.Update(CurrentTick, Spheres, BoxHalfSize, DeltaTime);
        }
    }

    void FNetworkManager::UpdateClient(std::vector<FSphere>& Spheres, float BoxHalfSize, float DeltaTime)
    {
        if (CurrentRole == ENetworkRole::Client)
        {
            ClientInstance.Update(Spheres, BoxHalfSize, DeltaTime);
        }
    }

    bool FNetworkManager::IsConnected() const
    {
        if (CurrentRole == ENetworkRole::Client)
            return ClientInstance.IsConnected();
        return false;
    }

    size_t FNetworkManager::GetClientCount() const
    {
        if (CurrentRole == ENetworkRole::Server)
            return ServerInstance.GetClientCount();
        return 0;
    }

    uint32_t FNetworkManager::GetPacketsSent() const
    {
        if (CurrentRole == ENetworkRole::Server)
            return ServerInstance.GetPacketsSent();
        if (CurrentRole == ENetworkRole::Client)
            return ClientInstance.GetPacketsSent();
        return 0;
    }

    uint32_t FNetworkManager::GetPacketsReceived() const
    {
        if (CurrentRole == ENetworkRole::Server)
            return ServerInstance.GetPacketsReceived();
        if (CurrentRole == ENetworkRole::Client)
            return ClientInstance.GetPacketsReceived();
        return 0;
    }

    uint32_t FNetworkManager::GetLastSnapshotTick() const
    {
        if (CurrentRole == ENetworkRole::Client)
            return ClientInstance.GetLastSnapshotTick();
        return 0;
    }

    const wchar_t* FNetworkManager::GetRoleString() const
    {
        switch (CurrentRole)
        {
        case ENetworkRole::Server: return L"Server";
        case ENetworkRole::Client: return L"Client";
        default:                   return L"Standalone";
        }
    }
}
