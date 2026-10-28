# Klinglab Stream Mixer Stream Deck プラグイン

macOS 用の9アクションを、Elgato の公式 Node.js SDK で実装しています。通信形式は [REMOTE-PROTOCOL.md](../docs/REMOTE-PROTOCOL.md) を参照してください。ミキサーがなくても、模擬サーバーで操作を確認できます。

## 必要な環境

開発とパッケージ作成には Node.js 20.5.1 以降と npm を使います。実機では macOS 14.4 以降、Stream Deck 7.1 以降が必要です。プラグインの Node.js 24 は Stream Deck が用意するため、利用者による Node.js の導入は不要です。

## ビルドと配布ファイル

```bash
cd streamdeck
npm ci --no-audit --no-fund
npm run build
npm run validate
./scripts/pack.sh
```

`pack.sh` はビルド後、公式 CLI で検証して `.streamDeckPlugin` を作ります。保存先は `dist/<実行日時とプロセス番号>/site.klinglab.streammixer.streamDeckPlugin` です。既存のパッケージは上書きしません。必要な npm パッケージが無ければ `npm ci` を先に実行します。

配布ファイルにはバンドル済みの JavaScript と画像・設定画面が入ります。実機で `node_modules` を追加する必要はありません。動的なボタン画像は SVG、manifest 用の画像は独自に生成した PNG です。元の画像や外部サイトの素材は使っていません。

Mac に配布ファイルをコピーして開くと、Stream Deck の導入画面に進みます。導入後、各アクションをキーへ配置してください。FX On/Off と Project は1〜9、STREAM FX・BGM・SE はミキサーに表示される番号を指定します。Stop Sounds は BGM・SE・両方から選びます。LOUD の3アクションに設定項目はありません。

## 模擬サーバー

```bash
npm run mock
```

`127.0.0.1:47321` で待ち受けます。終了は Ctrl+C です。音声や本体アプリは起動しません。

```bash
node mock-server/server.mjs --quiet
node mock-server/server.mjs --fail-project
node mock-server/server.mjs --version=2
node mock-server/server.mjs --port=47322
```

起動オプションはそれぞれ、学習失敗・プロジェクト読み込み失敗・通信バージョン不一致・別ポートでの確認に使います。プラグインの接続先は固定で `ws://127.0.0.1:47321/` です。別ポートは通信モジュールを直接使う場合に指定できます。

## 通信モジュールの呼び出し

`MixerClient` は Stream Deck アプリが無い Node.js 環境でも使えます。次のコマンドは模擬サーバーを同じ前景プロセスで起動し、LOUD を切り替えて状態と SVG を出力したあと、両方を閉じます。

```bash
node --input-type=module <<'NODE'
import { startMock } from './mock-server/server.mjs';
import { MixerClient } from './site.klinglab.streammixer.sdPlugin/src/mixer-client.mjs';
import { renderButton } from './site.klinglab.streammixer.sdPlugin/src/render.mjs';
import { once } from 'node:events';
import { setTimeout } from 'node:timers/promises';
const mock = await startMock({ port: 0 });
const client = new MixerClient({ url: `ws://127.0.0.1:${mock.port}/` });
try {
    const ready = once(client, 'change');
    client.start();
    await Promise.race([ready, setTimeout(5500).then(() => { throw Error('接続待ちが時間切れです。'); })]);
    if (client.status !== 'ready') throw Error('接続できませんでした。');
    const changed = once(client, 'change');
    client.request({ command: 'loudToggle' }, console.error);
    await changed;
    console.log(client.state.loud);
    console.log(renderButton('loud', {}, client));
} finally {
    client.stop();
    await mock.close();
}
NODE
```

9アクションの種類は `fx`, `project`, `streamfx`, `bgm`, `se`, `stop`, `loud`, `learn`, `mode` です。`ButtonController` の `appear(type, action, settings)` と `press(type, action, settings)` は、`id`, `setImage`, `setTitle`, `showAlert` を持つオブジェクトを渡して呼べます。実機と同じ経路で画像・タイトル・警告を受け取れます。

## 接続中の表示

ミキサーが無ければ `MIXER OFF`、通信バージョンが違えば `UPDATE MIXER`、空番号なら `-` です。いずれも押すと警告だけを表示します。切断から2秒後に再接続し、操作はためません。全状態を受け取ってから操作できるようになります。要求の成功応答だけで表示を切り替えることもありません。

番号・名前・状態の文字は SVG 内にも描きます。manifest の `ShowTitle` は `false` にし、Stream Deck のタイトルが画像の文字に重ならないようにしています。SDK の `setTitle` にも状態を渡します。利用者がタイトル表示を有効にした場合は、文字が重なることがあります。

## ファイルと記録

- `site.klinglab.streammixer.sdPlugin/src/`: 公式 SDK の接続、ボタン制御、通信、描画。
- `site.klinglab.streammixer.sdPlugin/ui/`: 番号と停止対象の設定画面。
- `mock-server/server.mjs`: 音声処理を行わない模擬サーバー。
- `scripts/build.mjs`, `scripts/pack.sh`: バンドル・画像生成・配布ファイル作成。

生成物の `bin/`, `imgs/`, `dist/`, `verification-output/` と依存の `node_modules/` は git 管理の対象外です。ソースのライセンスはリポジトリと同じ AGPL-3.0-only です。バンドルに含める依存のライセンスも配布ファイルに収録します。
