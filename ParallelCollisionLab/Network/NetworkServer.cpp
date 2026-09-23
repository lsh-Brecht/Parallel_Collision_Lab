#include "NetworkServer.h"
#include <algorithm>

namespace Network
{
    FNetworkServer::FNetworkServer()
        : m_Socket(INVALID_SOCKET)
    {
    }

    FNetworkServer::~FNetworkServer()
    {
        Stop();
    }

    bool FNetworkServer::Start(uint16_t port)
    {
        Stop();

        m_Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_Socket == INVALID_SOCKET)
            return false;

        // Non-blocking mode
        u_long nonBlocking = 1;
        ioctlsocket(m_Socket, FIONBIO, &nonBlocking);

        // Socket buffer optimizations
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

        m_Port = port;
        m_PacketsSent = 0;
        m_PacketsReceived = 0;
        m_Clients.clear();
        return true;
    }

    void FNetworkServer::Stop()
    {
        if (m_Socket != INVALID_SOCKET)
        {
            closesocket(m_Socket);
            m_Socket = INVALID_SOCKET;
        }
        m_Clients.clear();
        m_PacketsSent = 0;
        m_PacketsReceived = 0;
    }

    void FNetworkServer::Update(uint32_t currentTick, const std::vector<FSphere>& spheres, float boxHalfSize, float dt)
    {
        if (m_Socket == INVALID_SOCKET)
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
                    break;
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
                chunkPacket.Spheres[i].Id        = (src.Id >= 0) ? src.Id : (startIdx + i);
                chunkPacket.Spheres[i].Position  = src.Center;
                chunkPacket.Spheres[i].Velocity  = src.Velocity;
                chunkPacket.Spheres[i].Radius    = src.Radius;
                chunkPacket.Spheres[i].ColorRGBA = PackRGBA(src.Color);
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

    void FNetworkServer::RegisterOrRefreshClient(const sockaddr_in& clientAddr)
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

    void FNetworkServer::RemoveClient(const sockaddr_in& clientAddr)
    {
        m_Clients.erase(
            std::remove_if(m_Clients.begin(), m_Clients.end(), [&](const FConnectedClient& client) {
                return client.Addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
                       client.Addr.sin_port == clientAddr.sin_port;
            }),
            m_Clients.end()
        );
    }

    void FNetworkServer::SendHandshakeResponse(const sockaddr_in& target, uint16_t sphereCount, float boxHalfSize)
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
}
