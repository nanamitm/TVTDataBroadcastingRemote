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
