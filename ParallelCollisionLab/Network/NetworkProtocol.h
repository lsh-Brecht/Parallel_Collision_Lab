#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstdint>
#include "../Core/Common.h"

namespace Network
{
    static const uint32_t PROTOCOL_MAGIC        = 0x50434C31; // "PCL1"
    static const uint16_t DEFAULT_SERVER_PORT   = 32768;
    static const uint16_t DEFAULT_CLIENT_PORT   = 32769;
    static const int      MAX_SPHERES_PER_CHUNK = 40; // ~1.3KB per UDP packet (< 1472 MTU safe payload)

    enum class EPacketType : uint8_t
    {
        HandshakeRequest  = 1, // Client -> Server
        HandshakeResponse = 2, // Server -> Client (Sphere count, bounds)
        SnapshotChunk     = 3, // Server -> Client (Batch of sphere states)
        Heartbeat         = 4, // Keep-alive
        Disconnect        = 5  // Client -> Server
    };

    #pragma pack(push, 1)

    struct FSphereNetData
    {
        int32_t  Id;
        FVector3 Position;
        FVector3 Velocity;
        float    Radius;
    };

    struct FPacketHeader
    {
        uint32_t    Magic = PROTOCOL_MAGIC;
        EPacketType Type;
    };

    struct FHandshakeRequestPacket
    {
        FPacketHeader Header;
        uint16_t      ClientListenPort;
    };

    struct FHandshakeResponsePacket
    {
        FPacketHeader Header;
        uint16_t      SphereCount;
        float         BoxHalfSize;
    };

    struct FSnapshotChunkPacket
    {
        FPacketHeader  Header;
        uint8_t        ChunkIndex;
        uint8_t        TotalChunks;
        uint8_t        Reserved;
        uint32_t       ServerTick;
        uint16_t       TotalSpheres;
        uint16_t       CountInPacket;
        FSphereNetData Spheres[MAX_SPHERES_PER_CHUNK];
    };

    #pragma pack(pop)

    // RAII Winsock Subsystem Manager
    class FWinsockScope
    {
    public:
        FWinsockScope()
        {
            WSADATA wsaData;
            m_bValid = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
        }

        ~FWinsockScope()
        {
            if (m_bValid)
            {
                WSACleanup();
                m_bValid = false;
            }
        }

        bool IsValid() const { return m_bValid; }

    private:
        bool m_bValid = false;
    };
}
