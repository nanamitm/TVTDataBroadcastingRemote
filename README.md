# TVTDataBroadcastingRemote

TVTestのデータ放送プラグイン **TVTDataBroadcastingWV2** をAndroidから操作する、Qt 6/C++のリモコンアプリとWindows用中継サーバーです。

```text
Android / Windows リモコン
    │ LAN内 WebSocket（ws://）
Windows 中継サーバー
    ├─ 名前付きパイプ ─ TVTest A のプラグイン
    └─ 名前付きパイプ ─ TVTest B のプラグイン
```

LAN内でURLを指定して接続します。接続の認証、証明書、ペアリングは使用しません。操作先識別トークンはTVTestを再起動しても同じ操作先へ復帰するための公開識別情報です。

## 使い始める

1. 対応プラグインをTVTestへ導入し、中継接続を有効にします。
2. Windowsで `tvt-remote-server.exe` を起動します（既定ポート: 18765）。
3. Androidアプリの歯車から `ws://PCのLAN内IP:18765` を指定し、操作するTVTestを選びます。

サーバーの終了は、Windowsの通知領域にあるサーバーアイコンを右クリックして「終了」、または `tvt-remote-server.exe --stop` で行います。

状態画面の「Windowsサインイン時に自動起動」で、サインイン時の常駐を設定できます（初期値はオフ）。

同じ画面の「待受ポート」からポート番号を変更・保存できます。変更後はAndroidアプリの接続URLも更新してください。

設定例、自動接続、複数TVTestの識別は [導入と接続](docs/getting-started.md) を参照してください。

## ドキュメント

- [導入と接続](docs/getting-started.md)
- [Windows・Androidのビルドと検証](docs/building.md)
- [GitHub ActionsでAndroid署名APKを作成](docs/android-release.md)
- [GitHub ActionsでWindowsサーバーZIPを作成](docs/windows-release.md)
- [接続エージェントのサンプル](examples/README.md)
- [プラグイン接続SDK](sdk/README.md)
- [通信仕様](docs/protocol.md)
- [開発・公開時の確認](CONTRIBUTING.md)

## 機能

- 接続中のTVTestの名前、放送局、使用中状態を表示。
- 歯車の設定画面で接続・TVTest選択・操作権の解放を行い、通常画面には選択中のTVTestとリモコンを表示。
- 対象ごとに1接続が操作権を取得。別々のTVTestは同時に操作可能。
- d、上下左右、決定、戻る、青赤緑黄、数字1～12（10/0）。方向キーの長押しは単発操作を反復。
- 字幕の表示切替、消音、音量（スライダー・±ボタン）。現在の状態を表示し、対応していないプラグインでは無効表示。
- 放送波（チューニング空間）ごとのタブでチャンネル一覧と現在の番組名を表示し、タップで選局。
- チャンネル一覧でBonDriverを選び、別のBonDriverの局もそのまま選局（録画中は切り替えない）。
- 方向キーは同じ三角形を回転して図形描画し、端末のフォントや絵文字に依存しない表示に統一。
- Android版は日本語フォントNoto Sans JPを同梱し、端末のCJKフォント選択による漢字の字形の違いを防止。
- BML・B62・HTML5や数字キーの選局処理は、既存プラグインのコマンドへ振り分け。
- ランダムな接続ID、順序番号、入力期限、応答、タイムアウト。古い操作を再送しない。
- 切断・再接続、サーバー再起動時のTVTest再登録、二重起動防止。
- メッセージサイズ、送信待ち、入力頻度、接続数の上限を設定。
- ネイティブ接続部は通信スレッドとUIスレッドを分離し、古い世代や期限切れの入力を破棄。
- アプリとサーバーに専用アイコン。Androidはアダプティブ・モノクロ表示にも対応。

`dispatched` は既存の入力経路へ渡したことを示し、放送コンテンツがキーを使用したことや画面遷移の完了を示しません。すでにプラグインへ渡した操作は、切断時に取り消せない場合があります。

## フォルダ構成

```text
src/
  app/          Android / Windows共通のC++・QMLリモコン
  server/       Windows中継サーバー
  shared/       Qt側の通信定義
sdk/            Qt不要のWin32プラグイン接続ヘッダー
examples/       Qtエージェントとネイティブエージェントのサンプル
android/        AndroidManifest・Android用リソース
assets/         アプリ・サーバーのアイコン元データと生成済み画像
tests/          実ソケット・実パイプによる統合テスト
scripts/        ビルド・パッケージ作成用PowerShellスクリプト
tools/          アイコン生成など開発補助ツール
docs/           導入・ビルド・通信仕様
third_party/    同梱ライブラリ
```

`build/` はビルド・配布物、`work/` は検証用の一時ファイル、`local/` は端末固有の設定です。これらはGit管理の対象外です。

## ライセンス

独自コードはMITです。`third_party/nlohmann/json.hpp` はnlohmann/json 3.10.5（MIT、ライセンス本文をヘッダーに保持）です。QtやAndroid関連の同梱ライブラリには、それぞれのライセンスが適用されます。
