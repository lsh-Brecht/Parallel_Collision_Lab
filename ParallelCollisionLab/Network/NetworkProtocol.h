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
    static const uint32_t PROTOCOL_MAGIC             = 0x50434C31; // "PCL1"
    static const uint16_t DEFAULT_SERVER_PORT        = 32768;
    static const int      MAX_SPHERES_PER_CHUNK      = 60;   // 60 * 24B + 19B = 1459B (<= 1472 MTU safe payload)
    static const int      MAX_CHUNKS_PER_TICK_BUDGET = 4;    // max 4 chunks (240 spheres) per tick
    static const float    DEFAULT_AOI_RADIUS         = 1.4f;
    static const float    SNAPSHOT_SEND_INTERVAL     = 1.0f / 30.0f; // 30 FPS server snapshot rate

    enum class EPacketType : uint8_t
    {
        HandshakeRequest  = 1,
        HandshakeResponse = 2,
        SnapshotChunk     = 3,
        Heartbeat         = 4,
        Disconnect        = 5,
        ClientInput       = 6
    };

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

    inline uint16_t CompressCoord(float val, float boxHalfSize)
    {
        float normalized = (val + boxHalfSize) / (2.0f * boxHalfSize);
        normalized = (std::max)(0.0f, (std::min)(1.0f, normalized));
        return static_cast<uint16_t>(normalized * 65535.0f);
    }

    inline float DecompressCoord(uint16_t q, float boxHalfSize)
    {
        float normalized = static_cast<float>(q) / 65535.0f;
        return -boxHalfSize + normalized * (2.0f * boxHalfSize);
    }

    inline int16_t CompressVelocity(float val, float maxVel = 16.0f)
    {
        float ratio = val / maxVel;
        ratio = (std::max)(-1.0f, (std::min)(1.0f, ratio));
        return static_cast<int16_t>(ratio * 32767.0f);
    }

    inline float DecompressVelocity(int16_t q, float maxVel = 16.0f)
    {
        return (static_cast<float>(q) / 32767.0f) * maxVel;
    }

    inline uint16_t CompressRadius(float radius, float maxRadius = 1.0f)
    {
        float ratio = radius / maxRadius;
        ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));
        return static_cast<uint16_t>(ratio * 65535.0f);
    }

    inline float DecompressRadius(uint16_t q, float maxRadius = 1.0f)
    {
        return (static_cast<float>(q) / 65535.0f) * maxRadius;
    }

    #pragma pack(push, 1)

    struct FSphereNetData
    {
        uint32_t Id;         // 4B: sphere index
        uint16_t PosX;       // 2B: quantized X in [-BoxHalfSize, +BoxHalfSize]
        uint16_t PosY;       // 2B: quantized Y
        uint16_t PosZ;       // 2B: quantized Z
        int16_t  VelX;       // 2B: quantized velocity X in [-16, +16]
        int16_t  VelY;       // 2B: quantized velocity Y
        int16_t  VelZ;       // 2B: quantized velocity Z
        uint16_t Radius;     // 2B: quantized radius in [0, 1]
        uint8_t  PlanetType; // 1B: EPlanetType
        uint8_t  Flags;      // 1B: bit0 = bIsSleeping
        uint32_t ColorRGBA;  // 4B: packed RGBA
    };                       // Total: 24 bytes

    static_assert(sizeof(FSphereNetData) == 24, "FSphereNetData must be exactly 24 bytes");

    struct FPacketHeader
    {
        uint32_t    Magic = PROTOCOL_MAGIC;
        EPacketType Type;
    };

    struct FHandshakeRequestPacket
    {
        FPacketHeader Header;
    };

    struct FHandshakeResponsePacket
    {
        FPacketHeader Header;
        uint32_t      SphereCount;
        float         BoxHalfSize;
        int32_t       AssignedSphereId;
        uint8_t       AssignedPlanet;
    };

    struct FSnapshotChunkPacket
    {
        FPacketHeader  Header;
        uint16_t       ChunkIndex;
        uint16_t       TotalChunks;
        uint32_t       ServerTick;
        uint32_t       TotalSpheres;
        uint16_t       CountInPacket;
        FSphereNetData Spheres[MAX_SPHERES_PER_CHUNK];
    };

    struct FClientInputPacket
    {
        FPacketHeader Header;
        uint32_t      InputSeq;
        int32_t       AssignedSphereId;
        float         InputX;
        float         InputY;
        float         InputZ;
    };

    #pragma pack(pop)

    inline bool MatchesAddress(const sockaddr_in& a, const sockaddr_in& b)
    {
        return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
    }

    // RAII Winsock lifecycle manager
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
