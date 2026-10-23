# 可読性リファクタリング用リネームスクリプト

## 目的

このスクリプトとルールJSONは、`simutrans-extended-plus` と `simutrans-exjp` の両方で使うことを想定しています。コード中の識別子とファイルパスの変更を、明示的なルールに限定して行います。

## 安全な使い方

1. クリーンな作業ツリーで実行します。
2. `rename-rules.json` に、調査・レビュー済みの `old` と `new` の対応を登録します。
3. まずプレビューを実行します。

   ```sh
   python rename_identifiers.py --root /path/to/simutrans-extended-plus
   ```

4. 出力を確認してから適用します。識別子の内容置換だけを行い、ファイル名は変更しない場合は通常どおり実行します。ファイル名も変更する場合だけ `--rename-files` を追加します。

   ```sh
   python rename_identifiers.py --root /path/to/simutrans-extended-plus --apply
   # ファイル名も変える場合は --rename-files を追加
   ```

5. `git diff --stat`、`git diff --check`、検索、全ビルド設定、テスト結果を確認します。exjpでも同じルールをプレビューし、差分に適用できない名前や追加参照を確認してから適用します。

## 現在の制限

- 初期のルール集合は空です。意味を十分確認せずに推測で名称を変更しないためです。
- C++パーサーではありません。文字列リテラル・コメントも一致する場合があります。現在のルールを英数字とアンダースコアからなる完全な識別子に限定し、適用後の差分を必ず確認してください。
- 同一の短いトークン（例：`weg`、`alt`）は、単語の一部・API・外部形式に使われている可能性があるため、ルール登録時に特に注意します。
- C++識別子、include、ビルド設定、リソース、スクリプト、外部API名を同時に変える場合は、追加ファイル形式と除外範囲を明示的に確認してください。
- セーブ形式、ネットワークプロトコル、pak形式、翻訳キーなど、外部互換性がある名前は、ソース内の置換だけで安全とは限りません。
- このスクリプト自体はC++ビルド、単体テスト、ゲーム起動テストを実行しません。

## 初期調査の候補（提案であり、承認済みの置換規則ではない）

| 現行名 | 候補 | 注意点 |
|---|---|---|
| `dataobj/koord.h` / `koord` | `coordinate` | 座標型。型・関数・全includeを一括確認 |
| `dataobj/ribi.h` / `ribi` | `direction_bits` | Richtungs-Bit由来。ゲーム内部の方向ビット表現を意味する |
| `boden/grund.h` / `grund_t` | `ground_tile_t` | クラス名のため波及が大きい |
| `karte_t` | `world_t` | ワールドの中央管理クラス。単純な文字置換は危険 |
| `perlin_hoehe` | `perlin_height` | 公開・内部APIの参照範囲を要確認 |
| `simconvoi.*` / `convoihandle_t.h` | `simconvoy.*` / `convoy_handle.h` | `convoy` と `convoi` の混在解消案 |
| `boden/wege/schiene.*` | `boden/ways/track.*` など | ディレクトリ移動はプロジェクトファイル・make依存関係の更新が必要 |

候補は、現行コードを理解するための調査項目にすぎません。スクリプトのルールに入れる前に、Extended Plusとexjpそれぞれの参照、意味、外部互換性を確認します。
