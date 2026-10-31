# Sidetop

## 概要

Sidetop（Horizontal Scrolling Desktop）は、Windows のデスクトップをマウスホイールで左右にスクロールできるようにする Windhawk モッドです。

- 形態：Windhawk モッド（C++、1ファイル `sidetop.wh.cpp`）
- モッドID：`sidetop`
- 対象プロセス：`explorer.exe`
- 優先 OS：Windows 10（22H2）。Windows 11 は v1.0 以降
- ライセンス：MIT
- 検証環境：開発者の Windows 10 実機のみ

## 今の状況（2026-10-31）

- v1.0 をコミットしました。Windows 11 は、ユーザーが仮想マシンで試し、主な機能が動きました（バージョンは未記録）。0.4.7 で足した「モニター2台以上で止まる」処理（v1.0-test.md の6つ目）は、まだ実機で確かめていません。
- v0.3（アイコン操作）は終わりました。手順と結果は [docs/v0.3-test.md](docs/v0.3-test.md) にあります。
- v1.0（仕様書 7 章）の位置インジケーター、離散モード、壁紙の3モード、軽量化（0.4.5）、ホームへ戻るショートカットキー（0.4.6）は、ユーザーの実機で確かめました。手順と結果は [docs/v1.0-test.md](docs/v1.0-test.md) にあります。README も書きました。マルチモニターは v1.1 に回しました。
- 実機で動くことを確かめた最新の版は、コミット 6458407（`old-history` ブランチ）の `sidetop.wh.cpp`（0.4.6）です。
- v0.1 と v0.2 の結果は [docs/v0.1-report.md](docs/v0.1-report.md) と [docs/v0.2-test.md](docs/v0.2-test.md) にあります。Explorer の内部について分かったことは [docs/explorer-internals.md](docs/explorer-internals.md) にあります。
- 既知の問題（あとで直す）：
  - アイコンのサイズを変えたときや Explorer の再起動で、配置がまれに崩れます（v0.1 の g と i）。
  - アイコンが多いところでは、スクロールの滑らかさが落ちます（0.4.5 で少し軽くしました。これ以上は描き方を大きく変える必要があります）。
- Claude は Windhawk に mod を組み込めません（管理者権限が要るため）。
  - ただし、今入っている mod のログは、自分で `DBWIN_BUFFER` を開いて読めます。
  - デスクトップのアイコンは、外のプロセスから IFolderView で操作できます（`tools/desktool.cpp`）。
  - 組み込まれている版は、レジストリ `HKLM\SOFTWARE\Windhawk\Engine\Mods\local@sidetop` の `Version` で分かります。
- 版を比べるときは、`versions/`（git に入れていない）に版ごとのファイルを置き、ユーザーに入れ替えてもらいます。
- Windhawk のエディタ（vscodium）は、computer use では「クリックだけ」の扱いで、貼り付けできません。編集中のファイル `C:\ProgramData\Windhawk\EditorWorkspace\mod.wh.cpp` も、一般ユーザーには書けません。
  - Explorer の中での描き方などは、`tools/wallprobe.cpp` のように、`SetWindowsHookEx` でデスクトップのスレッドに一時的に読み込む検証用の DLL を作れば、Claude だけで確かめられます。
  - 画面は computer use のスクリーンショットで見ます。デスクトップは許可なしで写ります。
- 処理を消すときは、1つずつ消して、そのつど実機で確かめます。まとめて消したところ、必要な処理（ドロップのあとの置き直し）まで消してしまったことがあります。
- 「アイコンの自動整列」は、Explorer が F5 か終了のときにしか、レジストリ（FFlags）に書きません。
- Windhawk は導入済みで、モッドが動くことを確認済みです。
- アイコン配置のレジストリ（`HKCU\Software\Microsoft\Windows\Shell\Bags\1\Desktop`）は `C:\Users\User\Documents\desktop-icons-backup.reg` に書き出し済みです。

## 資料の場所

- 仕様書：[docs/SPEC.md](docs/SPEC.md)（マイルストーン、設定項目、テスト項目もここにあります）
- リポジトリ：https://github.com/cazami/sidetop（remote `origin`）
  - 2026-10-31 に、前のアカウントから移しました。新しいリポジトリは v1.0 の1コミットから始まっています。それより前の履歴は、ローカルのブランチ `old-history` にだけ残しています（push しません）。docs に出てくる古いコミット番号は、このブランチで見られます。
  - 前のアカウント名を、ファイルやコミットに入れないでください。

## ルール

- ユーザーとは日本語の です／ます調で話します。
- マイルストーンごとに、動作確認の手順と結果を報告します。
- テストの前には、必ずアイコン配置のバックアップを案内します。
- 区切りのよいところでコミットします。
- push はユーザーの了承を得てから行います。
- この PC には `gh` コマンドがないため、GitHub 操作は `git` だけで行います。
- コミットの作者はこのリポジトリのローカル設定（`min` / `350581848+cazami@users.noreply.github.com`）を使い、個人のメールアドレスは使いません。

## Windhawk での作り方

- コードは `sidetop.wh.cpp` の1ファイルに収めます（Windhawk カタログの投稿ルール）。
- 設定は Windhawk の設定機能（`// ==WindhawkModSettings==`）で提供します。
- コンパイルと組み込みは、ユーザーが Windhawk の画面で行います。Claude はファイルを書き、ユーザーに Windhawk のエディタへ貼り付けて「Compile Mod」するよう案内します。
- Windhawk のコンパイラは `C:\Program Files\Windhawk\Compiler`（clang）にあります。Claude は渡す前に、`x86_64-w64-mingw32` と `i686-w64-mingw32` の両方で `-c` でコンパイルできるかを確かめます。Windhawk は 32 ビット版もコンパイルするためです。あわせて `-shared` でリンクも試し、足りない関数が Windhawk 本体のもの（`InternalWh_...`）だけであることを確かめます。新しい Windows の関数を使うと、`@compilerOptions` にライブラリ（例：`-lgdi32`）が要ることがあります（2026-10-30、これを忘れてユーザーの手元でリンクエラーになりました）。
- 終了処理（`Wh_ModUninit`）では、すべてのアイコンを画面内に戻します（仕様書 4 章）。
