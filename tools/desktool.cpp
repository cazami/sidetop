// Test tool: list / move desktop icons through DefView's IFolderView and
// refresh the desktop view, from outside Explorer.
//   desktool list
//   desktool move <index> <x> <y>
//   desktool flags
//   desktool arrange <0|1>
//   desktool save
//   desktool refresh
#include <windows.h>
#include <exdisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#include <cwchar>

static HRESULT GetDesktopView(IShellView** view) {
    *view = nullptr;
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
    if (hr != S_OK || !disp) return FAILED(hr) ? hr : E_FAIL;
    IServiceProvider* sp = nullptr;
    hr = disp->QueryInterface(IID_PPV_ARGS(&sp));
    disp->Release();
    if (FAILED(hr)) return hr;
    IShellBrowser* browser = nullptr;
    hr = sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser));
    sp->Release();
    if (FAILED(hr)) return hr;
    hr = browser->QueryActiveShellView(view);
    browser->Release();
    return hr;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    CoInitialize(nullptr);
    IShellView* view = nullptr;
    HRESULT hr = GetDesktopView(&view);
    if (FAILED(hr)) {
        wprintf(L"no desktop view: %08X\n", (unsigned)hr);
        return 1;
    }
    IFolderView* fv = nullptr;
    hr = view->QueryInterface(IID_PPV_ARGS(&fv));
    if (FAILED(hr)) {
        wprintf(L"no IFolderView: %08X\n", (unsigned)hr);
        return 1;
    }
    IShellFolder* folder = nullptr;
    fv->GetFolder(IID_PPV_ARGS(&folder));

    if (wcscmp(argv[1], L"list") == 0) {
        int count = 0;
        fv->ItemCount(SVGIO_ALLVIEW, &count);
        for (int i = 0; i < count; i++) {
            PITEMID_CHILD pidl = nullptr;
            if (FAILED(fv->Item(i, &pidl))) continue;
            POINT pt{};
            fv->GetItemPosition(pidl, &pt);
            STRRET sr{};
            WCHAR name[MAX_PATH] = L"?";
            if (folder && SUCCEEDED(folder->GetDisplayNameOf(pidl, SHGDN_NORMAL, &sr))) {
                StrRetToBufW(&sr, pidl, name, MAX_PATH);
            }
            wprintf(L"%3d (%5ld,%5ld) %ls\n", i, pt.x, pt.y, name);
            CoTaskMemFree(pidl);
        }
    } else if (wcscmp(argv[1], L"move") == 0 && argc == 5) {
        int index = _wtoi(argv[2]);
        POINT pt{_wtoi(argv[3]), _wtoi(argv[4])};
        PITEMID_CHILD pidl = nullptr;
        hr = fv->Item(index, &pidl);
        if (SUCCEEDED(hr)) {
            PCUITEMID_CHILD pidls[] = {pidl};
            hr = fv->SelectAndPositionItems(1, pidls, &pt, SVSI_POSITIONITEM);
            POINT now{};
            fv->GetItemPosition(pidl, &now);
            wprintf(L"move %d -> (%ld,%ld): hr=%08X, now (%ld,%ld)\n", index,
                    pt.x, pt.y, (unsigned)hr, now.x, now.y);
            CoTaskMemFree(pidl);
        }
    } else if (wcscmp(argv[1], L"flags") == 0) {
        IFolderView2* fv2 = nullptr;
        DWORD flags = 0;
        if (SUCCEEDED(fv->QueryInterface(IID_PPV_ARGS(&fv2)))) {
            hr = fv2->GetCurrentFolderFlags(&flags);
            fv2->Release();
        }
        HWND defView = nullptr;
        view->GetWindow(&defView);
        HWND list = FindWindowEx(defView, nullptr, L"SysListView32", nullptr);
        LONG style = GetWindowLong(list, GWL_STYLE);
        wprintf(L"folder flags %08lX (hr=%08X) autoArrange=%d snapToGrid=%d\n",
                flags, (unsigned)hr, !!(flags & FWF_AUTOARRANGE),
                !!(flags & FWF_SNAPTOGRID));
        wprintf(L"ListView %p style %08lX LVS_AUTOARRANGE=%d\n", list, style,
                !!(style & LVS_AUTOARRANGE));
    } else if (wcscmp(argv[1], L"arrange") == 0 && argc == 3) {
        // Turns "Auto arrange icons" on (1) or off (0), like the menu.
        IFolderView2* fv2 = nullptr;
        hr = fv->QueryInterface(IID_PPV_ARGS(&fv2));
        if (SUCCEEDED(hr)) {
            hr = fv2->SetCurrentFolderFlags(
                FWF_AUTOARRANGE, _wtoi(argv[2]) ? FWF_AUTOARRANGE : 0);
            fv2->Release();
        }
        wprintf(L"arrange %ls: %08X\n", argv[2], (unsigned)hr);
    } else if (wcscmp(argv[1], L"save") == 0) {
        hr = view->SaveViewState();
        wprintf(L"save: %08X\n", (unsigned)hr);
    } else if (wcscmp(argv[1], L"refresh") == 0) {
        hr = view->Refresh();
        wprintf(L"refresh: %08X\n", (unsigned)hr);
    }
    if (folder) folder->Release();
    fv->Release();
    view->Release();
    CoUninitialize();
    return 0;
}
