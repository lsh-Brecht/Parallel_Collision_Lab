#include "NetworkServer.h"
#include "../Core/SimulationWorld.h"
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

    void FNetworkServer::ProcessIncoming(FSimulationWorld& World, float DeltaTime)
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

                    RegisterOrRefreshClient(senderAddr, World);
                }
                else if (header->Type == EPacketType::Heartbeat)
                {
                    if (bytesRead < static_cast<int>(sizeof(FPacketHeader)))
                        continue;

                    for (auto& client : ConnectedClients)
                    {
                        if (client.Addr.sin_addr.s_addr == senderAddr.sin_addr.s_addr &&
                            client.Addr.sin_port == senderAddr.sin_port)
                        {
                            client.TimeSinceLastSeen = 0.0f;
                            break;
                        }
                    }
                }
                else if (header->Type == EPacketType::Disconnect)
                {
                    if (bytesRead < static_cast<int>(sizeof(FPacketHeader)))
                        continue;

                    RemoveClient(senderAddr, World);
                }
                else if (header->Type == EPacketType::ClientInput)
                {
                    if (bytesRead < static_cast<int>(sizeof(FClientInputPacket)))
                        continue;

                    const auto* inputPacket = reinterpret_cast<const FClientInputPacket*>(recvBuffer);
                    for (auto& client : ConnectedClients)
                    {
                        if (client.Addr.sin_addr.s_addr == senderAddr.sin_addr.s_addr &&
                            client.Addr.sin_port == senderAddr.sin_port)
                        {
                            client.TimeSinceLastSeen = 0.0f;
                            if (client.AssignedSphereId >= 0 && client.AssignedSphereId == inputPacket->AssignedSphereId)
                            {
                                FVector3 inputDir(inputPacket->InputX, inputPacket->InputY, inputPacket->InputZ);
                                World.ApplySphereAcceleration(client.AssignedSphereId, inputDir, DeltaTime);
                            }
                            break;
                        }
                    }
                }
            }
        }

        // 2. Client heartbeat timeout check (5.0s threshold)
        for (auto it = ConnectedClients.begin(); it != ConnectedClients.end(); )
        {
            it->TimeSinceLastSeen += DeltaTime;
            if (it->TimeSinceLastSeen > 5.0f)
            {
                if (it->AssignedSphereId >= 0)
                {
                    World.DemotePlanet(it->AssignedSphereId);
                }
                it = ConnectedClients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void FNetworkServer::BroadcastSnapshot(uint32_t CurrentTick, const std::vector<FSphere>& Spheres, float BoxHalfSize)
    {
        if (ServerSocket == INVALID_SOCKET || ConnectedClients.empty() || Spheres.empty())
            return;

        const int totalSpheres = static_cast<int>(Spheres.size());
        const uint16_t totalChunks = static_cast<uint16_t>((totalSpheres + MAX_SPHERES_PER_CHUNK - 1) / MAX_SPHERES_PER_CHUNK);

        for (uint16_t c = 0; c < totalChunks; ++c)
        {
            FSnapshotChunkPacket chunkPacket = {};
            chunkPacket.Header.Magic = PROTOCOL_MAGIC;
            chunkPacket.Header.Type  = EPacketType::SnapshotChunk;
            chunkPacket.ChunkIndex   = c;
            chunkPacket.TotalChunks  = totalChunks;
            chunkPacket.ServerTick   = CurrentTick;
            chunkPacket.TotalSpheres = static_cast<uint32_t>(totalSpheres);

            int startIdx = c * MAX_SPHERES_PER_CHUNK;
            int count    = (std::min)(MAX_SPHERES_PER_CHUNK, totalSpheres - startIdx);
            chunkPacket.CountInPacket = static_cast<uint16_t>(count);

            for (int i = 0; i < count; ++i)
            {
                const FSphere& src = Spheres[startIdx + i];
                chunkPacket.Spheres[i].Id         = static_cast<uint32_t>((src.Id >= 0) ? src.Id : (startIdx + i));
                chunkPacket.Spheres[i].PosX       = CompressCoord(src.Center.x, BoxHalfSize);
                chunkPacket.Spheres[i].PosY       = CompressCoord(src.Center.y, BoxHalfSize);
                chunkPacket.Spheres[i].PosZ       = CompressCoord(src.Center.z, BoxHalfSize);
                chunkPacket.Spheres[i].VelX       = CompressVelocity(src.Velocity.x);
                chunkPacket.Spheres[i].VelY       = CompressVelocity(src.Velocity.y);
                chunkPacket.Spheres[i].VelZ       = CompressVelocity(src.Velocity.z);
                chunkPacket.Spheres[i].Radius     = CompressRadius(src.Radius);
                chunkPacket.Spheres[i].PlanetType = static_cast<uint8_t>(src.PlanetType);
                chunkPacket.Spheres[i].Flags      = src.bIsSleeping ? 1 : 0;
                chunkPacket.Spheres[i].ColorRGBA  = PackRGBA(src.Color);
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

    void FNetworkServer::Update(uint32_t CurrentTick, FSimulationWorld& World, float DeltaTime)
    {
        ProcessIncoming(World, DeltaTime);
        BroadcastSnapshot(CurrentTick, World.GetSpheres(), World.GetBoxHalfSize());
    }

    void FNetworkServer::RegisterOrRefreshClient(const sockaddr_in& ClientAddr, FSimulationWorld& World)
    {
        for (auto& existing : ConnectedClients)
        {
            if (existing.Addr.sin_addr.s_addr == ClientAddr.sin_addr.s_addr &&
                existing.Addr.sin_port == ClientAddr.sin_port)
            {
                existing.TimeSinceLastSeen = 0.0f; // Heartbeat refreshed
                SendHandshakeResponse(existing.Addr, static_cast<uint32_t>(World.GetSphereCount()), World.GetBoxHalfSize(), existing.AssignedSphereId, existing.AssignedPlanet);
                return;
            }
        }

        // New client: check available planet slots
        bool bEarthTaken = false;
        bool bMarsTaken  = false;
        bool bUvTaken    = false;

        for (const auto& c : ConnectedClients)
        {
            if (c.AssignedPlanet == static_cast<uint8_t>(EPlanetType::Earth)) bEarthTaken = true;
            else if (c.AssignedPlanet == static_cast<uint8_t>(EPlanetType::Mars)) bMarsTaken = true;
            else if (c.AssignedPlanet == static_cast<uint8_t>(EPlanetType::UVMap)) bUvTaken = true;
        }

        int32_t assignedSphere = -1;
        uint8_t assignedPlanet = 0;

        if (!bEarthTaken && World.GetSphereCount() > 0)
        {
            assignedSphere = 0;
            assignedPlanet = static_cast<uint8_t>(EPlanetType::Earth);
        }
        else if (!bMarsTaken && World.GetSphereCount() > 1)
        {
            assignedSphere = 1;
            assignedPlanet = static_cast<uint8_t>(EPlanetType::Mars);
        }
        else if (!bUvTaken && World.GetSphereCount() > 2)
        {
            assignedSphere = 2;
            assignedPlanet = static_cast<uint8_t>(EPlanetType::UVMap);
        }
        else
        {
            assignedSphere = -1;
            assignedPlanet = static_cast<uint8_t>(EPlanetType::None);
        }

        if (assignedSphere >= 0)
        {
            World.PromoteToPlanet(assignedSphere, static_cast<EPlanetType>(assignedPlanet));
        }

        FConnectedClient newClient;
        newClient.Addr              = ClientAddr;
        newClient.TimeSinceLastSeen = 0.0f;
        newClient.AssignedSphereId  = assignedSphere;
        newClient.AssignedPlanet    = assignedPlanet;
        ConnectedClients.push_back(newClient);

        SendHandshakeResponse(newClient.Addr, static_cast<uint32_t>(World.GetSphereCount()), World.GetBoxHalfSize(), assignedSphere, assignedPlanet);
    }

    void FNetworkServer::RemoveClient(const sockaddr_in& ClientAddr, FSimulationWorld& World)
    {
        for (auto it = ConnectedClients.begin(); it != ConnectedClients.end(); )
        {
            if (it->Addr.sin_addr.s_addr == ClientAddr.sin_addr.s_addr &&
                it->Addr.sin_port == ClientAddr.sin_port)
            {
                if (it->AssignedSphereId >= 0)
                {
                    World.DemotePlanet(it->AssignedSphereId);
                }
                it = ConnectedClients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void FNetworkServer::SendHandshakeResponse(const sockaddr_in& Target, uint32_t SphereCount, float BoxHalfSize, int32_t AssignedSphereId, uint8_t AssignedPlanet)
    {
        if (ServerSocket == INVALID_SOCKET) return;

        FHandshakeResponsePacket packet = {};
        packet.Header.Magic     = PROTOCOL_MAGIC;
        packet.Header.Type      = EPacketType::HandshakeResponse;
        packet.SphereCount      = SphereCount;
        packet.BoxHalfSize      = BoxHalfSize;
        packet.AssignedSphereId = AssignedSphereId;
        packet.AssignedPlanet   = AssignedPlanet;

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

    bool FNetworkServer::IsPlanetActive(EPlanetType type) const
    {
        uint8_t target = static_cast<uint8_t>(type);
        for (const auto& c : ConnectedClients)
        {
            if (c.AssignedPlanet == target) return true;
        }
        return false;
    }

    int32_t FNetworkServer::GetPlanetSphereId(EPlanetType type) const
    {
        uint8_t target = static_cast<uint8_t>(type);
        for (const auto& c : ConnectedClients)
        {
            if (c.AssignedPlanet == target) return c.AssignedSphereId;
        }
        return -1;
    }
}
