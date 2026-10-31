// Test tool: measures where the time of one scrolling frame goes, without
// Windhawk. It loads itself into Explorer's desktop thread with a
// WH_GETMESSAGE hook (like wallprobe.cpp), subclasses the desktop ListView
// and DefView, scrolls the desktop with posted wheel messages (the
// installed mod animates it), and times:
//   - the ListView's WM_PAINT as a whole,
//   - DefView's WM_PRINTCLIENT / WM_ERASEBKGND (the wallpaper behind icons),
//   - the WM_NOTIFY messages the ListView sends DefView while painting,
//     by notification code (custom draw, display info, ...).
// Experiments, given as flags:
//   1  let SetWindowPos copy the old pixels (clear SWP_NOCOPYBITS)
//   2  turn off LVS_EX_TRANSPARENTSHADOWTEXT while measuring
//   4  turn off LVS_EX_DOUBLEBUFFER while measuring
//   8  stop half way for 10 seconds (to look at the screen)
//   16 skip the ListView's own WM_ERASEBKGND (the double-buffered paint
//      draws the background again anyway)
//   32 answer LVN_GETDISPINFOW from a cache filled on first ask
//   64 log the first LVN_GETDISPINFOW requests and answers
//   128 do not scroll; repaint the screen as it is 30 times (one warm-up
//      repaint first, not counted)
//   1024 time 10000 GetProp calls on DefView and the ListView, and 1000
//      GetDC(NULL)/ReleaseDC pairs
//   512 count the messages the ListView and DefView get while painting
//   256 with 128: sample where the desktop thread is, about every
//      millisecond, into C:\dev\Sidetop\versions\lvprof-samples.txt
// Everything is put back and taken out before it exits.
//   lvprof <flags> <label>
// Writes C:\dev\Sidetop\versions\lvprof.log.
// Build (x64):
//   clang++ --target=x86_64-w64-mingw32 -municode -static -O1 -DPROBE_DLL
//     -shared tools/lvprof.cpp -o lvprof.dll -lcomctl32
//   clang++ --target=x86_64-w64-mingw32 -municode -static -O1
//     tools/lvprof.cpp -o lvprof.exe
#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <cstdio>
#include <cwchar>

static const wchar_t kStart[] = L"Sidetop_LvProf_Start";
static const wchar_t kStop[] = L"Sidetop_LvProf_Stop";
static const wchar_t kLog[] = L"C:\\dev\\Sidetop\\versions\\lvprof.log";

static HWND FindDesktopListView() {
    HWND found = nullptr;
    EnumWindows(
        [](HWND top, LPARAM lParam) -> BOOL {
            WCHAR cls[64];
            GetClassName(top, cls, 64);
            if (wcscmp(cls, L"Progman") && wcscmp(cls, L"WorkerW")) {
                return TRUE;
            }
            HWND defView =
                FindWindowEx(top, nullptr, L"SHELLDLL_DefView", nullptr);
            HWND list = defView ? FindWindowEx(defView, nullptr,
                                               L"SysListView32", nullptr)
                                : nullptr;
            if (list) {
                *(HWND*)lParam = list;
                return FALSE;
            }
            return TRUE;
        },
        (LPARAM)&found);
    return found;
}

#ifdef PROBE_DLL

static HWND g_list, g_defView;
static DWORD g_flags;
static DWORD g_savedExStyle;
static LARGE_INTEGER g_freq;
static bool g_inPaint;
static int g_logged;
static int g_msgCount[2][0x10000];  // [ListView, DefView][message]

// Sampling (flag 256). While the desktop thread is suspended the sampler
// only copies registers and stack memory into fixed buffers: no heap, no
// loader lock, nothing the suspended thread might hold. A watchdog resumes
// the thread if the sampler ever takes longer than 50 ms.
struct Sample {
    DWORD64 rip;
    DWORD64 stack[48];  // return-address candidates found on the stack
    int stackCount;
};
static Sample g_samples[8000];
static volatile LONG g_sampleCount;
static volatile LONG g_sampling;
static volatile LONG g_suspended;
static volatile LONGLONG g_suspendedAt;
static HANDLE g_uiThread;
static DWORD64 g_textStart[16], g_textEnd[16];  // code ranges to look for
static int g_ranges;

static void AddCodeRange(const wchar_t* module) {
    HMODULE m = GetModuleHandle(module);
    if (!m || g_ranges >= 16) return;
    auto* dos = (IMAGE_DOS_HEADER*)m;
    auto* nt = (IMAGE_NT_HEADERS*)((BYTE*)m + dos->e_lfanew);
    g_textStart[g_ranges] = (DWORD64)m;
    g_textEnd[g_ranges] = (DWORD64)m + nt->OptionalHeader.SizeOfImage;
    g_ranges++;
}

static bool InCode(DWORD64 a) {
    for (int i = 0; i < g_ranges; i++) {
        if (a >= g_textStart[i] + 0x1000 && a < g_textEnd[i]) return true;
    }
    return false;
}

// Whether the bytes before a are a call instruction, so that a is a
// return address and not just a pointer into code.
static bool AfterCall(DWORD64 a) {
    const BYTE* p = (const BYTE*)a;
    if (p[-5] == 0xE8) return true;                    // call rel32
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;  // call [rip+x]
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true;  // call reg
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;  // call [reg+x]
    if (p[-7] == 0x48 && p[-6] == 0xFF) return true;
    return false;
}

static DWORD WINAPI Watchdog(LPVOID) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    while (g_sampling) {
        Sleep(10);
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (g_suspended &&
            (now.QuadPart - g_suspendedAt) * 1000 / freq.QuadPart > 50 &&
            InterlockedExchange(&g_suspended, 0) == 1) {
            ResumeThread(g_uiThread);
        }
    }
    return 0;
}

static DWORD WINAPI Sampler(LPVOID) {
    timeBeginPeriod(1);
    while (g_sampling && g_sampleCount < 8000) {
        Sleep(1);
        if (SuspendThread(g_uiThread) == (DWORD)-1) break;
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        g_suspendedAt = t.QuadPart;
        InterlockedExchange(&g_suspended, 1);
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(g_uiThread, &ctx)) {
            Sample& sample = g_samples[g_sampleCount];
            sample.rip = ctx.Rip;
            sample.stackCount = 0;
            DWORD64* sp = (DWORD64*)ctx.Rsp;
            for (int i = 0; i < 2048 && sample.stackCount < 48; i++) {
                DWORD64 v;
                // The stack of a suspended thread is ours to read; stop at
                // its end.
                if (IsBadReadPtr(sp + i, 8)) break;
                v = sp[i];
                if (InCode(v) && AfterCall(v)) {
                    sample.stack[sample.stackCount++] = v;
                }
            }
            g_sampleCount++;
        }
        if (InterlockedExchange(&g_suspended, 0) == 1) {
            ResumeThread(g_uiThread);
        }
    }
    timeEndPeriod(1);
    return 0;
}

static void WriteSamples() {
    HANDLE f = CreateFile(L"C:\\dev\\Sidetop\\versions\\lvprof-samples.txt",
                          GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                          CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[4096];
    for (LONG i = 0; i < g_sampleCount; i++) {
        int n = 0;
        DWORD64 addrs[49];
        int count = 0;
        addrs[count++] = g_samples[i].rip;
        for (int j = 0; j < g_samples[i].stackCount; j++) {
            addrs[count++] = g_samples[i].stack[j];
        }
        for (int j = 0; j < count; j++) {
            HMODULE m = nullptr;
            wchar_t name[MAX_PATH] = L"?";
            if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCWSTR)addrs[j], &m)) {
                GetModuleFileName(m, name, MAX_PATH);
            }
            const wchar_t* base = wcsrchr(name, L'\\');
            base = base ? base + 1 : name;
            n += snprintf(line + n, sizeof(line) - n, "%s%ls+%llX",
                          j ? " " : "", base,
                          (unsigned long long)(addrs[j] - (DWORD64)m));
            if (n > (int)sizeof(line) - 100) break;
        }
        n += snprintf(line + n, sizeof(line) - n, "\r\n");
        DWORD written;
        WriteFile(f, line, n, &written, nullptr);
    }
    CloseHandle(f);
}

// Answers of DefView to LVN_GETDISPINFOW, by item, for flag 32.
struct CacheEntry {
    int item;
    UINT mask;  // asked
    LVITEMW out;
    wchar_t text[MAX_PATH];
};
static CacheEntry g_cache[512];
static int g_cacheCount, g_cacheHits;

static CacheEntry* FindCache(int item, UINT mask) {
    for (int i = 0; i < g_cacheCount; i++) {
        if (g_cache[i].item == item && g_cache[i].mask == mask) {
            return &g_cache[i];
        }
    }
    return nullptr;
}

// Totals over all frames.
static int g_paints;
static double g_paintMs, g_paintMax;
static double g_bgMs;
static int g_bgCount;
struct NotifyStat {
    int code;
    int count;
    double ms;
};
static NotifyStat g_notify[32];
static int g_notifyKinds;
static long long g_paintArea;  // update rectangle area, summed

static double Ms(LARGE_INTEGER a, LARGE_INTEGER b) {
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / g_freq.QuadPart;
}

static void Append(const wchar_t* format, ...) {
    wchar_t text[1024];
    va_list args;
    va_start(args, format);
    int n = _vsnwprintf(text, 1023, format, args);
    va_end(args);
    if (n < 0) n = 1023;
    text[n] = 0;
    char utf8[2048];
    int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8),
                                  nullptr, nullptr);
    HANDLE f = CreateFile(kLog, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                          OPEN_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(f, utf8, len - 1, &written, nullptr);
        WriteFile(f, "\r\n", 2, &written, nullptr);
        CloseHandle(f);
    }
}

static LRESULT CALLBACK ListProc(HWND hWnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (g_inPaint && (g_flags & 512) && msg < 0x10000) g_msgCount[0][msg]++;
    if (msg == WM_PAINT) {
        RECT update{};
        GetUpdateRect(hWnd, &update, FALSE);
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        g_inPaint = true;
        LRESULT r = DefSubclassProc(hWnd, msg, wParam, lParam);
        g_inPaint = false;
        QueryPerformanceCounter(&b);
        double ms = Ms(a, b);
        g_paints++;
        g_paintMs += ms;
        if (ms > g_paintMax) g_paintMax = ms;
        g_paintArea += (long long)(update.right - update.left) *
                       (update.bottom - update.top);
        return r;
    }
    if (msg == WM_ERASEBKGND && (g_flags & 16)) {
        return 1;
    }
    if (msg == WM_WINDOWPOSCHANGING && (g_flags & 1)) {
        ((WINDOWPOS*)lParam)->flags &= ~SWP_NOCOPYBITS;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK DefViewProc(HWND hWnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (!g_inPaint) {
        return DefSubclassProc(hWnd, msg, wParam, lParam);
    }
    if ((g_flags & 512) && msg < 0x10000) g_msgCount[1][msg]++;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    LRESULT r = 0;
    NMLVDISPINFOW* di = nullptr;
    if (msg == WM_NOTIFY && ((NMHDR*)lParam)->hwndFrom == g_list &&
        ((NMHDR*)lParam)->code == LVN_GETDISPINFOW) {
        di = (NMLVDISPINFOW*)lParam;
    }
    UINT askedMask = di ? di->item.mask : 0;
    LPWSTR buffer = di ? di->item.pszText : nullptr;
    int bufferLen = di ? di->item.cchTextMax : 0;
    CacheEntry* hit = (di && (g_flags & 32) && di->item.iSubItem == 0)
                          ? FindCache(di->item.iItem, askedMask)
                          : nullptr;
    if (hit) {
        LVITEMW& it = di->item;
        UINT mask = hit->out.mask;
        if (mask & LVIF_IMAGE) it.iImage = hit->out.iImage;
        if (mask & LVIF_STATE) {
            it.state = (it.state & ~hit->out.stateMask) |
                       (hit->out.state & hit->out.stateMask);
            it.stateMask = hit->out.stateMask;
        }
        if (mask & LVIF_INDENT) it.iIndent = hit->out.iIndent;
        if ((mask & LVIF_TEXT) && buffer && bufferLen > 0) {
            lstrcpynW(buffer, hit->text, bufferLen);
            it.pszText = buffer;
        }
        it.mask = mask;
        g_cacheHits++;
    } else {
        r = DefSubclassProc(hWnd, msg, wParam, lParam);
    }
    if (di && !hit && (g_flags & 32) && di->item.iSubItem == 0 &&
        g_cacheCount < 512) {
        CacheEntry& e = g_cache[g_cacheCount++];
        e.item = di->item.iItem;
        e.mask = askedMask;
        e.out = di->item;
        e.text[0] = 0;
        if ((di->item.mask & LVIF_TEXT) && di->item.pszText &&
            di->item.pszText != LPSTR_TEXTCALLBACKW) {
            lstrcpynW(e.text, di->item.pszText, MAX_PATH);
        }
    }
    if (di && !hit && (g_flags & 64) && g_logged < 30) {
        g_logged++;
        LVITEMW& it = di->item;
        Append(L"  dispinfo item %d sub %d asked %08X -> mask %08X image %d "
               L"state %08X/%08X indent %d text %s (own buffer %d, max %d) "
               L"columns %u",
               it.iItem, it.iSubItem, askedMask, it.mask, it.iImage, it.state,
               it.stateMask, it.iIndent,
               (it.mask & LVIF_TEXT) && it.pszText &&
                       it.pszText != LPSTR_TEXTCALLBACKW
                   ? it.pszText
                   : L"-",
               it.pszText == buffer, bufferLen,
               (it.mask & LVIF_COLUMNS) ? it.cColumns : 0);
    }
    QueryPerformanceCounter(&b);
    double ms = Ms(a, b);
    if (msg == WM_PRINTCLIENT || msg == WM_ERASEBKGND) {
        g_bgMs += ms;
        g_bgCount++;
    } else if (msg == WM_NOTIFY) {
        int code = ((NMHDR*)lParam)->code;
        if (code == NM_CUSTOMDRAW) {
            // Split custom draw by stage.
            code = -100000 - (int)((NMCUSTOMDRAW*)lParam)->dwDrawStage;
        }
        int i = 0;
        while (i < g_notifyKinds && g_notify[i].code != code) i++;
        if (i == g_notifyKinds && g_notifyKinds < 32) {
            g_notify[g_notifyKinds++] = {code, 0, 0};
        }
        if (i < 32) {
            g_notify[i].count++;
            g_notify[i].ms += ms;
        }
    }
    return r;
}

extern "C" __declspec(dllexport) LRESULT CALLBACK GetMsgProc(int code,
                                                            WPARAM wParam,
                                                            LPARAM lParam) {
    static UINT startMsg = RegisterWindowMessage(kStart);
    static UINT stopMsg = RegisterWindowMessage(kStop);
    MSG* m = (MSG*)lParam;
    if (code == HC_ACTION && wParam == PM_REMOVE && m->message == startMsg &&
        !g_list) {
        QueryPerformanceFrequency(&g_freq);
        g_list = m->hwnd;
        g_defView = GetParent(g_list);
        g_flags = (DWORD)m->wParam;
        g_savedExStyle = ListView_GetExtendedListViewStyle(g_list);
        DWORD off = 0;
        if (g_flags & 2) off |= LVS_EX_TRANSPARENTSHADOWTEXT;
        if (g_flags & 4) off |= LVS_EX_DOUBLEBUFFER;
        if (off) ListView_SetExtendedListViewStyleEx(g_list, off, 0);
        SetWindowSubclass(g_list, ListProc, 7, 0);
        SetWindowSubclass(g_defView, DefViewProc, 7, 0);
        Append(L"start flags=%lu exStyle=%08lX items=%d", g_flags,
               g_savedExStyle, ListView_GetItemCount(g_list));
        if (g_flags & 1024) {
            LARGE_INTEGER a, b;
            HWND targets[2] = {g_defView, g_list};
            for (HWND w : targets) {
                QueryPerformanceCounter(&a);
                for (int i = 0; i < 10000; i++) GetPropW(w, L"UxSubclassInfo");
                QueryPerformanceCounter(&b);
                Append(L"  GetProp on %p: %.2f us each", w, Ms(a, b) * 1000 / 10000);
            }
            QueryPerformanceCounter(&a);
            for (int i = 0; i < 1000; i++) ReleaseDC(nullptr, GetDC(nullptr));
            QueryPerformanceCounter(&b);
            Append(L"  GetDC/ReleaseDC: %.2f us a pair", Ms(a, b) * 1000 / 1000);
        }
        if (g_flags & 128) {
            RedrawWindow(g_list, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            g_paints = 0;
            g_paintMs = g_paintMax = g_bgMs = 0;
            g_bgCount = 0;
            g_notifyKinds = 0;
            g_paintArea = 0;
            HANDLE sampler = nullptr, watchdog = nullptr;
            if (g_flags & 256) {
                const wchar_t* modules[] = {
                    L"comctl32.dll", L"shell32.dll", L"windows.storage.dll",
                    L"gdi32full.dll", L"gdi32.dll", L"win32u.dll",
                    L"user32.dll", L"uxtheme.dll", L"dwrite.dll",
                    L"textshaping.dll", L"lpk.dll", L"usp10.dll",
                    L"ntdll.dll", L"kernelbase.dll", L"explorerframe.dll",
                    L"windowscodecs.dll"};
                // comctl32 v6 is the one the ListView uses.
                g_ranges = 0;
                for (const wchar_t* module : modules) AddCodeRange(module);
                HMODULE lv = (HMODULE)GetClassLongPtr(g_list, GCLP_HMODULE);
                if (lv && g_ranges < 16) {
                    auto* dos = (IMAGE_DOS_HEADER*)lv;
                    auto* nt = (IMAGE_NT_HEADERS*)((BYTE*)lv + dos->e_lfanew);
                    g_textStart[g_ranges] = (DWORD64)lv;
                    g_textEnd[g_ranges] =
                        (DWORD64)lv + nt->OptionalHeader.SizeOfImage;
                    g_ranges++;
                }
                DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                                GetCurrentProcess(), &g_uiThread,
                                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE,
                                0);
                g_sampleCount = 0;
                g_sampling = 1;
                watchdog = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
                sampler = CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr);
            }
            for (int i = 0; i < 30; i++) {
                RedrawWindow(g_list, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            }
            if (sampler) {
                g_sampling = 0;
                WaitForSingleObject(sampler, INFINITE);
                WaitForSingleObject(watchdog, INFINITE);
                CloseHandle(sampler);
                CloseHandle(watchdog);
                CloseHandle(g_uiThread);
                WriteSamples();
                Append(L"  samples: %ld", g_sampleCount);
            }
        }
    } else if (code == HC_ACTION && wParam == PM_REMOVE &&
               m->message == stopMsg && g_list) {
        RemoveWindowSubclass(g_list, ListProc, 7);
        RemoveWindowSubclass(g_defView, DefViewProc, 7);
        ListView_SetExtendedListViewStyleEx(g_list, 0xFFFFFFFF,
                                            g_savedExStyle);
        RedrawWindow(g_list, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
        int n = g_paints ? g_paints : 1;
        Append(L"paints %d: avg %.2f ms, max %.2f ms, avg area %lld px",
               g_paints, g_paintMs / n, g_paintMax, g_paintArea / n);
        Append(L"  background: %d calls, %.2f ms per paint", g_bgCount,
               g_bgMs / n);
        double notifyMs = 0;
        for (int i = 0; i < g_notifyKinds; i++) {
            notifyMs += g_notify[i].ms;
            int c = g_notify[i].code;
            if (c <= -100000) {
                Append(L"  NM_CUSTOMDRAW stage %08X: %.1f per paint, %.2f ms "
                       L"per paint",
                       (unsigned)(-100000 - c), g_notify[i].count / (double)n,
                       g_notify[i].ms / n);
            } else {
                Append(L"  WM_NOTIFY %d: %.1f per paint, %.2f ms per paint", c,
                       g_notify[i].count / (double)n, g_notify[i].ms / n);
            }
        }
        Append(L"  ListView itself (paint - background - notify): %.2f ms",
               (g_paintMs - g_bgMs - notifyMs) / n);
        if (g_flags & 32) {
            Append(L"  cache: %d entries, %d hits", g_cacheCount, g_cacheHits);
        }
        if (g_flags & 512) {
            for (int w = 0; w < 2; w++) {
                for (int m = 0; m < 0x10000; m++) {
                    if (g_msgCount[w][m]) {
                        Append(L"  %s message %04X: %.1f per paint",
                               w ? L"DefView" : L"ListView", m,
                               g_msgCount[w][m] / (double)n);
                    }
                }
            }
        }
        g_list = nullptr;
        if (HANDLE done = OpenEvent(EVENT_MODIFY_STATE, FALSE, L"Sidetop_LvProf_Done")) {
            SetEvent(done);
            CloseHandle(done);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// If the DLL goes away before the stop message (the hook was removed),
// take the subclasses out first, or Explorer would call into freed code.
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        if (g_list) {
            RemoveWindowSubclass(g_list, ListProc, 7);
            RemoveWindowSubclass(g_defView, DefViewProc, 7);
        }
    }
    return TRUE;
}

#else

static void Wheel(HWND list, int notches) {
    int delta = notches > 0 ? -120 : 120;
    for (int i = 0; i < abs(notches); i++) {
        PostMessage(list, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)(short)delta),
                    MAKELPARAM(960, 540));
        Sleep(30);
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        wprintf(L"lvprof <flags> <label>\n");
        return 2;
    }
    HWND list = FindDesktopListView();
    if (!list) {
        wprintf(L"no desktop ListView\n");
        return 1;
    }
    wchar_t dllPath[MAX_PATH];
    GetModuleFileName(nullptr, dllPath, MAX_PATH);
    wcscpy(wcsrchr(dllPath, L'.'), L".dll");
    HMODULE dll = LoadLibrary(dllPath);
    auto proc = dll ? (HOOKPROC)GetProcAddress(dll, "GetMsgProc") : nullptr;
    if (!proc) {
        wprintf(L"cannot load %ls\n", dllPath);
        return 1;
    }
    HANDLE done = CreateEvent(nullptr, TRUE, FALSE, L"Sidetop_LvProf_Done");
    HHOOK hook = SetWindowsHookEx(WH_GETMESSAGE, proc, dll,
                                  GetWindowThreadProcessId(list, nullptr));
    if (!hook) {
        wprintf(L"SetWindowsHookEx failed: %lu\n", GetLastError());
        return 1;
    }
    FILE* f = _wfopen(kLog, L"a");
    if (f) {
        fwprintf(f, L"== %ls\n", argv[2]);
        fclose(f);
    }
    PostMessage(list, RegisterWindowMessage(kStart), _wtoi(argv[1]), 0);
    Sleep(500);
    DWORD flags = _wtoi(argv[1]);
    if (flags & 8) {
        Wheel(list, 5);
        wprintf(L"holding half way\n");
        fflush(stdout);
        Sleep(10000);
        Wheel(list, -10);
        Sleep(1500);
    }
    // Over to the added page and back, three times.
    for (int i = 0; i < ((flags & (8 | 128)) ? 0 : 3); i++) {
        Wheel(list, 10);
        Sleep(1500);
        Wheel(list, -10);
        Sleep(1500);
    }
    PostMessage(list, RegisterWindowMessage(kStop), 0, 0);
    // The hook (and with it the DLL) goes away when this thread exits, so
    // wait until the DLL says it has taken everything out of Explorer.
    while (WaitForSingleObject(done, 60000) == WAIT_TIMEOUT) {
        wprintf(L"still waiting for the DLL to stop\n");
        fflush(stdout);
        PostMessage(list, RegisterWindowMessage(kStop), 0, 0);
    }
    UnhookWindowsHookEx(hook);
    PostMessage(list, WM_NULL, 0, 0);
    wprintf(L"done\n");
    return 0;
}

#endif
