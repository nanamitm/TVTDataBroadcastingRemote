# TVTDataBroadcastingRemote

TVTestデータ放送プラグイン **TVTDataBroadcastingWV2** をAndroidから操作するQt 6/C++アプリと、Windows用中継サーバーです。初期実装です。

```text
Android / Windows リモコン（Qt Quick + C++）
    │ WSS / JSON / トークン認証
Windows 中継サーバー（Qt Core / Network / WebSockets）
    ├─ 名前付きパイプ ─ TVTest A のプラグイン（Qt不要）
    └─ 名前付きパイプ ─ TVTest B のプラグイン（Qt不要）
```

## 実装済み

- 接続中のTVTestの一覧、名前・放送局・使用中状態の表示。
- 対象ごとに1接続だけが操作権を取得。異なるTVTestは同時に別のリモコンから操作可能。
- d、上下左右、決定、戻る、青赤緑黄、数字1～12（10/0）。方向キーは長押しで単発操作を反復。
- プラグインの既存コマンドへの振り分け。BML・B62・HTML5や数字キーの選局処理を再実装しない。
- プラグインの表示機能が無効なときも接続を維持し、dボタンで有効化。
- 切断時に操作先を解除。接続復旧後は操作先の再選択が必要。未送信操作は蓄積・再送しない。
- ランダムな起動接続ID、順序番号、要求ID、応答、2秒タイムアウト。
- サーバー時刻から入力の期限を算出し、TCP内で遅延した古い操作も破棄。
- TLS 1.2以上、SHA-256証明書ピン、トークン認証。平文WSはループバック限定。
- サーバー二重起動の防止、IPCへの現在ユーザーのアクセス制限、メッセージ・接続・送信待ち上限。
- ネイティブ接続SDKは、通信スレッドとUIスレッドを分離。世代の違う入力と期限切れ入力を破棄。

初版のペアリングは、PCで生成したトークンと証明書指紋の手動入力です。トークンはアプリ設定に永続保存しません。アプリがバックグラウンドへ移行すると切断し、操作権を解放します。復帰時は再接続してください。

0.1.1から、接続用のトークンは既定で3桁の数字です。短いコードを使う場合、サーバー全体で認証に5回失敗すると、サーバーを再起動するまで新規認証を停止します。認証済みの接続とTVTestの登録は維持します。従来の長いトークンも引き続き使用できます。

## フォルダ

| パス | 内容 |
|---|---|
| `server/` | Windows用中継サーバー |
| `app/` | Android / Windows共通リモコンとQML画面 |
| `shared/` | Qt側のメッセージ処理・キー定義 |
| `sdk/` | Qt不要のWin32プラグイン接続部 |
| `tools/` | ビルド、証明書作成、デモ接続 |
| `tests/` | 実ソケット・実パイプによる統合テスト |
| `docs/protocol.md` | 通信仕様v1 |
| `assets/icons/` | リモコン／サーバーのSVG、PNG、Windows用ICO |
| `android/res/` | Android用の標準・アダプティブ・モノクロアイコン |

アイコンの元データはSVGです。Windowsの実行ファイルとリモコンのウインドウにアイコンを設定し、Androidのランチャーには端末の形状・テーマに対応するアイコンを同梱しています。

アイコン素材を更新した場合は、デスクトップQtのGui/Svgを使って生成済みPNG・ICOを更新します（通常のビルドにはSvgは不要です）。

```powershell
cmake -S . -B build/icon-tools -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 `
    -DREMOTE_BUILD_APP=OFF -DREMOTE_BUILD_TESTS=OFF -DREMOTE_BUILD_ICON_TOOLS=ON
cmake --build build/icon-tools --target remote-render-icons
./build/icon-tools/remote-render-icons.exe .
```

## Windowsビルド

Qt 6.8以上のCore / Network / WebSockets / Quick / QuickControls2 / Test、CMake 3.24以上、C++20コンパイラが必要です。ローカル検証環境はQt 6.11.1、MinGW 13.1です。

```powershell
./tools/Build-Windows.ps1
```

スクリプトのパラメーターでQt・コンパイラ等の場所を変更できます。Qt Creatorから `CMakeLists.txt` を開くこともできます。

```powershell
# まずデスクトップで接続を確認する例（3つのターミナルで実行）
./build/windows/tvt-remote-server.exe --token-file ./local/token.txt
./build/windows/tvt-remote-demo.exe --name 'TVTest A' --channel 'テスト放送A'
./build/windows/tvt-remote-demo.exe --name 'TVTest B' --channel 'テスト放送B'
```

`local/` は事前に作成してください。QtをPATHに追加している開発端末での実行例です。`tvt-remote.exe` に `ws://127.0.0.1:18765` と `token.txt` の内容を入力します。ループバックでは証明書指紋は空で構いません。デモはキー名を出力するだけで、TVTestを操作しません。`tvt-remote-native-demo.exe` はQt不要のSDKを使用するデモです。

## LAN接続

```powershell
# 専用フォルダを現在ユーザーだけが読めるACLにして、証明書とトークンを生成
./tools/New-ServerIdentity.ps1

# 開発環境ではQtのbinをPATHへ追加して実行
./build/windows/tvt-remote-server.exe --address 0.0.0.0 --port 18765 `
    --token-file ./local/token.txt --cert ./local/server-cert.pem --key ./local/server-key.pem
```

Androidアプリの接続設定へ次を入力します。

1. `wss://PCのLAN内IP:18765`
2. PCの `local/token.txt` の内容
3. PCの `local/fingerprint.txt` の内容

自己署名証明書は指紋と有効期間を確認して信頼します。指紋が空の場合は通常のCA検証を使います。証明書やトークンはGitへ登録しないでください。Windowsの証明書ストアへ登録する必要はありません。必要に応じてWindowsファイアウォールでこのアプリへのプライベートネットワークからの接続を許可してください。自動的なファイアウォール変更は行いません。

トークンを変更する場合はファイルを更新してサーバーを再起動します。初版ではクライアントごとの登録・失効管理はありません。

## TVTest側

対応する `TVTDataBroadcastingWV2` の `feature/remote-broker-bridge` ブランチに接続部があります。リモコン用SDKのヘッダーはプラグインの `remote/` へコピーして管理し、別チェックアウトへのビルド依存を作りません。

```ini
[TVTDataBroadcastingWV2]
EnableRemoteBridge=1
RemoteName=リビング
RemotePipeName=TVTDataBroadcastingRemote-v1
```

各TVTestに違う名前を付けてください。設定はTVTest起動時に読みます。中継サーバーの `--pipe` を変更する場合は `RemotePipeName` も合わせます。プラグインの既定値は接続無効です。現在稼働しているTVTestの設定・配置ファイルはビルドだけでは変更しません。

`dispatched` は既存コマンド処理へ渡したことを意味します。コンテンツがそのキーを使用したことや画面遷移が完了したことは保証しません。既にプラグインへ渡した入力は、切断によって取り消せない場合があります。

## Androidビルド

Qt 6のAndroid ARM64キット、同じ版のデスクトップQt、Android SDK/NDK、JDKが必要です。WSS用のOpenSSLを `SDK/android_openssl` から同梱します。

検証環境: Qt 6.11.1、NDK 27.2.12479018、JDK 21、compile SDK 36、target SDK 35、min SDK 28（Android 9）。対象SDKを上げる際にはAndroidのローカルネットワーク権限仕様も確認してください。

```powershell
./tools/Build-Android.ps1
```

`build/android-arm64/android-build/build/outputs/apk/debug/android-build-debug.apk` にインストール可能な開発用APKを生成します。C++はReleaseビルドで、Androidの標準デバッグ署名を使用します。公開配布用の署名設定、Google Playへの登録、端末への自動インストールは行いません。

## 検証と未対応

統合テストでは複数対象の振り分け、操作権競合、未認証・不正メッセージの拒否、分割受信、タイムアウト、TLS証明書ピン、サーバー再起動後の再登録、UIスレッド上のコマンド呼び出しを確認します。証明書テストではOpenSSLで一時的な証明書を生成します。

```powershell
ctest --test-dir build/windows --output-on-failure
```

スマホでの実操作と、実放送を表示したTVTestでのBML/B62/HTML5操作は別途実機確認が必要です。

今後の候補: QRによるペアリング、端末別の認証情報と失効、自動発見、サーバーのトレイUI、音量・選局専用ボタン、PC画面の転送。現時点ではLAN内でPCの画面を見ながら操作するリモコンです。

## ライセンス

このリポジトリの独自コードはMITです。`third_party/nlohmann/json.hpp` はnlohmann/json 3.10.5（MIT、ライセンス本文をヘッダーに保持）です。Qt、OpenSSL、Android関連の同梱ライブラリにはそれぞれのライセンスが適用されます。使用するQtキットのライセンス・再配布条件に従ってください。
