# 可読性リファクタリング用リネームスクリプト

## 基本方針

このツールは `simutrans-extended-plus` と `simutrans-exjp` の両方で使うことを想定しています。名称変更を一度に混ぜず、次の3段階を**別々の変更セット・レビュー・テスト**として扱います。

1. **識別子** — 関数・変数・定数・型などのコード上の名前。ファイルパスは変更しません。
2. **ファイル名** — ファイルのベース名だけを変更します。ディレクトリ名は変更しません。
3. **ディレクトリ名** — ディレクトリの各パス要素だけを変更します。ファイル名は変更しません。

## 使い方

ルールは `rename-rules.json` の対応する配列に登録します。初期状態ではすべて空です。

まず各段階を個別にプレビューします。

```sh
python tools/refactor/rename_identifiers.py --root /path/to/simutrans-extended-plus --mode identifiers
python tools/refactor/rename_identifiers.py --root /path/to/simutrans-extended-plus --mode filenames
python tools/refactor/rename_identifiers.py --root /path/to/simutrans-extended-plus --mode directories
```

レビュー・承認後、実行したい段階だけ `--apply` を付けます。

```sh
python tools/refactor/rename_identifiers.py --root /path/to/simutrans-extended-plus --mode identifiers --apply
```

`--mode` の既定値は `identifiers` です。識別子モードはファイル名・ディレクトリ名を絶対に変更しません。ファイル名・ディレクトリ名モードは、それぞれのパス要素だけを変更し、コード本文は変更しません。

## パス変更時の必須作業

ファイル名やディレクトリ名の変更モードは、`#include`、CMake/Visual Studioプロジェクト、Makefile、スクリプト、ドキュメントなどに書かれたパス参照を自動修正しません。パス名変更は次のように進めてください。

1. パス変更の対象と全参照箇所を検索して一覧化する。
2. 対応するパス要素の名前だけを変更する。
3. 参照先の更新を、識別子置換とは別のレビュー対象として実施する。
4. 古いパスの残存検索、ビルド設定の確認、ビルドと実行テストを行う。
5. 同じ段階を exjp に適用する場合は、差分と参照構造を再確認してから実施する。

パス変更モードは名前の変更だけを行うため、参照先が自動で直ると想定しないでください。各段階を同じコミットやPRにまとめず、レビュー・テストしやすい単位に分離します。

## 安全策と制限

- 初期のルール集合は空です。候補名を推測だけで一括変更しません。
- C++パーサーではありません。識別子モードではコメントや文字列リテラルも置換対象になり得るため、差分を必ず確認します。
- 識別子は英数字とアンダースコアからなる完全なトークンとして照合し、部分文字列は置換しません。
- ファイル名・ディレクトリ名は、パス要素との完全一致でのみ変更します。
- セーブ形式、ネットワークプロトコル、pak形式、翻訳キー、外部APIなどの名前は、ソース内の置換だけで安全とは限りません。
- 適用前にクリーンな作業ツリーとバックアップを用意し、適用後に `git diff --check`、古い名前の検索、ビルド、テストを行ってください。
- このスクリプトはC++ビルド、単体テスト、ゲーム起動テストを実行しません。

## 初期調査の候補（提案であり、承認済みルールではない）

| 現行名 | 候補 | 変更段階・注意点 |
|---|---|---|
| `dataobj/koord.h` / `koord` | `coordinate` | 識別子とファイル名を別段階に分け、型・全includeを確認 |
| `dataobj/ribi.h` / `ribi` | `direction_bits` | Richtungs-Bit由来。方向ビット表現の意味を確認 |
| `boden/grund.h` / `grund_t` | `ground_tile_t` | 識別子とファイル名を別段階に分ける。波及が大きい |
| `karte_t` | `world_t` | ワールドの中央管理クラス。単純な文字置換は危険 |
| `perlin_hoehe` | `perlin_height` | 識別子の参照範囲を要確認 |
| `simconvoi.*` / `convoihandle_t.h` | `simconvoy.*` / `convoy_handle.h` | ファイル名変更と識別子変更を別段階に分ける |
| `boden/wege/schiene.*` | `boden/ways/track.*` など | ディレクトリ名・ファイル名をそれぞれ別段階に分ける |

候補は調査項目であり、承認済みの置換規則ではありません。両リポジトリの参照、意味、外部互換性を確認してからルールに追加します。
