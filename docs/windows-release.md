# Windowsサーバーの配布

GitHub Actionsの **Windows Server Release** で、Windows x64向けサーバーZIPを作成します。Windowsへのコード署名は行いません。

- 手動実行：Actions → Windows Server Release → Run workflow。`windows-server-x64` ArtifactからZIPとSHA-256を取得できます。
- `v*` タグをpush：同じZIPとSHA-256をGitHub Releaseへ添付します。AndroidのWorkflowと同じタグを使用できます。

ZIPは `TVTestRemoteServer-<バージョン>-windows-x64.zip` です。展開後に `tvt-remote-server.exe` を実行してください。Qtを別途インストールする必要はありません。DLL・plugins・licensesも含め、フォルダ全体を配置してください。

サーバー専用パッケージのため、Windowsリモコン・デモの実行ファイルは含めません。AndroidアプリとTVTestプラグインは別途用意します。通知領域、設定画面、ポート変更、自動起動設定の操作方法は [導入と接続](getting-started.md) を参照してください。

## ビルドと検証

Qt 6.11.1とMinGW 13.1のReleaseビルドを使用します。既存の統合テストとウィンドウ表示テストを実行してから、`windeployqt` でDLLとプラグインを収集します。Qtと同じ版の公式ソースからライセンス本文を同梱します。

Qt 6.11以降のWindows配布構成に対応するため、aqtinstallは対応済みの公式ソースのコミットへ固定しています（[上流の対応](https://github.com/miurahr/aqtinstall/pull/1000)）。

さらに、Qt関連のPATHとプラグイン環境変数を除いた環境で、パッケージのGUIサーバーが起動し、`--stop` で正常終了することを確認します。テスト結果は別の `windows-test-results` Artifactに保存します。

ローカルで同じ確認を行う場合：

```powershell
./scripts/Build-Windows.ps1
./scripts/Package-Windows.ps1 -ServerOnly -OutputDirectory ./build/package/windows-server
./scripts/Test-WindowsServerPackage.ps1 -PackageDirectory ./build/package/windows-server
```

Qtの場所とソースの場所は、`Package-Windows.ps1` の `-Qt`、`-Compiler`、`-QtSources` で変更できます。GitHub Actionsではライセンス取得元もQtのバージョンに対応するコミットへ固定しています。Qtを更新するときはWorkflowの `QT_VERSION` と `QTBASE_LICENSES_REF` を合わせて更新してください。
