#ifndef CLIENTVTABLEBYNAME_H
#define CLIENTVTABLEBYNAME_H

// ============================================================================
//  Resolve Steam client internal-interface methods BY NAME at runtime.
//
//  Why: interfaces like IClientEngine and IClientRemoteClientManager are
//  undocumented, and Valve inserts / removes / reorders their methods between
//  Steam client builds. Because a C++ virtual call is compiled to a fixed
//  vtable *index* taken from the declaration order in the OpenSteamworks
//  headers, a single drifted build makes every call after the inserted method
//  land on the wrong function -> wrong arguments -> crash. This is the usual
//  "worked last month, crashes today" report.
//
//  Fix: each Steam client method stub references its own bare name as an ASCII
//  string near the top of the function (used by Valve's telemetry / profiling
//  scopes). We disassemble just far enough to read the `lea reg, [rip+disp32]`
//  that loads that string, and build a name -> vtable-index map at runtime.
//  Calls then go through the resolved index, so they self-adapt to whatever
//  build is installed.
//
//  Cross-platform: the scan is pure x86-64 machine-code pattern matching and
//  the string is present in steamclient64.dll (Windows / PE), steamclient.so
//  (Linux / ELF, incl. Steam Deck) and steamclient.dylib (macOS / Mach-O), so
//  the same code path works on every target this project builds for. Only
//  x86-64 is handled; that is the only arch the Steam client ships as (on
//  Apple Silicon Macs Steam runs under Rosetta 2, and so does the Mac build).
//
//  Memory safety: a Windows module is one contiguous committed image, but an
//  ELF object is several PT_LOAD segments (text / rodata / data) with UNMAPPED
//  gaps between them. So we never scan against one big [base, base+size) span;
//  instead we collect the individual mapped segments and confine every read to
//  the segment that owns the address. Reading across a gap would SIGSEGV (and
//  on Linux that takes the whole Steam session down) — this is why an earlier
//  single-span version crashed on Linux while working on Windows.
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <vector>
#include <unordered_map>

#if defined(_WIN32)
    #include <windows.h>
    #include <share.h>
#elif defined(__APPLE__)
    #include <dlfcn.h>
    #include <mach-o/loader.h>
#else
    #include <dlfcn.h>
    #include <link.h>
#endif

namespace clientvt
{
    // Opt-in diagnostics: set env RPW_VTDEBUG=1 to log the resolver's view of
    // the module (segments, scan progress, resolved names/indices) to stderr.
    // On Windows it's also on when a file named rpw-debug.log already exists
    // next to the program, and the log then goes into that file too (a GUI
    // build has no console to show stderr), so a log can be collected however
    // the app is started. Off by default and effectively zero cost.
#if defined(_WIN32)
    // rpw-debug.log next to the program (empty if the path can't be had).
    inline std::wstring DebugLogPath()
    {
        wchar_t path[MAX_PATH];
        const DWORD n = ::GetModuleFileNameW(NULL, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return std::wstring();
        const std::wstring exe(path, n);
        const size_t slash = exe.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : exe.substr(0, slash + 1) + L"rpw-debug.log";
    }
#endif

    inline bool DebugEnabled()
    {
        static int on = -1;
        if (on < 0)
        {
            on = (std::getenv("RPW_VTDEBUG") != NULL) ? 1 : 0;
#if defined(_WIN32)
            if (!on)
            {
                const std::wstring log = DebugLogPath();
                on = (!log.empty() && ::GetFileAttributesW(log.c_str()) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
            }
#endif
        }
        return on != 0;
    }

    // Windows: rpw-debug.log, appended to on every run (and readable while
    // the app runs); null where there's no such file. It's never created here,
    // so RPW_VTDEBUG on its own doesn't leave logging on for later runs.
    inline std::FILE* LogFile()
    {
        static std::FILE* file = nullptr;
        static bool tried = false;
        if (!tried)
        {
            tried = true;
#if defined(_WIN32)
            const std::wstring log = DebugLogPath();
            if (!log.empty() && ::GetFileAttributesW(log.c_str()) != INVALID_FILE_ATTRIBUTES)
                file = ::_wfsopen(log.c_str(), L"a", _SH_DENYNO);
#endif
        }
        return file;
    }

    inline void Log(const char* fmt, ...)
    {
        if (!DebugEnabled())
            return;
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(stderr, fmt, ap);
        va_end(ap);
        std::fflush(stderr);
        if (std::FILE* file = LogFile())
        {
#if defined(_WIN32)
            SYSTEMTIME now;
            ::GetLocalTime(&now);
            std::fprintf(file, "[%02u:%02u:%02u.%03u] ", (unsigned)now.wHour, (unsigned)now.wMinute,
                         (unsigned)now.wSecond, (unsigned)now.wMilliseconds);
#endif
            va_start(ap, fmt);
            std::vfprintf(file, fmt, ap);
            va_end(ap);
            std::fflush(file);
        }
    }

    // A single mapped, readable address range within a module.
    struct Segment { uintptr_t lo; uintptr_t hi; };

    // Is [a, a+n) fully inside one mapped segment?  Reads must pass this so we
    // never touch an unmapped inter-segment gap.
    inline bool Readable(const std::vector<Segment>& segs, uintptr_t a, size_t n)
    {
        for (size_t i = 0; i < segs.size(); ++i)
            if (a >= segs[i].lo && a + n <= segs[i].hi)
                return true;
        return false;
    }

    // Upper bound of the segment that owns `a` (so a forward scan starting at a
    // known-good address can't walk past that segment into a gap). 0 if none.
    inline uintptr_t SegmentEndOf(const std::vector<Segment>& segs, uintptr_t a)
    {
        for (size_t i = 0; i < segs.size(); ++i)
            if (a >= segs[i].lo && a < segs[i].hi)
                return segs[i].hi;
        return 0;
    }

    // --- collect the mapped segments of the module that owns `addr` -----------
#if defined(_WIN32)
    inline bool ModuleSegments(const void* addr, std::vector<Segment>& segs)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
            return false;

        uintptr_t base = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
        if (!base)
            return false;

        const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        const IMAGE_NT_HEADERS* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;

        // A PE image is mapped as one contiguous committed region, so a single
        // [base, base+SizeOfImage) segment is both correct and safe to read.
        Segment s;
        s.lo = base;
        s.hi = base + nt->OptionalHeader.SizeOfImage;
        segs.push_back(s);
        Log("[vt] module base=%p size=0x%zx (1 contiguous PE image)\n",
            reinterpret_cast<void*>(base),
            static_cast<size_t>(nt->OptionalHeader.SizeOfImage));
        return true;
    }
#elif defined(__APPLE__)
    // A Mach-O image (steamclient.dylib) is also several segments (__TEXT,
    // __DATA_CONST, __DATA, __LINKEDIT...), all moved by the same slide from
    // the addresses in its load commands. The mach header sits at the start
    // of __TEXT, so slide = header - __TEXT.vmaddr.
    inline bool ModuleSegments(const void* addr, std::vector<Segment>& segs)
    {
        Dl_info di;
        std::memset(&di, 0, sizeof(di));
        if (!dladdr(addr, &di) || !di.dli_fbase)
            return false;

        const mach_header_64* header = static_cast<const mach_header_64*>(di.dli_fbase);
        if (header->magic != MH_MAGIC_64)
            return false;

        const load_command* first = reinterpret_cast<const load_command*>(header + 1);
        bool haveSlide = false;
        uintptr_t slide = 0;
        const load_command* lc = first;
        for (uint32_t i = 0; i < header->ncmds && !haveSlide; ++i)
        {
            if (lc->cmd == LC_SEGMENT_64)
            {
                const segment_command_64* seg = reinterpret_cast<const segment_command_64*>(lc);
                if (std::strncmp(seg->segname, "__TEXT", sizeof(seg->segname)) == 0)
                {
                    slide = reinterpret_cast<uintptr_t>(header) - static_cast<uintptr_t>(seg->vmaddr);
                    haveSlide = true;
                }
            }
            lc = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(lc) + lc->cmdsize);
        }
        if (!haveSlide)
            return false;

        lc = first;
        for (uint32_t i = 0; i < header->ncmds; ++i)
        {
            if (lc->cmd == LC_SEGMENT_64)
            {
                const segment_command_64* seg = reinterpret_cast<const segment_command_64*>(lc);
                // Skip empty and unreadable ones (an executable's __PAGEZERO).
                if (seg->vmsize > 0 && (seg->initprot & VM_PROT_READ) != 0)
                {
                    Segment s;
                    s.lo = static_cast<uintptr_t>(seg->vmaddr) + slide;
                    s.hi = s.lo + static_cast<uintptr_t>(seg->vmsize);
                    segs.push_back(s);
                }
            }
            lc = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(lc) + lc->cmdsize);
        }

        if (DebugEnabled())
        {
            Log("[vt] module %s base=%p : %zu segment(s)\n",
                di.dli_fname ? di.dli_fname : "?",
                di.dli_fbase, segs.size());
            for (size_t i = 0; i < segs.size(); ++i)
                Log("[vt]   seg[%zu] %p .. %p\n", i,
                    reinterpret_cast<void*>(segs[i].lo),
                    reinterpret_cast<void*>(segs[i].hi));
        }
        return !segs.empty();
    }
#else
    struct SegQuery { uintptr_t base; std::vector<Segment>* segs; };

    inline int SegCb(struct dl_phdr_info* info, size_t, void* data)
    {
        SegQuery* q = static_cast<SegQuery*>(data);
        if (static_cast<uintptr_t>(info->dlpi_addr) != q->base)
            return 0; // not the object we want; keep iterating

        for (int i = 0; i < info->dlpi_phnum; ++i)
        {
            const ElfW(Phdr)& ph = info->dlpi_phdr[i];
            if (ph.p_type != PT_LOAD)
                continue;
            Segment s;
            s.lo = q->base + ph.p_vaddr;
            s.hi = s.lo + ph.p_memsz; // p_memsz covers bss too (mapped anon)
            q->segs->push_back(s);
        }
        return 1; // found it; stop
    }

    inline bool ModuleSegments(const void* addr, std::vector<Segment>& segs)
    {
        Dl_info di;
        std::memset(&di, 0, sizeof(di));
        if (!dladdr(const_cast<void*>(addr), &di) || !di.dli_fbase)
            return false;

        SegQuery q;
        q.base = reinterpret_cast<uintptr_t>(di.dli_fbase);
        q.segs = &segs;
        dl_iterate_phdr(SegCb, &q);

        if (DebugEnabled())
        {
            Log("[vt] module %s base=%p : %zu PT_LOAD segment(s)\n",
                di.dli_fname ? di.dli_fname : "?",
                di.dli_fbase, segs.size());
            for (size_t i = 0; i < segs.size(); ++i)
                Log("[vt]   seg[%zu] %p .. %p\n", i,
                    reinterpret_cast<void*>(segs[i].lo),
                    reinterpret_cast<void*>(segs[i].hi));
        }
        return !segs.empty();
    }
#endif

    // --- read the bare method name a stub references via `lea reg,[rip+d32]` --
    inline std::string NameOf(uintptr_t fn, const std::vector<Segment>& segs)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(fn);

        // Confine the instruction scan to the code segment that owns `fn`, so we
        // can't walk off the end of .text into an unmapped gap.
        uintptr_t codeHi = SegmentEndOf(segs, fn);
        if (!codeHi)
            return std::string();

        // Scan window (bytes into the stub). The name-loading `lea` sits near the
        // top of the function, but on Linux (steamclient.so) the telemetry-scope
        // string is emitted deeper in the prologue than on Windows, so a tight cap
        // misses it for some methods and they resolve to -1. 300 covers both; the
        // window is only an upper bound — the per-segment guard below stops the
        // scan at the real end of .text, so a larger cap is free and safe.
        for (int k = 0; k < 300; ++k)
        {
            if (fn + k + 7 > codeHi) // instruction bytes must stay in this segment
                break;

            uint8_t rex = p[k];
            if (rex != 0x48 && rex != 0x4C && rex != 0x49 && rex != 0x4D)
                continue;                       // REX.W prefix
            if (p[k + 1] != 0x8D)
                continue;                       // LEA opcode
            if ((p[k + 2] & 0xC7) != 0x05)
                continue;                       // ModRM: mod=00, r/m=101 (RIP-relative)

            int32_t disp;
            std::memcpy(&disp, p + k + 3, sizeof(disp));
            uintptr_t target = fn + k + 7 + static_cast<int64_t>(disp);

            // The string may live in a different segment (rodata); cap the read
            // to whatever segment owns `target` so we never cross a gap.
            uintptr_t strHi = SegmentEndOf(segs, target);
            if (!strHi)
                continue;

            const char* s = reinterpret_cast<const char*>(target);
            std::string out;
            bool bad = false;
            for (int j = 0; j < 96 && target + j < strHi; ++j)
            {
                char c = s[j];
                if (c == 0)
                    break;
                if (c < 32 || c > 126) { bad = true; break; }
                out.push_back(c);
            }
            if (bad)
                continue;

            // A method name is a single C++ identifier. Requiring that (instead
            // of merely "no spaces") rejects the __FILE__ path and module-name
            // strings some stubs reference; we keep scanning past those until we
            // reach a real identifier.
            if (out.size() >= 6)
            {
                char c0 = out[0];
                bool ok = (c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_';
                int letters = 0;
                for (size_t i = 0; i < out.size() && ok; ++i)
                {
                    char c = out[i];
                    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                          || (c >= '0' && c <= '9') || c == '_'))
                        ok = false;
                    else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                        ++letters;
                }
                if (ok && letters >= 4)
                    return out;
            }
        }
        return std::string();
    }

    // --- build name -> vtable index for a COM-like interface pointer ----------
    inline std::unordered_map<std::string, int> ScanVTableNames(void* iface)
    {
        std::unordered_map<std::string, int> map;
        if (!iface)
            return map;

        void** vt = *reinterpret_cast<void***>(iface);

        std::vector<Segment> segs;
        if (!ModuleSegments(reinterpret_cast<void*>(vt[0]), segs))
        {
            Log("[vt] ModuleSegments failed for vt[0]=%p\n",
                reinterpret_cast<void*>(vt[0]));
            return map;
        }

        // The vtable array itself lives in a segment; don't read entries past
        // the end of that segment (another potential cross-gap read on Linux).
        uintptr_t vtHi = SegmentEndOf(segs, reinterpret_cast<uintptr_t>(vt));

        for (int i = 0; i < 400; ++i)
        {
            uintptr_t slot = reinterpret_cast<uintptr_t>(&vt[i]);
            if (vtHi && slot + sizeof(void*) > vtHi) // ran off the vtable's segment
                break;

            uintptr_t fn = reinterpret_cast<uintptr_t>(vt[i]);
            if (!SegmentEndOf(segs, fn)) // slot doesn't point into the module -> end of vtable
                break;

            std::string name = NameOf(fn, segs);
            if (!name.empty() && map.find(name) == map.end())
                map[name] = i;
        }
        Log("[vt] resolved %zu named method(s)\n", map.size());
        return map;
    }

    // --- call a method by fixed vtable slot, defensively --------------------
    //
    // For an interface whose methods carry no names (IClientEngine), the slot
    // from the OpenSteamworks declaration is all there is. If Valve reorders
    // it, that slot holds some other method, so: the slot must point at code
    // in the interface's own module, the call runs under a crash guard on
    // Windows, and the result must be an object whose vtable is in that module
    // too. Anything else is reported as a failure instead of crashing later.
#if defined(_MSC_VER)
    // SEH can't share a function with C++ objects that need unwinding, so
    // these two only touch raw pointers.
    inline bool ReadPointerSafely(const void* address, void** value)
    {
        __try
        {
            *value = *reinterpret_cast<void* const*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    typedef void* (*PipeGetterFn)(void* self, int32_t pipe);
    inline bool CallGetterSafely(PipeGetterFn fn, void* self, int32_t pipe, void** result)
    {
        __try
        {
            *result = fn(self, pipe);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    typedef void* (*UserPipeGetterFn)(void* self, int32_t user, int32_t pipe);
    inline bool CallUserGetterSafely(UserPipeGetterFn fn, void* self, int32_t user, int32_t pipe, void** result)
    {
        __try
        {
            *result = fn(self, user, pipe);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
#else
    // No crash guard here; the module checks below still apply.
    inline bool ReadPointerSafely(const void* address, void** value)
    {
        if (!address)
            return false;
        *value = *reinterpret_cast<void* const*>(address);
        return true;
    }

    typedef void* (*PipeGetterFn)(void* self, int32_t pipe);
    inline bool CallGetterSafely(PipeGetterFn fn, void* self, int32_t pipe, void** result)
    {
        *result = fn(self, pipe);
        return true;
    }

    typedef void* (*UserPipeGetterFn)(void* self, int32_t user, int32_t pipe);
    inline bool CallUserGetterSafely(UserPipeGetterFn fn, void* self, int32_t user, int32_t pipe, void** result)
    {
        *result = fn(self, user, pipe);
        return true;
    }
#endif

    // The function in iface's vtable at slot, if it points into iface's own
    // module (whose segments are put in segs); null otherwise.
    inline void* GetterAt(void* iface, int slot, std::vector<Segment>& segs)
    {
        void* vtable = NULL;
        void* first = NULL;
        if (!iface || slot < 0 || !ReadPointerSafely(iface, &vtable) || !vtable || !ReadPointerSafely(vtable, &first))
            return NULL;
        if (!ModuleSegments(first, segs))
            return NULL;
        const uintptr_t slotAddr = reinterpret_cast<uintptr_t>(vtable) + static_cast<uintptr_t>(slot) * sizeof(void*);
        if (!Readable(segs, slotAddr, sizeof(void*)))
        {
            Log("[vt] slot %d is past the end of the vtable's module\n", slot);
            return NULL;
        }
        void* fn = *reinterpret_cast<void**>(slotAddr);
        if (!SegmentEndOf(segs, reinterpret_cast<uintptr_t>(fn)))
        {
            Log("[vt] slot %d doesn't point into the module (%p)\n", slot, fn);
            return NULL;
        }
        return fn;
    }

    // Whether got is an object whose vtable (and its first entry) is in segs.
    inline bool IsModuleInterface(const std::vector<Segment>& segs, void* got, int slot)
    {
        void* gotVtable = NULL;
        void* gotFirst = NULL;
        if (!got || !ReadPointerSafely(got, &gotVtable) || !ReadPointerSafely(gotVtable, &gotFirst) ||
            !SegmentEndOf(segs, reinterpret_cast<uintptr_t>(gotVtable)) ||
            !SegmentEndOf(segs, reinterpret_cast<uintptr_t>(gotFirst)))
        {
            Log("[vt] slot %d returned %p, which isn't an interface from the module\n", slot, got);
            return false;
        }
        return true;
    }

    // iface->slot(pipe) for a method returning another interface pointer.
    inline bool CallPipeGetterAt(void* iface, int slot, int32_t pipe, void** result)
    {
        *result = NULL;
        std::vector<Segment> segs;
        void* fn = GetterAt(iface, slot, segs);
        if (!fn)
            return false;
        void* got = NULL;
        if (!CallGetterSafely(reinterpret_cast<PipeGetterFn>(fn), iface, pipe, &got))
        {
            Log("[vt] slot %d crashed when called\n", slot);
            return false;
        }
        if (!IsModuleInterface(segs, got, slot))
            return false;
        *result = got;
        return true;
    }

    // iface->slot(user, pipe), the same way (e.g. GetIClientShortcuts).
    inline bool CallUserPipeGetterAt(void* iface, int slot, int32_t user, int32_t pipe, void** result)
    {
        *result = NULL;
        std::vector<Segment> segs;
        void* fn = GetterAt(iface, slot, segs);
        if (!fn)
            return false;
        void* got = NULL;
        if (!CallUserGetterSafely(reinterpret_cast<UserPipeGetterFn>(fn), iface, user, pipe, &got))
        {
            Log("[vt] slot %d crashed when called\n", slot);
            return false;
        }
        if (!IsModuleInterface(segs, got, slot))
            return false;
        *result = got;
        return true;
    }

    // --- a bound interface you can call by method name ------------------------
    //
    // On x86-64 every calling convention (Windows x64 and System V) passes
    // `this` as the first integer argument, so invoking the resolved function
    // pointer as a free function with the interface pointer prepended is ABI-
    // identical to the original virtual call. Argument structs are forwarded by
    // value with their declared types, so their passing (registers vs memory)
    // matches too.
    class NamedVTable
    {
    public:
        NamedVTable() : m_iface(NULL) {}

        void Bind(void* iface)
        {
            m_iface = iface;
            m_names = iface ? ScanVTableNames(iface)
                            : std::unordered_map<std::string, int>();
        }

        bool Ok() const { return m_iface != NULL && !m_names.empty(); }
        bool Has(const char* name) const { return m_names.find(name) != m_names.end(); }
        size_t Count() const { return m_names.size(); }

        // Dump the full resolved map (only when RPW_VTDEBUG is set). Handy for a
        // tester to paste back so drift on their build can be diagnosed.
        void DebugDump() const
        {
            if (!DebugEnabled())
                return;
            Log("[vt] --- %zu resolved methods ---\n", m_names.size());
            for (std::unordered_map<std::string, int>::const_iterator it = m_names.begin();
                 it != m_names.end(); ++it)
                Log("[vt]   %-48s -> %d\n", it->first.c_str(), it->second);
        }

        // Resolved vtable index for a method, or -1 if its stub didn't embed a
        // name (a few don't). Such a method can still be reached by anchoring to
        // a named neighbour; see CancelRemotePlayInviteAndSession in SteamStuff.h.
        int IndexOf(const char* name) const
        {
            std::unordered_map<std::string, int>::const_iterator it = m_names.find(name);
            return (it != m_names.end()) ? it->second : -1;
        }

        // Call the named method. The caller is responsible for checking Has()
        // for anything that might legitimately be missing; a name that is known
        // to exist (verified once at startup) can be called directly.
        template <class Ret>
        Ret Invoke(const char* name) const { return InvokeAt<Ret>(IndexOf(name)); }
        template <class Ret, class A0>
        Ret Invoke(const char* name, A0 a0) const { return InvokeAt<Ret>(IndexOf(name), a0); }
        template <class Ret, class A0, class A1>
        Ret Invoke(const char* name, A0 a0, A1 a1) const { return InvokeAt<Ret>(IndexOf(name), a0, a1); }
        template <class Ret, class A0, class A1, class A2>
        Ret Invoke(const char* name, A0 a0, A1 a1, A2 a2) const { return InvokeAt<Ret>(IndexOf(name), a0, a1, a2); }

        // Call by explicit (already resolved) vtable index.
        template <class Ret>
        Ret InvokeAt(int idx) const
        {
            typedef Ret (*Fn)(void*);
            return reinterpret_cast<Fn>(SlotAt(idx))(m_iface);
        }
        template <class Ret, class A0>
        Ret InvokeAt(int idx, A0 a0) const
        {
            typedef Ret (*Fn)(void*, A0);
            return reinterpret_cast<Fn>(SlotAt(idx))(m_iface, a0);
        }
        template <class Ret, class A0, class A1>
        Ret InvokeAt(int idx, A0 a0, A1 a1) const
        {
            typedef Ret (*Fn)(void*, A0, A1);
            return reinterpret_cast<Fn>(SlotAt(idx))(m_iface, a0, a1);
        }
        template <class Ret, class A0, class A1, class A2>
        Ret InvokeAt(int idx, A0 a0, A1 a1, A2 a2) const
        {
            typedef Ret (*Fn)(void*, A0, A1, A2);
            return reinterpret_cast<Fn>(SlotAt(idx))(m_iface, a0, a1, a2);
        }

    protected:
        void* SlotAt(int idx) const
        {
            void** vt = *reinterpret_cast<void***>(m_iface);
            return vt[idx < 0 ? 0 : idx]; // callers verify idx; clamp defensively
        }

        void* m_iface;
        std::unordered_map<std::string, int> m_names;
    };
}

#endif // CLIENTVTABLEBYNAME_H
