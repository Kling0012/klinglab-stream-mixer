# はじめに

Klinglab Stream Mixer は、配信や歌枠のための音声ミキサーです。マイクの声と、ほかのアプリで流している音（カラオケ音源やゲームの音）をまとめて OBS に送ります。同時に、自分のヘッドホンへは別のバランスで返します。

できることは次のとおりです。

- 自分に聞こえる音と、視聴者に届く音を別々に調整する
- アプリの音を、仮想ケーブルなしで直接取り込む（最大8アプリ）
- 声にエフェクト（VST3・Audio Unit）をかける
- マイクの定常ノイズを、声を遅らせずに減らす
- BGM と効果音をボタンひとつで鳴らす
- 配信の音を YouTube 向けの大きさにそろえる（LOUD）
- 設定をプロジェクトとして保存し、ショートカットや Stream Deck で切り替える

## 動作環境

macOS 14.4 以降が必要です。Apple Silicon と Intel のどちらでも動きます。配信には OBS Studio 30 以降を使います。

## 最初にすること

1. `KlinglabStreamMixer-1.0.0.pkg` を開き、画面の案内どおりに進めます。アプリは `/Applications`、OBS プラグインはインストールする利用者の `~/Library/Application Support/obs-studio/plugins/` に入ります。OBS を起動していた場合は再起動してください。
2. インストーラーやアプリを開くと、「開発元を確認できません」と出ることがあります。その場合は、Finder で対象を右クリックして「開く」を選んでください。起動を試したあとに「システム設定 → プライバシーとセキュリティ」の下に出る「このまま開く」でもかまいません。現在はアプリと OBS プラグインを ad-hoc 署名しており、インストーラーの Developer ID 署名と公証は行っていません。
3. マイクとシステムオーディオ録音の使用を求められたら「許可」を押します。どちらも、拒否すると音が入りません。
4. 初回はチュートリアルが始まります。光っている部分を順に設定すると、OBS に音が届くところまで進みます。

Stream Deck プラグインを同梱した配布物では、ミキサーのメニュー「Stream Deck の使い方」から追加できます。Stream Deck を使わない場合は追加する必要はありません。

## アンインストール

1. ミキサー、OBS、Stream Deck を終了します。
2. ターミナルで次を実行し、表示される移動対象を確認してください。この段階ではファイルを変更しません。

```bash
bash "/Applications/Klinglab Stream Mixer.app/Contents/Resources/uninstall.command"
```

3. 表示した対象を取り除くには、次を実行してください。アプリ、OBS プラグイン、Stream Deck プラグインのうち存在するものを、設定フォルダ内の `archive/uninstall-日時-番号/` へ移します。設定、プロジェクト、音源、ログは残します。

```bash
bash "/Applications/Klinglab Stream Mixer.app/Contents/Resources/uninstall.command" --apply
```

権限がなく移動できない場合は、Finder で管理者の認証を使ってアプリを取り除いてください。OBS プラグインの場所は上記の `obs-studio/plugins/`、Stream Deck プラグインの場所は `~/Library/Application Support/com.elgato.StreamDeck/Plugins/site.klinglab.streammixer.sdPlugin` です。設定フォルダは `~/Library/Application Support/Klinglab Stream Mixer/` にあります。

## ログと終了後の通知

起動ログは設定フォルダの `logs/` に、今回分を含めて5ファイル残します。さらに古いログは `archive/logs/` へ移します。前回の終了処理が完了しなかった場合は、次回起動時に画面下部へ30秒間知らせます。クラッシュのほか、強制終了や電源断でも表示されます。
