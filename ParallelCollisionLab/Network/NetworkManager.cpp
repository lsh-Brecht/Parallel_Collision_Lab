#include "NetworkManager.h"

namespace Network
{
    FNetworkManager::FNetworkManager()
        : m_Role(ENetworkRole::Standalone)
    {
    }

    FNetworkManager::~FNetworkManager()
    {
        Shutdown();
    }

    void FNetworkManager::Shutdown()
    {
        if (m_Role == ENetworkRole::Server)
        {
            m_Server.Stop();
        }
        else if (m_Role == ENetworkRole::Client)
        {
            m_Client.Disconnect();
        }
        m_Role = ENetworkRole::Standalone;
    }

    bool FNetworkManager::StartServer(uint16_t port)
    {
        Shutdown();

        if (!m_WinsockScope.IsValid())
            return false;

        if (m_Server.Start(port))
        {
            m_Role = ENetworkRole::Server;
            return true;
        }
        return false;
    }

    bool FNetworkManager::StartClient(const char* serverIp, uint16_t serverPort)
    {
        Shutdown();

        if (!m_WinsockScope.IsValid())
            return false;

        if (m_Client.Connect(serverIp, serverPort))
        {
            m_Role = ENetworkRole::Client;
            return true;
        }
        return false;
    }

    void FNetworkManager::UpdateServer(uint32_t currentTick, const std::vector<FSphere>& spheres, float boxHalfSize, float dt)
    {
        if (m_Role == ENetworkRole::Server)
        {
            m_Server.Update(currentTick, spheres, boxHalfSize, dt);
        }
    }

    void FNetworkManager::UpdateClient(std::vector<FSphere>& spheres, float boxHalfSize, float dt)
    {
        if (m_Role == ENetworkRole::Client)
        {
            m_Client.Update(spheres, boxHalfSize, dt);
        }
    }

    bool FNetworkManager::IsConnected() const
    {
        if (m_Role == ENetworkRole::Client)
            return m_Client.IsConnected();
        return false;
    }

    size_t FNetworkManager::GetClientCount() const
    {
        if (m_Role == ENetworkRole::Server)
            return m_Server.GetClientCount();
        return 0;
    }

    uint32_t FNetworkManager::GetPacketsSent() const
    {
        if (m_Role == ENetworkRole::Server)
            return m_Server.GetPacketsSent();
        if (m_Role == ENetworkRole::Client)
            return m_Client.GetPacketsSent();
        return 0;
    }

    uint32_t FNetworkManager::GetPacketsReceived() const
    {
        if (m_Role == ENetworkRole::Server)
            return m_Server.GetPacketsReceived();
        if (m_Role == ENetworkRole::Client)
            return m_Client.GetPacketsReceived();
        return 0;
    }

    uint32_t FNetworkManager::GetLastSnapshotTick() const
    {
        if (m_Role == ENetworkRole::Client)
            return m_Client.GetLastSnapshotTick();
        return 0;
    }

    const wchar_t* FNetworkManager::GetRoleString() const
    {
        switch (m_Role)
        {
        case ENetworkRole::Server: return L"Server";
        case ENetworkRole::Client: return L"Client";
        default:                   return L"Standalone";
        }
    }
}
