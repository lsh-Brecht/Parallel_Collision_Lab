#pragma once

#include "NetworkProtocol.h"
#include "../Core/Sphere.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstddef>
#include <algorithm>

namespace Network
{
    enum class ENetworkRole
    {
        Standalone,
        Server,
        Client
    };

    struct FConnectedClient
    {
        sockaddr_in Addr;
        float       TimeSinceLastSeen = 0.0f;
    };

    class FNetworkManager
    {
    public:
        FNetworkManager()
            : m_Socket(INVALID_SOCKET)
        {
        }

        ~FNetworkManager()
        {
            Shutdown();
        }

        void Shutdown()
        {
            if (m_Role == ENetworkRole::Client && m_bConnected && m_Socket != INVALID_SOCKET)
            {
                SendDisconnect();
            }

            if (m_Socket != INVALID_SOCKET)
            {
                closesocket(m_Socket);
                m_Socket = INVALID_SOCKET;
            }
            m_Role = ENetworkRole::Standalone;
            m_Clients.clear();
            m_bConnected = false;
            m_PacketsSent = 0;
            m_PacketsReceived = 0;
            m_LastSnapshotTick = 0;
        }

        //---------------------------------------------------------------------
        // Server Mode Setup
        //---------------------------------------------------------------------
        bool StartServer(uint16_t port = DEFAULT_SERVER_PORT)
        {
            Shutdown();

            if (!m_WinsockScope.IsValid())
                return false;

            m_Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (m_Socket == INVALID_SOCKET)
                return false;

            // Non-blocking mode
            u_long nonBlocking = 1;
            ioctlsocket(m_Socket, FIONBIO, &nonBlocking);

            // Buffer optimizations
            int bufSize = 512 * 1024;
            setsockopt(m_Socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
            setsockopt(m_Socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

            sockaddr_in serverAddr = {};
            serverAddr.sin_family      = AF_INET;
            serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
            serverAddr.sin_port        = htons(port);

            if (bind(m_Socket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
            {
                closesocket(m_Socket);
                m_Socket = INVALID_SOCKET;
                return false;
            }

            m_Role = ENetworkRole::Server;
            m_ServerPort = port;
            return true;
        }

        //---------------------------------------------------------------------
        // Client Mode Setup
        //---------------------------------------------------------------------
        bool StartClient(const char* serverIp = "127.0.0.1", uint16_t serverPort = DEFAULT_SERVER_PORT)
        {
            Shutdown();

            if (!m_WinsockScope.IsValid())
                return false;

            m_Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (m_Socket == INVALID_SOCKET)
                return false;

            // Non-blocking mode
            u_long nonBlocking = 1;
            ioctlsocket(m_Socket, FIONBIO, &nonBlocking);

            int bufSize = 512 * 1024;
            setsockopt(m_Socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
            setsockopt(m_Socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

            // Bind to OS-allocated ephemeral port
            sockaddr_in clientAddr = {};
            clientAddr.sin_family      = AF_INET;
            clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
            clientAddr.sin_port        = htons(0);

            if (bind(m_Socket, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr)) == SOCKET_ERROR)
            {
                closesocket(m_Socket);
                m_Socket = INVALID_SOCKET;
                return false;
            }

            // Configure Server destination address
            m_ServerAddr = {};
            m_ServerAddr.sin_family = AF_INET;
            m_ServerAddr.sin_port   = htons(serverPort);
            inet_pton(AF_INET, serverIp, &m_ServerAddr.sin_addr);

            m_Role       = ENetworkRole::Client;
            m_ServerIp   = serverIp;
            m_ServerPort = serverPort;

            // Send initial Handshake Request
            SendHandshakeRequest();
            return true;
        }

        //---------------------------------------------------------------------
        // Server Update: Handle incoming handshakes, heartbeats, disconnects & Broadcast snapshots
        //---------------------------------------------------------------------
        void UpdateServer(uint32_t currentTick, const std::vector<FSphere>& spheres, float boxHalfSize, float dt = 0.016f)
        {
            if (m_Role != ENetworkRole::Server || m_Socket == INVALID_SOCKET)
                return;

            // 1. Process all incoming UDP packets (non-blocking)
            uint8_t recvBuffer[2048];
            sockaddr_in senderAddr = {};
            int senderLen = sizeof(senderAddr);

            while (true)
            {
                int bytesRead = recvfrom(
                    m_Socket,
                    reinterpret_cast<char*>(recvBuffer),
                    sizeof(recvBuffer),
                    0,
                    reinterpret_cast<sockaddr*>(&senderAddr),
                    &senderLen
                );

                if (bytesRead <= 0)
                {
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK)
                        break; // No more packets in buffer
                    if (err == WSAECONNRESET)
                        continue; // Ignore ICMP Port Unreachable from dead clients
                    break;
                }

                m_PacketsReceived++;

                if (bytesRead >= sizeof(FPacketHeader))
                {
                    const auto* header = reinterpret_cast<const FPacketHeader*>(recvBuffer);
                    if (header->Magic != PROTOCOL_MAGIC)
                        continue;

                    if (header->Type == EPacketType::HandshakeRequest)
                    {
                        if (bytesRead < static_cast<int>(sizeof(FHandshakeRequestPacket)))
                            continue;

                        RegisterOrRefreshClient(senderAddr);
                        SendHandshakeResponse(senderAddr, static_cast<uint16_t>(spheres.size()), boxHalfSize);
                    }
                    else if (header->Type == EPacketType::Heartbeat)
                    {
                        if (bytesRead < static_cast<int>(sizeof(FPacketHeader)))
                            continue;

                        RegisterOrRefreshClient(senderAddr);
                    }
                    else if (header->Type == EPacketType::Disconnect)
                    {
                        if (bytesRead < static_cast<int>(sizeof(FPacketHeader)))
                            continue;

                        RemoveClient(senderAddr);
                    }
                }
            }

            // 2. Client heartbeat timeout check (Zombie client cleanup: 5.0 seconds threshold)
            for (auto& client : m_Clients)
            {
                client.TimeSinceLastSeen += dt;
            }

            m_Clients.erase(
                std::remove_if(m_Clients.begin(), m_Clients.end(), [](const FConnectedClient& client) {
                    return client.TimeSinceLastSeen > 5.0f;
                }),
                m_Clients.end()
            );

            // 3. Broadcast current world snapshot to all registered clients
            if (m_Clients.empty() || spheres.empty())
                return;

            const int totalSpheres = static_cast<int>(spheres.size());
            const uint8_t totalChunks = static_cast<uint8_t>((totalSpheres + MAX_SPHERES_PER_CHUNK - 1) / MAX_SPHERES_PER_CHUNK);

            for (uint8_t c = 0; c < totalChunks; ++c)
            {
                FSnapshotChunkPacket chunkPacket = {};
                chunkPacket.Header.Magic = PROTOCOL_MAGIC;
                chunkPacket.Header.Type  = EPacketType::SnapshotChunk;
                chunkPacket.ChunkIndex   = c;
                chunkPacket.TotalChunks  = totalChunks;
                chunkPacket.ServerTick   = currentTick;
                chunkPacket.TotalSpheres = static_cast<uint16_t>(totalSpheres);

                int startIdx = c * MAX_SPHERES_PER_CHUNK;
                int count    = (std::min)(MAX_SPHERES_PER_CHUNK, totalSpheres - startIdx);
                chunkPacket.CountInPacket = static_cast<uint16_t>(count);

                for (int i = 0; i < count; ++i)
                {
                    const FSphere& src = spheres[startIdx + i];
                    chunkPacket.Spheres[i].Id       = (src.Id >= 0) ? src.Id : (startIdx + i);
                    chunkPacket.Spheres[i].Position = src.Center;
                    chunkPacket.Spheres[i].Velocity = src.Velocity;
                    chunkPacket.Spheres[i].Radius   = src.Radius;
                }

                int packetSize = sizeof(chunkPacket) - sizeof(chunkPacket.Spheres) + (count * sizeof(FSphereNetData));

                for (const auto& client : m_Clients)
                {
                    sendto(
                        m_Socket,
                        reinterpret_cast<const char*>(&chunkPacket),
                        packetSize,
                        0,
                        reinterpret_cast<const sockaddr*>(&client.Addr),
                        sizeof(client.Addr)
                    );
                    m_PacketsSent++;
                }
            }
        }

        //---------------------------------------------------------------------
        // Client Update: Receive server snapshots & update sphere states
        //---------------------------------------------------------------------
        void UpdateClient(std::vector<FSphere>& spheres, float boxHalfSize, float dt)
        {
            if (m_Role != ENetworkRole::Client || m_Socket == INVALID_SOCKET)
                return;

            // Keep-alive timer
            m_HandshakeTimer += dt;
            if (!m_bConnected && m_HandshakeTimer >= 0.5f)
            {
                SendHandshakeRequest();
                m_HandshakeTimer = 0.0f;
            }
            else if (m_bConnected && m_HandshakeTimer >= 2.0f)
            {
                SendHeartbeat();
                m_HandshakeTimer = 0.0f;
            }

            uint8_t recvBuffer[2048];
            sockaddr_in senderAddr = {};
            int senderLen = sizeof(senderAddr);

            while (true)
            {
                int bytesRead = recvfrom(
                    m_Socket,
                    reinterpret_cast<char*>(recvBuffer),
                    sizeof(recvBuffer),
                    0,
                    reinterpret_cast<sockaddr*>(&senderAddr),
                    &senderLen
                );

                if (bytesRead <= 0)
                {
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK)
                        break;
                    if (err == WSAECONNRESET)
                        continue;
                    break;
                }

                m_PacketsReceived++;

                if (bytesRead >= sizeof(FPacketHeader))
                {
                    const auto* header = reinterpret_cast<const FPacketHeader*>(recvBuffer);
                    if (header->Magic != PROTOCOL_MAGIC)
                        continue;

                    if (header->Type == EPacketType::HandshakeResponse)
                    {
                        if (bytesRead < static_cast<int>(sizeof(FHandshakeResponsePacket)))
                            continue;

                        const auto* resp = reinterpret_cast<const FHandshakeResponsePacket*>(recvBuffer);
                        m_bConnected = true;

                        // Validate sphere count sanity bounds
                        if (resp->SphereCount >= MIN_SPHERES && resp->SphereCount <= MAX_SPHERES && resp->SphereCount != spheres.size())
                        {
                            spheres = CreateSpheres(resp->SphereCount, boxHalfSize);
                        }
                    }
                    else if (header->Type == EPacketType::SnapshotChunk)
                    {
                        // 1. Validate minimum header size for snapshot chunk
                        const size_t minChunkHeaderSize = offsetof(FSnapshotChunkPacket, Spheres);
                        if (bytesRead < static_cast<int>(minChunkHeaderSize))
                            continue;

                        const auto* chunk = reinterpret_cast<const FSnapshotChunkPacket*>(recvBuffer);

                        // 2. Validate CountInPacket and ChunkIndex bounds
                        if (chunk->CountInPacket > MAX_SPHERES_PER_CHUNK || chunk->TotalChunks == 0 || chunk->ChunkIndex >= chunk->TotalChunks)
                            continue;

                        // 3. Validate that buffer actually contains all CountInPacket sphere elements
                        const size_t expectedPacketSize = minChunkHeaderSize + (chunk->CountInPacket * sizeof(FSphereNetData));
                        if (bytesRead < static_cast<int>(expectedPacketSize))
                            continue;

                        m_bConnected = true;
                        if (m_LastSnapshotTick > 0)
                        {
                            int32_t tickDiff = static_cast<int32_t>(chunk->ServerTick - m_LastSnapshotTick);
                            if (tickDiff < 0)
                            {
                                continue; // Past tick packet arrived late, drop it
                            }
                        }

                        m_LastSnapshotTick = (std::max)(m_LastSnapshotTick, chunk->ServerTick);

                        // Ensure sphere buffer is sized to match (with sanity bounds)
                        if (chunk->TotalSpheres >= MIN_SPHERES && chunk->TotalSpheres <= MAX_SPHERES && chunk->TotalSpheres != spheres.size())
                        {
                            spheres = CreateSpheres(chunk->TotalSpheres, boxHalfSize);
                        }

                        // Apply received sphere positions directly
                        for (uint16_t i = 0; i < chunk->CountInPacket; ++i)
                        {
                            const auto& netData = chunk->Spheres[i];
                            int idx = netData.Id;
                            if (idx >= 0 && idx < static_cast<int>(spheres.size()))
                            {
                                spheres[idx].Center   = netData.Position;
                                spheres[idx].Velocity = netData.Velocity;
                                if (netData.Radius > 0.0f)
                                {
                                    spheres[idx].Radius = netData.Radius;
                                }
                            }
                        }
                    }
                }
            }
        }

        //---------------------------------------------------------------------
        // Getters for HUD & App Info
        //---------------------------------------------------------------------
        ENetworkRole GetRole() const { return m_Role; }
        bool IsConnected()     const { return m_bConnected; }
        size_t GetClientCount() const { return m_Clients.size(); }
        uint32_t GetPacketsSent() const { return m_PacketsSent; }
        uint32_t GetPacketsReceived() const { return m_PacketsReceived; }
        uint32_t GetLastSnapshotTick() const { return m_LastSnapshotTick; }

        const wchar_t* GetRoleString() const
        {
            switch (m_Role)
            {
            case ENetworkRole::Server: return L"Server";
            case ENetworkRole::Client: return L"Client";
            default:                   return L"Standalone";
            }
        }

    private:
        void RegisterOrRefreshClient(const sockaddr_in& clientAddr)
        {
            for (auto& existing : m_Clients)
            {
                if (existing.Addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
                    existing.Addr.sin_port == clientAddr.sin_port)
                {
                    existing.TimeSinceLastSeen = 0.0f; // Heartbeat refreshed
                    return;
                }
            }
            FConnectedClient newClient;
            newClient.Addr = clientAddr;
            newClient.TimeSinceLastSeen = 0.0f;
            m_Clients.push_back(newClient);
        }

        void RemoveClient(const sockaddr_in& clientAddr)
        {
            m_Clients.erase(
                std::remove_if(m_Clients.begin(), m_Clients.end(), [&](const FConnectedClient& client) {
                    return client.Addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
                           client.Addr.sin_port == clientAddr.sin_port;
                }),
                m_Clients.end()
            );
        }

        void SendDisconnect()
        {
            if (m_Socket == INVALID_SOCKET) return;

            FPacketHeader packet = {};
            packet.Magic = PROTOCOL_MAGIC;
            packet.Type  = EPacketType::Disconnect;

            sendto(
                m_Socket,
                reinterpret_cast<const char*>(&packet),
                sizeof(packet),
                0,
                reinterpret_cast<const sockaddr*>(&m_ServerAddr),
                sizeof(m_ServerAddr)
            );
            m_PacketsSent++;
        }

        void SendHandshakeRequest()
        {
            if (m_Socket == INVALID_SOCKET) return;

            FHandshakeRequestPacket packet = {};
            packet.Header.Magic = PROTOCOL_MAGIC;
            packet.Header.Type  = EPacketType::HandshakeRequest;

            sendto(
                m_Socket,
                reinterpret_cast<const char*>(&packet),
                sizeof(packet),
                0,
                reinterpret_cast<const sockaddr*>(&m_ServerAddr),
                sizeof(m_ServerAddr)
            );
            m_PacketsSent++;
        }

        void SendHandshakeResponse(const sockaddr_in& target, uint16_t sphereCount, float boxHalfSize)
        {
            if (m_Socket == INVALID_SOCKET) return;

            FHandshakeResponsePacket packet = {};
            packet.Header.Magic = PROTOCOL_MAGIC;
            packet.Header.Type  = EPacketType::HandshakeResponse;
            packet.SphereCount  = sphereCount;
            packet.BoxHalfSize  = boxHalfSize;

            sendto(
                m_Socket,
                reinterpret_cast<const char*>(&packet),
                sizeof(packet),
                0,
                reinterpret_cast<const sockaddr*>(&target),
                sizeof(target)
            );
            m_PacketsSent++;
        }

        void SendHeartbeat()
        {
            if (m_Socket == INVALID_SOCKET) return;

            FPacketHeader packet = {};
            packet.Magic = PROTOCOL_MAGIC;
            packet.Type  = EPacketType::Heartbeat;

            sendto(
                m_Socket,
                reinterpret_cast<const char*>(&packet),
                sizeof(packet),
                0,
                reinterpret_cast<const sockaddr*>(&m_ServerAddr),
                sizeof(m_ServerAddr)
            );
            m_PacketsSent++;
        }

    private:
        FWinsockScope           m_WinsockScope;
        SOCKET                  m_Socket = INVALID_SOCKET;
        ENetworkRole            m_Role   = ENetworkRole::Standalone;

        uint16_t                m_ServerPort = DEFAULT_SERVER_PORT;
        std::string             m_ServerIp   = "127.0.0.1";

        // Server state
        std::vector<FConnectedClient> m_Clients;

        // Client state
        sockaddr_in             m_ServerAddr       = {};
        bool                    m_bConnected       = false;
        float                   m_HandshakeTimer   = 0.0f;
        uint32_t                m_LastSnapshotTick = 0;

        // Statistics
        uint32_t                m_PacketsSent      = 0;
        uint32_t                m_PacketsReceived  = 0;
    };
}
