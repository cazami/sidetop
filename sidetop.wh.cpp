// ==WindhawkMod==
// @id              sidetop
// @name            Sidetop – Horizontal Scrolling Desktop
// @description     Scroll the desktop left and right with the mouse wheel
// @version         1.0
// @author          min
// @github          https://github.com/cazami
// @homepage        https://github.com/cazami/sidetop
// @include         explorer.exe
// @compilerOptions -lcomctl32 -lole32 -luuid -lshlwapi -lgdi32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Sidetop – Horizontal Scrolling Desktop

Scroll the desktop to the right with the mouse wheel (wheel down =
right, wheel up = left). The left edge stops at the original layout ("home").
Ctrl+wheel keeps the native icon-size behavior.

- Choose smooth scrolling or page by page in the settings.
- Drag with the middle mouse button to move the desktop by hand.
- Double-click an empty spot, or press a shortcut key (Ctrl+Alt+Home by
  default), to go back home. Choose which in the settings.
- A thin bar at the bottom of the screen shows where you are while the
  desktop moves, and fades out when it stops.
- The wallpaper can stay fixed, move a little (parallax), or be a wide image
  that moves with the icons.
- While dragging icons, hold the cursor at the left or right screen edge to
  scroll that way; move it off the edge to stop. This works for drags that
  start in Explorer (the desktop or a folder window), not for drags from
  other programs.
- New files on the desktop (downloads, for example) appear on the screen you
  are looking at.
- The position you were at is restored after Explorer restarts.

**Back up your desktop icon layout before testing**
(`HKCU\Software\Microsoft\Windows\Shell\Bags\1\Desktop`).

After enabling the mod, restart Explorer once so that icons placed on the
added pages stay there after a refresh.

Disabling or uninstalling the mod moves every icon that is outside the home
screen back into a free spot on the home screen.

"Auto arrange icons" must be off. While it is on, the mod does nothing;
turning it on moves every icon back home, and turning it off again starts
the mod again.

Tested on Windows 10 22H2 with one monitor. On Windows 11 it was tried in a
virtual machine, where the main features worked; not every feature has been
checked there.

Known issues:
- Changing the icon size, or restarting Explorer, can now and then shift
  some icons out of place.
- With many icons on the screen, scrolling is less smooth with the fixed and
  parallax wallpaper. The wide image mode is the smoothest.
- Only one monitor is supported so far. With two or more, the mod stays
  off; when a monitor is added, every icon goes back home first.

Source and notes: https://github.com/cazami/sidetop
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- scrollMode: continuous
  $name: Scrolling
  $description: >-
    Page by page moves one screen per wheel notch and always stops at a page
    edge.
  $options:
  - continuous: Smooth
  - discrete: Page by page
- scrollStep: 200
  $name: Pixels per wheel notch
  $description: Smooth scrolling only.
- glide: 100
  $name: Glide (inertia), in milliseconds
  $description: >-
    How long the desktop keeps gliding after the wheel stops. Larger values
    feel heavier. 0 moves at once.
- maxPages: 1
  $name: Pages added to the right of home
- homeAction: doubleClick
  $name: Go back home with
  $options:
  - doubleClick: Double-click on an empty spot
  - shortcut: Shortcut key
  - none: Nothing
- homeShortcut: Ctrl+Alt+Home
  $name: Shortcut key for going back home
  $description: >-
    Used when "Go back home with" is "Shortcut key". It works from any
    window. Join Ctrl, Alt, Shift and Win with + and end with one key: a
    letter, a digit, F1 to F24, Home, End, PageUp, PageDown, Insert, Delete,
    Left, Right, Up, Down, Space or Backspace. For example Win+Alt+H.
- wallpaper: fixed
  $name: Wallpaper
  $description: >-
    Parallax moves the current wallpaper a little as the desktop moves. Wide
    image stretches the image below over home and the added pages and moves
    it with the icons.
  $options:
  - fixed: Fixed
  - parallax: Parallax
  - wide: Wide image
- wideWallpaper: ""
  $name: Wide image file
  $description: >-
    Full path of the image for "Wide image", for example
    C:\Users\me\Pictures\panorama.jpg. A width of 2 screens or more (with 1
    added page) looks best.
- newIconsInView: true
  $name: Put new icons on the screen you are looking at
- dragAutoScroll: true
  $name: Move to the next page while dragging icons at the screen edge
- dragAutoScrollDelay: 500
  $name: Wait at the edge before moving, in milliseconds
- showIndicator: true
  $name: Show a position bar at the bottom while scrolling
- dragAutoScrollSpeed: 1500
  $name: Top speed while dragging at the edge, in pixels per second
  $description: >-
    Moving starts at 40% of this speed and reaches it after one second.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <commctrl.h>
#include <exdisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <vector>

struct {
    bool discrete;  // page by page (spec 3.3)
    int scrollStep;
    int glideMs;
    int maxPages;
    enum { kHomeDoubleClick, kHomeShortcut, kHomeNone } homeAction;
    std::wstring homeShortcut;
    bool showIndicator;
    bool newIconsInView;
    enum { kFixed, kParallax, kWide } wallpaper;
    std::wstring wideWallpaper;
    bool dragAutoScroll;
    int dragAutoScrollDelay;
    int dragAutoScrollSpeed;
} g_settings;

// Everything below except g_hList is used only on the desktop ListView's
// thread, from inside the subclass procedure.
HWND g_hList;
bool g_active;
int g_pageWidth;    // width of one page = the ListView parent's client width
double g_offset;    // animated offset, in pixels to the right of home
int g_target;       // offset the animation is heading to
int g_applied;      // offset the ListView window is slid by
bool g_animating;
// Middle-button drag: cursor x and offset when the drag started.
bool g_dragging;
int g_dragStartX;
int g_dragStartOffset;
LARGE_INTEGER g_lastTick;  // time of the previous animation frame
// Auto-scroll while dragging icons: the edge the cursor is at (-1 left,
// 1 right, 0 none) and since when.
int g_edgeDir;
DWORD g_edgeSince;
bool g_edgeScrolling;  // moving, after the wait at the edge
bool g_edgeLogged;     // whether the window under the cursor was logged
int g_posOverrides; // times Explorer tried to move/resize the ListView
// Where the last button press on the ListView was, in screen coordinates.
// Explorer starts an icon drag only after the cursor has moved some way, so
// this, not the cursor at DoDragDrop, is where the drag began.
POINT g_buttonDownPt;
bool g_haveButtonDown;
std::vector<RECT> g_workAreas;  // Explorer's own work areas, one per monitor
bool g_settingWorkAreas;        // true while we send LVM_SETWORKAREAS
bool g_arrangeBlocked;  // off because "Auto arrange icons" is on
// Off because there is more than one monitor. Each monitor should scroll on
// its own (spec 3.7), which is not done yet, and it has not been tried with
// more than one monitor. Until then the mod stays off rather than risk the
// layout.
bool g_monitorsBlocked;
// Whether CDesktopBrowser::SetDesktopWorkAreas_Hook stretches the desktop
// work areas over the pages. Set before Explorer creates the desktop too.
bool g_stretchDesktop;
void ResetDesktopWorkAreas(HWND hList);
// Whether SetDesktopWorkAreas_Hook is in place. When it is, Explorer itself
// gives the ListView the stretched work areas, and the ListView work-area
// code below (the fallback) must stay out of the way: it would stretch them
// a second time.
bool HaveDesktopHook();

// Frame statistics for one animation, reported when it settles.
int g_frames;
LONGLONG g_frameTotalUs;
LONGLONG g_frameMaxUs;

UINT g_ctlMsg;
constexpr UINT_PTR kAnimTimer = 0x51D70001;
constexpr UINT_PTR kNewItemTimer = 0x51D70002;
constexpr UINT_PTR kIndicatorTimer = 0x51D70003;
constexpr UINT_PTR kRefreshTimer = 0x51D70004;
constexpr UINT_PTR kMonitorTimer = 0x51D70005;
constexpr int kHomeHotkey = 0x51D7;  // RegisterHotKey id, below 0xC000
enum : WPARAM {
    kActivate = 1,
    kDeactivate,
    kDeactivateAndGather,
    kDragStart,  // an OLE drag that starts in Explorer begins
    kDragTick,   // sent by the auto-scroll thread during the drag
    kDragEnd,
    kArrangeChanged,  // "Auto arrange icons" was turned on or off
};

void LoadSettings() {
    PCWSTR mode = Wh_GetStringSetting(L"scrollMode");
    g_settings.discrete = wcscmp(mode, L"discrete") == 0;
    Wh_FreeStringSetting(mode);
    g_settings.scrollStep = std::clamp(Wh_GetIntSetting(L"scrollStep"), 10, 2000);
    g_settings.glideMs = std::clamp(Wh_GetIntSetting(L"glide"), 0, 1000);
    g_settings.maxPages = std::clamp(Wh_GetIntSetting(L"maxPages"), 1, 10);
    PCWSTR home = Wh_GetStringSetting(L"homeAction");
    g_settings.homeAction = wcscmp(home, L"shortcut") == 0 ? g_settings.kHomeShortcut
                            : wcscmp(home, L"none") == 0   ? g_settings.kHomeNone
                                                           : g_settings.kHomeDoubleClick;
    Wh_FreeStringSetting(home);
    PCWSTR shortcut = Wh_GetStringSetting(L"homeShortcut");
    g_settings.homeShortcut = shortcut;
    Wh_FreeStringSetting(shortcut);
    g_settings.showIndicator = Wh_GetIntSetting(L"showIndicator") != 0;
    g_settings.newIconsInView = Wh_GetIntSetting(L"newIconsInView") != 0;
    PCWSTR wallpaper = Wh_GetStringSetting(L"wallpaper");
    g_settings.wallpaper = wcscmp(wallpaper, L"parallax") == 0 ? g_settings.kParallax
                           : wcscmp(wallpaper, L"wide") == 0   ? g_settings.kWide
                                                               : g_settings.kFixed;
    Wh_FreeStringSetting(wallpaper);
    PCWSTR wide = Wh_GetStringSetting(L"wideWallpaper");
    g_settings.wideWallpaper = wide;
    Wh_FreeStringSetting(wide);
    // Paths copied from Explorer come in quotes.
    std::wstring& path = g_settings.wideWallpaper;
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') {
        path = path.substr(1, path.size() - 2);
    }
    g_settings.dragAutoScroll = Wh_GetIntSetting(L"dragAutoScroll") != 0;
    g_settings.dragAutoScrollDelay =
        std::clamp(Wh_GetIntSetting(L"dragAutoScrollDelay"), 0, 5000);
    g_settings.dragAutoScrollSpeed =
        std::clamp(Wh_GetIntSetting(L"dragAutoScrollSpeed"), 100, 10000);
}

bool HasClass(HWND hWnd, PCWSTR className) {
    WCHAR buffer[64];
    return hWnd && GetClassName(hWnd, buffer, ARRAYSIZE(buffer)) &&
           wcscmp(buffer, className) == 0;
}

// Progman (or WorkerW) -> SHELLDLL_DefView -> SysListView32.
bool IsDesktopListView(HWND hWnd) {
    if (!HasClass(hWnd, WC_LISTVIEW)) {
        return false;
    }
    HWND defView = GetParent(hWnd);
    if (!HasClass(defView, L"SHELLDLL_DefView")) {
        return false;
    }
    HWND top = GetParent(defView);
    return HasClass(top, L"Progman") || HasClass(top, L"WorkerW");
}

// A named CALLBACK function, not a lambda: on x86 a lambda does not convert
// to the stdcall WNDENUMPROC.
BOOL CALLBACK FindDesktopListViewProc(HWND hWnd, LPARAM lParam) {
    DWORD pid;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid != GetCurrentProcessId() ||
        !(HasClass(hWnd, L"Progman") || HasClass(hWnd, L"WorkerW"))) {
        return TRUE;
    }
    HWND defView = FindWindowEx(hWnd, nullptr, L"SHELLDLL_DefView", nullptr);
    HWND list =
        defView ? FindWindowEx(defView, nullptr, WC_LISTVIEW, nullptr) : nullptr;
    if (!list) {
        return TRUE;
    }
    *(HWND*)lParam = list;
    return FALSE;
}

HWND FindDesktopListView() {
    HWND result = nullptr;
    EnumWindows(FindDesktopListViewProc, (LPARAM)&result);
    return result;
}

SIZE PageSize(HWND hList) {
    RECT rc{};
    GetClientRect(GetParent(hList), &rc);
    return {rc.right - rc.left, rc.bottom - rc.top};
}

int MaxOffset() {
    return g_pageWidth * g_settings.maxPages;
}

// The page edge nearest to offset, plus pages pages.
int PageEdge(double offset, int pages = 0) {
    if (g_pageWidth <= 0) {
        return 0;
    }
    int page = (int)std::lround(offset / g_pageWidth) + pages;
    return std::clamp(page, 0, g_settings.maxPages) * g_pageWidth;
}

// The home screen in ListView client coordinates, assuming offset 0. With a
// single monitor the taskbar area is excluded.
RECT HomeRect(HWND hList) {
    SIZE page = PageSize(hList);
    RECT rc{0, 0, page.cx, page.cy};
    if (GetSystemMetrics(SM_CMONITORS) == 1) {
        RECT workArea;
        if (SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0)) {
            MapWindowPoints(HWND_DESKTOP, hList, (POINT*)&workArea, 2);
            IntersectRect(&rc, &rc, &workArea);
        }
    }
    return rc;
}

void LogListViewInfo(HWND hList) {
    LONG style = GetWindowLong(hList, GWL_STYLE);
    DWORD exStyle = ListView_GetExtendedListViewStyle(hList);
    RECT client{};
    GetClientRect(hList, &client);
    SIZE page = PageSize(hList);
    DWORD spacing = ListView_GetItemSpacing(hList, FALSE);
    POINT origin{};
    ListView_GetOrigin(hList, &origin);
    WCHAR topClass[64] = L"";
    GetClassName(GetParent(GetParent(hList)), topClass, ARRAYSIZE(topClass));

    Wh_Log(L"ListView %p under %s: style=%08X lvExStyle=%08X view=%u",
           hList, topClass, style, exStyle, ListView_GetView(hList));
    Wh_Log(L"  autoArrange=%d noScroll=%d client=%dx%d page=%dx%d",
           !!(style & LVS_AUTOARRANGE), !!(style & LVS_NOSCROLL), client.right,
           client.bottom, page.cx, page.cy);
    Wh_Log(L"  items=%d spacing=%dx%d origin=(%d,%d) monitors=%d",
           ListView_GetItemCount(hList), LOWORD(spacing), HIWORD(spacing),
           origin.x, origin.y, GetSystemMetrics(SM_CMONITORS));
}

// Position indicator (spec 3.6): a thin bar at the bottom of the screen that
// shows which part of the desktop is in view. It shows while the desktop
// moves and fades out shortly after it stops. It is a layered popup owned by
// the desktop window, so it stays above the desktop and below other windows,
// and clicks go through it.
constexpr WCHAR kIndicatorClass[] = L"Sidetop_Indicator_" WH_MOD_ID;
constexpr int kIndicatorHoldMs = 500;  // fully shown after the desktop stops
constexpr int kIndicatorFadeMs = 250;
struct {
    HWND hWnd;
    int width, height;        // in pixels, for the current DPI
    int thumbX = -1, thumbW;  // last drawn thumb
    DWORD settledAt;          // when the desktop stopped; 0 while moving
} g_indicator;

HINSTANCE ModuleInstance() {
    HMODULE module = nullptr;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      (LPCWSTR)&ModuleInstance, &module);
    return module;
}

void DestroyIndicator(HWND hList) {
    KillTimer(hList, kIndicatorTimer);
    if (g_indicator.hWnd) {
        DestroyWindow(g_indicator.hWnd);
        g_indicator.hWnd = nullptr;
    }
}

// Draws the track and the thumb as bars with round ends, in premultiplied
// BGRA, and shows the window fully opaque.
void DrawIndicator(int thumbX, int thumbW) {
    int w = g_indicator.width, h = g_indicator.height;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap =
        CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);

    // How much of pixel (x, y) a bar from x0 to x1 covers.
    double r = h / 2.0;
    auto cover = [&](int x, int y, double x0, double x1) {
        double px = x + 0.5, py = y + 0.5;
        double cx = std::clamp(px, x0 + r, std::max(x1 - r, x0 + r));
        double d = std::hypot(px - cx, py - r);
        return std::clamp(r - d + 0.5, 0.0, 1.0);
    };
    DWORD* pixels = (DWORD*)bits;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // A white thumb over a faint gray track.
            double track = cover(x, y, 0, w) * 0.35;
            double thumb = cover(x, y, thumbX, thumbX + thumbW) * 0.9;
            double a = thumb + track * (1 - thumb);
            double white = thumb + track * 0.5 * (1 - thumb);
            DWORD A = (DWORD)std::lround(a * 255);
            DWORD C = (DWORD)std::lround(white * 255);
            pixels[y * w + x] = (A << 24) | (C << 16) | (C << 8) | C;
        }
    }

    POINT src{0, 0};
    SIZE size{w, h};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_indicator.hWnd, screen, nullptr, &size, dc, &src, 0,
                        &blend, ULW_ALPHA);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    g_indicator.thumbX = thumbX;
    g_indicator.thumbW = thumbW;
}

bool CreateIndicator(HWND hList) {
    WNDCLASS wc{};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = ModuleInstance();
    wc.lpszClassName = kIndicatorClass;
    if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Wh_Log(L"Indicator class not registered: %u", GetLastError());
        return false;
    }

    // Bottom center of the work area of the monitor the desktop is on.
    HWND parent = GetParent(hList);
    RECT page{};
    GetClientRect(parent, &page);
    MapWindowPoints(parent, HWND_DESKTOP, (POINT*)&page, 2);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(MonitorFromRect(&page, MONITOR_DEFAULTTOPRIMARY), &mi);
    UINT dpi = GetDpiForWindow(hList);
    if (!dpi) {
        dpi = 96;
    }
    g_indicator.width = MulDiv(160, dpi, 96);
    g_indicator.height = std::max(MulDiv(4, dpi, 96), 2);
    int x = (mi.rcWork.left + mi.rcWork.right - g_indicator.width) / 2;
    int y = mi.rcWork.bottom - MulDiv(12, dpi, 96) - g_indicator.height;

    g_indicator.hWnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kIndicatorClass, L"", WS_POPUP, x, y, g_indicator.width,
        g_indicator.height, GetAncestor(hList, GA_ROOT), nullptr,
        ModuleInstance(), nullptr);
    if (!g_indicator.hWnd) {
        Wh_Log(L"Indicator window not created: %u", GetLastError());
        return false;
    }
    g_indicator.thumbX = -1;
    return true;
}

// Called for each frame while the desktop moves.
void ShowIndicator(HWND hList) {
    if (!g_settings.showIndicator || MaxOffset() <= 0) {
        return;
    }
    if (!g_indicator.hWnd && !CreateIndicator(hList)) {
        return;
    }
    int w = g_indicator.width;
    int total = g_pageWidth + MaxOffset();
    int thumbW = std::max(MulDiv(w, g_pageWidth, total), g_indicator.height * 3);
    int thumbX = MulDiv(w - thumbW, std::clamp(g_applied, 0, MaxOffset()),
                        MaxOffset());
    bool fading = g_indicator.settledAt != 0;
    g_indicator.settledAt = 0;
    KillTimer(hList, kIndicatorTimer);
    if (fading || thumbX != g_indicator.thumbX ||
        thumbW != g_indicator.thumbW) {
        DrawIndicator(thumbX, thumbW);
    }
    if (!IsWindowVisible(g_indicator.hWnd)) {
        SetWindowPos(g_indicator.hWnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_SHOWWINDOW);
    }
}

// Called when the desktop stops: stay for a moment, then fade out.
void FadeIndicator(HWND hList) {
    if (!g_indicator.hWnd || !IsWindowVisible(g_indicator.hWnd)) {
        return;
    }
    g_indicator.settledAt = GetTickCount();
    SetTimer(hList, kIndicatorTimer, 16, nullptr);
}

void OnIndicatorTimer(HWND hList) {
    if (!g_indicator.hWnd || g_indicator.settledAt == 0) {
        KillTimer(hList, kIndicatorTimer);
        return;
    }
    int elapsed = (int)(GetTickCount() - g_indicator.settledAt);
    if (elapsed < kIndicatorHoldMs) {
        return;
    }
    int fade = elapsed - kIndicatorHoldMs;
    if (fade >= kIndicatorFadeMs) {
        // Made again next time, where the work area and DPI are then.
        g_indicator.settledAt = 0;
        DestroyIndicator(hList);
        return;
    }
    BLENDFUNCTION blend{AC_SRC_OVER, 0,
                        (BYTE)(255 - 255 * fade / kIndicatorFadeMs),
                        AC_SRC_ALPHA};
    UpdateLayeredWindow(g_indicator.hWnd, nullptr, nullptr, nullptr, nullptr,
                        nullptr, 0, &blend, ULW_ALPHA);
}

bool WallpaperMovesWithIcons();
void CheckDispInfo(HWND hList);
void ForgetDispInfo(HWND hList);

// Slides the wide ListView left by offset. The icon positions stay the same.
void ApplyOffset(HWND hList, int offset) {
    if (offset == g_applied) {
        return;
    }

    LARGE_INTEGER start, end, freq;
    QueryPerformanceCounter(&start);

    g_applied = offset;
    // The old pixels include the wallpaper behind the icons. When it stays
    // put (fixed, parallax), blitting them along smears it, so repaint the
    // whole ListView (SWP_NOCOPYBITS). When it moves with the icons (wide
    // image), let Windows blit them: only the strip that comes into view is
    // painted, about 5 ms a frame instead of 30 to 170.
    CheckDispInfo(hList);
    UINT copyBits = WallpaperMovesWithIcons() ? 0 : SWP_NOCOPYBITS;
    SetWindowPos(hList, nullptr, -offset, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | copyBits);
    UpdateWindow(hList);
    // Only while the user moves the desktop, not when it is put back on
    // start or when the mod stops.
    if (g_animating || g_dragging || g_edgeScrolling) {
        ShowIndicator(hList);
    }

    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&freq);
    LONGLONG us = (end.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart;
    g_frames++;
    g_frameTotalUs += us;
    g_frameMaxUs = std::max(g_frameMaxUs, us);
}

void StopAnimation(HWND hList) {
    KillTimer(hList, kAnimTimer);
    g_animating = false;
}

void StartAnimation(HWND hList) {
    if (g_animating) {
        return;
    }
    g_animating = true;
    QueryPerformanceCounter(&g_lastTick);
    SetTimer(hList, kAnimTimer, 10, nullptr);
}

// Called when a scroll or a drag ends. Remembers the position for the next
// start (spec 3.4) and reports the frame statistics.
void OnSettled() {
    Wh_SetIntValue(L"offset", g_applied);
    if (g_hList) {
        FadeIndicator(g_hList);
        ForgetDispInfo(g_hList);
    }

    if (g_frames > 0) {
        Wh_Log(L"Settled at %d: %d frames, avg %lld us, max %lld us", g_applied,
               g_frames, g_frameTotalUs / g_frames, g_frameMaxUs);
    }
    g_frames = 0;
    g_frameTotalUs = 0;
    g_frameMaxUs = 0;
}

// Glides toward the target by elapsed time, not by frame, so slow frames do
// not slow the motion down: the remaining distance shrinks by a factor of e
// every glideMs milliseconds.
void OnAnimTick(HWND hList) {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    double dt = (double)(now.QuadPart - g_lastTick.QuadPart) / freq.QuadPart;
    g_lastTick = now;

    double diff = g_target - g_offset;
    if (g_settings.glideMs > 0 && std::fabs(diff) >= 0.5) {
        g_offset += diff * (1 - std::exp(-dt * 1000 / g_settings.glideMs));
        ApplyOffset(hList, (int)std::lround(g_offset));
        return;
    }

    g_offset = g_target;
    ApplyOffset(hList, g_target);
    StopAnimation(hList);
    OnSettled();
}

void StartDrag(HWND hList) {
    POINT pt;
    GetCursorPos(&pt);
    StopAnimation(hList);
    g_offset = g_target = g_applied;
    g_dragStartX = pt.x;
    g_dragStartOffset = g_applied;
    g_dragging = true;
    SetCapture(hList);
}

// The page follows the cursor: dragging left shows what is on the right.
// Screen coordinates, since the ListView itself moves.
void OnDragMove(HWND hList) {
    POINT pt;
    GetCursorPos(&pt);
    int moved = pt.x - g_dragStartX;
    int offset = std::clamp(g_dragStartOffset - moved, 0, MaxOffset());
    g_offset = g_target = offset;
    ApplyOffset(hList, offset);
}

void EndDrag() {
    if (!g_dragging) {
        return;
    }
    g_dragging = false;
    if (g_settings.discrete && g_hList) {
        // Go on to the next page after a quarter of a page, else back.
        int start = PageEdge(g_dragStartOffset);
        int moved = g_applied - start;
        int pages = moved > g_pageWidth / 4    ? 1
                    : moved < -g_pageWidth / 4 ? -1
                                               : 0;
        g_target = PageEdge(start, pages);
        if (g_target != g_applied) {
            StartAnimation(g_hList);
            return;  // settles when the animation ends
        }
    }
    OnSettled();
}

void GoHome(HWND hList) {
    if (g_target != 0 && !g_dragging) {
        g_target = 0;
        StartAnimation(hList);
    }
}

// Double-clicking an empty spot goes back home; on an icon it stays native.
void OnDoubleClick(HWND hList, LPARAM lParam) {
    LVHITTESTINFO hit{};
    hit.pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (ListView_HitTest(hList, &hit) == -1) {
        GoHome(hList);
    }
}

// Reads a shortcut such as "Ctrl+Alt+Home". Returns false when it cannot.
bool ParseShortcut(const std::wstring& text, UINT* mods, UINT* vk) {
    static const struct {
        PCWSTR name;
        UINT vk;
    } kKeys[] = {
        {L"Home", VK_HOME},     {L"End", VK_END},
        {L"PageUp", VK_PRIOR},  {L"PageDown", VK_NEXT},
        {L"Insert", VK_INSERT}, {L"Delete", VK_DELETE},
        {L"Left", VK_LEFT},     {L"Right", VK_RIGHT},
        {L"Up", VK_UP},         {L"Down", VK_DOWN},
        {L"Space", VK_SPACE},   {L"Backspace", VK_BACK},
    };
    *mods = 0;
    *vk = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'+', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        std::wstring part = text.substr(start, end - start);
        part.erase(0, part.find_first_not_of(L' '));
        part.erase(part.find_last_not_of(L' ') + 1);
        start = end + 1;
        if (*vk || part.empty()) {
            return false;  // the key has to come last
        }
        PCWSTR name = part.c_str();
        int number = _wtoi(name + 1);
        if (_wcsicmp(name, L"Ctrl") == 0 || _wcsicmp(name, L"Control") == 0) {
            *mods |= MOD_CONTROL;
        } else if (_wcsicmp(name, L"Alt") == 0) {
            *mods |= MOD_ALT;
        } else if (_wcsicmp(name, L"Shift") == 0) {
            *mods |= MOD_SHIFT;
        } else if (_wcsicmp(name, L"Win") == 0) {
            *mods |= MOD_WIN;
        } else if (part.size() == 1 && iswascii(part[0]) && iswalnum(part[0])) {
            *vk = towupper(part[0]);  // 'A'..'Z' and '0'..'9' are their VKs
        } else if ((part[0] == L'F' || part[0] == L'f') && part.size() >= 2 &&
                   part.size() <= 3 &&
                   part.find_first_not_of(L"0123456789", 1) == std::wstring::npos &&
                   number >= 1 && number <= 24) {
            *vk = VK_F1 + number - 1;
        } else {
            for (const auto& key : kKeys) {
                if (_wcsicmp(name, key.name) == 0) {
                    *vk = key.vk;
                }
            }
            if (!*vk) {
                return false;
            }
        }
    }
    return *vk != 0;
}

// The shortcut comes to the ListView's thread as WM_HOTKEY. RegisterHotKey
// and UnregisterHotKey have to be called on that thread.
void RegisterHomeShortcut(HWND hList) {
    if (g_settings.homeAction != g_settings.kHomeShortcut) {
        return;
    }
    UINT mods, vk;
    if (!ParseShortcut(g_settings.homeShortcut, &mods, &vk)) {
        Wh_Log(L"WARNING: cannot read the home shortcut \"%s\"",
               g_settings.homeShortcut.c_str());
        return;
    }
    if (RegisterHotKey(hList, kHomeHotkey, mods | MOD_NOREPEAT, vk)) {
        Wh_Log(L"Home shortcut: %s", g_settings.homeShortcut.c_str());
    } else {
        Wh_Log(L"WARNING: cannot use the home shortcut %s (error %u); another "
               L"program may be using it",
               g_settings.homeShortcut.c_str(), GetLastError());
    }
}

// Whether the cursor is over the desktop, or over the drag image that
// follows the cursor during a drag.
bool IsOverDesktop(HWND hList, POINT pt) {
    HWND hit = WindowFromPoint(pt);
    HWND root = GetAncestor(hit, GA_ROOT);
    if (hit == hList || root == GetAncestor(hList, GA_ROOT) ||
        HasClass(root, L"SysDragImage")) {
        return true;
    }
    if (!g_edgeLogged) {
        WCHAR rootClass[64] = L"";
        GetClassName(root, rootClass, ARRAYSIZE(rootClass));
        Wh_Log(L"Auto-scroll: cursor at the edge is over %s, not the desktop",
               rootClass);
        g_edgeLogged = true;
    }
    return false;
}

void StopEdgeScroll() {
    if (g_edgeScrolling) {
        g_edgeScrolling = false;
        Wh_Log(L"Auto-scroll stopped");
        OnSettled();
    }
}

// Called about every 15 ms during an OLE drag (spec 3.4). Holding the cursor
// at the left or right edge of the screen for the set time scrolls in that
// direction until the cursor leaves the edge. The speed rises from 40% to the
// top speed over the first second, so a short hold moves a little and a long
// one goes far. Ticks also drive a wheel glide still under way, since the
// drag loop may not dispatch WM_TIMER.
void OnDragTick(HWND hList) {
    if (!g_active) {
        return;
    }
    DWORD now = GetTickCount();
    if (g_animating) {
        OnAnimTick(hList);
        if (!g_animating) {
            g_edgeSince = now;  // wait again before the next page
        }
        return;
    }

    POINT pt;
    RECT page;
    if (!GetCursorPos(&pt) || !GetWindowRect(GetParent(hList), &page)) {
        return;
    }
    // The cursor stops at the screen edge, so a narrow band is enough.
    constexpr int kEdge = 16;
    int dir = 0;
    if (pt.y >= page.top && pt.y < page.bottom) {
        if (pt.x < page.left + kEdge) {
            dir = -1;
        } else if (pt.x >= page.right - kEdge) {
            dir = 1;
        }
    }
    if (dir != 0 && !IsOverDesktop(hList, pt)) {
        dir = 0;
    }
    if (dir != g_edgeDir) {
        StopEdgeScroll();
        g_edgeDir = dir;
        g_edgeSince = now;
        return;
    }
    DWORD held = now - g_edgeSince;
    if (dir == 0 || held < (DWORD)g_settings.dragAutoScrollDelay) {
        return;
    }

    if (g_settings.discrete) {
        // One page per wait at the edge.
        int next = PageEdge(g_target, dir);
        if (next != g_target) {
            Wh_Log(L"Auto-scroll: %d -> %d", g_applied, next);
            g_target = next;
            StartAnimation(hList);
        }
        return;
    }

    LARGE_INTEGER tick, freq;
    QueryPerformanceCounter(&tick);
    QueryPerformanceFrequency(&freq);
    if (!g_edgeScrolling) {
        g_edgeScrolling = true;
        g_offset = g_applied;
        g_lastTick = tick;
        Wh_Log(L"Auto-scroll %s from %d", dir > 0 ? L"right" : L"left",
               g_applied);
        return;
    }
    // Slow frames make bigger steps, so the speed stays the same.
    double dt = std::min((double)(tick.QuadPart - g_lastTick.QuadPart) /
                             freq.QuadPart,
                         0.1);
    g_lastTick = tick;

    double ramp = std::min(
        (held - g_settings.dragAutoScrollDelay) / 1000.0, 1.0);
    double speed = g_settings.dragAutoScrollSpeed * (0.4 + 0.6 * ramp);
    g_offset = std::clamp(g_offset + dir * speed * dt, 0.0, (double)MaxOffset());
    g_target = (int)std::lround(g_offset);
    ApplyOffset(hList, g_target);
}

void EnsureExtendedWorkAreas(HWND hList);

void OnWheel(HWND hList, int delta, bool horizontal) {
    if (!HaveDesktopHook()) {
        EnsureExtendedWorkAreas(hList);
    }

    // Wheel up scrolls toward home; tilting right scrolls right.
    if (!horizontal) {
        delta = -delta;
    }

    if (g_settings.discrete) {
        // A page per notch. Touchpads send smaller amounts; add them up,
        // and start over after a pause or a change of direction.
        static int pending;
        static DWORD last;
        DWORD now = GetTickCount();
        if (now - last > 400 || (pending > 0) != (delta > 0)) {
            pending = 0;
        }
        last = now;
        pending += delta;
        int pages = pending / WHEEL_DELTA;
        if (pages == 0) {
            return;
        }
        pending -= pages * WHEEL_DELTA;
        g_target = PageEdge(g_target, pages);
        StartAnimation(hList);
        return;
    }

    int move = MulDiv(delta, g_settings.scrollStep, WHEEL_DELTA);
    g_target = std::clamp(g_target + move, 0, MaxOffset());
    StartAnimation(hList);
}

// Keep the ListView wide and slid left by the current offset, whatever
// position Explorer asks for.
void EnforceWindowPos(HWND hList, WINDOWPOS* pos) {
    bool overridden = false;
    if (!(pos->flags & SWP_NOMOVE) && pos->x != -g_applied) {
        pos->x = -g_applied;
        overridden = true;
    }
    if (!(pos->flags & SWP_NOSIZE)) {
        g_pageWidth = PageSize(hList).cx;
        int wide = g_pageWidth * (1 + g_settings.maxPages);
        if (pos->cx != wide) {
            pos->cx = wide;
            overridden = true;
        }
    }
    if (overridden && g_posOverrides++ < 20) {
        Wh_Log(L"Overrode a move/resize of the ListView: x=%d cx=%d flags=%X",
               pos->x, pos->cx, pos->flags);
    }
}

// The desktop ListView is owner-data (LVS_OWNERDATA): DefView keeps the icon
// positions and puts back any position set with LVM_SETITEMPOSITION on the
// next refresh. Lasting positions must go through DefView's IFolderView.
// Progman does not answer WM_GETISHELLBROWSER, so find the desktop browser
// through ShellWindows.
HRESULT GetDesktopFolderView(IFolderView** folderView, PCWSTR* step) {
    *folderView = nullptr;
    IShellWindows* windows = nullptr;
    IDispatch* dispatch = nullptr;
    IServiceProvider* services = nullptr;
    IShellBrowser* browser = nullptr;
    IShellView* view = nullptr;

    *step = L"CoCreateInstance(ShellWindows)";
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr,
                                  CLSCTX_LOCAL_SERVER | CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&windows));
    if (SUCCEEDED(hr)) {
        VARIANT location{};
        location.vt = VT_I4;
        location.lVal = CSIDL_DESKTOP;
        VARIANT empty{};
        long hwnd = 0;
        *step = L"FindWindowSW";
        hr = windows->FindWindowSW(&location, &empty, SWC_DESKTOP, &hwnd,
                                   SWFO_NEEDDISPATCH, &dispatch);
        if (hr == S_FALSE || (SUCCEEDED(hr) && !dispatch)) {
            hr = E_FAIL;
        }
    }
    if (SUCCEEDED(hr)) {
        *step = L"QueryInterface(IServiceProvider)";
        hr = dispatch->QueryInterface(IID_PPV_ARGS(&services));
    }
    if (SUCCEEDED(hr)) {
        *step = L"QueryService(SID_STopLevelBrowser)";
        hr = services->QueryService(SID_STopLevelBrowser,
                                    IID_PPV_ARGS(&browser));
    }
    if (SUCCEEDED(hr)) {
        *step = L"QueryActiveShellView";
        hr = browser->QueryActiveShellView(&view);
    }
    if (SUCCEEDED(hr)) {
        *step = L"QueryInterface(IFolderView)";
        hr = view->QueryInterface(IID_PPV_ARGS(folderView));
    }

    if (view) view->Release();
    if (browser) browser->Release();
    if (services) services->Release();
    if (dispatch) dispatch->Release();
    if (windows) windows->Release();
    return hr;
}

// Explorer writes "Auto arrange icons" to the registry only on a refresh or
// when it closes, and a restart from Task Manager can skip that. Write it
// now so that the next Explorer starts with the same setting.
void SaveDesktopViewState() {
    IFolderView* folderView = nullptr;
    IShellView* view = nullptr;
    PCWSTR step = L"";
    HRESULT hr = GetDesktopFolderView(&folderView, &step);
    if (SUCCEEDED(hr)) {
        step = L"QueryInterface(IShellView)";
        hr = folderView->QueryInterface(IID_PPV_ARGS(&view));
    }
    if (SUCCEEDED(hr)) {
        step = L"SaveViewState";
        hr = view->SaveViewState();
    }
    if (FAILED(hr)) {
        Wh_Log(L"Desktop view state not saved: %s failed (%08X)", step,
               (unsigned)hr);
    }
    if (view) view->Release();
    if (folderView) folderView->Release();
}

bool SetIconPosition(HWND hList, int index, POINT pt) {
    IFolderView* folderView = nullptr;
    PCWSTR step = L"";
    HRESULT hr = GetDesktopFolderView(&folderView, &step);
    if (SUCCEEDED(hr)) {
        PITEMID_CHILD pidl = nullptr;
        step = L"IFolderView::Item";
        hr = folderView->Item(index, &pidl);
        if (SUCCEEDED(hr)) {
            PCUITEMID_CHILD pidls[] = {pidl};
            step = L"SelectAndPositionItems";
            hr = folderView->SelectAndPositionItems(1, pidls, &pt,
                                                    SVSI_POSITIONITEM);
            POINT stored{};
            HRESULT hrGet = folderView->GetItemPosition(pidl, &stored);
            Wh_Log(L"Position of item %d: asked (%d,%d), hr=%08X; DefView "
                   L"has (%d,%d), hr=%08X",
                   index, pt.x, pt.y, (unsigned)hr, stored.x, stored.y,
                   (unsigned)hrGet);
            CoTaskMemFree(pidl);
        }
        folderView->Release();
    }
    if (FAILED(hr)) {
        Wh_Log(L"IFolderView positioning failed for item %d at %s "
               L"(hr=%08X); using the ListView",
               index, step, (unsigned)hr);
        ListView_SetItemPosition32(hList, index, pt.x, pt.y);
    }
    return SUCCEEDED(hr);
}

// Moves every icon that is not inside the home screen to a free grid spot on
// the home screen, so nothing is left where the user cannot see it.
int GatherToHome(HWND hList) {
    RECT home = HomeRect(hList);
    DWORD spacing = ListView_GetItemSpacing(hList, FALSE);
    int cellX = LOWORD(spacing);
    int cellY = HIWORD(spacing);
    if (cellX <= 0 || cellY <= 0) {
        Wh_Log(L"Gather: bad item spacing %08X", spacing);
        return 0;
    }

    struct Item {
        int index;
        POINT pos;
        RECT icon;
    };
    std::vector<POINT> taken;
    std::vector<Item> outside;
    int gridX = INT_MAX;
    int gridY = INT_MAX;

    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; i++) {
        Item item{i, {}, {}};
        if (!ListView_GetItemPosition(hList, i, &item.pos) ||
            !ListView_GetItemRect(hList, i, &item.icon, LVIR_ICON)) {
            continue;
        }
        RECT& r = item.icon;
        if (r.left >= home.left && r.right <= home.right && r.top >= home.top &&
            r.bottom <= home.bottom) {
            taken.push_back(item.pos);
            gridX = std::min(gridX, (int)item.pos.x);
            gridY = std::min(gridY, (int)item.pos.y);
        } else {
            outside.push_back(item);
        }
    }

    if (outside.empty()) {
        return 0;
    }

    // Align candidate spots with the grid the existing icons sit on.
    if (gridX == INT_MAX) {
        gridX = home.left;
        gridY = home.top;
    }
    while (gridX - cellX >= home.left) {
        gridX -= cellX;
    }
    while (gridY - cellY >= home.top) {
        gridY -= cellY;
    }

    // Keep the page order: leftmost icons first, top to bottom.
    std::sort(outside.begin(), outside.end(), [](const Item& a, const Item& b) {
        return a.pos.x != b.pos.x ? a.pos.x < b.pos.x : a.pos.y < b.pos.y;
    });

    auto isFree = [&](int x, int y) {
        return std::none_of(taken.begin(), taken.end(), [&](POINT p) {
            return std::abs(p.x - x) < cellX * 3 / 4 &&
                   std::abs(p.y - y) < cellY * 3 / 4;
        });
    };

    int notPlaced = 0;
    for (const Item& item : outside) {
        // Offset from the item position to its icon rectangle.
        int dx = item.icon.left - item.pos.x;
        int dy = item.icon.top - item.pos.y;
        int w = item.icon.right - item.icon.left;
        int h = item.icon.bottom - item.icon.top;

        POINT spot{gridX, gridY};
        bool found = false;
        // Column by column, like the desktop fills new icons.
        for (int x = gridX; !found && x + dx + w <= home.right; x += cellX) {
            for (int y = gridY; y + dy + h <= home.bottom; y += cellY) {
                if (isFree(x, y)) {
                    spot = {x, y};
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            notPlaced++;
        }
        SetIconPosition(hList, item.index, spot);
        taken.push_back(spot);
    }

    Wh_Log(L"Gather: moved %d icon(s) to home, %d without a free spot",
           (int)outside.size(), notPlaced);
    return (int)outside.size();
}

// New icons on the screen in view (spec 3.4). Explorer puts a new desktop
// item (a download, a pasted file) at the first free spot from the top left
// of home. Check the number of icons every half second, and move a new item
// that is not on the screen in view to a free spot there. Items dropped or
// created on the screen in view are already there and stay where they are.
// Shell change notifications are not used: for a file written with plain
// CreateFile, none arrived (2026-10-30), only when the program also called
// SHChangeNotify. DefView shows such files all the same.
struct {
    bool running;
    int count;                       // icons at the last check
    std::vector<std::wstring> known; // names seen; new ones are not in it
    DWORD unsettledUntil;            // after a refresh or a delete
    bool prune;                      // drop deleted names once settled
} g_newItems;

// Names of the desktop items in ListView order, or false.
bool GetItemNames(std::vector<std::wstring>* names) {
    names->clear();
    IFolderView* folderView = nullptr;
    IShellFolder* folder = nullptr;
    PCWSTR step = L"";
    bool ok = SUCCEEDED(GetDesktopFolderView(&folderView, &step)) &&
              SUCCEEDED(folderView->GetFolder(IID_PPV_ARGS(&folder)));
    int count = 0;
    if (ok) {
        ok = SUCCEEDED(folderView->ItemCount(SVGIO_ALLVIEW, &count));
    }
    for (int i = 0; ok && i < count; i++) {
        PITEMID_CHILD pidl = nullptr;
        STRRET str{};
        WCHAR name[MAX_PATH] = L"";
        ok = SUCCEEDED(folderView->Item(i, &pidl)) &&
             SUCCEEDED(folder->GetDisplayNameOf(
                 pidl, SHGDN_INFOLDER | SHGDN_FORPARSING, &str)) &&
             SUCCEEDED(StrRetToBufW(&str, pidl, name, ARRAYSIZE(name)));
        CoTaskMemFree(pidl);
        names->push_back(name);
    }
    if (folder) folder->Release();
    if (folderView) folderView->Release();
    return ok;
}

bool IsKnownName(const std::wstring& name) {
    return std::any_of(g_newItems.known.begin(), g_newItems.known.end(),
                       [&](const std::wstring& n) {
                           return _wcsicmp(n.c_str(), name.c_str()) == 0;
                       });
}

void StartNewItemWatch(HWND hList) {
    if (g_newItems.running || !g_settings.newIconsInView) {
        return;
    }
    g_newItems.running = true;
    g_newItems.count = ListView_GetItemCount(hList);
    if (!GetItemNames(&g_newItems.known)) {
        g_newItems.count = -1;  // take a fresh look at the next check
    }
    g_newItems.unsettledUntil = 0;
    g_newItems.prune = false;
    SetTimer(hList, kNewItemTimer, 500, nullptr);
    Wh_Log(L"Watching for new icons (%d now)", g_newItems.count);
}

void StopNewItemWatch(HWND hList) {
    KillTimer(hList, kNewItemTimer);
    g_newItems.running = false;
    g_newItems.known.clear();
}

// A free grid spot inside screen, column by column like Explorer fills new
// icons, or false if there is none.
bool FindFreeSpot(HWND hList, const RECT& screen, int skip, POINT* spot) {
    DWORD spacing = ListView_GetItemSpacing(hList, FALSE);
    int cellX = LOWORD(spacing);
    int cellY = HIWORD(spacing);
    RECT icon;
    POINT pos;
    if (cellX <= 0 || cellY <= 0 ||
        !ListView_GetItemRect(hList, skip, &icon, LVIR_ICON) ||
        !ListView_GetItemPosition(hList, skip, &pos)) {
        return false;
    }
    // Offset from the item position to its icon rectangle.
    int dx = icon.left - pos.x;
    int dy = icon.top - pos.y;
    int w = icon.right - icon.left;
    int h = icon.bottom - icon.top;

    // Align with the grid the other icons sit on.
    std::vector<POINT> taken;
    int gridX = INT_MAX;
    int gridY = INT_MAX;
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; i++) {
        POINT p;
        if (i != skip && ListView_GetItemPosition(hList, i, &p)) {
            taken.push_back(p);
            gridX = std::min(gridX, (int)p.x);
            gridY = std::min(gridY, (int)p.y);
        }
    }
    if (gridX == INT_MAX) {
        gridX = screen.left;
        gridY = screen.top;
    }
    auto firstOnGrid = [](int grid, int from, int cell) {
        int steps = (from - grid + cell - 1) / cell;
        if (from - grid < 0) {
            steps = -((grid - from) / cell);
        }
        return grid + steps * cell;
    };
    int startX = firstOnGrid(gridX, screen.left - dx, cellX);
    int startY = firstOnGrid(gridY, screen.top - dy, cellY);

    for (int x = startX; x + dx + w <= screen.right; x += cellX) {
        for (int y = startY; y + dy + h <= screen.bottom; y += cellY) {
            bool free = std::none_of(taken.begin(), taken.end(), [&](POINT p) {
                return std::abs(p.x - x) < cellX * 3 / 4 &&
                       std::abs(p.y - y) < cellY * 3 / 4;
            });
            if (free) {
                *spot = {x, y};
                return true;
            }
        }
    }
    return false;
}

// The screen in view in ListView client coordinates, at the current offset.
// With a single monitor the taskbar area is excluded.
RECT ScreenInView(HWND hList) {
    HWND parent = GetParent(hList);
    RECT rc{};
    GetClientRect(parent, &rc);
    MapWindowPoints(parent, hList, (POINT*)&rc, 2);
    if (GetSystemMetrics(SM_CMONITORS) == 1) {
        RECT workArea;
        if (SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0)) {
            MapWindowPoints(HWND_DESKTOP, hList, (POINT*)&workArea, 2);
            IntersectRect(&rc, &rc, &workArea);
        }
    }
    return rc;
}

// Moves one new item onto the screen in view, if it is not there.
void PlaceNewItem(HWND hList, int index, PCWSTR name) {
    RECT screen = ScreenInView(hList);
    RECT icon;
    POINT pos{};
    if (!ListView_GetItemRect(hList, index, &icon, LVIR_ICON)) {
        return;
    }
    ListView_GetItemPosition(hList, index, &pos);
    if (icon.left >= screen.left && icon.right <= screen.right &&
        icon.top >= screen.top && icon.bottom <= screen.bottom) {
        Wh_Log(L"New item %s at (%d,%d) is in view", name, pos.x, pos.y);
        return;
    }
    POINT spot;
    if (!FindFreeSpot(hList, screen, index, &spot)) {
        Wh_Log(L"New item %s at (%d,%d): no free spot in view", name, pos.x,
               pos.y);
        return;
    }
    Wh_Log(L"New item %s: (%d,%d) -> (%d,%d)", name, pos.x, pos.y, spot.x,
           spot.y);
    SetIconPosition(hList, index, spot);
}

void OnNewItemTimer(HWND hList) {
    if (!g_active || g_edgeScrolling) {
        return;
    }
    DWORD now = GetTickCount();
    int count = ListView_GetItemCount(hList);
    if (count == g_newItems.count) {
        if (g_newItems.prune && (LONG)(now - g_newItems.unsettledUntil) >= 0) {
            std::vector<std::wstring> names;
            if (GetItemNames(&names) && (int)names.size() == count) {
                g_newItems.known = names;
                g_newItems.prune = false;
            }
        }
        return;
    }

    std::vector<std::wstring> names;
    if (!GetItemNames(&names) || (int)names.size() != count) {
        return;  // DefView is busy; look again next time
    }

    // A refresh empties and refills the view, and the last items to come
    // back would look new. Move icons only for a small increase while
    // nothing else is going on; otherwise just remember the names.
    int added = count - g_newItems.count;
    bool settled = g_newItems.count > 0 &&
                   (LONG)(now - g_newItems.unsettledUntil) >= 0;
    if (added < 1 || added > 3 || !settled) {
        if (added < 0 || added > 3 || g_newItems.count <= 0) {
            g_newItems.unsettledUntil = now + 2000;
        }
        Wh_Log(L"Icons: %d -> %d, not placing", g_newItems.count, count);
    } else {
        for (int i = 0; i < count; i++) {
            if (!IsKnownName(names[i])) {
                PlaceNewItem(hList, i, names[i].c_str());
            }
        }
    }

    for (const std::wstring& name : names) {
        if (!IsKnownName(name)) {
            g_newItems.known.push_back(name);
        }
    }
    g_newItems.prune = true;
    g_newItems.count = count;
}

void LoadWorkAreas(HWND hList) {
    UINT count = 0;
    ListView_GetNumberOfWorkAreas(hList, &count);
    g_workAreas.resize(count);
    if (count) {
        ListView_GetWorkAreas(hList, count, g_workAreas.data());
    }
    for (const RECT& rc : g_workAreas) {
        Wh_Log(L"  work area (%d,%d)-(%d,%d)", rc.left, rc.top, rc.right,
               rc.bottom);
    }
}

void SendWorkAreas(HWND hList, const std::vector<RECT>& areas) {
    g_settingWorkAreas = true;
    ListView_SetWorkAreas(hList, (int)areas.size(),
                          areas.empty() ? nullptr : (RECT*)areas.data());
    g_settingWorkAreas = false;
}

// Explorer confines icon drops and snapping to the ListView work areas, so
// stretch the rightmost ones over the added pages. The number of work areas
// must stay the same: adding one per page crashed comctl32 (AV at
// COMCTL32.dll+0x8ee43 on 10.0.19041.6456), most likely because Explorer
// keeps one work area per monitor.
// Always sends work areas, Explorer's own ones when they cannot be extended.
void SetExtendedWorkAreas(HWND hList) {
    if (g_workAreas.empty() || g_pageWidth <= 0) {
        Wh_Log(L"Work areas not extended (count=%d, page width=%d)",
               (int)g_workAreas.size(), g_pageWidth);
        SendWorkAreas(hList, g_workAreas);
        return;
    }
    LONG rightmost = LONG_MIN;
    for (const RECT& rc : g_workAreas) {
        rightmost = std::max(rightmost, rc.right);
    }
    std::vector<RECT> areas = g_workAreas;
    for (RECT& rc : areas) {
        if (rc.right == rightmost) {
            rc.right += g_pageWidth * g_settings.maxPages;
        }
    }
    SendWorkAreas(hList, areas);
    Wh_Log(L"Work areas: right edge %d -> %d", rightmost,
           rightmost + g_pageWidth * g_settings.maxPages);
}

// Explorer also resets the work areas without LVM_SETWORKAREAS (probably
// through the ListView's COM interface), so the subclass never sees it. Check before each scroll and extend again if they were reset.
void EnsureExtendedWorkAreas(HWND hList) {
    UINT count = 0;
    ListView_GetNumberOfWorkAreas(hList, &count);
    if (count == 0) {
        return;
    }
    std::vector<RECT> current(count);
    ListView_GetWorkAreas(hList, count, current.data());

    LONG rightmost = LONG_MIN;
    for (const RECT& rc : current) {
        rightmost = std::max(rightmost, rc.right);
    }
    if (rightmost > g_pageWidth) {
        return;  // still extended
    }

    Wh_Log(L"Work areas were reset (right edge %d); extending again",
           rightmost);
    g_workAreas = current;
    SetExtendedWorkAreas(hList);
}

// Moving wallpaper (spec 3.5). The ListView has no background of its own
// (LVS_EX_TRANSPARENTBKGND): comctl32's CLVBackgroundManager::
// DrawSimpleBackground moves the DC origin to DefView's coordinates and
// sends DefView WM_PRINTCLIENT with PRF_ERASEBKGND, or WM_ERASEBKGND if that
// returns 0. The wallpaper painted there is lined up with the screen, so it
// stays put while the ListView slides. For the parallax and wide modes,
// DefViewSubclassProc paints a picture of our own instead, shifted by the
// offset. The picture is in "desktop" coordinates: x is the screen x plus
// how far it has moved.
constexpr double kParallaxShare = 0.2;  // parallax moves 20% of the icons
struct {
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ oldBitmap;
    int width, height;
    std::wstring source;   // the image file it was made from
    FILETIME sourceTime;   // its last write time
    DWORD checkedAt;       // when the current wallpaper was last checked
} g_wall;

void FreeWallpaper() {
    if (g_wall.dc) {
        SelectObject(g_wall.dc, g_wall.oldBitmap);
        DeleteDC(g_wall.dc);
    }
    if (g_wall.bitmap) {
        DeleteObject(g_wall.bitmap);
    }
    g_wall.dc = nullptr;
    g_wall.bitmap = nullptr;
    g_wall.source.clear();
}

FILETIME FileTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &data);
    return data.ftLastWriteTime;
}

// The wallpaper Windows shows now. For a slideshow the file stays the same
// and its contents change.
std::wstring CurrentWallpaperPath() {
    WCHAR path[MAX_PATH] = L"";
    SystemParametersInfo(SPI_GETDESKWALLPAPER, ARRAYSIZE(path), path, 0);
    return path;
}

// Loads path scaled to cover width x height (cut evenly at the sides or at
// the top and bottom), as a bitmap selected into g_wall.dc.
bool LoadWallpaper(const std::wstring& path, int width, int height) {
    FreeWallpaper();
    if (path.empty() || width <= 0 || height <= 0) {
        return false;
    }
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICBitmapClipper* clipper = nullptr;
    IWICFormatConverter* converter = nullptr;
    PCWSTR step = L"CoCreateInstance(WICImagingFactory)";
    UINT imageW = 0, imageH = 0, scaledW = 0, scaledH = 0;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        step = L"CreateDecoderFromFilename";
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
                                                GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand,
                                                &decoder);
    }
    if (SUCCEEDED(hr)) {
        step = L"GetFrame";
        hr = decoder->GetFrame(0, &frame);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->GetSize(&imageW, &imageH);
        if (SUCCEEDED(hr) && (!imageW || !imageH)) {
            hr = E_FAIL;
        }
    }
    if (SUCCEEDED(hr)) {
        double scale = std::max((double)width / imageW, (double)height / imageH);
        scaledW = std::max((UINT)width, (UINT)std::lround(imageW * scale));
        scaledH = std::max((UINT)height, (UINT)std::lround(imageH * scale));
        step = L"Scaler";
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr)) {
            hr = scaler->Initialize(frame, scaledW, scaledH,
                                    WICBitmapInterpolationModeFant);
        }
    }
    if (SUCCEEDED(hr)) {
        step = L"Clipper";
        hr = factory->CreateBitmapClipper(&clipper);
        if (SUCCEEDED(hr)) {
            WICRect rc{(INT)(scaledW - width) / 2, (INT)(scaledH - height) / 2,
                       width, height};
            hr = clipper->Initialize(scaler, &rc);
        }
    }
    if (SUCCEEDED(hr)) {
        step = L"FormatConverter";
        hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr)) {
            hr = converter->Initialize(clipper, GUID_WICPixelFormat32bppBGR,
                                       WICBitmapDitherTypeNone, nullptr, 0,
                                       WICBitmapPaletteTypeCustom);
        }
    }
    if (SUCCEEDED(hr)) {
        step = L"CreateDIBSection";
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        g_wall.bitmap =
            CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        hr = g_wall.bitmap ? S_OK : E_OUTOFMEMORY;
        if (SUCCEEDED(hr)) {
            step = L"CopyPixels";
            hr = converter->CopyPixels(nullptr, width * 4, width * 4 * height,
                                       (BYTE*)bits);
        }
    }

    if (converter) converter->Release();
    if (clipper) clipper->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();

    if (FAILED(hr)) {
        Wh_Log(L"Wallpaper %s not loaded: %s failed (%08X)", path.c_str(), step,
               (unsigned)hr);
        if (g_wall.bitmap) {
            DeleteObject(g_wall.bitmap);
            g_wall.bitmap = nullptr;
        }
        return false;
    }
    g_wall.dc = CreateCompatibleDC(nullptr);
    g_wall.oldBitmap = SelectObject(g_wall.dc, g_wall.bitmap);
    g_wall.width = width;
    g_wall.height = height;
    g_wall.source = path;
    g_wall.sourceTime = FileTime(path);
    g_wall.checkedAt = GetTickCount();
    Wh_Log(L"Wallpaper %s: %ux%u -> %dx%d", path.c_str(), imageW, imageH, width,
           height);
    return true;
}

// Makes the picture for the current mode and size. Returns whether there is
// one to paint.
bool PrepareWallpaper(HWND hList) {
    if (g_settings.wallpaper == g_settings.kFixed) {
        FreeWallpaper();
        return false;
    }
    SIZE page = PageSize(hList);
    std::wstring path;
    int width;
    if (g_settings.wallpaper == g_settings.kWide) {
        path = g_settings.wideWallpaper;
        width = page.cx + MaxOffset();
    } else {
        path = CurrentWallpaperPath();
        width = page.cx + (int)std::lround(MaxOffset() * kParallaxShare);
    }
    FILETIME time = FileTime(path);
    if (g_wall.dc && g_wall.source == path && g_wall.width == width &&
        g_wall.height == page.cy &&
        CompareFileTime(&time, &g_wall.sourceTime) == 0) {
        return true;
    }
    if (path.empty()) {
        Wh_Log(L"Wallpaper: no image to move (%s)",
               g_settings.wallpaper == g_settings.kWide
                   ? L"set the wide image file"
                   : L"the wallpaper is a solid color");
        FreeWallpaper();
        return false;
    }
    return LoadWallpaper(path, width, page.cy);
}

// For the parallax mode: picks up a new wallpaper (or the next slideshow
// picture) at most every 2 seconds.
void CheckWallpaperChanged(HWND hList) {
    if (g_settings.wallpaper != g_settings.kParallax ||
        GetTickCount() - g_wall.checkedAt < 2000) {
        return;
    }
    g_wall.checkedAt = GetTickCount();
    std::wstring path = CurrentWallpaperPath();
    FILETIME time = FileTime(path);
    if (path != g_wall.source || CompareFileTime(&time, &g_wall.sourceTime)) {
        PrepareWallpaper(hList);
        InvalidateRect(hList, nullptr, TRUE);
    }
}

bool WallpaperMovesWithIcons() {
    return g_settings.wallpaper == g_settings.kWide && g_wall.dc;
}

// How far the picture has moved at the current offset.
int WallpaperShift() {
    return g_settings.wallpaper == g_settings.kWide
               ? g_applied
               : (int)std::lround(g_applied * kParallaxShare);
}

// Paints the moved picture for hWnd (the desktop ListView or its parent)
// into hdc, which is in hWnd's client coordinates. Returns false when
// Explorer should paint the wallpaper as usual.
bool PaintMovedWallpaper(HWND hWnd, HDC hdc, const RECT* area, PCWSTR via) {
    HWND hList = g_hList;
    if (!hList || !g_active ||
        GetWindowThreadProcessId(hList, nullptr) != GetCurrentThreadId() ||
        (hWnd != hList && hWnd != GetParent(hList))) {
        return false;
    }
    static bool logged;
    if (!logged) {
        logged = true;
        WCHAR cls[64] = L"";
        GetClassName(hWnd, cls, ARRAYSIZE(cls));
        Wh_Log(L"Wallpaper behind %s is painted through %s", cls, via);
    }
    CheckWallpaperChanged(hList);
    if (!g_wall.dc) {
        return false;
    }

    RECT clip;
    if (GetClipBox(hdc, &clip) == ERROR) {
        GetClientRect(hWnd, &clip);
    }
    if (area && !IntersectRect(&clip, &clip, area)) {
        return true;
    }
    POINT origin{0, 0};
    MapWindowPoints(hWnd, HWND_DESKTOP, &origin, 1);
    RECT parentScreen{};
    GetWindowRect(GetParent(hList), &parentScreen);
    int srcX = clip.left + origin.x - parentScreen.left + WallpaperShift();
    int srcY = clip.top + origin.y - parentScreen.top;
    BitBlt(hdc, clip.left, clip.top, clip.right - clip.left,
           clip.bottom - clip.top, g_wall.dc, srcX, srcY, SRCCOPY);
    return true;
}

HWND g_hDefView;  // the ListView's parent, subclassed for the wallpaper

// Answers DefView gave to LVN_GETDISPINFOW while the desktop moves. The
// ListView is owner-data, so for every visible item on every frame it asks
// DefView twice (the label to measure, then label, icon and overlay), and
// each answer takes DefView about a millisecond. With 50 icons on the screen
// one frame took 170 ms, 92 of them there (tools/lvprof.cpp). Names and
// icons do not change in the second a scroll takes, so the answers are kept
// until the desktop stops, and then the screen is painted once more with
// fresh ones in case something did change.
struct DispInfoAnswer {
    int item;
    UINT asked;  // the mask the ListView asked with
    UINT mask;   // the mask DefView answered with
    int image;
    UINT state, stateMask;
    int indent;
    std::wstring text;
};
struct {
    std::vector<DispInfoAnswer> answers;
    int itemCount;  // icons when the answers were collected
    bool used;      // whether a kept answer was given since the last refresh
} g_dispInfo;

constexpr UINT kDispInfoMasks = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE | LVIF_INDENT;

bool IsMoving() {
    return g_animating || g_dragging || g_edgeScrolling;
}

// Fills di from a kept answer. Returns false when there is none.
bool AnswerDispInfo(NMLVDISPINFOW* di) {
    LVITEMW& item = di->item;
    if (item.iSubItem != 0 || (item.mask & ~kDispInfoMasks)) {
        return false;
    }
    for (const DispInfoAnswer& answer : g_dispInfo.answers) {
        if (answer.item != item.iItem || answer.asked != item.mask) {
            continue;
        }
        if (answer.mask & LVIF_IMAGE) {
            item.iImage = answer.image;
        }
        if (answer.mask & LVIF_STATE) {
            item.state = (item.state & ~answer.stateMask) |
                         (answer.state & answer.stateMask);
            item.stateMask = answer.stateMask;
        }
        if (answer.mask & LVIF_INDENT) {
            item.iIndent = answer.indent;
        }
        if ((answer.mask & LVIF_TEXT) && item.pszText && item.cchTextMax > 0) {
            lstrcpynW(item.pszText, answer.text.c_str(), item.cchTextMax);
        }
        item.mask = answer.mask;
        g_dispInfo.used = true;
        return true;
    }
    return false;
}

void RememberDispInfo(const NMLVDISPINFOW* di, UINT asked) {
    const LVITEMW& item = di->item;
    if (item.iSubItem != 0 || (asked & ~kDispInfoMasks) ||
        g_dispInfo.answers.size() >= 2000) {
        return;
    }
    if (g_dispInfo.answers.empty()) {
        g_dispInfo.itemCount = ListView_GetItemCount(g_hList);
    }
    DispInfoAnswer answer{item.iItem,     asked,          item.mask,
                          item.iImage,    item.state,     item.stateMask,
                          item.iIndent,   {}};
    if ((item.mask & LVIF_TEXT) && item.pszText &&
        item.pszText != LPSTR_TEXTCALLBACKW) {
        answer.text = item.pszText;
    }
    g_dispInfo.answers.push_back(std::move(answer));
}

// Once a frame, before the ListView paints: when icons came or went, the
// item indexes moved and the kept answers are wrong. (Not on every answer:
// LVM_GETITEMCOUNT goes through the ListView's subclasses, and the window
// manager can be slow at that.)
void CheckDispInfo(HWND hList) {
    if (!g_dispInfo.answers.empty() &&
        ListView_GetItemCount(hList) != g_dispInfo.itemCount) {
        g_dispInfo.answers.clear();
    }
}

// When the desktop stops, or the mod stops.
void ForgetDispInfo(HWND hList) {
    g_dispInfo.answers.clear();
    g_dispInfo.itemCount = -1;
    if (g_dispInfo.used) {
        g_dispInfo.used = false;
        // Paint once more with fresh answers, a little later so that it
        // does not hold up what the user does right after stopping.
        SetTimer(hList, kRefreshTimer, 300, nullptr);
    }
}

void OnRefreshTimer(HWND hList) {
    KillTimer(hList, kRefreshTimer);
    if (!IsMoving()) {
        InvalidateRect(hList, nullptr, TRUE);
    }
}

LRESULT CALLBACK DefViewSubclassProc(HWND hWnd,
                                     UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_PRINTCLIENT:
            // Only the background request from the ListView. A full print
            // (PrintWindow and the like) also wants the client painted.
            if (lParam == PRF_ERASEBKGND &&
                PaintMovedWallpaper(hWnd, (HDC)wParam, nullptr,
                                    L"WM_PRINTCLIENT")) {
                return 1;
            }
            break;

        case WM_ERASEBKGND:
            if (PaintMovedWallpaper(hWnd, (HDC)wParam, nullptr,
                                    L"WM_ERASEBKGND")) {
                return 1;
            }
            break;

        case WM_NOTIFY: {
            auto* hdr = (NMHDR*)lParam;
            HWND hList = g_hList;
            if (hList && hdr->hwndFrom == hList &&
                hdr->code == LVN_GETDISPINFOW && g_active && IsMoving() &&
                GetWindowThreadProcessId(hList, nullptr) ==
                    GetCurrentThreadId()) {
                auto* di = (NMLVDISPINFOW*)lParam;
                if (AnswerDispInfo(di)) {
                    return 0;
                }
                UINT asked = di->item.mask;
                LRESULT result =
                    DefSubclassProc(hWnd, uMsg, wParam, lParam);
                RememberDispInfo(di, asked);
                return result;
            }
            break;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Explorer's own wallpaper painting, used for some of the desktop.
using PaintWallpaper_t = HRESULT(WINAPI*)(void* pThis, HWND hWnd, HDC hdc);
PaintWallpaper_t PaintWallpaper_Original;
HRESULT WINAPI PaintWallpaper_Hook(void* pThis, HWND hWnd, HDC hdc) {
    if (PaintMovedWallpaper(hWnd, hdc, nullptr,
                            L"CDesktopBrowser::PaintWallpaper")) {
        return S_OK;
    }
    return PaintWallpaper_Original(pThis, hWnd, hdc);
}

void Activate(HWND hList) {
    LoadSettings();
    LogListViewInfo(hList);

    // Auto arrange packs the icons from the top left, so the pages would
    // only get in the way. Keep the work areas as they are: Explorer keeps
    // a separate icon layout for each work area size, and changing it here
    // brings back an old layout when auto arrange is turned off again.
    // Checks the number of monitors from now on, also while off.
    SetTimer(hList, kMonitorTimer, 1000, nullptr);
    g_monitorsBlocked = false;

    g_arrangeBlocked = GetWindowLong(hList, GWL_STYLE) & LVS_AUTOARRANGE;
    if (g_arrangeBlocked) {
        Wh_Log(L"WARNING: \"Auto arrange icons\" is on. Sidetop stays off.");
        return;
    }

    if (GetSystemMetrics(SM_CMONITORS) > 1) {
        g_monitorsBlocked = true;
        Wh_Log(L"WARNING: %d monitors. Sidetop supports only one so far and "
               L"stays off.",
               GetSystemMetrics(SM_CMONITORS));
        return;
    }

    g_pageWidth = PageSize(hList).cx;
    g_offset = 0;
    g_target = 0;
    g_applied = 0;
    g_posOverrides = 0;
    g_active = true;

    SIZE page = PageSize(hList);
    // EnforceWindowPos widens this to the full width.
    SetWindowPos(hList, nullptr, 0, 0, page.cx, page.cy,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (!HaveDesktopHook()) {
        LoadWorkAreas(hList);
        SetExtendedWorkAreas(hList);
    }
    // Also covers a changed number of pages.
    g_stretchDesktop = true;
    ResetDesktopWorkAreas(hList);

    // Back to where the user was before Explorer restarted.
    int saved = std::clamp(Wh_GetIntValue(L"offset", 0), 0, MaxOffset());
    if (g_settings.discrete) {
        saved = PageEdge(saved);
    }
    g_offset = g_target = saved;
    ApplyOffset(hList, saved);

    RECT client{};
    GetClientRect(hList, &client);
    Wh_Log(L"Active: page=%d maxOffset=%d offset=%d client=%dx%d", g_pageWidth,
           MaxOffset(), saved, client.right, client.bottom);

    StartNewItemWatch(hList);
    RegisterHomeShortcut(hList);

    if (PrepareWallpaper(hList) || g_settings.wallpaper != g_settings.kFixed) {
        InvalidateRect(hList, nullptr, TRUE);
    }
}

void Deactivate(HWND hList, bool gather);

// A monitor was added: put every icon back on a screen and stay off. The
// last one but one was removed: start again.
void OnMonitorTimer(HWND hList) {
    int count = GetSystemMetrics(SM_CMONITORS);
    if (count > 1 && g_active) {
        Wh_Log(L"WARNING: %d monitors now. Sidetop stops.", count);
        Deactivate(hList, true);
        g_monitorsBlocked = true;
    } else if (count == 1 && g_monitorsBlocked) {
        Wh_Log(L"One monitor again. Sidetop starts.");
        Activate(hList);
    }
}

void Deactivate(HWND hList, bool gather) {
    if (!g_active) {
        return;
    }

    if (g_dragging) {
        g_dragging = false;
        ReleaseCapture();
    }
    g_edgeScrolling = false;
    UnregisterHotKey(hList, kHomeHotkey);
    StopNewItemWatch(hList);
    StopAnimation(hList);
    DestroyIndicator(hList);
    ForgetDispInfo(hList);
    KillTimer(hList, kRefreshTimer);
    ApplyOffset(hList, 0);
    g_offset = 0;
    g_target = 0;

    // When gathering, give Explorer back its one-screen work areas. It then
    // moves the icons that are off the screen back on by itself; gathering
    // catches the rest. Otherwise keep them, or Explorer would do the same.
    if (gather && g_stretchDesktop) {
        g_stretchDesktop = false;
        ResetDesktopWorkAreas(hList);
    }

    if (gather) {
        GatherToHome(hList);
        // Every icon is home now, so start there next time.
        Wh_SetIntValue(L"offset", 0);
    }

    g_active = false;

    if (!g_workAreas.empty()) {
        SendWorkAreas(hList, g_workAreas);
    }
    SIZE page = PageSize(hList);
    SetWindowPos(hList, nullptr, 0, 0, page.cx, page.cy,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    if (g_wall.dc) {
        FreeWallpaper();
        InvalidateRect(hList, nullptr, TRUE);
    }

    Wh_Log(L"Inactive (gather=%d)", gather);
}

LRESULT CALLBACK ListViewSubclassProc(HWND hWnd,
                                      UINT uMsg,
                                      WPARAM wParam,
                                      LPARAM lParam,
                                      DWORD_PTR dwRefData) {
    if (uMsg == g_ctlMsg) {
        switch (wParam) {
            case kActivate:
                Activate(hWnd);
                break;
            case kDeactivate:
                Deactivate(hWnd, false);
                break;
            case kDeactivateAndGather:
                KillTimer(hWnd, kMonitorTimer);
                Deactivate(hWnd, true);
                break;
            case kDragStart:
                g_edgeDir = 0;
                g_edgeLogged = false;
                break;
            case kDragTick:
                OnDragTick(hWnd);
                break;
            case kDragEnd:
                StopEdgeScroll();
                g_edgeDir = 0;
                break;
            case kArrangeChanged:
                SaveDesktopViewState();
                if (GetWindowLong(hWnd, GWL_STYLE) & LVS_AUTOARRANGE) {
                    if (g_active) {
                        // Explorer has packed the icons into home already.
                        // Gathering would give it one-screen work areas
                        // (see Activate).
                        Wh_Log(L"\"Auto arrange icons\" was turned on");
                        Deactivate(hWnd, false);
                        Wh_SetIntValue(L"offset", 0);
                        g_arrangeBlocked = true;
                    }
                } else if (g_arrangeBlocked) {
                    Wh_Log(L"\"Auto arrange icons\" was turned off");
                    Activate(hWnd);
                }
                break;
        }
        return 0;
    }

    switch (uMsg) {
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            // Ctrl+wheel stays native (icon size).
            if (g_active && !(GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)) {
                OnWheel(hWnd, GET_WHEEL_DELTA_WPARAM(wParam),
                        uMsg == WM_MOUSEHWHEEL);
                return 0;
            }
            break;

        case WM_MBUTTONDOWN:
            if (g_active) {
                StartDrag(hWnd);
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
            if (g_dragging) {
                OnDragMove(hWnd);
                return 0;
            }
            break;

        case WM_MBUTTONUP:
            if (g_dragging) {
                ReleaseCapture();  // ends the drag in WM_CAPTURECHANGED
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            EndDrag();
            break;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            DWORD pos = GetMessagePos();
            g_buttonDownPt = {GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
            g_haveButtonDown = true;
            break;
        }

        case WM_LBUTTONDBLCLK:
            if (g_active &&
                g_settings.homeAction == g_settings.kHomeDoubleClick) {
                OnDoubleClick(hWnd, lParam);
            }
            break;

        case WM_HOTKEY:
            if (wParam == kHomeHotkey) {
                if (g_active) {
                    GoHome(hWnd);
                }
                return 0;
            }
            break;

        case WM_TIMER:
            if (wParam == kAnimTimer) {
                OnAnimTick(hWnd);
                return 0;
            }
            if (wParam == kNewItemTimer) {
                OnNewItemTimer(hWnd);
                return 0;
            }
            if (wParam == kIndicatorTimer) {
                OnIndicatorTimer(hWnd);
                return 0;
            }
            if (wParam == kRefreshTimer) {
                OnRefreshTimer(hWnd);
                return 0;
            }
            if (wParam == kMonitorTimer) {
                OnMonitorTimer(hWnd);
                return 0;
            }
            break;

        case WM_WINDOWPOSCHANGING:
            if (g_active) {
                EnforceWindowPos(hWnd, (WINDOWPOS*)lParam);
            }
            break;

        case LVM_SETWORKAREAS:
            // Explorer resets the work areas on display or taskbar changes.
            // Keep its areas and stretch them over the pages again.
            if (g_active && !g_settingWorkAreas && !HaveDesktopHook() &&
                GetSystemMetrics(SM_CMONITORS) == 1) {
                const RECT* areas = (const RECT*)lParam;
                int count = areas ? (int)wParam : 0;
                g_workAreas.assign(areas, areas + count);
                g_pageWidth = PageSize(hWnd).cx;
                Wh_Log(L"Explorer set %d work area(s)", count);
                SetExtendedWorkAreas(hWnd);
                return 0;
            }
            break;

        case WM_STYLECHANGED:
            // Explorer sets LVS_AUTOARRANGE when "Auto arrange icons" is
            // turned on or off. Act after it has finished the change.
            if ((int)wParam == GWL_STYLE &&
                ((((STYLESTRUCT*)lParam)->styleOld ^
                  ((STYLESTRUCT*)lParam)->styleNew) &
                 LVS_AUTOARRANGE)) {
                PostMessage(hWnd, g_ctlMsg, kArrangeChanged, 0);
            }
            break;

        case WM_NCDESTROY:
            KillTimer(hWnd, kMonitorTimer);
            UnregisterHotKey(hWnd, kHomeHotkey);
            StopAnimation(hWnd);
            DestroyIndicator(hWnd);
            KillTimer(hWnd, kRefreshTimer);
            StopNewItemWatch(hWnd);
            if (g_hList == hWnd) {
                g_hList = nullptr;
                g_active = false;
            }
            break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void Attach(HWND hList, bool fromWindowThread) {
    g_hList = hList;
    if (!WindhawkUtils::SetWindowSubclassFromAnyThread(
            hList, ListViewSubclassProc, 0)) {
        Wh_Log(L"Failed to subclass the desktop ListView");
        g_hList = nullptr;
        return;
    }
    g_hDefView = GetParent(hList);
    if (!WindhawkUtils::SetWindowSubclassFromAnyThread(
            g_hDefView, DefViewSubclassProc, 0)) {
        Wh_Log(L"Failed to subclass DefView; the wallpaper stays fixed");
        g_hDefView = nullptr;
    }
    // A freshly created ListView is not set up yet, so activate later.
    if (fromWindowThread) {
        PostMessage(hList, g_ctlMsg, kActivate, 0);
    } else {
        SendMessage(hList, g_ctlMsg, kActivate, 0);
    }
}

struct DraggedItem {
    int index;
    POINT pos;
};

// Explorer clamps dropped desktop icons to its own one-screen work area,
// which the ListView work areas do not change. After a drop inside the
// desktop, move the icons it clamped on to where they were dropped, keeping
// the grid Explorer snapped them to.
void FixClampedDrop(HWND hList,
                    const std::vector<DraggedItem>& items,
                    int dx,
                    int dy) {
    DWORD spacing = ListView_GetItemSpacing(hList, FALSE);
    int cellX = LOWORD(spacing);
    if (cellX <= 0) {
        return;
    }
    int maxX = g_pageWidth * (1 + g_settings.maxPages) - cellX;

    UINT areaCount = 0;
    ListView_GetNumberOfWorkAreas(hList, &areaCount);
    std::vector<RECT> areas(areaCount);
    if (areaCount) {
        ListView_GetWorkAreas(hList, areaCount, areas.data());
    }
    for (const RECT& rc : areas) {
        Wh_Log(L"Drop: work area (%d,%d)-(%d,%d)", rc.left, rc.top, rc.right,
               rc.bottom);
    }

    int fixed = 0;
    for (const DraggedItem& item : items) {
        POINT now;
        if (!ListView_GetItemPosition(hList, item.index, &now)) {
            continue;
        }
        int wantX = item.pos.x + dx;
        if (wantX < g_pageWidth - cellX) {
            continue;  // dropped on home, which Explorer does not clamp
        }
        int steps = (int)std::lround((double)(wantX - now.x) / cellX);
        if (steps <= 0) {
            continue;  // not clamped
        }
        int x = std::min(now.x + steps * cellX, (LONG)maxX);
        SetIconPosition(hList, item.index, {x, now.y});
        fixed++;
    }
    if (fixed) {
        Wh_Log(L"Drop: moved %d of %d icon(s) past the clamp (dx=%d dy=%d)",
               fixed, (int)items.size(), dx, dy);
    }
}

// Runs during an OLE drag that starts in Explorer. The drag loop owns the
// mouse, so the ListView sees no mouse messages; poke it from here instead.
// SendMessage is handled even while that thread is inside the drag loop.
struct AutoScrollThread {
    HANDLE thread = nullptr;
    HANDLE stop = nullptr;

    static DWORD WINAPI Run(LPVOID param) {
        auto* self = (AutoScrollThread*)param;
        while (WaitForSingleObject(self->stop, 15) == WAIT_TIMEOUT) {
            HWND hList = g_hList;
            if (!hList) {
                break;
            }
            DWORD_PTR result;
            SendMessageTimeout(hList, g_ctlMsg, kDragTick, 0, SMTO_ABORTIFHUNG,
                               200, &result);
        }
        return 0;
    }

    void Start() {
        stop = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        thread = stop ? CreateThread(nullptr, 0, Run, this, 0, nullptr) : nullptr;
    }

    ~AutoScrollThread() {
        if (thread) {
            SetEvent(stop);
            // The thread may be in the middle of sending kDragTick to this
            // thread. Handle that while waiting, or both wait for each other
            // until its 200 ms timeout, and the dropped icons are drawn that
            // much later.
            while (MsgWaitForMultipleObjects(1, &thread, FALSE, INFINITE,
                                             QS_SENDMESSAGE) ==
                   WAIT_OBJECT_0 + 1) {
                MSG msg;
                PeekMessage(&msg, nullptr, 0, 0,
                            PM_NOREMOVE | PM_QS_SENDMESSAGE);
            }
            CloseHandle(thread);
        }
        if (stop) {
            CloseHandle(stop);
        }
    }
};

using DoDragDrop_t = decltype(&DoDragDrop);
DoDragDrop_t DoDragDrop_Original;
HRESULT WINAPI DoDragDrop_Hook(LPDATAOBJECT pDataObj,
                               LPDROPSOURCE pDropSource,
                               DWORD dwOKEffects,
                               LPDWORD pdwEffect) {
    HWND hList = g_hList;
    POINT start;
    bool fromDesktop =
        hList && g_active &&
        GetWindowThreadProcessId(hList, nullptr) == GetCurrentThreadId() &&
        GetCursorPos(&start) && WindowFromPoint(start) == hList;
    if (fromDesktop && g_haveButtonDown) {
        start = g_buttonDownPt;
    }
    g_haveButtonDown = false;

    std::vector<DraggedItem> items;
    int countBefore = 0;
    if (fromDesktop) {
        countBefore = ListView_GetItemCount(hList);
        for (int i = ListView_GetNextItem(hList, -1, LVNI_SELECTED); i != -1;
             i = ListView_GetNextItem(hList, i, LVNI_SELECTED)) {
            DraggedItem item{i, {}};
            if (ListView_GetItemPosition(hList, i, &item.pos)) {
                items.push_back(item);
            }
        }
    }

    int offsetBefore = g_applied;
    HRESULT hr;
    {
        AutoScrollThread autoScroll;
        if (hList && g_active && g_settings.dragAutoScroll) {
            SendMessage(hList, g_ctlMsg, kDragStart, 0);
            autoScroll.Start();
        }
        hr = DoDragDrop_Original(pDataObj, pDropSource, dwOKEffects, pdwEffect);
    }
    if (hList && g_hList == hList) {
        SendMessage(hList, g_ctlMsg, kDragEnd, 0);
    }

    POINT end;
    // Only a drop back onto the desktop that did not add or remove icons
    // (dropping onto a folder icon moves the files into it). If the desktop
    // scrolled during the drag, the icons moved that much further.
    if (!items.empty() && g_hList == hList && g_active &&
        GetCursorPos(&end) && WindowFromPoint(end) == hList &&
        ListView_GetItemCount(hList) == countBefore) {
        FixClampedDrop(hList, items, end.x - start.x + g_applied - offsetBefore,
                       end.y - start.y);
    }
    return hr;
}

// Explorer (shell32's CDesktopBrowser) hands the desktop work areas, one per
// monitor, to DefView's icon layout engine here. The engine keeps icons inside
// these areas: it moves icons outside them back on screen when the view is
// refreshed or loaded. Stretching the rightmost areas over the added pages
// lets icons stay there. The rectangles are in an array of up to 16 on the
// caller's stack, one per monitor (shell32 10.0.19041).
using SetDesktopWorkAreas_t = HRESULT(WINAPI*)(void* pThis,
                                               HWND hWnd,
                                               RECT* workAreas);
SetDesktopWorkAreas_t SetDesktopWorkAreas_Original;
HRESULT WINAPI SetDesktopWorkAreas_Hook(void* pThis,
                                        HWND hWnd,
                                        RECT* workAreas) {
    if (g_stretchDesktop && workAreas &&
        GetSystemMetrics(SM_CMONITORS) == 1) {
        int count = 1;
        int pageWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        LONG rightmost = LONG_MIN;
        for (int i = 0; i < count; i++) {
            rightmost = std::max(rightmost, workAreas[i].right);
        }
        for (int i = 0; i < count; i++) {
            if (workAreas[i].right == rightmost) {
                workAreas[i].right += pageWidth * g_settings.maxPages;
            }
        }
        Wh_Log(L"Desktop work areas (%d): right edge %d -> %d", count,
               rightmost, rightmost + pageWidth * g_settings.maxPages);
    } else {
        Wh_Log(L"Desktop work areas left as they are");
    }
    return SetDesktopWorkAreas_Original(pThis, hWnd, workAreas);
}

bool HaveDesktopHook() {
    return SetDesktopWorkAreas_Original != nullptr;
}

// Makes Explorer compute the desktop work areas again and pass them through
// SetDesktopWorkAreas_Hook, the same as when the taskbar moves. Only Progman
// (CDesktopBrowser) handles this. The ListView can be under a WorkerW
// instead, after a wallpaper change moves DefView there, and the WorkerW
// ignores it.
void ResetDesktopWorkAreas(HWND hList) {
    if (!SetDesktopWorkAreas_Original) {
        return;
    }
    HWND desktop = FindWindow(L"Progman", nullptr);
    if (!desktop) {
        desktop = GetParent(GetParent(hList));
    }
    Wh_Log(L"Asking Explorer to pass the desktop work areas again (%s)",
           g_stretchDesktop ? L"stretched" : L"one screen");
    SendMessage(desktop, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
}

bool HookShell32() {
#ifdef _WIN64
    HMODULE shell32 = LoadLibraryEx(L"shell32.dll", nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!shell32) {
        Wh_Log(L"shell32.dll not loaded");
        return false;
    }
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(protected: long __cdecl CDesktopBrowser::SetDesktopWorkAreas(struct HWND__ *,struct tagRECT *))"},
         &SetDesktopWorkAreas_Original, SetDesktopWorkAreas_Hook, true},
        {{LR"(public: virtual long __cdecl CDesktopBrowser::PaintWallpaper(struct HWND__ *,struct HDC__ *))"},
         &PaintWallpaper_Original, PaintWallpaper_Hook, true},
    };
    if (!WindhawkUtils::HookSymbols(shell32, hooks, ARRAYSIZE(hooks)) ||
        !SetDesktopWorkAreas_Original) {
        Wh_Log(L"CDesktopBrowser::SetDesktopWorkAreas not found; icons on "
               L"the added pages will not survive a refresh");
        return false;
    }
    if (!PaintWallpaper_Original) {
        Wh_Log(L"CDesktopBrowser::PaintWallpaper not found; the wallpaper "
               L"stays fixed");
    }
    return true;
#else
    // 32-bit Explorer is not supported for this hook.
    return false;
#endif
}

using CreateWindowExW_t = decltype(&CreateWindowExW);
CreateWindowExW_t CreateWindowExW_Original;
HWND WINAPI CreateWindowExW_Hook(DWORD dwExStyle,
                                 LPCWSTR lpClassName,
                                 LPCWSTR lpWindowName,
                                 DWORD dwStyle,
                                 int X,
                                 int Y,
                                 int nWidth,
                                 int nHeight,
                                 HWND hWndParent,
                                 HMENU hMenu,
                                 HINSTANCE hInstance,
                                 LPVOID lpParam) {
    HWND hWnd = CreateWindowExW_Original(dwExStyle, lpClassName, lpWindowName,
                                         dwStyle, X, Y, nWidth, nHeight,
                                         hWndParent, hMenu, hInstance, lpParam);
    if (hWnd && !g_hList && hWndParent && lpClassName &&
        !IS_INTRESOURCE(lpClassName) &&
        _wcsicmp(lpClassName, WC_LISTVIEW) == 0 && IsDesktopListView(hWnd)) {
        Wh_Log(L"Desktop ListView created: %p", hWnd);
        Attach(hWnd, true);
    }
    return hWnd;
}

BOOL Wh_ModInit() {
    Wh_Log(L"Init");
    g_ctlMsg = RegisterWindowMessage(L"Sidetop_Control_" WH_MOD_ID);
    LoadSettings();
    WindhawkUtils::SetFunctionHook(CreateWindowExW, CreateWindowExW_Hook,
                                  &CreateWindowExW_Original);
    WindhawkUtils::SetFunctionHook(DoDragDrop, DoDragDrop_Hook,
                                  &DoDragDrop_Original);
    if (HookShell32()) {
        // When Explorer is starting, the desktop gets its work areas before
        // the ListView is activated.
        g_stretchDesktop = !FindDesktopListView();
    }
    return TRUE;
}

void Wh_ModAfterInit() {
    HWND hList = FindDesktopListView();
    if (hList && !g_hList) {
        Wh_Log(L"Found existing desktop ListView: %p", hList);
        Attach(hList, false);
    }
}

void Wh_ModUninit() {
    Wh_Log(L"Uninit");
    HWND hList = g_hList;
    if (hList) {
        SendMessage(hList, g_ctlMsg, kDeactivateAndGather, 0);
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(hList,
                                                         ListViewSubclassProc);
    }
    if (g_hDefView && IsWindow(g_hDefView)) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(g_hDefView,
                                                         DefViewSubclassProc);
    }
    // The indicator window was destroyed on the desktop thread above.
    UnregisterClass(kIndicatorClass, ModuleInstance());
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    HWND hList = g_hList;
    if (hList) {
        SendMessage(hList, g_ctlMsg, kDeactivate, 0);
        SendMessage(hList, g_ctlMsg, kActivate, 0);
    }
}
