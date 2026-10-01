#include "NetworkClient.h"
#include "../Core/AppConfig.h"
#include <algorithm>
#include <cstddef>

namespace Network
{
    FNetworkClient::FNetworkClient()
        : ClientSocket(INVALID_SOCKET)
    {
    }

    FNetworkClient::~FNetworkClient()
    {
        Disconnect();
    }

    bool FNetworkClient::Connect(const char* InServerIp, uint16_t InServerPort)
    {
        Disconnect();

        ClientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (ClientSocket == INVALID_SOCKET)
            return false;

        // Non-blocking mode
        u_long nonBlocking = 1;
        ioctlsocket(ClientSocket, FIONBIO, &nonBlocking);

        int bufSize = 512 * 1024;
        setsockopt(ClientSocket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
        setsockopt(ClientSocket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

        // Bind to OS-allocated ephemeral port
        sockaddr_in clientAddr = {};
        clientAddr.sin_family      = AF_INET;
        clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        clientAddr.sin_port        = htons(0);

        if (bind(ClientSocket, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr)) == SOCKET_ERROR)
        {
            closesocket(ClientSocket);
            ClientSocket = INVALID_SOCKET;
            return false;
        }

        // Configure Server destination address
        ServerEndpoint = {};
        ServerEndpoint.sin_family = AF_INET;
        ServerEndpoint.sin_port   = htons(InServerPort);
        inet_pton(AF_INET, InServerIp, &ServerEndpoint.sin_addr);
        bIsConnected             = false;
        KeepAliveTimer           = 0.0f;
        LastReceivedSnapshotTick = 0;
        TotalPacketsSent         = 0;
        TotalPacketsReceived     = 0;
        AssignedSphereId         = -1;
        AssignedPlanet           = 0;
        InputSeq                 = 0;

        // Send initial Handshake Request
        SendSimplePacket(EPacketType::HandshakeRequest);
        return true;
    }

    void FNetworkClient::Disconnect()
    {
        if (bIsConnected && ClientSocket != INVALID_SOCKET)
        {
            SendSimplePacket(EPacketType::Disconnect);
        }

        if (ClientSocket != INVALID_SOCKET)
        {
            closesocket(ClientSocket);
            ClientSocket = INVALID_SOCKET;
        }

        bIsConnected             = false;
        KeepAliveTimer           = 0.0f;
        LastReceivedSnapshotTick = 0;
        TotalPacketsSent         = 0;
        TotalPacketsReceived     = 0;
        AssignedSphereId         = -1;
        AssignedPlanet           = 0;
        InputSeq                 = 0;
    }

    void FNetworkClient::Update(std::vector<FSphere>& Spheres, float BoxHalfSize, float DeltaTime)
    {
        if (ClientSocket == INVALID_SOCKET)
            return;

        // Keep-alive timer
        KeepAliveTimer += DeltaTime;
        if (!bIsConnected && KeepAliveTimer >= 0.5f)
        {
            SendSimplePacket(EPacketType::HandshakeRequest);
            KeepAliveTimer = 0.0f;
        }
        else if (bIsConnected && KeepAliveTimer >= 2.0f)
        {
            SendSimplePacket(EPacketType::Heartbeat);
            KeepAliveTimer = 0.0f;
        }

        uint8_t recvBuffer[2048];
        sockaddr_in senderAddr = {};
        int senderLen = sizeof(senderAddr);

        while (true)
        {
            int bytesRead = recvfrom(
                ClientSocket,
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

            TotalPacketsReceived++;

            // If simulator is active, enqueue packet for latency/loss/jitter simulation
            if (Simulator && Simulator->IsEnabled())
            {
                Simulator->EnqueuePacket(recvBuffer, bytesRead, senderAddr);
            }
            else
            {
                ProcessPacket(recvBuffer, bytesRead, Spheres, BoxHalfSize);
            }
        }

        // Process packets released by simulator whose delay has elapsed
        if (Simulator && Simulator->IsEnabled())
        {
            std::vector<uint8_t> readyData;
            sockaddr_in readySender = {};
            while (Simulator->PopReadyPacket(readyData, readySender))
            {
                ProcessPacket(readyData.data(), static_cast<int>(readyData.size()), Spheres, BoxHalfSize);
            }
        }

        // Remote entity dead reckoning / extrapolation
        const int count = static_cast<int>(Spheres.size());
        for (int i = 0; i < count; ++i)
        {
            if (i == AssignedSphereId)
                continue;

            FSphere& s = Spheres[i];
            if (!s.bIsSleeping)
            {
                s.Center += s.Velocity * (DeltaTime * SPEED_FACTOR);
                s.BoxCollisionCheck(BoxHalfSize);
            }
        }
    }

    void FNetworkClient::ProcessPacket(const uint8_t* buffer, int bytesRead, std::vector<FSphere>& Spheres, float BoxHalfSize)
    {
        if (bytesRead < static_cast<int>(sizeof(FPacketHeader)))
            return;

        const auto* header = reinterpret_cast<const FPacketHeader*>(buffer);
        if (header->Magic != PROTOCOL_MAGIC)
            return;

        if (header->Type == EPacketType::HandshakeResponse)
        {
            if (bytesRead < static_cast<int>(sizeof(FHandshakeResponsePacket)))
                return;

            const auto* resp = reinterpret_cast<const FHandshakeResponsePacket*>(buffer);
            bIsConnected     = true;
            AssignedSphereId = resp->AssignedSphereId;
            AssignedPlanet   = resp->AssignedPlanet;

            // Validate sphere count sanity bounds
            if (resp->SphereCount >= MIN_SPHERES && resp->SphereCount <= MAX_SPHERES && resp->SphereCount != Spheres.size())
            {
                Spheres = CreateSpheres(resp->SphereCount, BoxHalfSize);
            }
        }
        else if (header->Type == EPacketType::SnapshotChunk)
        {
            // 1. Validate minimum header size for snapshot chunk
            const size_t minChunkHeaderSize = offsetof(FSnapshotChunkPacket, Spheres);
            if (bytesRead < static_cast<int>(minChunkHeaderSize))
                return;

            const auto* chunk = reinterpret_cast<const FSnapshotChunkPacket*>(buffer);

            // 2. Validate CountInPacket and ChunkIndex bounds
            if (chunk->CountInPacket > MAX_SPHERES_PER_CHUNK || chunk->TotalChunks == 0 || chunk->ChunkIndex >= chunk->TotalChunks)
                return;

            // 3. Validate that buffer actually contains all CountInPacket sphere elements
            const size_t expectedPacketSize = minChunkHeaderSize + (chunk->CountInPacket * sizeof(FSphereNetData));
            if (bytesRead < static_cast<int>(expectedPacketSize))
                return;

            bIsConnected = true;
            if (LastReceivedSnapshotTick > 0)
            {
                int32_t tickDiff = static_cast<int32_t>(chunk->ServerTick - LastReceivedSnapshotTick);
                if (tickDiff < 0)
                {
                    return; // Past tick packet arrived late, drop it
                }
            }

            LastReceivedSnapshotTick = (std::max)(LastReceivedSnapshotTick, chunk->ServerTick);

            // Ensure sphere buffer is sized to match (with sanity bounds)
            if (chunk->TotalSpheres >= MIN_SPHERES && chunk->TotalSpheres <= MAX_SPHERES && chunk->TotalSpheres != Spheres.size())
            {
                Spheres = CreateSpheres(chunk->TotalSpheres, BoxHalfSize);
            }

            // Apply received sphere positions and properties directly (Dequantization)
            for (uint16_t i = 0; i < chunk->CountInPacket; ++i)
            {
                const auto& netData = chunk->Spheres[i];
                uint32_t idx = netData.Id;
                if (idx < Spheres.size())
                {
                    FVector3 serverPos(
                        DecompressCoord(netData.PosX, BoxHalfSize),
                        DecompressCoord(netData.PosY, BoxHalfSize),
                        DecompressCoord(netData.PosZ, BoxHalfSize)
                    );
                    FVector3 serverVel(
                        DecompressVelocity(netData.VelX),
                        DecompressVelocity(netData.VelY),
                        DecompressVelocity(netData.VelZ)
                    );

                    if (bEnablePrediction && idx == static_cast<uint32_t>(AssignedSphereId))
                    {
                        float errDist = (Spheres[idx].Center - serverPos).Length();
                        if (errDist > 0.6f)
                        {
                            Spheres[idx].Center   = serverPos;
                            Spheres[idx].Velocity = serverVel;
                        }
                        else
                        {
                            Spheres[idx].Center   = FVector3::Lerp(Spheres[idx].Center, serverPos, 0.15f);
                            Spheres[idx].Velocity = FVector3::Lerp(Spheres[idx].Velocity, serverVel, 0.25f);
                        }
                    }
                    else
                    {
                        float errDist = (Spheres[idx].Center - serverPos).Length();
                        Spheres[idx].Center   = (errDist > 0.5f) ? serverPos : FVector3::Lerp(Spheres[idx].Center, serverPos, 0.4f);
                        Spheres[idx].Velocity = serverVel;
                    }
                    Spheres[idx].Radius      = DecompressRadius(netData.Radius);
                    Spheres[idx].PlanetType  = static_cast<EPlanetType>(netData.PlanetType);
                    Spheres[idx].bIsSleeping = (netData.Flags & 1) != 0;
                    if (netData.ColorRGBA != 0)
                    {
                        Spheres[idx].Color = UnpackRGBA(netData.ColorRGBA);
                    }
                }
            }
        }
    }

    void FNetworkClient::SendSimplePacket(EPacketType type)
    {
        if (ClientSocket == INVALID_SOCKET) return;

        FPacketHeader packet = {};
        packet.Magic = PROTOCOL_MAGIC;
        packet.Type  = type;

        sendto(
            ClientSocket,
            reinterpret_cast<const char*>(&packet),
            sizeof(packet),
            0,
            reinterpret_cast<const sockaddr*>(&ServerEndpoint),
            sizeof(ServerEndpoint)
        );
        TotalPacketsSent++;
    }


    void FNetworkClient::SendInput(float x, float y, float z)
    {
        if (ClientSocket == INVALID_SOCKET || !bIsConnected || AssignedSphereId < 0)
            return;

        if (x == 0.0f && y == 0.0f && z == 0.0f)
            return;

        FClientInputPacket packet = {};
        packet.Header.Magic     = PROTOCOL_MAGIC;
        packet.Header.Type      = EPacketType::ClientInput;
        packet.InputSeq         = ++InputSeq;
        packet.AssignedSphereId = AssignedSphereId;
        packet.InputX           = x;
        packet.InputY           = y;
        packet.InputZ           = z;

        sendto(
            ClientSocket,
            reinterpret_cast<const char*>(&packet),
            sizeof(packet),
            0,
            reinterpret_cast<const sockaddr*>(&ServerEndpoint),
            sizeof(ServerEndpoint)
        );
        TotalPacketsSent++;
    }

    void FNetworkClient::PredictMovement(std::vector<FSphere>& Spheres, const FVector3& InputDir, float BoxHalfSize, float DeltaTime)
    {
        if (!bEnablePrediction || AssignedSphereId < 0 || AssignedSphereId >= static_cast<int32_t>(Spheres.size()))
            return;

        FSphere& s = Spheres[AssignedSphereId];
        if (InputDir.LengthSq() > 0.001f)
        {
            s.Velocity += InputDir.Normalize() * (Config::EARTH_ACCELERATION * DeltaTime);
            s.WakeUp();
        }

        float speedSq = s.Velocity.LengthSq();
        if (speedSq > Config::EARTH_MAX_SPEED * Config::EARTH_MAX_SPEED)
        {
            s.Velocity = s.Velocity.Normalize() * Config::EARTH_MAX_SPEED;
        }

        s.Center += s.Velocity * (DeltaTime * SPEED_FACTOR);
        s.BoxCollisionCheck(BoxHalfSize);
    }
}
