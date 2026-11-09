# 伴奏が大きく聞こえる場合の共通処理の調査

2026-11-09、正式版1.6.7の音源処理を対象に調査しました。
「戦え！アルカイザー」のファイルはこの環境にないため、その曲の原因を確定した結果ではありません。
曲名・楽器名による音量補正は加えていません。

## 確認できた制約

`MaPlayer::fireEvent_`は、指定されたPCM波形が存在しない場合、独自のFM代替音色を選びます。
ROMストリングス（波形番号26～28）は、`gmModernApprox(48)`の持続型4演算器音色を使います。
チャンネル音量・Expression・音符のVelocityは残りますが、元のPCM音色のTLと
AR/DR/SL/SR/RRは代替音色へ移しません。元の音色に指定された減衰や小音量が
この経路で失われ、伴奏が前に出る可能性があります。埋め込みFMや波形が実在するPCMには
同じ設定欠落はありません。音色指定そのものがないGM代替音色も、独自の持続・減衰になります。

これは以前からの意図的な互換処理です。元のROM波形とFM代替音色の基準音量は異なり、
そのままTLを移すと過小音量になる場合があったためです。設定欠落の再現は、その逆の処理が
正しいという証明にはなりません。実機のROM音色や楽器間の音量比を校正した結果でもありません。

## 実際のMMF→WAV経路での再現

独自作成の単音30素材を使い、Mobile形式2・MA7形式3、それぞれで
ROMストリングス代替・RAM波形欠落時の代替・実在するRAM PCMを比較しました。
基準からTLのみ28増加、ARのみ0、減衰設定変更、RRのみ1の4条件を作りました。

| 条件 | ROMストリングス代替 | RAM欠落時の代替 | 実在するRAM PCM |
| --- | --- | --- | --- |
| TLを28増加 | WAV全体が基準と同一 | 同一 | 持続部が約21dB低下 |
| ARを0に変更 | 同一 | 同一 | 無音 |
| DR/SL/SRを変更 | 同一 | 同一 | 持続部が減衰 |
| RRを1に変更 | 同一 | 同一 | 余韻が増加 |

「同一」はファイル全体のSHA-256一致です。2形式とも同じ分類になりました。
PCMの波形は独自生成で、メーカーROMや実際の曲は含めていません。
この検査は時間・音量・エンベロープの因果関係を調べるもので、聴感の一致を保証しません。

再実行するには、ビルド済みレンダラーと更新後の診断バイナリを指定します。

```sh
c++ -std=c++20 -O2 -Ithird_party/yamaha-smaf-player/src tools/tone-usage-audit.cpp build/native-render/libsmaf.a -o build/tone-usage-audit
python3 tools/probe-fallback-envelope.py --renderer build/native-render/smaf-render --audit build/tone-usage-audit build/fallback-envelope-probes
```

出力先は新規ディレクトリに限定します。`manifest.json`に入力・レンダラーのハッシュ、
WAVのハッシュ、持続部・余韻の測定値、経路と完了状態を記録します。
`routes.tsv`にも各素材で実際に選択された経路を残します。

`--rom-wave`で波形番号を指定すると、同じ検査を任意のROM代替音色にも適用できます。
指定は繰り返せます。省略時の30素材に加え、打楽器1/2/4/5/6を指定した70素材でも、
元のPCM設定を変えても代替側のWAVは変わらず、実在RAMの対照だけが変わることを確認しました。
診断部分はASan/UBSan付きでも検査しました。波形番号はアドレスで、ROM本体は含みません。

```sh
python3 tools/probe-fallback-envelope.py --renderer build/native-render/smaf-render --audit build/tone-usage-audit --rom-wave 1 --rom-wave 2 --rom-wave 4 --rom-wave 5 --rom-wave 6 build/fallback-percussion-probes
```

打楽器の代替レシピは独自の立ち上がり・減衰を持ち、元のPCMのAR/SRなどを選び直しても
そのままです。したがって、音量だけでなく打撃の鋭さ・残り方もバランスの調査対象です。
TL12・14には本来9・10.5dBの減衰指定がありますが、これが落ちているという事実と、
代替音色が実機より何dB大きいかは別です。ROM波形自体の音量が未校正のため、
単純にその数値だけ下げれば実機と一致するという結論にはしません。

## 実ファイルで切り分ける情報

`tone-usage-audit --details`に次の列を追加しました。既存の集計形式は変えていません。

- `requested_pcm_tl/ar/dr/sl/sr/rr`：要求されたPCM音色の設定。PCM音色未選択は-1。
- `pcm_envelope_applied`：実在するPCM音声へ設定した場合1、代替・無発音の場合0、PCM未選択は-1。

これは発音指示時の経路調査で、持続中の各音の音量測定ではありません。
経路が`bound_fm`なら、元のTL・演算方式・エンベロープ・音の重なりを調べる必要があります。
`explicit_rom_wave`や`missing_ram_wave`なら、今回再現した設定欠落の影響を調べられます。
どちらでも、曲全体のリミッターと発音数不足によって主旋律が弱まる可能性は別に確認します。

## 音量カーブについての留保

調査中に参照したSMAF3.05の二乗カーブの式は、HandyPhone/ATRのページでした。
Mobile形式のControl Changeの説明（p44）はその式を指定していません。
そのページだけを根拠にMA3/5の音量処理を誤りと断定できないため、今回変更していません。
MA7の既存二乗カーブ、パン、Velocity、FM/PCMの音声処理・キャッシュ・正式版は変更しません。
