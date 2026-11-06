# MMF Pocket

<p align="center">
  <img src="app/src/main/res/drawable-nodpi/ic_launcher_foreground_v2.png" width="180" alt="MMF Pocket icon">
</p>

MMF Pocketは、Android端末内のYamaha SMAF（`.mmf`）をフォルダ単位で一覧表示し、WAVへ変換して再生する小さなプレーヤーです。Android 10以降に対応します。

> [!IMPORTANT]
> 信頼できないMMFは手動で選んだ場合にのみ処理されますが、ネイティブデコーダーを利用するため、出所不明のファイルには注意してください。

## 機能

- Android標準のフォルダ選択
- 選択フォルダを起点に、サブフォルダを辿れるファイルブラウザ
- タップすると変換を開始し、先行部分の生成後から段階的に再生
- 同じフォルダ内のWAVファイルも、変換せずそのまま再生
- 前の曲、再生、一時停止、次の曲、停止、シーク、変換済み範囲の読み込みバー
- フォルダ内の連続再生と、次曲を重複なしで選ぶランダム再生
- アプリを閉じた後や画面消灯中も続くバックグラウンド再生
- 通知・ロック画面・Bluetooth機器・イヤホンからの再生操作
- 他アプリの音声に応じて再生を制御するAudio Focus対応
- 選択中ファイルのMA世代とSMAFスコア形式を再生領域に表示
- MMF内部の日本語タイトルと作者情報を表示
- 再生中の音の強さや立ち上がりに合わせて踊り、側転やバク転もする3Dキャラクター「まふ」
- 変換結果のキャッシュと、通常操作を妨げないフォルダ内一括変換
- 小型スピーカーの帯域と歪みを模した「携帯スピーカー風」モード
- 広告、解析、外部通信なし

## インストール

署名済みAPKはGitHubの[Releases](../../releases/latest)から配布しています。インストール時は、ダウンロードに使ったブラウザまたはファイル管理アプリに対して「不明なアプリのインストール」を一時的に許可してください。

公開APKにはSHA-256と署名証明書のSHA-256フィンガープリントを併記します。

## 対応範囲と制限

- 対応OS：Android 10（API 29）以降
- 主な対応：MA-1/MA-2/MA-3/MA-5、MA-7 Score Format 3（試験対応）、FM音色、対応形式の埋め込みADPCM・PCM8、スコア内の録音音声
- 制限：MA-7のAnalog Liteフィルター、非ゼロのピッチエンベロープ、固定周波数合成、端末ROM固有音色・エフェクトなどは未再現または近似。MA-7の符号付きPCM8は実験的対応
- 入力上限：1ファイル16MB
- 変換キャッシュ上限：512MB
- バックグラウンド再生中は通知とロック画面のプレーヤーを表示
- フォルダ内一括変換はバックグラウンド処理の対象外

端末ROM固有のPCM音色を指定しながら波形を内包しないMMFは、内蔵のFM近似音色へフォールバックします。そのため、当時の実機と完全に同じ音色にはなりません。「携帯スピーカー風」は小型スピーカーの出音を模す汎用エフェクトであり、特定端末や音源ROMを再現する機能ではありません。本アプリはYamaha Corporationおよび各端末メーカーとは提携・承認関係にありません。

1.6.5では、MMF内部の曲名・作者を再生領域に表示し、画面を整理しました。音声側はADPCMの端数処理、高い比率でのサンプル変換、限定したFM波形の折り返しを改善し、録音波形の参照先・同時発音の置き換え順・曲末の時刻などを修正しました。古い変換キャッシュは使わず、更新後のエンジンで再生成します。詳しくは[CHANGELOG.md](CHANGELOG.md)、[曲情報の表示](docs/song-information.md)、[音質改善の現在地](docs/audio-quality-ja.md)を参照してください。

「まふ」の音連動は1.6.4から引き継いでいます。主旋律の抽出や正確な拍子・BPM判定ではなく、伴奏を含めた音全体への反応です。[まふの音連動](docs/mafu-visualizer.md)と[音源の対応・制限](docs/audio-fidelity.md)も参照してください。

通常の公開版は従来の署名鍵を使って更新できます。

## プライバシー

インターネット権限、広告SDK、解析SDKは使いません。MMF、ファイル名、変換音声を端末外へ送信しません。詳しくは[PRIVACY.md](PRIVACY.md)を参照してください。

## ビルド

必要なもの：

- JDK 17
- Android SDK 35
- Android NDK `27.2.12479018`
- CMake `3.22.1`

```shell
./gradlew lint assembleDebug
```

ネイティブ回帰テストはGitHub Actionsで実行されます。個人所有のMMFコーパス、生成WAV、署名鍵、ローカルSDK設定はリポジトリに含めません。

Windowsでは`gradlew.bat`を使います。リリース署名の秘密鍵はリポジトリへ保存しません。設定方法は[docs/releasing.md](docs/releasing.md)を参照してください。

## セキュリティ

脆弱性の可能性がある場合は、公開IssueへMMFを添付せず、GitHubのPrivate vulnerability reportingを利用してください。詳しくは[SECURITY.md](SECURITY.md)を参照してください。

## ライセンス

MMF Pocketは[Apache License 2.0](LICENSE)で公開します。

再生エンジンとして、Apache-2.0の[akustikrausch/yamaha-smaf-player](https://github.com/akustikrausch/yamaha-smaf-player)を改変して同梱しています。詳細は[NOTICE](NOTICE)と[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。

サンプルMMFや市販楽曲の音源は、このリポジトリおよびリリースAPKに含めません。
