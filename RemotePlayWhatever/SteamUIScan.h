#ifndef STEAMUISCAN_H
#define STEAMUISCAN_H

#include <cstddef>
#include <cstdint>
#include <cstring>

// IClientEngine's methods carry no names, and the slot of
// GetIClientControllerSerialized (Steam Input) moves between Steam builds
// (66 in Steam 1794536281; Open Steamworks' list, from older Steam, says 65).
// Steam's own UI fetches that interface at startup and logs "ClientAPI_Init
// (GlobalInstance): GetIClientControllerSerialized returned NULL." when it gets
// nothing, so the slot for this exact Steam can be read from the 64-bit
// SteamUI.dll that comes with it, where the code is:
//
//     mov rcx, [engine]          48 8B 0D rel32
//     mov edx, [pipe]            8B 15 rel32      (the pipe is the only argument)
//     mov rax, [rcx]             48 8B 01
//     call [rax + slot * 8]      FF 90 disp32
//     ...                        (keep the result, test it, jump if not null)
//     lea r8, [message]          4C 8D 05 rel32
//
// The other interfaces fetched next to it also take the user (mov r8d, [user]
// where the mov rcx is here), so the exact sequence tells this one apart.
namespace steamui
{
    inline uint32_t Read32(const unsigned char* p)
    {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    // The slot, or -1 if the file isn't a 64-bit PE file with exactly one
    // such call (several different slots also give -1).
    inline int ControllerSerializedSlot(const unsigned char* file, size_t size)
    {
        static const char kMessage[] = "GetIClientControllerSerialized returned NULL";
        if (size < 0x200 || file[0] != 'M' || file[1] != 'Z')
            return -1;
        const uint32_t pe = Read32(file + 0x3C);
        if (pe > size - 24 || std::memcmp(file + pe, "PE\0\0", 4) != 0 || (file[pe + 4] | (file[pe + 5] << 8)) != 0x8664)
            return -1;
        const unsigned sections = file[pe + 6] | (file[pe + 7] << 8);
        const size_t table = pe + 24 + (file[pe + 20] | (file[pe + 21] << 8));
        if (sections == 0 || table + (size_t)sections * 40 > size)
            return -1;

        struct Section { uint32_t rva; uint32_t raw; uint32_t rawSize; bool code; };
        Section secs[96];
        unsigned count = 0;
        for (unsigned i = 0; i < sections && count < 96; ++i)
        {
            const unsigned char* s = file + table + (size_t)i * 40;
            Section sec;
            sec.rva = Read32(s + 12);
            sec.rawSize = Read32(s + 16);
            sec.raw = Read32(s + 20);
            sec.code = (Read32(s + 36) & 0x20000020u) != 0; // code, or executable
            if (sec.raw > size || sec.rawSize > size - sec.raw)
                continue;
            secs[count++] = sec;
        }

        // The message's address once loaded (from the start of its string).
        const size_t msgLen = sizeof(kMessage) - 1;
        size_t found = (size_t)-1;
        for (size_t i = 0; i + msgLen <= size; ++i)
        {
            if (file[i] == 'G' && std::memcmp(file + i, kMessage, msgLen) == 0)
            {
                found = i;
                break;
            }
        }
        if (found == (size_t)-1)
            return -1;
        size_t start = found;
        while (start > 0 && found - start < 256 && file[start - 1] != 0)
            --start;
        int64_t msgRva = -1;
        for (unsigned i = 0; i < count; ++i)
        {
            if (start >= secs[i].raw && start < (size_t)secs[i].raw + secs[i].rawSize)
                msgRva = (int64_t)secs[i].rva + (int64_t)(start - secs[i].raw);
        }
        if (msgRva < 0)
            return -1;

        int slot = -1;
        for (unsigned n = 0; n < count; ++n)
        {
            if (!secs[n].code)
                continue;
            const unsigned char* code = file + secs[n].raw;
            const size_t len = secs[n].rawSize;
            for (size_t i = 16 + 6; i + 7 <= len; ++i)
            {
                // lea r64, [rip + rel32] that loads the message
                if ((code[i] != 0x48 && code[i] != 0x4C) || code[i + 1] != 0x8D || (code[i + 2] & 0xC7) != 0x05)
                    continue;
                const int32_t rel = (int32_t)Read32(code + i + 3);
                if ((int64_t)secs[n].rva + (int64_t)i + 7 + rel != msgRva)
                    continue;
                // The nearest such call [rax + disp32] before it, within 32 bytes.
                for (size_t back = 6; back <= 32 && back <= i - 16; ++back)
                {
                    const size_t c = i - back;
                    if (code[c] != 0xFF || code[c + 1] != 0x90 ||
                        code[c - 3] != 0x48 || code[c - 2] != 0x8B || code[c - 1] != 0x01 ||
                        code[c - 9] != 0x8B || code[c - 8] != 0x15 ||
                        code[c - 16] != 0x48 || code[c - 15] != 0x8B || code[c - 14] != 0x0D)
                        continue;
                    const int32_t disp = (int32_t)Read32(code + c + 2);
                    if (disp <= 0 || disp % 8 != 0 || disp / 8 > 1000)
                        return -1;
                    if (slot >= 0 && slot != disp / 8)
                        return -1;
                    slot = disp / 8;
                    break;
                }
            }
        }
        return slot;
    }
}

#endif // STEAMUISCAN_H
