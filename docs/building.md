# ビルドと検証

以下のコマンドはリポジトリのルートから実行します。PowerShell 7で検証しています。生成物は `build/` に出力し、ソースとは分離します。

## Windowsビルド

Qt 6.8以上のCore / Network / WebSockets / Widgets / Quick / QuickControls2 / Test、CMake 3.24以上、C++20コンパイラが必要です。WidgetsはWindowsサーバーの通知領域アイコンと状態画面に使用します。検証環境はQt 6.11.1、MinGW 13.1です。

```powershell
./scripts/Build-Windows.ps1
./scripts/Package-Windows.ps1
```

サーバー専用ZIPのWorkflowとパッケージ検証は [Windowsサーバーの配布](windows-release.md) を参照してください。

Qt Creatorから `CMakeLists.txt` を開くこともできます。スクリプトのパラメーターでQt・コンパイラ等の場所を変更できます。

デモの実行方法は [examples/README.md](../examples/README.md) を参照してください。

## Androidビルド

QtのAndroid ARM64キット、同じ版のデスクトップQt、Android SDK/NDK、JDKを使用します。

検証環境: Qt 6.11.1、NDK 27.2.12479018、JDK 21、compile SDK 36、target SDK 35、min SDK 28（Android 9）。

```powershell
./scripts/Build-Android.ps1
```

`build/android-arm64/android-build/build/outputs/apk/debug/android-build-debug.apk` に開発用APKを生成します。C++はReleaseビルドで、Android標準のデバッグ署名を使用します。

GitHub Actionsによる署名済みAPKの作成とSecrets設定は [Android署名ビルド](android-release.md) を参照してください。

## アイコンの再生成

元データは `assets/icons/*.svg` です。デスクトップQtのGui/Svgで生成済みPNG・ICOを更新できます。通常のビルドにSvgは不要です。

```powershell
cmake -S . -B build/icon-tools -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 `
    -DREMOTE_BUILD_APP=OFF -DREMOTE_BUILD_TESTS=OFF -DREMOTE_BUILD_ICON_TOOLS=ON
cmake --build build/icon-tools --target remote-render-icons
./build/icon-tools/remote-render-icons.exe .
```

## 検証

統合テストでは、認証なしでのLAN待受・対象一覧の受信・キー送信、複数対象の振り分け、操作権競合、不正メッセージ、分割受信、タイムアウト、古い入力の破棄、再登録、UIスレッドでの入力呼び出しを確認します。

```powershell
ctest --test-dir build/windows --output-on-failure
```

実放送での確認範囲は別途記録します。BML・B62・HTML5の各経路や、Android端末での操作確認を区別してください。
