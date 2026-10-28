# Klinglab Stream Mixer for macOS

配信者向けの音声ミキサーです。マイクとアプリの音声をまとめ、配信用とヘッドホン用に別のバランスで送ります。

対象は macOS 14.4 以降、Apple Silicon と Intel です。C++20 と JUCE 8 を使い、アプリのライセンスは [AGPL-3.0-only](LICENSE) です。

## インストール

`KlinglabStreamMixer-1.0.0.pkg` を開き、画面の案内に従ってください。アプリは `/Applications`、OBS プラグインはインストールする利用者の `~/Library/Application Support/obs-studio/plugins/` に入ります。OBS を起動していた場合は再起動してください。Stream Deck プラグインを同梱した配布物では、ミキサーのメニュー「Stream Deck の使い方」から任意で追加できます。

Developer ID がないため、アプリと OBS プラグインは ad-hoc 署名です。インストーラーには Developer ID 署名と公証を行っていません。Gatekeeper で止められたときは、Finder で対象を右クリックして「開く」を選ぶか、起動を試したあとに「システム設定 → プライバシーとセキュリティ → このまま開く」を選んでください。

## アンインストール

ミキサー、OBS、Stream Deck を終了し、[説明書の手順](docs/manual/01-start.md)に従ってください。同梱の `uninstall.command` は対象を表示し、`--apply` を付けたときだけアプリとプラグインを設定フォルダ内の `archive/` へ移します。設定、プロジェクト、音源、ログは残します。

## ビルドと配布物の作成

Mac 上ではリポジトリ直下で `bash scripts/package.sh` を実行します。Linux などから SSH で Mac に同期してビルドする場合は、`KSM_MAC_HOST=ユーザー@Macのホスト bash scripts/package.sh` のように接続先を指定します。Release の universal ビルドと ad-hoc 署名を行い、`dist/KlinglabStreamMixer-<version>.pkg` を出力します。必要なのは Xcode と CMake 3.25 以降です。version は CMake の `PROJECT_VERSION` です。`--without-streamdeck` を付けると Stream Deck プラグインを同梱しません。`dist/` は git 管理外です。

仕様は [docs/SPEC.md](docs/SPEC.md)、Stream Deck との通信形式は [docs/REMOTE-PROTOCOL.md](docs/REMOTE-PROTOCOL.md)、使い方は [docs/manual/01-start.md](docs/manual/01-start.md) にあります。

## 成り立ち

このアプリは、ある Windows 用配信ミキサーの公開された利用者向け説明書を読み、機能だけを仕様書に書き起こして、ゼロから作ったものです（クリーンルーム設計）。元のソフトのコード、バイナリ、画像、文章は使っていません。元のソフトとその作者とは関係がありません。

## ライセンス

アプリは AGPL-3.0-only、OBS プラグイン（`obs-plugin/`）は GPL-2.0-or-later です。VST は Steinberg Media Technologies GmbH の登録商標です。Stream Deck は Corsair Memory, Inc. の商標です。
