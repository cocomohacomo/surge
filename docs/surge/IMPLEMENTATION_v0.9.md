# SURGE v0.9 実装着手報告

## 目的と位置付け

本ディレクトリのC++ Coreは、v0.9規範の一部を実行可能な最小実装として具体化したプロトタイプである。完全なSURGE Core、意味認識モデル、永続イベントストア、トランザクションエンジンを実装済みとするものではない。

## 実装済みプリミティブ

- SHA-256 source verifier v0.1（C++）: SHA-256再計算と厳密なUTF-8妥当性検証を実装し、正規化済みスナップショット全体と半開区間のdigestをCore側で独立照合する。NFC変換自体はAdapter責務。
- PacketAdmissionCoordinator v0.2（C++）: SourceSequencer/RangeLedgerのstaged validation後、単一RECOGNITION_PACKET_ADMITTEDイベントとしてEventJournalへcommitし、成功後のみメモリ状態を公開。packet ID/idempotency/source sequenceを復元し、再起動後のreplayとexact retryを検証。
- RecognitionPacket C ABI / Python ctypes bridge v0.1: C++ shared libraryをロードし、PythonでSchema/NFC境界検証したRecognitionPacketをC++ PacketAdmissionCoordinatorへ渡す。Journal open/replay、admit、duplicate、state version、packet countを公開。
- RecognitionPacket Adapter v0.2（Python）: Draft 2020-12 schema validation、immutable NFC UTF-8 source snapshotの全体digestとrange digest検証、byte offset bounds、packet/idempotency/sequence/range identity検査、append-only revision lineage、exact retryの冪等Admission。12件の独立テストをfixture検証コマンドから実行する。

- ContextHintBuilder / RetrievalTicketRegistry: mandatory優先順、stable dedup/sort、budget overflow、mandatory omitted digest、state-version再評価/STALE、read-only resolve

- ScopeSpanValidator: 11種の型別検証、UTF-8 byte anchor一致、packet lineage、target binding、未解決保持、否定・条件・法性・引用の過剰解釈防止

- QuestionRouter: 明示的なClaim間CONTRADICTSからQuestion/QuestionAspectを生成し、ABOUT関係からInterestを各対象Clusterへ向ける。対象Clusterが一意の場合のみFocusを設定し、複数候補ではFocusを推測しない

- DependencyEngine: prerequisite→dependentの推移的Impact Set、上限検査、Tarjan SCC検出、安定ソート、budget超過時のpartial扱い

- ContextGraph: typed Node/Relation、QuestionAspect/Interest/Focusの参照制約、staged SemanticDelta検証、EventJournalへの原子的commit、restart replay、冪等性復元

- EventJournal: append-only framed journal、transaction commit marker、state version、idempotency、event ID一意性、restart recovery、truncated tail回復、CRC不一致時のfail-closed

- SourceSequencer: source sequenceの欠落検出、連続watermark、権限付きskip、skip後の遅延到着
- RangeLedger: 同一要求の冪等再送、range digest競合、明示的revisionの追加
- ScopeSpan: OPEN/UNRESOLVEDからの条件付きclose、append-only invalidation ref
- RetrievalTicket判定: state version一致、再評価可能、再評価不能時のSTALE
- Benchmark gate: coverage、RTF、invariant violationを用いたPASS/FAIL/INCONCLUSIVE

## 現段階で未実装

- RecognitionPacket/ClassificationProposalのC++デコードとC++内schema validation（Python Adapterは別途実装済み）
- Python/C ABIはin-process・single-process限定。IPC bridge、複数プロセスwriter排他は未実装
- OSレベルのfsync/FlushFileBuffers、電源断耐性、snapshot/compaction
- ScopeSpanの全11種別に対する型別アンカー・構文検証とClaim/Relation原子更新
- RetrievalTicketの永続化、expiry/supersede、実グラフ検索
- read/write setによるrebase・commutativity proof
- 全評価指標（label別precision/recall/F1、selective risk、critical FNR等）
- Adapter、Recognition Engine、実モデル統合
- 旧T/W/A/C回帰セットの実行器

## 検証

ローカルでC++17、CMake、CTestを用いてビルドし、既存Coreの16グループ、EventJournalの5グループ、PacketAdmissionの3シナリオ、ContextGraphの2グループ、DependencyEngineの6グループ、QuestionRouterの3グループ、ScopeSpanValidatorの12グループ、ContextHintの7グループのassertionテストを実行する。Python検証器は3つのDraft 2020-12 Schemaのメタスキーマ検査、D01-D20 fixture ID、D01-D12のUTF-8 byte range/SHA-256整合性、およびRecognitionPacket Adapter 12テストとPython-C++統合テストを検査する。

Python Adapterの旧admit()はprocess-local registryを使用する。実運用接続ではadmit_to_core()を使用し、C ABI/ctypes経由でC++ PacketAdmissionCoordinatorへ渡す。C++はSHA-256を独立再計算し、全体digestとrange digestを照合する。Unicode NFC変換とJSON Schema検証はPython Adapterの責務。PacketAdmissionCoordinatorとContextGraphは同一EventJournalを共有でき、PacketAdmission後のSemanticDelta、Question/Interest routing、DependencyEngine、ContextHint/Ticket処理を連結したruntime pipeline testで実行性を確認した。パケット登録と意味Deltaは別transactionであり、両者の一括commitは未実装。source snapshotの真正性は呼出元のtrusted source adapterに依存する。fixture整合性検査とプリミティブテストは、D01-D20の意味的完全適合を意味しない。CIではC++ CTest 9/9、Python unittest 15/15、3 Schema、D01-D20構造検査を実行する。モデル精度、RTF、p99 latency、peak RSSは未測定。

## EventJournalの制約

EventJournalは単一プロセス内のmutexで直列化するローカルjournalであり、複数プロセスwriterは未対応。CRC-32は偶発的破損検出用、FNV-1aは冪等性照合用であり暗号学的完全性を提供しない。C++ stream flush後のOS電源断耐性も保証しない。ドメイン状態の完全なreplay、snapshot、compaction、暗号学的署名は未実装。

## ContextGraph v0.1の範囲

ContextGraphはContext単位の型付きグラフ基盤であり、独立EventJournalまたはPacketAdmissionCoordinatorと共有するEventJournalを利用できる。SemanticDeltaをstaged stateで検証後、EventJournal commit成功時だけメモリ状態へ反映する。共有Journalではpacket admission eventを除外しつつ、全transactionのstate versionを考慮してグラフ状態を再構築する。再起動時はcommit済みイベントから復元する。矛盾からQuestion/Interest/Focusを自動生成する推論、Dependency Impact Set、SCC収束、時間的有効性、信頼度評価、Entity merge/splitは未実装。

## DependencyEngine v0.1の範囲

Dependencyノードのreferences[0]を前提、references[1]を依存先として、変更ノードからの到達閉包を計算する。SCCを検出するが、SCC内部の意味推論の反復収束やDelta commitはまだ行わない。上限超過時は不完全結果として返し、完全なImpact Setとして利用してはならない。

## QuestionRouter v0.1の範囲

テキストから矛盾を自動発見する機能ではなく、検証済みのClaim間CONTRADICTS Relationを入力として受け取る。矛盾をQuestion/QuestionAspectに変換し、関係するClusterへInterestを向ける。複数Clusterがある場合はFocusを選ばず、既存Focusを維持する。信頼度・成長度・時間・重要度による候補順位付けは未実装。

## ScopeSpanValidator v0.1の範囲

11種のScopeSpanについて型別のanchor/target条件を検証する。anchor digestはtrusted Adapterから渡されたimmutable source digestとの整合性を確認し、C++側でdigest値そのものを再計算しない。複数source digestをまたぐanchor、ScopeSpanの永続revision、ContextGraphとの原子統合は次段階。

## ContextHint v0.1の範囲

mandatory refsの優先順位とoptional refsの切り分け、budget超過時のRetrievalTicket発行を実装。Ticket Registryはprocess-local/in-memoryで、Context認可、expiry/supersede、永続化、predicate保存、実グラフ検索は未実装。FNV-1a fingerprintは暗号学的保証を持たない。
