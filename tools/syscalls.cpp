// Test tool: times a few win32k calls from an ordinary process, to see
// whether the whole system is slow at them (not only Explorer).
// Result (2026-10-31 00:50): GetProp 270-310 us and GetDC/ReleaseDC
// 190-230 us, about a hundred times the usual, while calls that stay in
// user mode (GetWindowLong, GetClientRect) took 0.01 us. One thread of
// Cakewalk Sonar was spending 95% of a core in kernel mode at the time.
// At 00:58, with Sonar gone: GetProp 3-4 us, GetDC/ReleaseDC 15-21 us.
// Build: clang++ --target=x86_64-w64-mingw32 -static -O1 tools/syscalls.cpp
//   -o syscalls.exe
#include <windows.h>
#include <cstdio>
static double now() { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return c.QuadPart * 1e6 / f.QuadPart; }
int main() {
    HWND w = CreateWindowW(L"STATIC", L"", 0, 0, 0, 10, 10, nullptr, nullptr, nullptr, nullptr);
    SetPropW(w, L"x", (HANDLE)1);
    for (int round = 0; round < 3; round++) {
        double t = now();
        for (int i = 0; i < 2000; i++) GetPropW(w, L"x");
        double p = (now() - t) / 2000;
        t = now();
        for (int i = 0; i < 500; i++) ReleaseDC(nullptr, GetDC(nullptr));
        double d = (now() - t) / 500;
        t = now();
        for (int i = 0; i < 2000; i++) GetWindowLongW(w, GWL_STYLE);
        double l = (now() - t) / 2000;
        t = now();
        RECT r;
        for (int i = 0; i < 2000; i++) GetClientRect(w, &r);
        double c = (now() - t) / 2000;
        t = now();
        for (int i = 0; i < 2000; i++) GetDesktopWindow(), IsWindow(w);
        double iw = (now() - t) / 2000;
        printf("GetProp %.2f us, GetDC/ReleaseDC %.2f us, GetWindowLong %.2f us, GetClientRect %.2f us, IsWindow %.2f us\n", p, d, l, c, iw);
    }
    return 0;
}
