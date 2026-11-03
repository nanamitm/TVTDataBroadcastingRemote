# 接続エージェントのサンプル

| ディレクトリ | 内容 | ビルド対象 |
|---|---|---|
| `qt-agent/` | Qtのローカルソケットを使う模擬TVTest | `tvt-remote-demo` |
| `native-agent/` | Qt不要のWin32接続SDKの使用例 | `tvt-remote-native-demo` |

Windowsの通常ビルドで生成します。[ビルド手順](../docs/building.md)を参照してください。以下のコマンドはリポジトリのルートから実行します。

TVTestの代わりにデモで振り分けを確認する場合は、別々のターミナルで次を実行します。

```powershell
./build/windows/tvt-remote-server.exe
./build/windows/tvt-remote-demo.exe --name 'TVTest A' --channel 'テスト放送A'
./build/windows/tvt-remote-demo.exe --name 'TVTest B' --channel 'テスト放送B'
```

開発端末ではQtのbinをPATHに追加してください。デモは入力名を出力するだけでTVTestを操作しません。`tvt-remote-native-demo.exe` はQt不要のSDKを使用します。
