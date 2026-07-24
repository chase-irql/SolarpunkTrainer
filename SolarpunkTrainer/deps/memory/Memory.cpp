#include "Memory.h"
#include <cstring>

namespace seh {

    bool IsBadPtr(const void* ptr, size_t size) {
        __try {
            volatile uint8_t scratch = *static_cast<const uint8_t*>(ptr);
            (void)scratch;
            if (size > 1) {
                scratch = *(static_cast<const uint8_t*>(ptr) + size - 1);
                (void)scratch;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return true;
        }
    }

}

bool Memory::ReadBytes(uintptr_t address, void* buffer, size_t size) {
    if (!buffer || !size) return false;
    __try {
        std::memcpy(buffer, reinterpret_cast<const void*>(address), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Memory::WriteBytes(uintptr_t address, const void* buffer, size_t size) {
    if (!buffer || !size) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), buffer, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Memory::Nop(uintptr_t address, size_t count) {
    if (!address || !count) return false;

    DWORD old = 0;
    VirtualProtect(reinterpret_cast<void*>(address), count,
        PAGE_EXECUTE_READWRITE, &old);
    bool ok = false;
    __try {
        std::memset(reinterpret_cast<void*>(address), 0x90, count);
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    VirtualProtect(reinterpret_cast<void*>(address), count, old, &old);
    return ok;
}

uintptr_t Memory::ResolveRip(uintptr_t addr, size_t instrLen) {
    const auto rel = TryRead<int32_t>(addr + instrLen - 4);
    if (!rel) return 0;
    return addr + static_cast<uintptr_t>(instrLen)
        + static_cast<uintptr_t>(static_cast<int64_t>(*rel));
}

uintptr_t Memory::GetModuleBase(const wchar_t* name) {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(name));
}

Memory::ModuleInfo Memory::GetModuleInfo(const wchar_t* name) {
    HMODULE hMod = GetModuleHandleW(name);
    if (!hMod) return {};

    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), hMod, &mi, sizeof(mi)))
        return {};

    return {
        reinterpret_cast<uintptr_t>(mi.lpBaseOfDll),
        static_cast<size_t>(mi.SizeOfImage)
    };
}

uintptr_t Memory::PatternScan(uintptr_t start, size_t regionSize,
    const char* pattern)
{
    const auto [bytes, mask] = ParsePattern(pattern);
    if (bytes.empty()) return 0;

    const size_t patLen = bytes.size();

    for (size_t i = 0; i + patLen <= regionSize; ++i) {
        bool match = true;
        for (size_t j = 0; j < patLen && match; ++j) {
            if (!mask[j]) continue; // wildcard
            uint8_t b{};
            if (!seh::SafeRead(reinterpret_cast<const uint8_t*>(start + i + j), b)
                || b != bytes[j])
            {
                match = false;
            }
        }
        if (match) return start + i;
    }
    return 0;
}

uintptr_t Memory::PatternScanModule(const wchar_t* moduleName,
    const char* pattern)
{
    const auto [base, size] = GetModuleInfo(moduleName);
    if (!base || !size) return 0;
    return PatternScan(base, size, pattern);
}

std::vector<uintptr_t> Memory::PatternScanAll(uintptr_t start, size_t regionSize,
    const char* pattern)
{
    const auto [bytes, mask] = ParsePattern(pattern);
    std::vector<uintptr_t> results;
    if (bytes.empty()) return results;

    const size_t patLen = bytes.size();

    for (size_t i = 0; i + patLen <= regionSize; ++i) {
        bool match = true;
        for (size_t j = 0; j < patLen && match; ++j) {
            if (!mask[j]) continue;
            uint8_t b{};
            if (!seh::SafeRead(reinterpret_cast<const uint8_t*>(start + i + j), b)
                || b != bytes[j])
            {
                match = false;
            }
        }
        if (match) results.push_back(start + i);
    }
    return results;
}

bool Memory::IsValidPtr(uintptr_t address, size_t readSize) {
    if (address < 0x10000 || address > 0x7FFFFFFFFFFF) return false;
    return !seh::IsBadPtr(reinterpret_cast<const void*>(address), readSize);
}

Memory::ParsedPattern Memory::ParsePattern(const char* pattern) {
    ParsedPattern out;
    const char* p = pattern;

    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;

        if (*p == '?') {
            out.bytes.push_back(0x00);
            out.mask.push_back(false); // wildcard
            ++p;
            if (*p == '?') ++p;
        }
        else {
            char hex[3] = { p[0], p[1], '\0' };
            out.bytes.push_back(static_cast<uint8_t>(std::strtol(hex, nullptr, 16)));
            out.mask.push_back(true);
            p += 2;
        }
    }
    return out;
}