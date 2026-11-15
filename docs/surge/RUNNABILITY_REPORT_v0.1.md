# SURGE v0.9 実行性確認報告 v0.1

## 判定

**実行可能なCoreプロトタイプとして、主要な入力・状態管理・文脈処理経路の連結動作を確認した。**
これはSURGE v0.9の全規範・全意味機能・実運用性能が完成したという判定ではない。現段階で実行できる範囲と未実装範囲を以下に分離する。

## 検証環境

- GitHub Actions: Ubuntu Latest
- C++: C++17、CMake、警告をエラーとして扱うビルド
- Python: 3.12
- JSON Schema: Draft 2020-12
- Python-C++接続: shared library + C ABI + ctypes
- Core version: 0.9.11

## 実行経路

1. Python RecognitionPacket Adapterが入力Schemaを検証する。
2. Adapterがソース本文をUnicode NFCに正規化し、UTF-8 byte offsetとSHA-256を検証する。
3. ctypesからC ABIを通じてC++ PacketAdmissionCoordinatorへ本文bytesとpacket metadataを渡す。
4. C++ CoreがUTF-8、全文SHA-256、指定半開区間SHA-256を独立検証する。
5. SourceSequencerとRangeLedgerをstaged stateで検証し、EventJournalへAdmissionイベントをcommitする。
6. ContextGraphが同一EventJournalを共有し、Evidence/Claim/Cluster/Dependencyと関係をSemanticDeltaとしてcommitする。
7. Claim間の明示的CONTRADICTSからQuestionRouterがQuestion/QuestionAspectを生成し、対象ClusterごとにInterestを作成する。複数Clusterにまたがる場合、Focusを恣意的に選択しない。
8. DependencyEngineが変更Clusterからの依存影響範囲を計算する。
9. ContextHintBuilderが必須参照を優先し、budget超過時にcontext_complete=falseとRetrievalTicketを生成する。
10. RetrievalTicketRegistryが同一state versionで未取得参照を解決する。
11. Journalを閉じて再オープンし、PacketAdmissionCoordinatorとContextGraphが同じJournalからそれぞれの状態を復元する。

## 自動検証結果

| 検証項目 | 結果 |
|---|---:|
| C++ shared library build | PASS |
| C++ CTest | 9/9 |
| Python unittest | 15/15 |
| JSON Schema Draft 2020-12 | 3/3 |
| D01-D20 fixture inventory/structural assertions | 20/20 |
| D01-D12 NFC UTF-8 range/digest derivation | 12 packets |
| Python → C ABI → C++ admission | PASS |
| Exact duplicate retry | PASS |
| Restart replay (packet + graph, shared journal) | PASS |
| Contradiction → Question/Interest routing | PASS |
| Ambiguous multi-cluster focus suppression | PASS |
| Dependency impact calculation | PASS |
| ContextHint mandatory overflow / RetrievalTicket | PASS |

C++ end-to-end test:
`surge_runtime_pipeline_conformance`

Python/C++ integration:
`tests/test_core_bridge.py`

## 現時点の実行上の制約

- 実際のRecognition EngineやLLM/ASRが文章からClaim、Evidence、ScopeSpan、Contradictionを自動生成する機能は未接続。意味構造は現状、外部Recognition Engineが生成する前提。
- D01-D20は構造・fixture契約の検査であり、20ケースすべてを意味推論から最終出力まで自動実行したものではない。
- Packet admissionとSemanticDeltaは同一Journal上の別々のtransaction。Admission後に意味Deltaが失敗した場合、呼出側が未処理packetを再評価・再実行する必要があり、両者を一つのtransactionにまとめる仕組みは未実装。
- Python-C++ bridgeはin-process・single-process。IPC、複数process writer、認可、source authenticityは未実装。
- EventJournalはstream flushまでであり、OSレベルfsync/FlushFileBuffers、電源断耐性、snapshot/compactionは未実装。
- ContextHintのTicket Registryはprocess-local/in-memoryであり、永続Ticket検索・期限管理は未実装。
- C++ CoreはJSON Schema検証とUnicode NFC変換を行わない。これらはPython Adapter側の責務。
- 実データに対する意味精度、precision/recall/F1、selective risk、critical FNR、RTF、p99 latency、peak RSSは未測定。
- CIで確認した実行環境はUbuntu Latest。Windows/macOS向け配布・ABI検証は未実施。

## 再現手順

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

python -m pip install -r requirements-dev.txt
python tools/validate_fixtures.py
```

Linuxでは `build/libsurge_core.so` が生成される。Python側はこの共有ライブラリを自動検出する。別の場所に配置する場合は `SURGE_CORE_LIBRARY` を指定する。

実アプリケーションからは `CppCoreBridge(journal_path)` と
`RecognitionPacketAdapter.admit_to_core(packet, source_text, core)` を利用する。


## Recognition scoring extension v0.1

The scoring extension validates six independent dimensions and applies
ACCEPT/REVIEW/ABSTAIN gates. The 50 Japanese reference cases run through the
Python scoring policy and Draft 2020-12 result schema. These are assistant-scored deterministic regression
examples, not an independent gold-standard
corpus or a model-quality claim. Importance remains separate from confidence;
ambiguous or low-confidence cases are abstained rather than converted into
guessed semantic state.
