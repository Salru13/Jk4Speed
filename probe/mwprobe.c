/*
 * mwprobe.c - pre-flight probe for NFS Most Wanted (speed.exe) on the PS4
 * Wine runtime. Our own code; no game files.
 *
 * Question: can 32-bit code execute from heap/data memory? speed.exe is not
 * NX_COMPAT, so Wine turns on force-exec and asks for RWX on readable pages.
 * If the kernel refuses RWX, Wine silently falls back to RW (virtual.c
 * mprotect_exec) and code copied to the heap (sound banks) would fault.
 *
 * Writes mwprobe.log in the working directory, one flushed line per fact.
 * Exit code: 0 = every counted case executed, 2 = at least one faulted,
 *            1 = could not open the log.
 *
 * PE flags are set by build.sh to match speed.exe: base 0x400000, relocs
 * stripped, no NX_COMPAT, no DYNAMIC_BASE, not large-address-aware.
 * Imports: kernel32 + msvcrt only (winmm/ntdll are loaded dynamically).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static FILE *g_log;

static void logf_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

/* mov eax, 0x1234 ; ret */
static const unsigned char k_code[] = { 0xB8, 0x34, 0x12, 0x00, 0x00, 0xC3 };

/* Initialised so it lands in .data, not .bss. */
__attribute__((aligned(16))) static unsigned char g_static_buf[64] = { 1 };

/* ---- fault recovery (vectored handler, no SEH frames needed) ---------- */
static volatile LONG  g_active;
static volatile DWORD g_target;
static volatile DWORD g_fault_code, g_fault_info0, g_fault_addr, g_fault_eip;
static volatile LONG  g_faulted;

static LONG CALLBACK veh(PEXCEPTION_POINTERS ep)
{
    PCONTEXT c = ep->ContextRecord;
    PEXCEPTION_RECORD r = ep->ExceptionRecord;
    if (!g_active)
        return EXCEPTION_CONTINUE_SEARCH;
    if (c->Eip < g_target || c->Eip >= g_target + sizeof(k_code))
        return EXCEPTION_CONTINUE_SEARCH;
    g_faulted = 1;
    g_fault_code = r->ExceptionCode;
    g_fault_eip = c->Eip;
    g_fault_info0 = r->NumberParameters > 0 ? (DWORD)r->ExceptionInformation[0] : 0xFFFFFFFF;
    g_fault_addr = r->NumberParameters > 1 ? (DWORD)r->ExceptionInformation[1] : 0;
    /* Emulate "ret": the stub has not touched ESP yet. */
    c->Eip = *(DWORD *)c->Esp;
    c->Esp += 4;
    c->Eax = 0xDEADDEAD;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static const char *state_str(DWORD s)
{
    switch (s) {
    case MEM_COMMIT: return "COMMIT";
    case MEM_RESERVE: return "RESERVE";
    case MEM_FREE: return "FREE";
    default: return "?";
    }
}

static const char *type_str(DWORD t)
{
    switch (t) {
    case MEM_IMAGE: return "IMAGE";
    case MEM_MAPPED: return "MAPPED";
    case MEM_PRIVATE: return "PRIVATE";
    case 0: return "-";
    default: return "?";
    }
}

/* Calls code at p (already in place). Returns 1 on OK, 0 on FAULT/bad. */
static int exec_at(const char *name, void *p, int counted)
{
    MEMORY_BASIC_INFORMATION mbi;
    DWORD ret;
    typedef DWORD (*fn_t)(void);

    if (!p) {
        logf_("exec %s ptr=NULL err=%lu result=ALLOCFAIL counted=%d",
              name, GetLastError(), counted);
        return 0;
    }
    memset(&mbi, 0, sizeof(mbi));
    VirtualQuery(p, &mbi, sizeof(mbi));
    FlushInstructionCache(GetCurrentProcess(), p, sizeof(k_code));

    g_faulted = 0;
    g_fault_code = g_fault_info0 = g_fault_addr = g_fault_eip = 0;
    g_target = (DWORD)p;
    InterlockedExchange((LONG *)&g_active, 1);
    ret = ((fn_t)p)();
    InterlockedExchange((LONG *)&g_active, 0);

    if (!g_faulted && ret == 0x1234) {
        logf_("exec %s ptr=%p protect=0x%lx alloc_protect=0x%lx state=%s type=%s "
              "region=%p+0x%lx ret=0x%lx result=OK counted=%d",
              name, p, mbi.Protect, mbi.AllocationProtect, state_str(mbi.State),
              type_str(mbi.Type), mbi.BaseAddress, (unsigned long)mbi.RegionSize,
              ret, counted);
        return 1;
    }
    logf_("exec %s ptr=%p protect=0x%lx alloc_protect=0x%lx state=%s type=%s "
          "region=%p+0x%lx ret=0x%lx result=FAULT code=0x%lx info0=%lu "
          "fault_addr=0x%lx eip=0x%lx counted=%d",
          name, p, mbi.Protect, mbi.AllocationProtect, state_str(mbi.State),
          type_str(mbi.Type), mbi.BaseAddress, (unsigned long)mbi.RegionSize,
          ret, g_fault_code, g_fault_info0, g_fault_addr, g_fault_eip, counted);
    return 0;
}

static int copy_exec(const char *name, void *p, int counted)
{
    if (p)
        memcpy(p, k_code, sizeof(k_code));
    return exec_at(name, p, counted);
}

static int g_heap_faults;

static void counted_case(const char *name, void *p)
{
    if (!copy_exec(name, p, 1))
        g_heap_faults++;
}

static void exec_cases(void)
{
    HANDLE h;
    void *p;
    DWORD old;

    counted_case("heap_process", HeapAlloc(GetProcessHeap(), 0, 64));
    h = HeapCreate(0, 0x10000, 0);
    logf_("info heapcreate handle=%p", h);
    counted_case("heap_created", h ? HeapAlloc(h, 0, 64) : NULL);
    counted_case("heap_created_64k", h ? HeapAlloc(h, 0, 64 * 1024) : NULL);
    counted_case("malloc", malloc(64));
    counted_case("static_data", g_static_buf);
    counted_case("va_rw", VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

    p = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p) {
        memcpy(p, k_code, sizeof(k_code));
        if (!VirtualProtect(p, 4096, PAGE_EXECUTE_READ, &old))
            logf_("info va_rw_then_rx VirtualProtect failed err=%lu", GetLastError());
    }
    if (!exec_at("va_rw_then_rx", p, 1))
        g_heap_faults++;

    counted_case("va_rwx", VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    /* Control: must FAULT. Proves the fault path is detected and recovered. */
    p = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p) {
        memcpy(p, k_code, sizeof(k_code));
        if (!VirtualProtect(p, 4096, PAGE_NOACCESS, &old))
            logf_("info control_noaccess VirtualProtect failed err=%lu", GetLastError());
    }
    exec_at("control_noaccess", p, 0);
}

static void addr_walk(void)
{
    const DWORD lo = 0x10000, hi = 0x7FFF0000;
    DWORD a = lo, free_total = 0, largest = 0, largest_base = 0;
    DWORD commit_total = 0, reserve_total = 0, image_total = 0;
    unsigned regions = 0;
    MEMORY_BASIC_INFORMATION m;

    while (a < hi) {
        DWORD base, size, end;
        if (!VirtualQuery((void *)a, &m, sizeof(m))) {
            logf_("vm VirtualQuery failed at 0x%08lx err=%lu", a, GetLastError());
            break;
        }
        base = (DWORD)m.BaseAddress;
        size = (DWORD)m.RegionSize;
        if (size == 0)
            break;
        end = base + size;
        if (end > hi || end < base)
            end = hi;
        if (base < lo)
            base = lo;
        size = end - base;
        regions++;
        logf_("vm 0x%08lx-0x%08lx size=0x%08lx %s prot=0x%lx alloc_base=%p type=%s",
              base, end, size, state_str(m.State), m.Protect, m.AllocationBase,
              type_str(m.Type));
        if (m.State == MEM_FREE) {
            free_total += size;
            if (size > largest) {
                largest = size;
                largest_base = base;
            }
        } else if (m.State == MEM_COMMIT) {
            commit_total += size;
        } else if (m.State == MEM_RESERVE) {
            reserve_total += size;
        }
        if (m.Type == MEM_IMAGE)
            image_total += size;
        a = end;
    }
    logf_("vmsum regions=%u free_total=0x%lx largest_free=0x%lx largest_free_base=0x%08lx "
          "free_mib=%lu largest_mib=%lu commit=0x%lx reserve=0x%lx image=0x%lx",
          regions, free_total, largest, largest_base, free_total >> 20, largest >> 20,
          commit_total, reserve_total, image_total);
}

typedef UINT (WINAPI *tbp_t)(UINT);
typedef DWORD (WINAPI *tgt_t)(void);

static double qpc_us(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER f)
{
    return (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart;
}

static void sleep_stats(const char *name, int kind, LARGE_INTEGER f)
{
    double mn = 1e30, mx = 0, sum = 0;
    int i;
    for (i = 0; i < 200; i++) {
        LARGE_INTEGER a, b;
        double d;
        QueryPerformanceCounter(&a);
        if (kind == 0)
            SleepEx(1, TRUE);
        else if (kind == 1)
            Sleep(1);
        else
            Sleep(0);
        QueryPerformanceCounter(&b);
        d = qpc_us(a, b, f);
        if (d < mn) mn = d;
        if (d > mx) mx = d;
        sum += d;
    }
    logf_("sleep %s n=200 min_us=%.1f avg_us=%.1f max_us=%.1f", name, mn, sum / 200.0, mx);
}

static void timers(void)
{
    LARGE_INTEGER f;
    HMODULE wm;
    tbp_t tbp = NULL, tep = NULL;
    tgt_t tgt = NULL;

    QueryPerformanceFrequency(&f);
    logf_("timer qpc_freq=%I64d", f.QuadPart);

    wm = LoadLibraryA("winmm.dll");
    if (wm) {
        tbp = (tbp_t)(void *)GetProcAddress(wm, "timeBeginPeriod");
        tep = (tbp_t)(void *)GetProcAddress(wm, "timeEndPeriod");
        tgt = (tgt_t)(void *)GetProcAddress(wm, "timeGetTime");
    }
    if (tbp && tgt) {
        UINT r = tbp(1);
        DWORD prev, now, min_d = 0xFFFFFFFF, sum_d = 0;
        unsigned trans = 0;
        LARGE_INTEGER s, e;
        QueryPerformanceCounter(&s);
        prev = tgt();
        for (;;) {
            now = tgt();
            if (now != prev) {
                DWORD d = now - prev;
                if (d < min_d) min_d = d;
                sum_d += d;
                trans++;
                prev = now;
                if (trans >= 50)
                    break;
            }
            QueryPerformanceCounter(&e);
            if (qpc_us(s, e, f) > 3e6)
                break;
        }
        logf_("timer timegettime_res_ms=%lu avg_step_ms=%.2f transitions=%u timeBeginPeriod_ret=%u",
              trans ? min_d : 0, trans ? (double)sum_d / trans : 0.0, trans, r);
    } else {
        logf_("timer winmm unavailable (LoadLibrary/GetProcAddress failed err=%lu)", GetLastError());
    }

    sleep_stats("SleepEx1", 0, f);
    sleep_stats("Sleep1", 1, f);
    sleep_stats("Sleep0", 2, f);

    if (tep)
        tep(1);
}

static void env_info(void)
{
    typedef const char *(CDECL *wgv_t)(void);
    typedef BOOL (WINAPI *gdep_t)(HANDLE, LPDWORD, PBOOL);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    wgv_t wgv = nt ? (wgv_t)(void *)GetProcAddress(nt, "wine_get_version") : NULL;
    gdep_t gdep = k32 ? (gdep_t)(void *)GetProcAddress(k32, "GetProcessDEPPolicy") : NULL;
    unsigned char *img = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS32 *nh = (IMAGE_NT_HEADERS32 *)(img + ((IMAGE_DOS_HEADER *)img)->e_lfanew);
    SYSTEM_INFO si;

    logf_("mwprobe 1.0");
    logf_("info wine_version=%s", wgv ? wgv() : "(not wine)");
    logf_("info image_base=%p characteristics=0x%04x dllcharacteristics=0x%04x",
          img, nh->FileHeader.Characteristics, nh->OptionalHeader.DllCharacteristics);
    if (gdep) {
        DWORD flags = 0;
        BOOL perm = FALSE;
        BOOL ok = gdep(GetCurrentProcess(), &flags, &perm);
        logf_("info dep_policy ok=%d flags=0x%lx permanent=%d (flags 0 = DEP off for this process)",
              ok, flags, perm);
    }
    GetSystemInfo(&si);
    logf_("info page_size=0x%lx alloc_gran=0x%lx min_app=%p max_app=%p cpus=%lu",
          si.dwPageSize, si.dwAllocationGranularity, si.lpMinimumApplicationAddress,
          si.lpMaximumApplicationAddress, si.dwNumberOfProcessors);
}

int main(void)
{
    int rc;
    g_log = fopen("mwprobe.log", "w");
    if (!g_log)
        return 1;
    env_info();
    addr_walk();
    if (!AddVectoredExceptionHandler(1, veh))
        logf_("info AddVectoredExceptionHandler failed err=%lu", GetLastError());
    exec_cases();
    logf_("execsum heap_faults=%d", g_heap_faults);
    timers();
    rc = g_heap_faults ? 2 : 0;
    logf_("done exit=%d", rc);
    fclose(g_log);
    return rc;
}
