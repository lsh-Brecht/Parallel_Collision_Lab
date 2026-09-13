#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <intrin.h>

//=============================================================================
// FCPUInfo - Hardware processor and cache architecture information
//=============================================================================
struct FCPUInfo
{
    std::wstring BrandName;
    int          PhysicalCores = 0;
    int          LogicalCores  = 0;
    size_t       L1CacheBytes  = 0;
    size_t       L2CacheBytes  = 0;
    size_t       L3CacheBytes  = 0;

    std::wstring GetSummaryString() const
    {
        wchar_t buf[128];
        swprintf_s(buf, L"%s (%dC / %dT)", BrandName.c_str(), PhysicalCores, LogicalCores);
        return buf;
    }

    std::wstring GetCacheString() const
    {
        wchar_t buf[128];
        size_t L2CacheMB = L2CacheBytes / (1024 * 1024);
        size_t L3CacheMB = L3CacheBytes / (1024 * 1024);

        if (L2CacheMB > 0 && L3CacheMB > 0)
        {
            swprintf_s(buf, L"L2: %zu MB | L3: %zu MB", L2CacheMB, L3CacheMB);
        }
        else if (L3CacheMB > 0)
        {
            swprintf_s(buf, L"L2: %zu KB | L3: %zu MB", L2CacheBytes / 1024, L3CacheMB);
        }
        else
        {
            swprintf_s(buf, L"L2: %zu KB | L3: %zu KB", L2CacheBytes / 1024, L3CacheBytes / 1024);
        }
        return buf;
    }
};

inline FCPUInfo QueryCPUInfo()
{
    FCPUInfo info;

    // 1. Query CPU brand string via CPUID (0x80000002 ~ 0x80000004)
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 0x80000000);
    unsigned int nExIds = static_cast<unsigned int>(cpuInfo[0]);

    char brand[49] = {};
    if (nExIds >= 0x80000004)
    {
        __cpuid(reinterpret_cast<int*>(brand),      0x80000002);
        __cpuid(reinterpret_cast<int*>(brand + 16), 0x80000003);
        __cpuid(reinterpret_cast<int*>(brand + 32), 0x80000004);
        brand[48] = '\0';

        // Trim leading and trailing whitespace
        char* start = brand;
        while (*start == ' ') start++;

        char* end = start + strlen(start) - 1;
        while (end > start && *end == ' ')
        {
            *end = '\0';
            end--;
        }

        int wideLen = MultiByteToWideChar(CP_ACP, 0, start, -1, nullptr, 0);
        if (wideLen > 0)
        {
            info.BrandName.resize(wideLen - 1);
            MultiByteToWideChar(CP_ACP, 0, start, -1, &info.BrandName[0], wideLen);
        }
    }

    if (info.BrandName.empty())
    {
        info.BrandName = L"Generic x86_64 CPU";
    }

    // 2. Query physical cores, logical threads, and cache levels via Win32 API
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &len);
    if (len > 0)
    {
        std::vector<BYTE> buffer(len);
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pInfo =
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());

        if (GetLogicalProcessorInformationEx(RelationAll, pInfo, &len))
        {
            BYTE* ptr = buffer.data();
            while (ptr < buffer.data() + len)
            {
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX curr =
                    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);

                if (curr->Relationship == RelationProcessorCore)
                {
                    info.PhysicalCores++;
                    for (WORD g = 0; g < curr->Processor.GroupCount; ++g)
                    {
                        KAFFINITY mask = curr->Processor.GroupMask[g].Mask;
                        while (mask)
                        {
                            if (mask & 1) info.LogicalCores++;
                            mask >>= 1;
                        }
                    }
                }
                else if (curr->Relationship == RelationCache)
                {
                    if (curr->Cache.Level == 1 && (curr->Cache.Type == CacheData || curr->Cache.Type == CacheUnified))
                    {
                        info.L1CacheBytes += curr->Cache.CacheSize;
                    }
                    else if (curr->Cache.Level == 2)
                    {
                        info.L2CacheBytes += curr->Cache.CacheSize;
                    }
                    else if (curr->Cache.Level == 3)
                    {
                        info.L3CacheBytes += curr->Cache.CacheSize;
                    }
                }

                ptr += curr->Size;
            }
        }
    }

    // Fallback if GetLogicalProcessorInformationEx failed
    if (info.LogicalCores == 0)
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        info.LogicalCores = si.dwNumberOfProcessors;
    }
    if (info.PhysicalCores == 0)
    {
        info.PhysicalCores = info.LogicalCores;
    }

    return info;
}
