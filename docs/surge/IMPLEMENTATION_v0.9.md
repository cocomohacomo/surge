# SURGE v0.9 実装着手報告

## 目的と位置付け

本ディレクトリのC++ Coreは、v0.9規範の一部を実行可能な最小実装として具体化したプロトタイプである。完全なSURGE Core、意味認識モデル、永続イベントストア、トランザクションエンジンを実装済みとするものではない。

## 実装済みプリミティブ

- EventJournal: append-only framed journal、transaction commit marker、state version、idempotency、event ID一意性、restart recovery、truncated tail回復、CRC不一致時のfail-closed

- SourceSequencer: source sequenceの欠落検出、連続watermark、権限付きskip、skip後の遅延到着
- RangeLedger: 同一要求の冪等再送、range digest競合、明示的revisionの追加
- ScopeSpan: OPEN/UNRESOLVEDからの条件付きclose、append-only invalidation ref
- RetrievalTicket判定: state version一致、再評価可能、再評価不能時のSTALE
- Benchmark gate: coverage、RTF、invariant violationを用いたPASS/FAIL/INCONCLUSIVE

## 現段階で未実装

- JSON packet/proposalのC++デコードとschema validation
- Source/Context単位の永続ストア、OSレベルのfsync/FlushFileBuffers、複数プロセス排他、snapshot/compaction
- ScopeSpanの全11種別に対する型別アンカー・構文検証とClaim/Relation原子更新
- ContextHintのmandatory ref選定、digest生成、RetrievalTicket検索
- read/write setによるrebase・commutativity proof
- 全評価指標（label別precision/recall/F1、selective risk、critical FNR等）
- Adapter、Recognition Engine、実モデル統合
- 旧T/W/A/C回帰セットの実行器

## 検証

ローカルでC++17、CMake、CTestを用いてビルドし、既存Coreの16グループとEventJournalの5グループのassertionテストを実行する。Python検証器は3つのDraft 2020-12 Schemaのメタスキーマ検査とD01-D20 fixture ID、D01-D12のUTF-8 byte range/SHA-256整合性を検査する。

fixture整合性検査とC++プリミティブテストは、D01-D20の意味的完全適合を意味しない。CIで両者を継続実行する。モデル精度、RTF、p99 latency、peak RSSは実測値が得られるまで未評価とする。

## EventJournalの制約

EventJournalは単一プロセス内のmutexで直列化するローカルjournalであり、複数プロセスwriterは未対応。CRC-32は偶発的破損検出用、FNV-1aは冪等性照合用であり暗号学的完全性を提供しない。C++ stream flush後のOS電源断耐性も保証しない。ドメイン状態の完全なreplay、snapshot、compaction、暗号学的署名は未実装。
