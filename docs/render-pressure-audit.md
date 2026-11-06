# 音のバランスを調べる時刻付き診断

`tools/render-pressure-audit.cpp`は曲を実際に合成し、100msごとの出力音量と
既存の診断カウンターをJSONに記録します。代替音色の音量差だけでなく、発音枠の
置換や、全体のリミッターが関係する区間を後で探すための開発用ツールです。
アプリの音色・音量や変換キャッシュを変更する機能ではありません。

## 実行

```sh
cmake -S third_party/yamaha-smaf-player -B build/pressure-render -DCMAKE_BUILD_TYPE=Release
cmake --build build/pressure-render --target smaf -j2
c++ -std=c++20 -O2 -Ithird_party/yamaha-smaf-player/src tools/render-pressure-audit.cpp build/pressure-render/libsmaf.a -o build/pressure-render/render-pressure-audit
build/pressure-render/render-pressure-audit tune.mmf 60 > build/pressure-render/pressure.json
```

第2引数は秒単位の検査上限で、省略時・最大値は600秒です。入力は16MiBまでです。
44.1kHzの実際のステレオfloat出力を使い、PCM16への丸めは加えません。
入力を読み、標準出力に診断を返します。ファイル名・タイトル・作者などのメタデータは
JSONに含めません。正常終了は終了コード0、引数エラーは2、それ以外の失敗は1です。

## 読み方

| 項目 | 意味 |
| --- | --- |
| `start_frame`・`frames` | 出力サンプルによる区間。通常は4,410フレーム、最後は短くなることがある |
| `peak`・`rms` | 左右のミックス後のピークと、左右の電力を平均したRMS |
| `ac_rms` | 左右それぞれの平均値を除いたRMS。DC成分を音量と区別するための値 |
| `dc_left`・`dc_right` | 左右それぞれの平均値 |
| `limited_frames` | 既存リミッターがゲインを下げたフレーム数 |
| `limited_3db_frames`・`limited_6db_frames`・`limited_12db_frames` | 実際のゲイン低下がそれぞれ3/6/12dB以上だったフレーム数 |
| `stolen_fm`・`stolen_pcm` | 使用中の発音枠を置き換えた回数。無音枠の再利用や余韻の置換も含む |
| `stolen_held_fm`・`stolen_held_pcm` | 押鍵中の枠をラウンドロビンで置き換えた回数 |
| `fallback_notes` | 実際のレンダリング中に代替音色へ回った音符数 |
| `audio_notes`・`missing_audio`・`stolen_audio` | 録音Audioの開始、不足、2枠内での置換 |
| `fm_notes`・`pcm_notes` | 既存のFM・PCM全体の発音数。PCMには鍵盤PCM・ATR・録音Audioを含み、`audio_notes`はPCMの内数。`fallback_notes`はFMの内数 |
| `suppressed_silent_notes` | r68の固定無音音符の抑制。r74の無音エンベロープ再利用の専用件数ではない |

`windows`内のカウンターは区間中の増分、`totals`は検査範囲全体の値です。
各区間のフレーム数とカウンターを足すと、全体の値になります。
3/6/12dBの件数は入れ子です。12dB以上は6dB以上にも3dB以上にも含まれるため、
同じ区間の3項目を足して総数としないでください。軽い安全動作と、大きく音量を下げる
区間を区別するための観測で、リミッター設定そのものは変更していません。
通常は開始を含み終了を含まない区間です。終了位置で音声を出さずに診断だけが
更新された場合は、0フレームの末尾行でその増分を保持します。

`finished`がfalseで`cap_reached`がtrueなら検査を途中で打ち切っており、後続の区間は
未検査です。`tail_limit_reached`はエンジンが余韻の安全上限に達したことを表します。
自然な曲末と、指定した検査上限を区別してください。

RMSは聴感上のラウドネスやパート別の音量ではありません。押鍵中の枠の置換も、
実際に聴こえていた音が消えたことを単独では証明しません。リミッターの件数には
ピーク後のゲイン回復中も含み、クリップ・端末の音切れを測る値ではありません。

例えば、問題の区間で`stolen_held_*`が増えれば同時発音を、`limited_frames`が多ければ
ミックス全体のピークと回復時間を先に調べます。これらがなく代替音色が多ければ、
[音色依存のレポート](tone-dependency-probes.md)で指定楽器と不足波形を調べます。
音色ごとの調整には[単音の音量・余韻測定](fallback-balance-profile.md)や比較音源を使います。
どの条件もメーカー音源への音量校正を自動で決める基準ではありません。

## 検査

`tools/render-pressure-audit-test.cpp`で独立したピーク・DC・RMS・AC RMSの基準値、
微小ACとDCの混在、自作の密集音符によるリミッター・代替音色・発音枠の置換、
110msのイベントの区間、途中打ち切り、全体と区間の件数一致を確認します。
1/257/1024フレームの再生から同じJSONが得られます。

`python3 tools/render-pressure-audit-cli-test.py <バイナリ>`は引数・不正入力、
250msちょうどの検査上限で次の音符を含めないこと、自然終了、部分区間、
JSON形式、入力の保持、メタデータを出力しないことを自作MMFで確認します。


ローカルではASan/UBSan付きの専用検査とCLIの6検査が通過しました。
リーク検査はクラウドの制約に合わせて`ASAN_OPTIONS=detect_leaks=0`にしています。
既存の自作単音280素材でも全レポートが自然終了し、区間と全体の件数・入力SHA-256を
確認しました。この単音条件では押鍵中の枠の置換・リミッター作動はありませんでした。
専用検査では意図的に密集した音符を使い、両方が実際に発生することを確認しています。


音量低下の深さは、一定値の自作PCMを4/20/24/32音で合成した独立した定常ゲインの
基準値と比較します。制限なし・3dB未満・3dB以上・6dB以上・12dB以上を区別し、
シーク時の件数リセットも確認します。

診断追加後も既存2,121素材のWAVは全体が完全一致しました。Androidの3 ABIのDebug・Releaseとlintも通過し、変換キャッシュはr79を保持しています。
