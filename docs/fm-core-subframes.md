# ネイティブ時刻を保つFM追加サンプルAPI

検証済みの音源計算を`FmVoice`へ移し、ホスト専用の状態参照なしで呼べるようにしました。
通常の`tick()`と既定の`MaPlayer::render()`の経路はそのままです。公開版1.6.6、変換キャッシュr82は
変更しません。APIの追加だけではアプリの音質は変わりません。

```cpp
std::array<float, 8> samples;
if (voice.tickSineSubframes(samples.data(), 8)) {
    // samples.back() is the original native tick result.
    // Feed the block to a separately configured causal decimator.
}
```

## 契約

- 対象は正弦波の直列2演算器・接続0、または並列4演算器・接続5。使用する演算器の実効フィードバックはすべて0。
- `supportsSineSubframes()`で現在の接続を照会できます。2演算器で未使用の演算器の設定は判定に含めません。
- 倍率は2・4・8・16・32。呼び出し側がその個数の書き込み可能な領域を用意します。
- 成功時は元の`tick()`を1回進め、最終サンプルに元の出力を格納します。途中のサンプルは更新前の位相と更新後の増分・ゲインから計算します。
- EG・LFO・Hold・解放・発音枠評価はネイティブ時刻を保持します。EGとコントローラーのゲインはフレーム内で一定です。
- `FmSubframeMath::Exact`が既定です。`Hermite`は追加する途中の正弦波だけを[補間](fm-subframe-sine.md)します。元の終点は補間しません。
- 対象外の接続、不正な倍率・方式、null出力は`false`を返し、出力と状態を変更しません。失敗時に通常の`tick()`へ戻せます。
- フィルター、PCM等との共通遅延、停止・シーク・曲末の履歴処理、倍率選択は呼び出し側の責任です。倍率変更時のフィルター連続性は保証しません。

新しい状態メンバーや仮想関数は追加していません。補間係数8KiBは全音源で共有し、
初回利用時に準備します。音声スレッドで初期化を避ける場合は、再生開始前に準備します。
このAPI単体には帯域制限フィルターや自動的な品質選択はありません。

## 検証

`tools/fm-core-subframes-test.cpp`はprivate公開や試作ヘルパーを使わず、公開APIだけで検証します。
2サンプルレート、2接続、3音色プロファイル、2方式、5倍率の120条件で、元の終点・公開状態を
照合します。音程変更、Nyquist超え、音量0と復帰、Hold・解放・再発音、方式・倍率変更も含みます。
不正入力13条件では後続200フレームまで状態が進まないことを確認します。

既存の2演算器・並列4演算器の試作もこのAPIを使い、内部状態と終点を別に再構成して照合します。
独立したPM式・Bessel側帯波、因果FIR、PCMや独立入力との混在検証を保ちます。
通常再生の2,121件の既存WAVは、API追加前の検証済み出力とバイト単位で一致しました。
端末での性能測定、実曲の比較録音、アプリへの反映は今後の作業です。実際のプレイヤーへの明示的な接続は
[実験用の再生経路](fm-player-sine-render.md)を参照してください。

## 音源APIだけの負荷を測る

`tools/fm-core-subframes-cost.cpp`は公開APIだけを使い、通常の`tick()`、追加サンプルの
Exact/Hermiteを1・8・32声、2接続、5倍率で各3回測ります。順番を交代し、係数準備と
立ち上がりを計測から除きます。FIR・ミックス・入出力・診断用の終点再構成は含みません。

```sh
c++ -std=c++20 -O2 -Ithird_party/yamaha-smaf-player/src tools/fm-core-subframes-cost.cpp build/native-render/libsmaf.a -o build/fm-core-subframe-api/cost
build/fm-core-subframe-api/cost > build/fm-core-subframe-api/cost.json
```

共有ホストで198条件の有限な結果を得ました。32声・32倍では補間を使っても負荷が大きく、
この測定から全音符への32倍適用は選びません。ホストの他処理による変動があり、
端末での速度や再生全体の実時間処理を保証する測定ではありません。
