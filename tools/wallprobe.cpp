// Test tool: checks, without Windhawk, how the moving wallpaper of 0.4.4
// works. It loads itself into Explorer's desktop thread with a
// WH_GETMESSAGE hook, subclasses DefView (the desktop ListView's parent) the
// way the mod does, and paints a test image there on WM_PRINTCLIENT
// (PRF_ERASEBKGND) and WM_ERASEBKGND, shifted by how far the ListView is
// slid. Everything is taken out again before it exits.
// Result (2026-10-31): the test image showed behind the icons and moved
// with them at home, on the added page and in between (docs/v1.0-test.md).
//   wallprobe <image> <seconds>
// Build (x64, both parts in one file):
//   clang++ --target=x86_64-w64-mingw32 -municode -static -O1 -DPROBE_DLL
//     -shared tools/wallprobe.cpp -o wallprobe.dll -lcomctl32 -lole32 -lgdi32
//   clang++ --target=x86_64-w64-mingw32 -municode -static -O1
//     tools/wallprobe.cpp -o wallprobe.exe
#include <windows.h>
#include <commctrl.h>
#include <wincodec.h>
#include <cstdio>
#include <cwchar>

static const wchar_t kStart[] = L"Sidetop_WallProbe_Start";
static const wchar_t kStop[] = L"Sidetop_WallProbe_Stop";

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

static HDC g_dc;
static HBITMAP g_bitmap;
static HGDIOBJ g_old;
static int g_width, g_height;
static HWND g_list, g_defView;
static int g_counts[2];  // WM_PRINTCLIENT, WM_ERASEBKGND painted

static void Log(const wchar_t* format, ...) {
    wchar_t text[512];
    va_list args;
    va_start(args, format);
    _vsnwprintf(text, 511, format, args);
    va_end(args);
    text[511] = 0;
    OutputDebugString(text);
}

static bool LoadImage(const wchar_t* path, int width, int height) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromFilename(
            path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
            &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr))
        hr = scaler->Initialize(frame, width, height,
                                WICBitmapInterpolationModeFant);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(scaler, GUID_WICPixelFormat32bppBGR,
                                   WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom);
    void* bits = nullptr;
    if (SUCCEEDED(hr)) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        g_bitmap =
            CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        hr = g_bitmap ? S_OK : E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        hr = converter->CopyPixels(nullptr, width * 4, width * 4 * height,
                                   (BYTE*)bits);
    if (converter) converter->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (FAILED(hr)) {
        Log(L"WallProbe: image not loaded (%08X)", (unsigned)hr);
        if (g_bitmap) DeleteObject(g_bitmap);
        g_bitmap = nullptr;
        return false;
    }
    g_dc = CreateCompatibleDC(nullptr);
    g_old = SelectObject(g_dc, g_bitmap);
    g_width = width;
    g_height = height;
    return true;
}

// Same math as PaintMovedWallpaper in the mod, for the wide mode: the
// picture moves with the ListView.
static bool Paint(HWND hWnd, HDC hdc, int which) {
    if (!g_dc) return false;
    RECT clip;
    if (GetClipBox(hdc, &clip) == ERROR) GetClientRect(hWnd, &clip);
    POINT origin{0, 0};
    MapWindowPoints(hWnd, HWND_DESKTOP, &origin, 1);
    RECT parentScreen{};
    GetWindowRect(g_defView, &parentScreen);
    RECT listRect{};
    GetWindowRect(g_list, &listRect);
    int shift = parentScreen.left - listRect.left;  // the ListView's offset
    int srcX = clip.left + origin.x - parentScreen.left + shift;
    int srcY = clip.top + origin.y - parentScreen.top;
    BitBlt(hdc, clip.left, clip.top, clip.right - clip.left,
           clip.bottom - clip.top, g_dc, srcX, srcY, SRCCOPY);
    if (g_counts[which]++ < 3) {
        Log(L"WallProbe: %s clip (%d,%d)-(%d,%d) shift %d",
            which ? L"WM_ERASEBKGND" : L"WM_PRINTCLIENT", clip.left, clip.top,
            clip.right, clip.bottom, shift);
    }
    return true;
}

static LRESULT CALLBACK DefViewProc(HWND hWnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_PRINTCLIENT && lParam == PRF_ERASEBKGND &&
        Paint(hWnd, (HDC)wParam, 0)) {
        return 1;
    }
    if (msg == WM_ERASEBKGND && Paint(hWnd, (HDC)wParam, 1)) {
        return 1;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

extern "C" __declspec(dllexport) LRESULT CALLBACK GetMsgProc(int code,
                                                            WPARAM wParam,
                                                            LPARAM lParam) {
    static UINT startMsg = RegisterWindowMessage(kStart);
    static UINT stopMsg = RegisterWindowMessage(kStop);
    MSG* m = (MSG*)lParam;
    if (code == HC_ACTION && wParam == PM_REMOVE && m->message == startMsg &&
        !g_defView) {
        g_list = m->hwnd;
        g_defView = GetParent(g_list);
        RECT page{};
        GetClientRect(g_defView, &page);
        RECT list{};
        GetClientRect(g_list, &list);
        wchar_t path[MAX_PATH] = L"";
        GetEnvironmentVariable(L"SIDETOP_PROBE_IMAGE", path, MAX_PATH);
        // The image is read from a file the injector writes, since the
        // environment of Explorer is not ours.
        HANDLE f = CreateFile(L"C:\\dev\\Sidetop\\versions\\wallprobe-image.txt",
                              GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD read = 0;
            ReadFile(f, path, sizeof(path) - 2, &read, nullptr);
            path[read / 2] = 0;
            CloseHandle(f);
        }
        Log(L"WallProbe: start, DefView %dx%d, ListView %dx%d, image %s",
            page.right, page.bottom, list.right, list.bottom, path);
        LoadImage(path, list.right, page.bottom);
        SetWindowSubclass(g_defView, DefViewProc, 1, 0);
        RedrawWindow(g_list, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    } else if (code == HC_ACTION && wParam == PM_REMOVE &&
               m->message == stopMsg && g_defView) {
        RemoveWindowSubclass(g_defView, DefViewProc, 1);
        if (g_dc) {
            SelectObject(g_dc, g_old);
            DeleteDC(g_dc);
            DeleteObject(g_bitmap);
            g_dc = nullptr;
        }
        Log(L"WallProbe: stop (painted %d by WM_PRINTCLIENT, %d by "
            L"WM_ERASEBKGND)",
            g_counts[0], g_counts[1]);
        RedrawWindow(g_list, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        g_defView = nullptr;
        if (HANDLE done = OpenEvent(EVENT_MODIFY_STATE, FALSE, L"Sidetop_WallProbe_Done")) {
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
        if (g_defView) {
            RemoveWindowSubclass(g_defView, DefViewProc, 1);
        }
    }
    return TRUE;
}

#else

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        wprintf(L"wallprobe <image> <seconds>\n");
        return 2;
    }
    HWND list = FindDesktopListView();
    if (!list) {
        wprintf(L"no desktop ListView\n");
        return 1;
    }
    // Hand the image path over in a file (UTF-16, no terminator).
    HANDLE f = CreateFile(L"C:\\dev\\Sidetop\\versions\\wallprobe-image.txt",
                          GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    WriteFile(f, argv[1], (DWORD)(wcslen(argv[1]) * 2), &written, nullptr);
    CloseHandle(f);

    wchar_t dllPath[MAX_PATH];
    GetModuleFileName(nullptr, dllPath, MAX_PATH);
    wcscpy(wcsrchr(dllPath, L'.'), L".dll");
    HMODULE dll = LoadLibrary(dllPath);
    auto proc = (HOOKPROC)GetProcAddress(dll, "GetMsgProc");
    DWORD thread = GetWindowThreadProcessId(list, nullptr);
    if (!dll || !proc) {
        wprintf(L"cannot load %ls\n", dllPath);
        return 1;
    }
    HANDLE done = CreateEvent(nullptr, TRUE, FALSE, L"Sidetop_WallProbe_Done");
    HHOOK hook = SetWindowsHookEx(WH_GETMESSAGE, proc, dll, thread);
    if (!hook) {
        wprintf(L"SetWindowsHookEx failed: %lu\n", GetLastError());
        return 1;
    }
    PostMessage(list, RegisterWindowMessage(kStart), 0, 0);
    wprintf(L"started; waiting %ls s\n", argv[2]);
    fflush(stdout);
    Sleep(_wtoi(argv[2]) * 1000);
    PostMessage(list, RegisterWindowMessage(kStop), 0, 0);
    // The hook (and with it the DLL) goes away when this thread exits, so
    // wait until the DLL says it has taken everything out of Explorer.
    while (WaitForSingleObject(done, 60000) == WAIT_TIMEOUT) {
        wprintf(L"still waiting for the DLL to stop\n");
        fflush(stdout);
        PostMessage(list, RegisterWindowMessage(kStop), 0, 0);
    }
    UnhookWindowsHookEx(hook);
    // Make Explorer pump a message so that it lets go of the DLL.
    PostMessage(list, WM_NULL, 0, 0);
    wprintf(L"stopped\n");
    return 0;
}

#endif
