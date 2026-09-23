#include "NetworkClient.h"
#include <algorithm>
#include <cstddef>

namespace Network
{
    FNetworkClient::FNetworkClient()
        : m_Socket(INVALID_SOCKET)
    {
    }

    FNetworkClient::~FNetworkClient()
    {
        Disconnect();
    }

    bool FNetworkClient::Connect(const char* serverIp, uint16_t serverPort)
    {
        Disconnect();

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

        m_ServerIp         = serverIp;
        m_ServerPort       = serverPort;
        m_bConnected       = false;
        m_HandshakeTimer   = 0.0f;
        m_LastSnapshotTick = 0;
        m_PacketsSent      = 0;
        m_PacketsReceived  = 0;

        // Send initial Handshake Request
        SendHandshakeRequest();
        return true;
    }

    void FNetworkClient::Disconnect()
    {
        if (m_bConnected && m_Socket != INVALID_SOCKET)
        {
            SendDisconnect();
        }

        if (m_Socket != INVALID_SOCKET)
        {
            closesocket(m_Socket);
            m_Socket = INVALID_SOCKET;
        }

        m_bConnected       = false;
        m_HandshakeTimer   = 0.0f;
        m_LastSnapshotTick = 0;
        m_PacketsSent      = 0;
        m_PacketsReceived  = 0;
    }

    void FNetworkClient::Update(std::vector<FSphere>& spheres, float boxHalfSize, float dt)
    {
        if (m_Socket == INVALID_SOCKET)
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

                    // Apply received sphere positions and properties directly
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
                            if (netData.ColorRGBA != 0)
                            {
                                spheres[idx].Color = UnpackRGBA(netData.ColorRGBA);
                            }
                        }
                    }
                }
            }
        }
    }

    void FNetworkClient::SendHandshakeRequest()
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

    void FNetworkClient::SendHeartbeat()
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

    void FNetworkClient::SendDisconnect()
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
}
