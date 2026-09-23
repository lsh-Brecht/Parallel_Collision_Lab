#include "NetworkServer.h"
#include <algorithm>

namespace Network
{
    FNetworkServer::FNetworkServer()
        : ServerSocket(INVALID_SOCKET)
    {
    }

    FNetworkServer::~FNetworkServer()
    {
        Stop();
    }

    bool FNetworkServer::Start(uint16_t InPort)
    {
        Stop();

        ServerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (ServerSocket == INVALID_SOCKET)
            return false;

        // Non-blocking mode
        u_long nonBlocking = 1;
        ioctlsocket(ServerSocket, FIONBIO, &nonBlocking);

        // Socket buffer optimizations
        int bufSize = 512 * 1024;
        setsockopt(ServerSocket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
        setsockopt(ServerSocket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

        sockaddr_in serverAddr = {};
        serverAddr.sin_family      = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        serverAddr.sin_port        = htons(InPort);

        if (bind(ServerSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
        {
            closesocket(ServerSocket);
            ServerSocket = INVALID_SOCKET;
            return false;
        }

        ListenPort           = InPort;
        TotalPacketsSent     = 0;
        TotalPacketsReceived = 0;
        ConnectedClients.clear();
        return true;
    }

    void FNetworkServer::Stop()
    {
        if (ServerSocket != INVALID_SOCKET)
        {
            closesocket(ServerSocket);
            ServerSocket = INVALID_SOCKET;
        }
        ConnectedClients.clear();
        TotalPacketsSent     = 0;
        TotalPacketsReceived = 0;
    }

    void FNetworkServer::Update(uint32_t CurrentTick, const std::vector<FSphere>& Spheres, float BoxHalfSize, float DeltaTime)
    {
        if (ServerSocket == INVALID_SOCKET)
            return;

        // 1. Process all incoming UDP packets (non-blocking)
        uint8_t recvBuffer[2048];
        sockaddr_in senderAddr = {};
        int senderLen = sizeof(senderAddr);

        while (true)
        {
            int bytesRead = recvfrom(
                ServerSocket,
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

            TotalPacketsReceived++;

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
                    SendHandshakeResponse(senderAddr, static_cast<uint16_t>(Spheres.size()), BoxHalfSize);
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
        for (auto& client : ConnectedClients)
        {
            client.TimeSinceLastSeen += DeltaTime;
        }

        ConnectedClients.erase(
            std::remove_if(ConnectedClients.begin(), ConnectedClients.end(), [](const FConnectedClient& client) {
                return client.TimeSinceLastSeen > 5.0f;
            }),
            ConnectedClients.end()
        );

        // 3. Broadcast current world snapshot to all registered clients
        if (ConnectedClients.empty() || Spheres.empty())
            return;

        const int totalSpheres = static_cast<int>(Spheres.size());
        const uint8_t totalChunks = static_cast<uint8_t>((totalSpheres + MAX_SPHERES_PER_CHUNK - 1) / MAX_SPHERES_PER_CHUNK);

        for (uint8_t c = 0; c < totalChunks; ++c)
        {
            FSnapshotChunkPacket chunkPacket = {};
            chunkPacket.Header.Magic = PROTOCOL_MAGIC;
            chunkPacket.Header.Type  = EPacketType::SnapshotChunk;
            chunkPacket.ChunkIndex   = c;
            chunkPacket.TotalChunks  = totalChunks;
            chunkPacket.ServerTick   = CurrentTick;
            chunkPacket.TotalSpheres = static_cast<uint16_t>(totalSpheres);

            int startIdx = c * MAX_SPHERES_PER_CHUNK;
            int count    = (std::min)(MAX_SPHERES_PER_CHUNK, totalSpheres - startIdx);
            chunkPacket.CountInPacket = static_cast<uint16_t>(count);

            for (int i = 0; i < count; ++i)
            {
                const FSphere& src = Spheres[startIdx + i];
                chunkPacket.Spheres[i].Id        = (src.Id >= 0) ? src.Id : (startIdx + i);
                chunkPacket.Spheres[i].Position  = src.Center;
                chunkPacket.Spheres[i].Velocity  = src.Velocity;
                chunkPacket.Spheres[i].Radius    = src.Radius;
                chunkPacket.Spheres[i].ColorRGBA = PackRGBA(src.Color);
            }

            int packetSize = sizeof(chunkPacket) - sizeof(chunkPacket.Spheres) + (count * sizeof(FSphereNetData));

            for (const auto& client : ConnectedClients)
            {
                sendto(
                    ServerSocket,
                    reinterpret_cast<const char*>(&chunkPacket),
                    packetSize,
                    0,
                    reinterpret_cast<const sockaddr*>(&client.Addr),
                    sizeof(client.Addr)
                );
                TotalPacketsSent++;
            }
        }
    }

    void FNetworkServer::RegisterOrRefreshClient(const sockaddr_in& ClientAddr)
    {
        for (auto& existing : ConnectedClients)
        {
            if (existing.Addr.sin_addr.s_addr == ClientAddr.sin_addr.s_addr &&
                existing.Addr.sin_port == ClientAddr.sin_port)
            {
                existing.TimeSinceLastSeen = 0.0f; // Heartbeat refreshed
                return;
            }
        }
        FConnectedClient newClient;
        newClient.Addr = ClientAddr;
        newClient.TimeSinceLastSeen = 0.0f;
        ConnectedClients.push_back(newClient);
    }

    void FNetworkServer::RemoveClient(const sockaddr_in& ClientAddr)
    {
        ConnectedClients.erase(
            std::remove_if(ConnectedClients.begin(), ConnectedClients.end(), [&](const FConnectedClient& client) {
                return client.Addr.sin_addr.s_addr == ClientAddr.sin_addr.s_addr &&
                       client.Addr.sin_port == ClientAddr.sin_port;
            }),
            ConnectedClients.end()
        );
    }

    void FNetworkServer::SendHandshakeResponse(const sockaddr_in& Target, uint16_t SphereCount, float BoxHalfSize)
    {
        if (ServerSocket == INVALID_SOCKET) return;

        FHandshakeResponsePacket packet = {};
        packet.Header.Magic = PROTOCOL_MAGIC;
        packet.Header.Type  = EPacketType::HandshakeResponse;
        packet.SphereCount  = SphereCount;
        packet.BoxHalfSize  = BoxHalfSize;

        sendto(
            ServerSocket,
            reinterpret_cast<const char*>(&packet),
            sizeof(packet),
            0,
            reinterpret_cast<const sockaddr*>(&Target),
            sizeof(Target)
        );
        TotalPacketsSent++;
    }
}
