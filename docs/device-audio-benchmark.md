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
