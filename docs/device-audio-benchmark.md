# Android で合成処理の負荷を測る

`tools/synth-benchmark.cpp` は、アプリと同じ FM・PCM 合成処理を使う、
開発用のコマンドライン測定ツールです。自作の音色と波形だけを使い、楽曲は不要です。
通常利用については、ユーザーが都度実機で確認しており、現状問題なしとの報告があります。
以下は性能を数値評価する場合の手順であり、追加の定例確認を依頼するものではありません。
32音を同時に合成して、波形やサンプル変換の条件による処理時間の違いを調べます。
出力はアプリと同じ 44,100 Hz、処理ブロックも同じ1024フレームです。
FM は4波形・2アルゴリズム・AM/VIB の有無の16条件、PCM は4/8/16倍の
変換比率それぞれに線形補間と高品質補間を使う6条件です。

音をスピーカーへ出すツールではありません。ファイルの解析、ステレオのミックス・
出力フィルター、PCM16 変換、WAV 保存、AudioTrack、画面描画は測定に含みません。
PCM 32音も、録音音声の実際のスロット数を再現するものではなく、計算の負荷条件です。
結果だけで、アプリの音切れ・遅延・消費電力を判定することはできません。

## ビルドと実行

Linux のホスト測定は、C++20 対応のコンパイラーで実行します。

```sh
bash tools/build-synth-benchmark.sh --host
build/synth-benchmark/host/synth-benchmark --quick > build/synth-benchmark/host/quick.json
```

Android は、アプリと同じ NDK `27.2.12479018` を使います。
SDK/NDK の場所を設定してから、端末の ABI に合わせてビルドします。

```sh
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
bash tools/build-synth-benchmark.sh --abi arm64-v8a
```

USB デバッグを有効にした自分の端末で、ADB の接続を確認して実行します。
複数台が接続されている場合は、各 `adb` に `-s SERIAL` を付けます。

```sh
adb shell getprop ro.product.model
adb shell getprop ro.build.version.release
adb shell getprop ro.product.cpu.abi
adb push build/synth-benchmark/arm64-v8a/synth-benchmark /data/local/tmp/mmf-synth-benchmark
adb shell chmod 755 /data/local/tmp/mmf-synth-benchmark
adb shell /data/local/tmp/mmf-synth-benchmark --quick > build/synth-benchmark/device-quick.json
adb shell /data/local/tmp/mmf-synth-benchmark > build/synth-benchmark/device.json
```

`armeabi-v7a` と `x86_64` も指定できます。対応端末でのみ実行してください。
このクラウド環境でのクロスビルド成功は、端末上での実行成功を意味しません。

## 比較条件

- 同じ端末・OS・ABI・コンパイラー・オプションと、同じフレーム数で比較します。
  バイナリーの SHA-256、ソースの版、JSON と端末情報を一緒に保存します。
- 数回繰り返し、順番を入れ替えて比較します。温度上昇、充電、バックグラウンド処理、
  CPU の動作周波数で結果が変わります。1回だけの小さな差を改善と断定しません。
- `--quick` は動作確認用です。短い測定ではタイマーやスケジューラーの影響が大きくなります。
- `setup_seconds` は音源の初期化と512フレームのウォームアップを含む時間です。
  `wall_seconds` は、その後の合成時間です。フィルター表はプロセス内で共有されるため、
  後の条件は既に初期化された表を使います。初回の表生成時間だけを示す値ではありません。
- 1024フレーム単位の処理時間と、その音声区間の長さを比較します。時間超過の回数は
  合成部分だけの余裕を調べる指標であり、AudioTrack のアンダーラン回数ではありません。
- `realtime_ratio` は音声区間の秒数 ÷ 合成時間です。大きいほど高速ですが、32音を
  それぞれ数えた値ではありません。`checksum` は出力を実際に計算したことを確認する
  集計値であり、音質の点数でも、コンパイラーや ABI をまたぐ一致保証でもありません。

実際の音切れは、同じ端末上で通常のアプリを使い、多数の発音を含む MMF を再生して
別途確認します。聴感と他のプレーヤーとの比較は、[比較録音の手順](reference-capture.md)
を使います。

## 強い直列FMの負荷を測る

`tools/nested-fm-benchmark.cpp` は、強い直列FM（変調器を何段もつなぐ音色）を、
実際の `MaPlayer` の描画処理（フィルター・リミッターを含む）で測る開発用ツールです。
`synth-benchmark` は発音1つ分の計算だけを測りますが、こちらは再生全体の処理を測ります。
自作の音色だけを使い、楽曲は不要です。1音・8音・32音で次の6条件を測ります。

| 条件 | 内容 |
| --- | --- |
| `native_tl12_declined` | 現在の通常再生。変調が強い音は倍率を上げず、従来方式で処理 |
| `r91_weak_tl24_16x` | 現在の通常再生で、変調が弱い音を16倍で処理 |
| `adaptive_tl12_16x`〜`128x` | 実験用の適応方式（アプリでは無効）で、16/32/64/128倍を選ばせた場合 |

`synth-benchmark` と同じスクリプトでビルドされます。

```sh
bash tools/build-synth-benchmark.sh --host    # または --abi arm64-v8a
build/synth-benchmark/host/nested-fm-benchmark --quick > quick.json
build/synth-benchmark/host/nested-fm-benchmark > forward.json
build/synth-benchmark/host/nested-fm-benchmark --reverse > reverse.json
```

Android では、`synth-benchmark` と同じ手順で `adb push` して実行します。
結果は1秒の音声あたりのCPU秒数（`cpu_seconds_per_audio_second_median`）です。
小さいほど軽く、1.0を超えると再生より遅い処理です。`--reverse` は条件の順番を
逆にします。順番による差（温度上昇など）を見るため、`forward.json` と
`reverse.json` の両方を保存して比べてください。

ホスト（x86-64、GCC 13、共用環境）での参考値は、32音で、従来方式が約0.23秒/秒、
16倍が約0.8〜0.9秒/秒、128倍が約5.2秒/秒です。128倍・32音は再生速度の
約5分の1の速さでしか作れない計算で、段階変換が再生に追いつくかは端末で確かめるまで
分かりません。これらはホストの数値であり、端末の処理時間・消費電力・発熱を示すものではありません。
また Android 向けのビルドは、この環境の NDK がないため未確認です。
