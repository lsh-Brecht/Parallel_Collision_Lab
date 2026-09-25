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
    static const int      MAX_SPHERES_PER_CHUNK = 60; // 60 * 22B + 19B = 1339B (< 1472 MTU safe payload)

    enum class EPacketType : uint8_t
    {
        HandshakeRequest  = 1, // Client -> Server
        HandshakeResponse = 2, // Server -> Client (Sphere count, bounds, assigned sphere & planet)
        SnapshotChunk     = 3, // Server -> Client (Batch of sphere states)
        Heartbeat         = 4, // Keep-alive
        Disconnect        = 5, // Client -> Server
        ClientInput       = 6  // Client -> Server (Inputs for assigned sphere)
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

    //=============================================================================
    // Quantization & Bit Packing Helpers
    //=============================================================================
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
        uint32_t Id;        // 4 bytes: Sphere Index (supports >100,000 spheres)
        uint16_t PosX;      // 2 bytes: Quantized X in [-BoxHalfSize, +BoxHalfSize]
        uint16_t PosY;      // 2 bytes: Quantized Y in [-BoxHalfSize, +BoxHalfSize]
        uint16_t PosZ;      // 2 bytes: Quantized Z in [-BoxHalfSize, +BoxHalfSize]
        int16_t  VelX;      // 2 bytes: Quantized Velocity X in [-16.0, +16.0]
        int16_t  VelY;      // 2 bytes: Quantized Velocity Y in [-16.0, +16.0]
        int16_t  VelZ;      // 2 bytes: Quantized Velocity Z in [-16.0, +16.0]
        uint16_t Radius;    // 2 bytes: Quantized Radius in [0.0, 1.0]
        uint8_t  PlanetType;// 1 byte: EPlanetType (0: None, 1: Earth, 2: Mars, 3: UVMap)
        uint8_t  Flags;     // 1 byte: bit 0: bIsSleeping
        uint32_t ColorRGBA; // 4 bytes: 32-bit packed RGBA
    };                      // Total: exactly 24 bytes!

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
        int32_t       AssignedSphereId; // e.g. 0: Earth, 1: Mars, 2: UVMap, -1: Spectator
        uint8_t       AssignedPlanet;   // 1: Earth, 2: Mars, 3: UVMap, 0: None/Spectator
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
        float         InputX; // Left/Right (-1.0 ~ 1.0)
        float         InputY; // Up/Down (-1.0 ~ 1.0)
        float         InputZ; // Backward/Forward (-1.0 ~ 1.0)
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
