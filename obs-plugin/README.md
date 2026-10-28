# Klinglab Stream Mixer OBS plugin

macOS の OBS に音声ソース「Klinglab Stream Mixer」を追加するプラグインです。POSIX 共有メモリからステレオ音声を読み、`obs_source_output_audio` へ渡します。設定項目はありません。

対象は macOS 14.4 以降、OBS Studio 30/31、Apple Silicon と Intel です。ビルド成果物は universal binary になります。

## ビルドと導入

リポジトリのルートから実行します。Mac 上ならその場でビルドし、Linux などからは `KSM_MAC_HOST` に指定した Mac へ SSH で同期してビルドします。

```bash
bash scripts/obs-build.sh
```

Mac 側のソース配置先は `$KSM_MAC_DIR/obs-plugin`（既定 `~/dev/ksm/obs-plugin`）です。スクリプトは `obs-plugin/` と `scripts/obs-*.sh` だけを rsync で同期します。同期先のファイルを削除するオプションは使いません。

Mac には Xcode、rsync、curl と CMake（`KSM_CMAKE` で場所を指定できます）が必要です。brew と sudo は使いません。導入済みプラグインの置き換えに備え、OBS を終了してから実行してください。OBS が起動しているとスクリプトは終了し、プラグインを置き換えません。

OBS.app は `/Applications/OBS.app`、`~/Applications/OBS.app` の順に探します。どちらにもなければ、[OBS 公式の 31.1.2 リリース](https://github.com/obsproject/obs-studio/releases/tag/31.1.2) から実機に合う dmg を取得し、`~/Applications/OBS.app` に配置します。既存の OBS が30/31以外ならビルドを中止します。

ヘッダーは導入済み OBS と同じタグの [obs-studio](https://github.com/obsproject/obs-studio) ソースから取得します。公式 dmg がアーキテクチャ別の場合は、同じバージョンのもう一方の dmg も取得します。両方の `libobs.framework` のバイナリを lipo でまとめ、ビルド専用の universal framework としてリンクします。OBS.app 内の framework は変更しません。

依存物と dmg は `~/dev/ksm/build-obs-deps/` に保持します。ビルド後は ad-hoc 署名と署名検証を行い、次の場所へ配置します。

```text
~/Library/Application Support/obs-studio/plugins/klinglab-stream-mixer-source.plugin
```

既存プラグインがある場合は、置き換える前に `~/dev/ksm/build-obs-deps/archive/<日時>/` へコピーします。プラグインに libobs 自体は同梱せず、実行時は OBS.app の `@rpath/libobs.framework/Versions/A/libobs` を使います。

送信ツールの生成先は次のとおりです。

```text
~/dev/ksm/build-obs/ksm-shm-test
```

## OBS での利用

1. OBS を起動します。初回の権限案内やセットアップは利用者が操作してください。このプラグインはマイク・画面・システム音声の録音許可を要求しません。
2. シーンの「ソース」で「＋」から「Klinglab Stream Mixer」を追加します。設定画面は出ません。
3. ミキサーを起動し、OBS の音声ミキサーでレベルが動くことを確認します。本体からの送信は、このレーンの実装範囲外です。
4. OBS 側の元の「マイク」をミュートし、「デスクトップ音声」を無効にして、配信音はこのソース1本にします。

共有メモリが無い場合、形式が違う場合、送信が止まっている場合は無音を出します。約1秒ごとに接続を試みます。送信側が起動し直した場合は `sessionId` と `writePos` を見て最新位置へ同期します。読み取り位置が100msを超えて遅れた場合も、古い音をまとめて再生せず最新位置へ移ります。

## 440Hz の送信ツール

ミキサー本体が共有メモリに書く前の確認に使います。本体を終了した状態で、Mac のターミナルから実行してください。音量は0.1、48kHzのステレオで、両チャンネルに同じ440Hzのサイン波を書きます。

```bash
~/dev/ksm/build-obs/ksm-shm-test 10
```

引数は秒数で、1〜86400の整数です。指定時間後に終了します。途中で止める場合は Ctrl+C を押してください。OBS 側でこのソースをモニターすると音が出るため、確認前に出力音量を下げてください。

同時に起動した送信ツールはロックで拒否します。既存の共有メモリで書き込み位置が動いている場合も、上書きせず終了します。止まっている本体との所有権競合まで判定できる仕組みではないため、本体と同時に使わないでください。ツールは共有メモリを unlink せず、次回の起動時に再利用します。送信終了後は位置が進まなくなり、プラグインは残りの音声を読んだ後に無音へ戻ります。

## 共有メモリの実装

共通の定義は [include/ksm_shm.h](include/ksm_shm.h) です。本体もこのヘッダーを使ってください。C と C++ の両方に対応し、4096バイトのヘッダー、カウンターの位置、float32のサイズ、atomic 操作がロックフリーであることをコンパイル時に確認します。

- 名前は `/klinglab-stream-mixer-v1`、magic はリトルエンディアンの `KSM1`、version は1です。
- ヘッダーの後に、131072フレーム分の float32 インターリーブ音声を置きます。チャンネル数は2、サンプルレートは44100・48000・96000です。
- `writePos` は書き込み済みのフレーム数です。送信側はサンプルを書いた後、release store で公開します。受信側は acquire load で読みます。
- 送信側の起動ごとに、乱数から非ゼロの `sessionId` を作ります。初期化中は0にし、位置と形式を設定してから新しい値を公開します。
- プラグインは `O_RDONLY` と `PROT_READ` で接続します。共有メモリには書き込みません。音声は約10msごとに渡し、タイムスタンプは `os_gettime_ns` を基準に進めます。

同じ共有メモリを再利用する場合は、サイズを縮めないでください。macOS がページ単位でサイズを切り上げるため、`fstat` が返すサイズは必要なバイト数以上を許容します。形式を変える場合は共有メモリ名のバージョンも上げてください。フィールドごとの意味と初期化順序はヘッダー内のコメントに記載しています。

## ログと確認結果

OBS のログは `~/Library/Application Support/obs-studio/logs/` にあります。読み込みに成功すると次が出ます。

```text
[KSM] source type registered: klinglab-stream-mixer-source
```

ソース作成、共有メモリへの接続、再同期、音声受信、無音への移行、ソース終了時のフレーム数も `[KSM]` で記録します。今回の確認では、OBS GUI は初回の権限案内で待機したため、GUI が書くログでのプラグイン読み込みとメーターの目視確認は未確認です。libobs API からの読み込み・ソース登録・音声受信は確認済みです。

## ライセンスと参照

このディレクトリのプラグイン・共通ヘッダー・送信ツール、および `scripts/obs-build.sh` は GPL-2.0-or-later です。GPL version 2 の全文を [LICENSE](LICENSE) に収録しています。配布時には対応するソースとビルド手順を提供し、リンク先の libobs の条件にも従ってください。

実装の仕様は `docs/SPEC.md` 第5章を根拠にしています。API とバンドル構成には次の OBS 公式資料を参照しました。他のプラグインの実装コードは使っていません。

- [obs-studio 31.1.2 の公開 API](https://github.com/obsproject/obs-studio/tree/31.1.2/libobs)
- [OBS 公式プラグインテンプレートの macOS CMake 設定](https://github.com/obsproject/obs-plugintemplate/blob/master/cmake/macos/helpers.cmake)
- [OBS 30.2.3 のソース定義](https://github.com/obsproject/obs-studio/blob/30.2.3/libobs/obs-source.h)
