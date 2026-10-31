# 音声変更の比較

音質変更は、同じ入力・同じ出力サンプリング周波数・同じモードの変換結果で比較します。
以前と違う音が出ることと、当時の端末に近づくことは別です。実機の録音がない場合、
数値で確認できた改善と聴感・再現度の未検証部分を分けて記録します。

## WAV の数値比較

Python 3 の標準ライブラリだけで使えます。

```shell
python3 tools/compare-wav.py before.wav after.wav
python3 tools/compare-wav.py before.wav after.wav --require-identical
python3 tools/compare-wav.py before.wav after.wav --start-frame 22050 --frames 22050
python3 tools/compare-wav-test.py
```

対象は非圧縮 PCM16 WAV です。サンプリング周波数とチャンネル数の不一致、
不完全なフレームや宣言より短い音声データはエラーになります。ファイル全体を
メモリに載せずに読み取り、入力は変更しません。FFmpeg や追加パッケージは不要です。

出力は JSON です。主に次を確認します。

- `identical_pcm`: PCM とフレーム数が一致するか。WAV メタデータの一致ではありません。
- `reference_frames` / `candidate_frames`: 音符・余韻・末尾の長さが変わっていないか。
- `first_different_frame` / `last_different_frame`: PCM の変化が始まる・終わる位置。
  無音の末尾だけ長さが違う場合は両方 `null` でも、`identical_pcm` は false です。
- `channel_metrics`: 左右などチャンネルごとのピーク、RMS、DC、差分 RMS、変更サンプル数。
- `reference_only_frames` / `candidate_only_frames`: 比較相手にない末尾フレーム数。
- `nonzero_samples_in_unmatched_tail`: 片側にしかない末尾のうち、ゼロでないサンプル数。

差分は両ファイルのタイムライン全体で集計します。短い側の欠けた末尾は差分計算で
ゼロとして扱い、長い側の末尾を捨てません。各入力自身の RMS・DC は、その入力に
実在するサンプルだけで計算します。RMS がゼロの `rms_dbfs` は `null` です。
`full_scale_samples` は数値が -32768 または32767に等しい個数であり、歪みの証明ではありません。

通常は比較成功で終了コード0です。`--require-identical` を付けた場合、不一致は1、
読み込み・形式エラーは2になります。変化しないはずのファイルの回帰確認に使えます。

### 立ち上がりと持続音を別々に測る

`--start-frame` は両入力で比較を始めるフレーム位置、`--frames` は比較する長さです。
例えば44,100 Hzの上記コマンドは、開始0.5秒から1.0秒までを測ります。
ステレオでも1フレームは左右1組です。録音開始位置は事前に揃えてください。
この指定による自動の位置合わせや音量調整はありません。

`--frames` を指定した区間は両入力に完全に収まる必要があり、足りなければエラーです。
省略すると開始位置から各入力の末尾まで比較し、長さが違う末尾も従来どおり報告します。
開始位置は0以上、長さは1以上の整数です。開始位置だけなら、末尾ちょうども指定できます。

区間指定時の `identical_pcm`、各入力の `*_frames` / `*_seconds`、チャンネル指標は
**選んだ区間だけ**の結果です。ファイル全体の一致や区間外のデータの完全性を保証しません。
`first_different_frame` / `last_different_frame` は区間内でも元ファイルの絶対位置です。
`comparison_start_frame` / `comparison_frame_limit` が指定範囲、
`reference_source_frames` / `candidate_source_frames` がヘッダー上の元の総フレーム数です。
元のWAVは変更しないため、切り出したファイルを作らず同じ条件で再測定できます。

## 聴き比べと判断

まず同一設定の WAV を数値比較し、意図しない長さ・左右・音量の変化を調べます。
その後、別途聴感上の音量を揃えた A/B 比較を行います。このツールは自動で音量を
揃えないため、ゲインの変化が隠れません。

実機の録音との比較には、録音経路・開始位置・音量の校正が別途必要です。単なる
サンプル差分の小ささを再現度スコアにしません。また、DC の減少や RMS の増加だけで
音質改善と判断しません。音色・アタック・余韻・ビブラート・主旋律と伴奏の関係を
個別に確認します。

個人所有の MMF、生成 WAV、参照録音はローカルで扱い、リポジトリに追加しません。
