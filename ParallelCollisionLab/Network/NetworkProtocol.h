#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstdint>
#include <algorithm>
#include "../Core/Common.h"

namespace Network
{
    static const uint32_t PROTOCOL_MAGIC        = 0x50434C31; // "PCL1"
    static const uint16_t DEFAULT_SERVER_PORT   = 32768;
    static const uint16_t DEFAULT_CLIENT_PORT   = 32769;
    static const int      MAX_SPHERES_PER_CHUNK = 36; // 36 * 36B + 16B = ~1.3KB per UDP packet (< 1472 MTU safe payload)

    enum class EPacketType : uint8_t
    {
        HandshakeRequest  = 1, // Client -> Server
        HandshakeResponse = 2, // Server -> Client (Sphere count, bounds)
        SnapshotChunk     = 3, // Server -> Client (Batch of sphere states)
        Heartbeat         = 4, // Keep-alive
        Disconnect        = 5  // Client -> Server
    };

    // 32-bit RGBA packing/unpacking helpers
    inline uint32_t PackRGBA(const FVector4& c)
    {
        uint8_t r = static_cast<uint8_t>((std::max)(0.0f, (std::min)(1.0f, c.x)) * 255.0f);
        uint8_t g = static_cast<uint8_t>((std::max)(0.0f, (std::min)(1.0f, c.y)) * 255.0f);
        uint8_t b = static_cast<uint8_t>((std::max)(0.0f, (std::min)(1.0f, c.z)) * 255.0f);
        uint8_t a = static_cast<uint8_t>((std::max)(0.0f, (std::min)(1.0f, c.w)) * 255.0f);
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    }

    inline FVector4 UnpackRGBA(uint32_t color)
    {
        float r = static_cast<float>(color & 0xFF) / 255.0f;
        float g = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
        float b = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
        float a = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
        return FVector4(r, g, b, (a > 0.0f) ? a : 1.0f);
    }

    #pragma pack(push, 1)

    struct FSphereNetData
    {
        int32_t  Id;        // 4 bytes
        FVector3 Position;  // 12 bytes
        FVector3 Velocity;  // 12 bytes
        float    Radius;    // 4 bytes
        uint32_t ColorRGBA; // 4 bytes (32-bit packed RGBA)
    };                      // Total: 36 bytes

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
            bIsValid = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
        }

        ~FWinsockScope()
        {
            if (bIsValid)
            {
                WSACleanup();
                bIsValid = false;
            }
        }

        bool IsValid() const { return bIsValid; }

    private:
        bool bIsValid = false;
    };
}
