# Android署名ビルド

GitHub Actionsの **Android APK** は、`dailyCalendarPuzzleSolver` と同じくGitHub Secretsに登録したJKSと `apksigner` でAPKへ署名します。このリポジトリでは、そのプロジェクトと同じ署名鍵を共用します。秘密鍵・パスワードはソースへ含めません。

## 実行方法

- Actions → Android APK → Run workflow → `signed`：署名済みRelease APKをArtifactsの `android-arm64-signed` から取得できます。
- 同じ画面で `debug`：Secrets不要の開発用APKを生成します。
- `v*` タグをpush：署名済みAPKとSHA-256、署名検証結果をGitHub Releaseへ添付します。

Qt 6.11.1、JDK 21、NDK 27.2.12479018、compile SDK 36、build-tools 36.0.0、ARM64を使用します。Qt側のRelease署名を有効にしてビルドし、最後に `apksigner` で署名・検証します。ビルド失敗を無視する処理は入れていません。

## Secretsと署名者の検証

| 種類 | 名前 | 内容 |
|---|---|---|
| Secret | `ANDROID_KEYSTORE_BASE64` | JKSのBase64 |
| Secret | `ANDROID_STORE_PASSWORD` | ストアのパスワード |
| Secret | `ANDROID_KEY_ALIAS` | キーのエイリアス |
| Secret | `ANDROID_KEY_PASSWORD` | キーのパスワード |
| Variable | `ANDROID_SIGNER_SHA256` | 公開証明書のSHA-256（区切り文字なし） |

生成したAPKの署名者が `ANDROID_SIGNER_SHA256` と一致しなければ、成果物のアップロード・Release公開を失敗させます。一時的なJKSはジョブ終了時に削除し、ArtifactsにはAPK・そのハッシュ・公開署名情報のみを含めます。

既存の `android-signing.properties` とJKSから登録する場合は、PowerShell 7で次を実行します。

```powershell
./scripts/Set-AndroidSigningSecrets.ps1 -SigningPropertiesPath '既存プロジェクト/android-signing.properties'
```

`storeFile` の相対パスはpropertiesファイルのあるディレクトリを基準に解決します。値は標準入力からGitHubへ送信し、パスワードをコマンド引数やログへ表示しません。このスクリプトは既存のJKSやpropertiesファイルを変更しません。

ローカルのデバッグ署名APKと本Workflowの署名済みAPKは署名が異なるため、直接の上書き更新はできません。デバッグ版から切り替えるためにアンインストールした場合、アプリの接続設定も削除されます。以後は同じ署名鍵と適切に増やした `QT_ANDROID_VERSION_CODE` を使って更新してください。

参考：[QtのAPK署名](https://doc.qt.io/qt-6/cmake-variable-qt-android-sign-apk.html)、[androiddeployqt](https://doc.qt.io/qt-6/android-deploy-qt-tool.html)。
