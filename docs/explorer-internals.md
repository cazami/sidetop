# Explorer 内部のメモ（Windows 10 22H2）

shell32.dll 10.0.19041.6456 を、Microsoft のシンボル（shell32.pdb）を使って逆アセンブルし、分かったことのメモです。2026-10-29 に調べました。Windows の更新で変わることがあります。

## アイコン配置の仕組み

- デスクトップの ListView（`SysListView32`）は owner-data（`LVS_OWNERDATA`）です。アイコンの位置を覚えているのは DefView（`CDefView`）と、その中の `CListViewHost` です。
- `CListViewHost` は ListView を COM の IListView 経由で操作します。そのため、作業領域やアイコンの位置の変更の多くは、ウィンドウメッセージ（`LVM_SETWORKAREAS` など）としてサブクラスには見えません。
- 配置エンジン `IconLayoutEngine` は、作業領域ごとに格子（GridWorkspace）を作ります（`WorkAreasToGridWorkspaces`、`UpdateWorkAreaGrid`）。アイコンは格子の位置として保存され（`DesktopIconLayoutDataModel`、`WorkspaceData`）、`SaveDataModelToPropertyBag` で保存されます。
- 再読み込みや読み込みのときは `WorkspaceData::ResolveOffScreenIcons` が動き、作業領域の外のアイコンを画面内へ戻します。これが「F5 で戻る」の正体でした。

## 作業領域の流れ

```
CDesktopBrowser::_WndProcBS
  WM_SETTINGCHANGE（wParam != SPI_SETDESKWALLPAPER）または WM_SIZE など
  -> CDesktopBrowser::_OnSystemParametersChanged(this, uMsg, wParam, lParam)
     uMsg == WM_SETTINGCHANGE か 0x44B のとき
  -> CDesktopBrowser::UpdateViewSize(this, bool, HWND listView)
     GetViewRectsMinusToolbar で、モニターごとの矩形（最大16個、呼び出し元のスタック）を作る
  -> CDesktopBrowser::SetDesktopWorkAreas(this, HWND, RECT* rects)
     個数は this+0x124（モニターの数）。矩形ごとに GetDpiForMonitor で DPI を付けて
     WorkAreaWithDPI {RECT, UINT dpi, BOOL primary}（24 バイト）の配列にし、
     OID_ListControl の IUnknown_QueryObject で得たインターフェイスの vtable[3] に渡す
  -> CListViewHost::SetWorkAreasWithDPI -> IconLayoutEngine::UpdateWorkAreaGrid
```

- Windhawk のシンボル名：`protected: long __cdecl CDesktopBrowser::SetDesktopWorkAreas(struct HWND__ *,struct tagRECT *)`
- 外のプロセスから Progman へ `WM_SETTINGCHANGE`（`SPI_SETWORKAREA`, 0）を送ると、このフックが呼ばれることを確かめました（2026-10-29）。
- 壁紙が変わったあとなどは、DefView が Progman ではなく WorkerW の下に移っていることがあります。WorkerW へ送ってもフックは呼ばれず、Progman へ送ると呼ばれます（2026-10-30 に確認）。

## ドロップの流れ（未解決）

```
CDefView::Drop -> CDefView::_SameViewMoveIcons
  -> CDefView::_PositionSelectedItems / CDefView::_MoveItems
  -> CListViewHost::PositionSelectedItemsFromMove
     -> ラムダ（shell32+0x1332DC）
        CListViewHost::GetDPIForPoint, GetItemPositionInCollection
        CListViewHost::PlaceItemWithLogicalDisplacement -> CListViewHost::SetItemPosition
        IconLayoutEngine::RefreshDataModel -> IconLayoutEngine::UpdateView
```

- 作業領域を広げても、ドロップしたアイコンは1画面の中へ押し戻されます。押し戻しはこの流れのどこか（DPI からモニターを決める部分か、`RefreshDataModel`）で起きていると見ていますが、まだ特定していません。
- 今は `DoDragDrop` のフックと `FixClampedDrop` で、ドロップのあとに置き直しています。

## 背景（壁紙）とアイコンの描き方（2026-10-31、comctl32 6.0.19041.6456）

- デスクトップの ListView は、拡張スタイル `14C94C30` です。`LVS_EX_TRANSPARENTBKGND`、`LVS_EX_TRANSPARENTSHADOWTEXT`、`LVS_EX_DOUBLEBUFFER` などが付いています。背景色は `CLR_NONE` です。
- 背景は `CLVBackgroundManager::DrawSimpleBackground` が描きます。中では次のことをします。
  1. DC の原点を親（DefView）の座標へずらす。
  2. 親へ `WM_PRINTCLIENT`（lParam = `PRF_ERASEBKGND`）を送り、0 が返れば `WM_ERASEBKGND` を送る。
  3. 原点を戻す。
- `DrawThemeParentBackground` や `CDesktopBrowser::PaintWallpaper` は、この経路では使われません。1回の描き直しで、背景は2回描かれます。
- 項目の文字とアイコンは、描くたびに `LVN_GETDISPINFOW` で DefView に尋ねます（owner-data のため）。見えている項目ごとに2回（mask 0x1 と 0xB）尋ね、DefView は1回に約 0.9 ms かかります。
- 確かめるための道具：`tools/wallprobe.cpp`、`tools/lvprof.cpp`。

## 位置を外から操作する

- Progman は `WM_GETISHELLBROWSER` に答えません（0 を返します）。
- `IShellWindows::FindWindowSW(CSIDL_DESKTOP, SWC_DESKTOP, SWFO_NEEDDISPATCH)` → `IServiceProvider::QueryService(SID_STopLevelBrowser)` → `IShellBrowser::QueryActiveShellView` → `IFolderView` で取れます。
- この経路は、Explorer の外のプロセスからも使えます。テスト用のツールで、`SelectAndPositionItems` と `IShellView::Refresh` が動くことを確かめました。

## 調べ方

- シンボルは `https://msdl.microsoft.com/download/symbols/shell32.pdb/<GUID><Age>/shell32.pdb` から取ります。GUID と Age は、DLL のデバッグディレクトリ（CodeView）にあります。
- 関数名の一覧は、dbghelp の `SymEnumSymbolsW` で出しました。`SYMOPT_PUBLICS_ONLY` を付けると、装飾された名前が得られます。それを `UnDecorateSymbolNameW` で戻し、`__ptr64` を取ると、Windhawk 用の名前になります。
- 逆アセンブルには、Python の capstone を使いました。
- モッドのログは、OutputDebugString の受け口（`DBWIN_BUFFER`）を自分で開いて読めます。DbgView を使わなくても読めます。
