# TVTDataBroadcastingRemote

TVTestデータ放送プラグイン **TVTDataBroadcastingWV2** をAndroidから操作するQt 6/C++アプリと、Windows用中継サーバーです。

```text
Android / Windows リモコン（Qt Quick + C++）
    │ LAN内 WebSocket（ws://、認証なし）
Windows 中継サーバー（Qt Core / Network / WebSockets）
    ├─ 名前付きパイプ ─ TVTest A のプラグイン（Qt不要）
    └─ 名前付きパイプ ─ TVTest B のプラグイン（Qt不要）
```

0.2.0から、LAN内でURLだけを指定して接続する構成です。接続トークン、接続コード、証明書、TLS、ペアリング、認証失敗によるロックは使用しません。旧版で保存したURLは `wss://` から `ws://` に移行し、以前の証明書指紋設定を削除します。

## 起動と接続

Windowsパッケージを展開し、中継サーバーを起動します。

```powershell
./tvt-remote-server.exe
```

既定は `0.0.0.0:18765`、IPC名は `TVTDataBroadcastingRemote-v1` です。特定のIP・ポートを指定することもできます。

```powershell
./tvt-remote-server.exe --address 192.168.10.9 --port 18765
```

AndroidアプリまたはWindows版 `tvt-remote.exe` の接続設定に `ws://PCのLAN内IP:18765` を入力し、一覧から操作するTVTestを選びます。

必要に応じてWindowsファイアウォールでプライベートネットワークからの接続を許可してください。

0.3.0から、保存済みURLへ起動時・復帰時に自動接続します。初回だけ接続設定を開き、以降は毎回設定画面を表示しません。歯車の「起動・復帰時に自動接続」で無効化できます。

バックグラウンドへ移行すると接続と操作権を解放し、復帰時には再接続します。0.4.0から、プラグインに保存した操作先識別トークン `RemoteTargetToken` で前回のTVTestを探します。TVTestや中継サーバーが再起動して接続IDが変わっても、同じトークンの操作先が1つだけで空いていれば自動で操作権を取り直します。アプリを表示したままTVTestを再起動した場合も復帰します。キー操作は再送しません。

トークンはLANクライアントに公開する識別用の値で、認証・接続制限には使いません。未設定ならプラグインがUUIDを生成してINIに保存します。プラグイン設定画面の「リモコンの操作先識別トークン」、またはINIで半角英数字と `. _ -` の1～64文字に設定できます。大文字小文字は区別します。複数のTVTestには異なる値を設定してください。同じINIを共有する同時起動や、INIをコピーして同じ値にした場合は重複するため、自動選択を止めて重複を表示します。手動選択は可能です。

他のリモコンが使用中の操作権は取得しません。トークンを変更した場合はアプリで選び直してください。旧プラグインのようにトークンがない場合は従来の接続IDで復元し、TVTest再起動後は手動選択が必要です。

手動の「切断」はその起動中は維持します。「操作権を解放」を押した場合は記憶した操作先も解除し、復帰時に勝手に選び直しません。

## TVTest側

対応する `TVTDataBroadcastingWV2` の `feature/stable-target-token` ブランチに接続部があります。

```ini
[TVTDataBroadcastingWV2]
EnableRemoteBridge=1
RemoteName=リビング
RemoteTargetToken=living-room
RemotePipeName=TVTDataBroadcastingRemote-v1
```

各TVTestに違う名前と識別トークンを付けてください。接続機能の有効化とパイプ名はTVTest起動時に読みます。識別トークンの変更は約1秒で反映します。サーバーの `--pipe` を変更する場合は `RemotePipeName` も合わせます。プラグインの接続機能は既定で無効です。

プラグインの表示機能が無効でも登録を維持し、dボタンで有効化できます。プラグイン自身はネットワークのポートを開きません。接続SDKは `sdk/` からプラグインの `remote/` へコピーして管理し、Qtをプラグインへリンクしません。

## 機能

- 接続中のTVTestの名前、放送局、使用中状態を表示。
- 歯車の設定画面で接続・TVTest選択・操作権の解放を行い、通常画面には選択中のTVTestとリモコンを表示。
- 対象ごとに1接続が操作権を取得。別々のTVTestは同時に操作可能。
- d、上下左右、決定、戻る、青赤緑黄、数字1～12（10/0）。方向キーの長押しは単発操作を反復。
- 方向キーは同じ三角形を回転して図形描画し、端末のフォントや絵文字に依存しない表示に統一。
- BML・B62・HTML5や数字キーの選局処理は、既存プラグインのコマンドへ振り分け。
- ランダムな接続ID、順序番号、入力期限、応答、タイムアウト。古い操作を再送しない。
- 切断・再接続、サーバー再起動時のTVTest再登録、二重起動防止。
- メッセージサイズ、送信待ち、入力頻度、接続数の上限を設定。
- ネイティブ接続部は通信スレッドとUIスレッドを分離し、古い世代や期限切れの入力を破棄。
- アプリとサーバーに専用アイコン。Androidはアダプティブ・モノクロ表示にも対応。

`dispatched` は既存の入力経路へ渡したことを示し、放送コンテンツがキーを使用したことや画面遷移の完了を示しません。すでにプラグインへ渡した操作は、切断時に取り消せない場合があります。

## ソース構成

| パス | 内容 |
|---|---|
| `server/` | Windows中継サーバー |
| `app/` | Android / Windows共通のC++・QMLリモコン |
| `shared/` | Qt側のメッセージ・キー定義 |
| `sdk/` | Qt不要のWin32プラグイン接続部 |
| `tools/` | ビルド・パッケージ・デモ・アイコン生成 |
| `tests/` | 実ソケット・実パイプによる統合テスト |
| `assets/icons/` | SVG・PNG・Windows用ICO |
| `android/res/` | Android用アイコン |
| `docs/protocol.md` | 通信仕様 |

## Windowsビルド

Qt 6.8以上のCore / Network / WebSockets / Quick / QuickControls2 / Test、CMake 3.24以上、C++20コンパイラが必要です。検証環境はQt 6.11.1、MinGW 13.1です。

```powershell
./tools/Build-Windows.ps1
./tools/Package-Windows.ps1
```

Qt Creatorから `CMakeLists.txt` を開くこともできます。スクリプトのパラメーターでQt・コンパイラ等の場所を変更できます。

TVTestの代わりにデモで振り分けを確認する場合は、別々のターミナルで次を実行します。

```powershell
./build/windows/tvt-remote-server.exe
./build/windows/tvt-remote-demo.exe --name 'TVTest A' --channel 'テスト放送A'
./build/windows/tvt-remote-demo.exe --name 'TVTest B' --channel 'テスト放送B'
```

開発端末ではQtのbinをPATHに追加してください。デモは入力名を出力するだけでTVTestを操作しません。`tvt-remote-native-demo.exe` はQt不要のSDKを使用します。

## Androidビルド

QtのAndroid ARM64キット、同じ版のデスクトップQt、Android SDK/NDK、JDKを使用します。

検証環境: Qt 6.11.1、NDK 27.2.12479018、JDK 21、compile SDK 36、target SDK 35、min SDK 28（Android 9）。

```powershell
./tools/Build-Android.ps1
```

`build/android-arm64-lan/android-build/build/outputs/apk/debug/android-build-debug.apk` に開発用APKを生成します。C++はReleaseビルドで、Android標準のデバッグ署名を使用します。

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

## ライセンス

独自コードはMITです。`third_party/nlohmann/json.hpp` はnlohmann/json 3.10.5（MIT、ライセンス本文をヘッダーに保持）です。QtやAndroid関連の同梱ライブラリには、それぞれのライセンスが適用されます。
