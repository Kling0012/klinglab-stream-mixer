# 困ったとき

**オケがミキサーに届かない（INST INPUT のメーターが動かない）**
アプリで実際に音が鳴っているか、INST INPUT の左が `APP` か、システムオーディオ録音が許可されているかを確かめてください。一覧を開き直してアプリを選び直すと直ることがあります。

**自分の声が聞こえない**
VOCAL MON 列の `MON` が黒いか、MONITOR OUT に正しいヘッドホンを選んでいるか、マイクの使用が許可されているかを確かめてください。

**OBS に音が届かない**
ミキサーが起動しているか、STREAM の INST・VOCAL・MASTER が下がりすぎていないか、INST INPUT のメーターが動いているかを確かめてください。

**配信で声が二重に聞こえる**
OBS の元の「マイク」をミュートし、「デスクトップ音声」を無効にしてください。

**音がぷつぷつ途切れる**
`PANEL` でバッファを1段大きくするか、重いエフェクトを外してください。

**ノイズ除去で声がこもる**
つまみを 3〜6 dB まで下げ、黙ったまま `RELEARN` を押し直してください。

**持っているプラグインが一覧に出ない**
「フォルダを指定して追加で検索」で、プラグインのフォルダ（`/Library/Audio/Plug-Ins/VST3` など）を選んでください。それでも出ないときは「すべて検索し直す」を使います。

## アンインストール

`/Applications` から Klinglab Stream Mixer を削除し、`~/Library/Application Support/obs-studio/plugins/klinglab-stream-mixer-source.plugin` を削除します。設定とプロジェクトは `~/Library/Application Support/Klinglab Stream Mixer/` に残るので、不要なら一緒に削除してください。
