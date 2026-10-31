# オリジナルの比較用音声を作る

著作物の曲や端末 ROM を使わず、同じ入力で音声トラックの復号・リサンプリングを
比較するための合成信号です。音楽としての音色再現度を評価するものではありません。

```shell
python3 tools/generate-audio-probes.py build/my-audio-probes
python3 tools/generate-audio-probes.py build/my-smaf-probes --smaf
```

最初のコマンドは Python 3 の標準ライブラリだけで PCM16 のモノラル WAV を作ります。
`--smaf` は FFmpeg の `adpcm_yamaha` エンコーダーと `mmf` muxer を使い、同じ信号を
音声トラックだけの MMF に格納します。さらに FFmpeg で復号した参照 WAV も生成します。
出力先ディレクトリが既に存在する場合は拒否し、録音などを上書きしません。

FFmpeg が出力する MMF には外側の CRC がなく、OPDA に独自形式の `VN:` メタデータが
入るため、生成ツールはその OPDA を除去し、MMMD の長さと2バイトの CRC を書き直します。
CNTI と ATR（内部の Atsq・Awa を含む）はバイト単位で保持します。CRC は多項式
`0x1021`、初期値 `0xffff`、最終 XOR `0xffff`、非反転で、末尾 CRC 自身を除いた全体から
計算します。既存 CRC はチャンクを最後まで読んだ後の残り2バイトとしてのみ認識し、
不一致なら拒否します。ADPCM の末尾を CRC と誤認しません。

この処理は、現在の FFmpeg が生成する単一・モノラル ADPCM 音声トラックの構造だけを
受け付けます。外側と内部のチャンク長を検査し、不完全なデータや未対応の構造は推測して
修復せず拒否します。変換の前後を FFmpeg で復号し、PCM が完全に一致することも生成時に
確認します。標準ライブラリだけで実行する WAV 生成には、この処理も FFmpeg も不要です。

| 信号 | サンプリング周波数 | 用途 |
|---|---:|---|
| 無音 | 8 kHz | 復号後の量子化ノイズ・無音扱いの確認 |
| 小音量の440 Hz音＋前後の無音 | 8 kHz | 小音量の保持・開始と末尾の扱い |
| 正負の段差 | 8 kHz | ADPCM の状態・立ち上がり・余韻 |
| 80 Hz〜9 kHz のスイープ | 22.05 kHz | 帯域ごとの音量と雑音 |
| 440 Hz＋17 kHz | 44.1 kHz | 高域の復元と低域への影響 |

ファイル名の `.wav` は入力、`.mmf` は圧縮した SMAF、`.decoded.wav` はその MMF を
FFmpeg で復号した結果です。ADPCM は非可逆圧縮なので、元の WAV と復号 WAV の一致を
要求しません。エンコーダーが末尾をパディングする場合もあります。
無音の原信号も、ADPCM 復号後にはゼロでない値を含むことがあります。
`manifest.json` に原信号と復号後のフレーム数、ファイルの SHA-256、FFmpeg の版を記録します。
`padding_frames` は復号後と原信号のフレーム数の差です。`normalization` には FFmpeg
出力時点のファイル長・SHA-256・CRC の有無、OPDA 除去の有無、保持した CNTI/ATR の
SHA-256、完成した CRC、変換前後で一致した PCM（16ビット・リトルエンディアン）の
SHA-256 を記録します。

MMF Pocket と別アプリには、同じ `.mmf` をそのまま渡して比較します。
[参照録音の手順](reference-capture.md)と[WAV 比較ツール](audio-comparison.md)も参照してください。
元の WAV と異なることだけで、プレーヤーの誤りとは判断しません。

ローカルでは、生成した5ファイルをプロジェクトの SMAF パーサーと ADPCM 復号器に通し、
FFmpeg が出力した計266,240サンプルとの完全一致を確認しています。
正規化後の5ファイルは独立した [go-smaf](https://github.com/but80/go-smaf)
（MIT、コミット `545503dc3bc1fa7253b17c9739b154053b8cfd82`）でも外側のチャンク構造と
CRC の一致を確認しました。このパーサーは ATR を未解釈のチャンクとして保持するため、
音声イベントや波形の妥当性を独立に検証するものではありません。内部の長さは生成ツールの
検査で、復号結果は FFmpeg とプロジェクトの復号器で確認しています。

これはソフトウェア間の相互運用性の確認です。実機・Galamelo・他アプリでの受け入れは
未確認です。FM、ROM 音色、演奏イベントの再現度はこの音声トラックだけの素材では
検証できません。独立パーサーの固定版と CRC 確認ラッパーの準備方法は
[FM 比較素材の検証手順](fm-probes.md#repeating-the-independent-parser-check)にあります。
通常の回帰テストは `python3 tools/generate-audio-probes-test.py` で実行します。

以前に作成した CRC のない素材は上書きせず、正規化版を新しいディレクトリに生成して
使い分けます。同じ比較の途中でファイルを入れ替えず、録音には対応する manifest を添付します。

## このエンジンの WAV を作る

リポジトリのルートで、C++20 コンパイラーから既存の CLI をビルドできます。

```shell
mkdir -p build
c++ -std=c++20 -O2 -Ithird_party/yamaha-smaf-player/src third_party/yamaha-smaf-player/example/smaf-render.cpp third_party/yamaha-smaf-player/src/*.cpp -o build/smaf-render
build/smaf-render build/my-smaf-probes/quiet-8000.mmf build/quiet-rendered.wav 600
```

末尾の数値は最大変換秒数で、0より大きく600以下の有限値を指定します。
省略すると30秒なので、曲全体を確認する場合は上限を明示します。入力の上限は16 MiBです。
出力は通常モードのエンジンによる44.1 kHz・ステレオ・PCM16 WAVであり、
Android の音声経路や「携帯スピーカー風」の後処理、アプリ固有の特定入力向け互換設定は含みません。
今回の5種類の合成素材では、CLI とアプリの実際のネイティブ WAV 出力を比較し、
ファイル全体の完全一致を確認しています。
入力と同じファイル・ハードリンク・シンボリックリンクへの出力は拒否します。

終了コード0はゼロでない PCM の出力成功、3は量子化後に全サンプルがゼロの出力、
1は読み込み・復号・書き込みなどの失敗、2は引数エラーです。
ゼロだけの出力も WAV 自体は生成されるため、終了コードを分けて記録します。
