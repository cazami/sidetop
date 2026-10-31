// Test tool: checks, from outside Explorer, the pieces the mod uses to put
// new desktop icons on the screen in view. Registers for shell change
// notifications on the desktop folders the way the mod does, creates a file
// on the desktop, and looks the notified name up in DefView's IFolderView.
// Result (2026-10-30): no notification for the plain CreateFile and MoveFile,
// only after SHChangeNotify, so the mod checks the icon count instead.
// Creates and deletes sidetop-test.tmp / .txt on the desktop.
//   newitemtest
#include <windows.h>
#include <exdisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

static const UINT kNotify = WM_APP + 1;
static std::vector<PIDLIST_ABSOLUTE> g_dirs;
static std::wstring g_name;

static HRESULT GetDesktopFolderView(IFolderView** fv) {
    *fv = nullptr;
    IShellWindows* windows = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&windows));
    if (FAILED(hr)) return hr;
    VARIANT loc{};
    loc.vt = VT_I4;
    loc.lVal = CSIDL_DESKTOP;
    VARIANT empty{};
    long hwnd = 0;
    IDispatch* disp = nullptr;
    hr = windows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd,
                               SWFO_NEEDDISPATCH, &disp);
    windows->Release();
    if (hr != S_OK || !disp) return E_FAIL;
    IServiceProvider* sp = nullptr;
    hr = disp->QueryInterface(IID_PPV_ARGS(&sp));
    disp->Release();
    if (FAILED(hr)) return hr;
    IShellBrowser* browser = nullptr;
    hr = sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser));
    sp->Release();
    if (FAILED(hr)) return hr;
    IShellView* view = nullptr;
    hr = browser->QueryActiveShellView(&view);
    browser->Release();
    if (FAILED(hr)) return hr;
    hr = view->QueryInterface(IID_PPV_ARGS(fv));
    view->Release();
    return hr;
}

static void FindInView(PCWSTR wanted) {
    IFolderView* fv = nullptr;
    IShellFolder* folder = nullptr;
    if (FAILED(GetDesktopFolderView(&fv)) ||
        FAILED(fv->GetFolder(IID_PPV_ARGS(&folder)))) {
        wprintf(L"  no IFolderView\n");
        return;
    }
    int count = 0;
    fv->ItemCount(SVGIO_ALLVIEW, &count);
    bool found = false;
    for (int i = 0; i < count; i++) {
        PITEMID_CHILD pidl = nullptr;
        if (FAILED(fv->Item(i, &pidl))) continue;
        STRRET str{};
        WCHAR name[MAX_PATH];
        if (SUCCEEDED(folder->GetDisplayNameOf(
                pidl, SHGDN_INFOLDER | SHGDN_FORPARSING, &str)) &&
            SUCCEEDED(StrRetToBufW(&str, pidl, name, MAX_PATH)) &&
            _wcsicmp(name, wanted) == 0) {
            POINT pt{};
            fv->GetItemPosition(pidl, &pt);
            wprintf(L"  found in view: index %d of %d at (%ld,%ld)\n", i, count,
                    pt.x, pt.y);
            found = true;
        }
        CoTaskMemFree(pidl);
    }
    if (!found) wprintf(L"  not in view yet (%d items)\n", count);
    folder->Release();
    fv->Release();
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                LPARAM lParam) {
    if (msg >= WM_APP) {
        wprintf(L"message %04X\n", msg);
    }
    if (msg == kNotify) {
        PIDLIST_ABSOLUTE* pidls = nullptr;
        LONG event = 0;
        HANDLE lock = SHChangeNotification_Lock((HANDLE)wParam, (DWORD)lParam,
                                                &pidls, &event);
        if (lock) {
            PCIDLIST_ABSOLUTE item =
                (event & (SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER)) ? pidls[1]
                                                                  : pidls[0];
            bool onDesktop = false;
            for (auto dir : g_dirs) {
                if (item && ILIsParent(dir, item, TRUE)) onDesktop = true;
            }
            PWSTR name = nullptr;
            if (item) SHGetNameFromIDList(item, SIGDN_PARENTRELATIVEPARSING, &name);
            wprintf(L"notify event %08lX onDesktop=%d name=%ls\n", event,
                    onDesktop, name ? name : L"?");
            if (onDesktop && name) g_name = name;
            CoTaskMemFree(name);
            SHChangeNotification_Unlock(lock);
        }
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void Pump(DWORD ms) {
    DWORD end = GetTickCount() + ms;
    while ((LONG)(end - GetTickCount()) > 0) {
        MSG m;
        while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        Sleep(10);
    }
}

int wmain() {
    // Desktop paths can have non-ASCII names; plain wprintf stops at them.
    _setmode(_fileno(stdout), _O_U8TEXT);
    CoInitialize(nullptr);
    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"NewItemTest";
    RegisterClass(&wc);
    HWND hWnd = CreateWindow(wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, nullptr, nullptr);

    const KNOWNFOLDERID* ids[] = {&FOLDERID_Desktop, &FOLDERID_PublicDesktop};
    std::vector<SHChangeNotifyEntry> entries;
    for (auto id : ids) {
        // SHGetKnownFolderIDList(FOLDERID_Desktop) gives the namespace root,
        // not the file system folder, so go through the path.
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*id, 0, nullptr, &path))) {
            PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path);
            wprintf(L"watch %ls: pidl size %u\n", path,
                    pidl ? ILGetSize(pidl) : 0);
            if (pidl) {
                g_dirs.push_back(pidl);
                entries.push_back({pidl, TRUE});
            }
            CoTaskMemFree(path);
        }
    }
    ULONG reg = SHChangeNotifyRegister(
        hWnd, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
        SHCNE_CREATE | SHCNE_MKDIR | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER,
        kNotify, (int)entries.size(), entries.data());
    wprintf(L"registered: %lu\n", reg);

    PWSTR desktop = nullptr;
    SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop);
    std::wstring tmp = std::wstring(desktop) + L"\\sidetop-test.tmp";
    std::wstring final = std::wstring(desktop) + L"\\sidetop-test.txt";
    CoTaskMemFree(desktop);

    // Like a download: create under a temporary name, then rename.
    HANDLE f = CreateFile(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    CloseHandle(f);
    wprintf(L"created %ls (handle ok=%d)\n", tmp.c_str(),
            f != INVALID_HANDLE_VALUE);
    Pump(3000);
    wprintf(L"  now telling the shell with SHChangeNotify\n");
    SHChangeNotify(SHCNE_CREATE, SHCNF_PATH | SHCNF_FLUSH, tmp.c_str(), nullptr);
    Pump(5000);
    if (!g_name.empty()) FindInView(g_name.c_str());

    g_name.clear();
    MoveFile(tmp.c_str(), final.c_str());
    wprintf(L"renamed to %ls\n", final.c_str());
    Pump(5000);
    if (!g_name.empty()) FindInView(g_name.c_str());

    DeleteFile(tmp.c_str());
    DeleteFile(final.c_str());
    SHChangeNotifyDeregister(reg);
    CoUninitialize();
    return 0;
}
