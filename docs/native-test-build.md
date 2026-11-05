# ネイティブ検査の共有ビルド

Android CIの`native-security-smoke`は、音源本体を一度だけASan/UBSan付きの静的ライブラリに
ビルドし、18個の検査用プログラムからリンクします。従来は14個が音源4ファイル、4個が
FMコアを個別にコンパイルしていたため、音源の翻訳単位のコンパイルは合計60回ありました。
変更後は4回です。各検査自身のコンパイルと実行は維持します。

```sh
cmake -S third_party/yamaha-smaf-player -B build/native-sanitizer \
  -DCMAKE_CXX_COMPILER=c++ -DCMAKE_CXX_EXTENSIONS=OFF \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build/native-sanitizer --target smaf --parallel 2
c++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ithird_party/yamaha-smaf-player/src tools/fm-rectified-carrier-test.cpp \
  build/native-sanitizer/libsmaf.a -o build/fm-rectified-carrier-test
build/fm-rectified-carrier-test
```

既存のQt不要のCMakeターゲットを使います。CIでは毎回新しいワークスペースで作成し、
別のビルドのオブジェクトや外部キャッシュは取り込みません。ローカルの増分ビルドは
CMakeのソース・ヘッダー依存関係で更新されます。

最適化・デバッグ・アドレス検査・未定義動作検査・フレームポインターの設定は従来と同じです。
`RelWithDebInfo`の通常のフラグを明示的に置き換え、`NDEBUG`も追加しません。
検査の名前・引数・リンク時のsanitizer設定、Androidビルド側のジョブは保持しています。
これは検査用の変更で、アプリの音声処理・キャッシュ・公開版は変えません。

クラウドのローカル検査では、既存の環境制限に合わせて`ASAN_OPTIONS=detect_leaks=0`を
指定します。CI側のリーク検査設定は変更していません。

共有ライブラリを使った整流波形・高周波ビブラート・PCM16/JNIブリッジ・入力保護・
FM帯域制限・音色調査の6スイートが通過しました。公開処理の既存20検査も通過しています。
変更前後の各ステップを比較し、元の検査引数・sanitizerフラグが維持されていることも確認しました。

整理後のコミット`eb817f2`（音声はr70）の[GitHub Actions全体](https://github.com/Meyonze/mmf-pocket/actions/runs/42061799676)
も成功しています。
