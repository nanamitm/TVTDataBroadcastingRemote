# Win32プラグイン接続SDK

TVTestプラグインから中継サーバーへ接続する、ヘッダーのみのC++17以上のSDKです。Qtは不要です。

| ファイル | 用途 |
|---|---|
| `RemoteAgent.h` | 名前付きパイプ接続、状態通知、入力キューと応答 |
| `Win32RemoteBridge.h` | メッセージ専用ウィンドウで入力をUIスレッドへ渡す |
| `TargetIdentity.h` | INIに保存する操作先識別トークンの生成・検証 |

`RemoteAgent.h` より前に nlohmann/json のヘッダーを読み込みます。`TargetIdentity.h` を使用する場合は `ole32` をリンクしてください。Windows APIを使用するため、Windows向けです。

基本的な使い方は [ネイティブサンプル](../examples/native-agent/main.cpp)、通信形式は [通信仕様](../docs/protocol.md) を参照してください。

TVTDataBroadcastingWV2との連携では、このディレクトリの3つのヘッダーをプラグイン側の `TVTDataBroadcastingWV2/remote/` へ同期します。SDK変更時は両方のリポジトリでビルド・検証してください。プラグイン本体は別リポジトリで管理します。

操作先識別トークンは公開される識別情報で、認証には使いません。同じトークンを持つTVTestが複数存在する場合の自動選択は中継サーバーとアプリが停止します。

## 字幕・音量などの値つき操作（0.5.0）

`Win32Bridge::Start` の `Control` コールバック付きオーバーロードを使うと、`("caption", true)`・`("volume", 40)`・`("mute", false)` のような値つき操作をUIスレッドで受け取れます。対応する操作と現在値は、状態コールバックが返すJSONの `controls` オブジェクトで通知してください（例: `{"caption":false,"volume":40,"mute":false}`）。中継サーバーは `controls` に含まれる操作だけをプラグインへ転送するため、`controls` を通知しない従来のプラグインには届きません。操作後は状態がすぐ再通知されます。

## チャンネル一覧と選局（0.6.0）

`Win32Bridge::Start` の `ChannelList` コールバック付きオーバーロードで、チューニング空間ごとのチャンネル一覧（`[{"space":0,"name":"…","channels":[…]}]`）を返すと、ブリッジが16 KiB未満のページに分割して送信します。選局は `Control` コールバックに `("channel", {space,index,serviceId,networkId,transportStreamId})` として届きます。状態の `controls.channel` に現在のチャンネルを通知したプラグインだけが、一覧要求と選局を受け取ります。

## BonDriverの切り替え（0.7.0）

`DriverChannelList` コールバック（引数は要求されたBonDriverのファイル名、空なら使用中のもの）で `{"driver","current","drivers":[…],"spaces":[…]}` を返すと、ブリッジがBonDriver一覧（`channelDrivers`）とBonDriver名付きのページを送信します。選局値の `driver` が使用中と異なる場合は、プラグイン側で切り替えてから選局してください。
