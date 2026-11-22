# SURGE 基礎設計 v0.5

## 0. 基本原理: 認知情報クラスターとフォーカス

SURGE Coreの基本原理は、認知情報クラスターとフォーカスの二つである。すべての認知状態は、この二つの構造とその相互作用として表現する。用途固有の認知モデルをCoreへ固定しない。

- **認知情報クラスター:** 経験・情報・関係・重みを保持する認知情報の単位。複数クラスターの共起によって新しい認識が成立した場合、その認識自体を新しいクラスターとして生成する。生成元クラスターとの由来関係を保持する。
- **フォーカス:** 現在注目する認知情報クラスターを選択・遷移させる機構。フォーカスは認知の正誤を判定せず、クラスターのLifecycleも変更しない。
- **外部観測:** 外部システムへ公開する中心対象はフォーカスされた認知情報クラスターである。外部システムがその観測結果を用いて用途固有の判断・行動を行う。
- **用途分離:** 会話理解、文書理解、ASR後処理、エージェント状況認識等は、二原理を利用する上位レイヤーに配置する。

Coreの共通データ構造やAPIは用途非依存とし、認知の生成・想起・関係形成・フォーカス遷移をクラスター操作として表現する。新規認知の生成はProposalだけで終わらず、Commit対象のClusterとそのLineage Relationとして永続化できること。

## Universal Context Cognition Framework
### 統合基礎仕様書・実装契約

**Version:** 0.5.0  
**Status:** Architecture and Core Contract Specification  
**Supersedes:** SPECIFICATION_v0.4.md  
**Integrated design:** DETAILED_DESIGN_v0.2.md  
**Design priority:** Streaming-Native / Model-Independent / Japanese-First / Evidence-First

---

# 1. 概要


### 0.1 Spatial Focus Region (migration contract)

Focus is a spatial computation boundary, not a single selected Cluster. The legacy
`FocusRecord` remains readable and writable for compatibility, but it is not the
authoritative representation of spatial focus.

The spatial projection contract has four independent inputs:

- `FocusRegion.center`: the region center in 3D graph coordinates.
- `FocusRegion.half_extents`: non-negative extent along each axis. The initial
  geometry is axis-aligned; this does not prescribe the eventual graph embedding.
- `ClusterSpatialPosition`: the position of each Cluster or ChaosCluster.
- `ClusterFocusDepth`: an independent traversal depth for each Cluster.

Changing region geometry MUST NOT implicitly modify per-cluster depth. Changing
one Cluster's depth MUST NOT implicitly expand or contract the region. Coordinates
may be negative; extents may not. Invalid or duplicate position records are ignored
by the read projection and MUST be rejected by a future persistent write API.

Depth zero exposes the Cluster itself. Each positive depth permits one additional
Contains-membership level. When traversal reaches a nested Cluster, its own
configured depth applies independently; it does not inherit the parent's depth.
This is a structural traversal rule, not a truth or relevance score.

The observation reports clusters considered/in-region, nodes and relations
processed, configured limits, and whether a work budget was exhausted. Budget
exhaustion is an explicit partial observation, never a claim that the full region
was processed. External observers may consume all nodes, memberships and relations
inside the projection, not just the legacy focused Cluster.

The current C++ overload accepts positions and depths as read inputs. Durable
spatial-position storage, independently journaled region movement/resize and
per-cluster depth updates, and transport/schema bindings remain migration work;
they MUST NOT be represented as complete by this read-only projection.

## 1.1 定義

SURGEは、異なる形式・粒度・時間軸で逐次入力される情報を共通イベント形式に正規化し、意味認識、文脈統合、Focus管理、状態更新、履歴管理を継続的に実行するモデル非依存の汎用文脈認識フレームワークである。

SURGEは単純な要約、分類、情報抽出、検索、外部行動の実行を目的としない。情報間の関係、依存、時間的変化、矛盾、不確実性を保持しながら、文脈を継続的に構築・維持・再評価する。

## 1.2 基本目的

1. 入力粒度に依存しない意味認識
2. 異なる入力形式の共通処理
3. 文脈の逐次構築と更新
4. 話題・意味構造・Entity・Clusterの動的管理
5. 複数・階層型Focusの管理
6. RelationとDependencyの分離管理
7. 時間経過に伴う状態変化の管理
8. 曖昧性・矛盾・不確実性の保持
9. 解釈・状態の非破壊的更新
10. 履歴からの状態再構成
11. 未解決事項であるQuestionの生成・管理
12. Interestに基づく認知資源とFocusの配分
13. 文脈依存のSource TrustとEvidence独立性の評価

## 1.3 認知サイクル

文脈内の矛盾、曖昧性、情報不足はQuestionの生成契機となる。QuestionはInterestに影響し、InterestはFocus選択の判断材料となる。この内部認知サイクルは外部への質問、操作、行動を要求しない。

**Context is continuously constructed, maintained, and reevaluated.**

---

# 2. 基本設計原則

## 2.1 Streaming-Native

全ての入力を逐次処理可能なイベントとして扱う。未完成の文字列、単語、文、発言、段落、文書も入力可能とする。入力確定前の結果は暫定状態として区別する。

## 2.2 Granularity Invariance

入力分割単位と意味認識単位を独立させる。文字、トークン、単語、句、文、発言、段落、文書、会話、構造化入力を同一Coreで扱う。同じ意味情報・順序・確定境界を持つ入力は、分割方法が異なっても最終的に意味的に同等な状態へ収束することを目標とする。中間状態の一致は要求しない。

## 2.3 Model Independence

Coreは特定のLLM、認識モデル、言語モデルに依存しない。モデルは候補を生成する外部Recognition Engineであり、Coreはモデル交換の影響を受けない共通契約を提供する。

## 2.4 Non-destructive Context Evolution

Evidence、Interpretation、Committed State、Historyを分離する。過去の根拠や確定履歴を上書きせず、訂正・置換・統合・分割を新しいイベントとLineageで表す。

## 2.5 Evidence-Based State Management

確定状態はEvidence、適用Interpretation、Provenance、変更履歴へ追跡可能でなければならない。Coreのみが確定状態をCommitする。

## 2.6 Uncertainty Preservation

意味、参照先、Entity同一性、時間、Relation、Dependency等が未確定の場合、無理に確定しない。不確実性は対象と影響範囲を持ち、Dependency Policyに従って伝播する。

## 2.7 Structure / Focus / Evolution Separation

Context Structure、Context Focus、Context Evolutionを独立して管理する。Focus変更はCluster構造の変更を意味せず、Cluster再編もFocus変更を意味しない。

---

# 3. 全体アーキテクチャ

SURGEは6つの論理レイヤーと横断Core制御機構から構成する。

| Layer | 名称 | 責務 |
|---|---|---|
| 1 | Input Normalization | Adapter入力、Event正規化、時刻・位置・順序・原データ管理 |
| 2 | Semantic Recognition | 意味単位、仮説、Entity/参照/時間/矛盾候補の抽出 |
| 3 | Context Integration | Unit、Entity、Cluster、Relation、Dependency、既存文脈との統合 |
| 4 | Dynamic Focus Management | Scope別Focus、Interest、Focus遷移、Dormant再活性化 |
| 5 | State Evolution | INIT、UPDATE、RECONCILE、検証、Delta統合、Commit制御 |
| 6 | Context Repository | Evidence、Interpretation、状態、履歴、Event Log、Snapshotの保存・復元 |

Input Adapterは形式変換を担当し、文脈認識ロジックを独自実装しない。全入力は共通Input Eventを介して同じSURGE Coreへ渡す。

## 3.1 横断Core機構

- Uncertainty Management
- Dependency Management
- Transaction Management
- Provenance Management
- Question / Interest Management
- Contextual Source Trust

これらは独立した意味認識レイヤーではなく、6レイヤーが共有する制御機構である。

## 3.2 内部コンポーネント

| Component | 責務 | 禁止事項 |
|---|---|---|
| Event Gateway | Event検証・正規化 | 意味解釈・Commit |
| Evidence Registrar | 不変Evidence登録 | 原入力上書き |
| Recognition Orchestrator | Recognition Proposal生成の調整 | Repository直接更新 |
| Interpretation Manager | 解釈候補・履歴管理 | 旧解釈削除 |
| Context Integrator | Unit/Entity/Cluster/Relation候補統合 | 未確定同一性の強制統合 |
| Question Manager | Question生成・統合・Lifecycle | 外部行動要求 |
| Interest Evaluator | Interest評価 | Interest単独で状態確定 |
| Focus Manager | Scope別Focus選択 | Focus変更によるCluster改変 |
| Dependency Planner | Impact Set算出 | Relationの無条件伝播 |
| Reconciliation Engine | 影響範囲の再評価 | 全文脈の無条件再解析 |
| State Validator | Invariant/Delta検証 | 不明点の暗黙確定 |
| Transaction Coordinator | Atomic Commit/Conflict | 古いBase Versionの強制Commit |
| Repository | Event/State/History/Snapshot | 外部モデルからの直接書き込み |

---

# 4. 共通データモデル

全永続レコードは型付き一意IDを持つ。IDは再利用せず、統合・分割・置換後も旧IDをLineageで参照する。全レコードはschema_versionとprovenance_refsを必要に応じて保持する。

## 4.1 Input Event

必須: event_id, source_id, observed_at, content_type, idempotency_key, payload_refまたはcontent, source_sequenceまたはsource_position。  
任意: event_time, speaker_id, document_id, input_position, causal_refs, locale, adapter_metadata, supersedes_event_ref, input_status。

Event本文は不変。訂正・撤回は新Eventとして追加する。source_sequenceは同一Source内の順序であり、異なるSource間の全順序を仮定しない。

## 4.2 Evidence

必須: evidence_id, event_refs, evidence_kind, observed_at, integrity_digest, validity_status, source_lineage_ref。派生Evidenceはderived_from_refsを持つ。Evidenceは原Eventの範囲へ逆参照できる。

## 4.3 Interpretation

必須: interpretation_id, evidence_refs, hypothesis, interpretation_status, confidence_record, created_at, producer_id, model_or_rule_version。複数候補を保持可能。

状態: CANDIDATE, PROVISIONAL, ACCEPTED, SUPERSEDED, REJECTED, NEEDS_REEVALUATION。REJECTEDはEvidence削除を意味しない。

confidence_recordはvalue、scale、calibration_ref、evaluated_at等を保持し、異なるモデルの無校正値を直接比較しない。

## 4.4 Semantic Unit / Claim

Semantic Unitは意味的まとまりを表す。真偽・有効性を評価可能な内容はClaimとして表現する。Claim比較キーはsubject、predicate、object、polarity、modality、temporal_scope、context_scopeを含む。文字列一致だけで同一Claimとしない。

状態: PROPOSED, PROVISIONAL, ACTIVE, DISPUTED, SUPERSEDED, INVALIDATED, ARCHIVED。DISPUTEDはClaimが消えた状態ではなく、競合Evidence/Interpretationが共存する状態。

## 4.5 Entity

Identity状態: UNRESOLVED, PROVISIONAL, CONFIRMED, DISTINCT, MERGED, SPLIT。Aliasは同一性の確定根拠ではない。MERGED/SPLITは履歴操作であり過去参照を書き換えない。

## 4.6 Context Cluster

状態: ACTIVE, DORMANT, ARCHIVED, MERGED, SPLIT。member lineage、parent/child lineage、merge/split eventを保持する。Cluster IDを再利用しない。

## 4.7 Relation / Dependency

Relationは意味的関係と根拠を保持する。Relation Type例: RELATED, DEPENDS_ON, DERIVED_FROM, REFERS_TO, CONTRADICTS, SUPERSEDES, CONTAINS, PART_OF, SAME_AS, CAUSED_BY, SUPPORTS, EXCLUDES。

DependencyはRelationと別型であり、trigger_condition、impact_policy、evaluation_policy、validity、provenance、cycle_group_ref等を持つ。AがBに依存する場合、Bの変更がAの再評価を引き起こす向きで表す。全RelationがDependencyではない。

Dependency状態: ACTIVE, INACTIVE, SUSPENDED, INVALIDATED。

## 4.8 Focus State

Focusはscope_id、target_ref、level、role、status、activation_reason、previous_focus_refs、transition_event_refを持つ。Role: PRIMARY, SECONDARY, BACKGROUND, DORMANT。複数Focusを許容し、Scope PolicyがPRIMARYの一意性を定義する。Global/Local Scope間のPRIMARY重複は許容する。

## 4.9 Context State

現在有効な文脈状態。state_id、state_version、active_entities、active_clusters、semantic_units、relations、dependencies、focus_states、unresolved_items、conflicts、temporal_context、provenance_refs、updated_atを保持する。過去状態はState HistoryとEvent Logから再構成可能とする。

## 4.10 State Delta

delta_id、base_state_version、input_event_refs、read_set、write_set、operations、affected_refs、dependency_generation、validation_requirements、producer_refsを持つ。操作はADD、LINK、UPDATE_STATUS、SUPERSEDE、MERGE、SPLIT、REOPEN等。原則DELETEは提供せず、無効化・アーカイブ・置換を用いる。

## 4.11 Question

必須: question_id, question_type, unresolved_aspects, target_refs, trigger_refs, status, created_at, updated_at, provenance_refs。Type: CONTRADICTION, AMBIGUITY, KNOWLEDGE_GAP, REFERENCE_UNRESOLVED, TEMPORAL_INCONSISTENCY, DEPENDENCY_INCONSISTENCY, EVIDENCE_INSUFFICIENT, SOURCE_INDEPENDENCE_UNCLEAR。

状態遷移:
- OPEN -> INVESTIGATING | DEFERRED | RESOLVED | SUPERSEDED
- INVESTIGATING -> OPEN | DEFERRED | RESOLVED | SUPERSEDED
- DEFERRED -> OPEN | INVESTIGATING | RESOLVED | SUPERSEDED
- RESOLVED -> OPENは新Evidenceまたは既存Evidence有効性変更による再オープンのみ
- SUPERSEDEDは終端。再利用せず新規Questionを生成

RESOLVEDは唯一の正解確定に限定しない。時間区分、複数候補の保持、矛盾原因特定等により元の未解決事項が解消された状態を含む。解消にはresolution_basisを必須とする。

### 4.11.1 Question重複統合

意味的一致、根本原因、Scope互換性、時間範囲、Evidence/Interpretation参照関係で判定する。単一類似度閾値だけで自動統合しない。確定重複は既存Questionへ根拠を追加し、包含関係は親子Question、部分重複はrelated_question_refsで表現する。統合元はSUPERSEDEDとして履歴を残す。曖昧な場合は統合せず関連付ける。

## 4.12 Interest

target_ref、scope_id、component_vector、priority_band、status、evaluation_version、history_refsを持つ。基本要素: unresolvedness, impact, dependency_reach, recency, recurrence, context_relevance, explicit_attention, evidence_uncertainty。各要素は値、尺度、根拠、評価時刻を保持する。Scope間で無条件比較しない。

状態: ACTIVE, DECAYING, DORMANT, REACTIVATED, CLOSED。QuestionとInterestは独立した状態遷移を持つ。時間経過によるRecency減衰はQuestion解消やEvidence無効化を意味しない。

## 4.13 Source Lineage / Contextual Source Trust

Lineage関係: ORIGINAL, QUOTES, REPOSTS, DERIVED_FROM, SYNDICATED_FROM, UNKNOWN_ORIGIN。独立性: INDEPENDENT, DEPENDENT, PARTIALLY_DEPENDENT, UNKNOWN。UNKNOWNは独立裏付け数に加えない。部分依存は独立成分を分離できる場合のみ評価する。

Source Trustはdomain/scope別の評価値と評価確度を分離して保持する。未知ソースを高信頼/低信頼に決めつけない。Trustは候補評価を補助するだけで、単独でClaimやEvidenceを確定・無効化しない。同じTrust評価を自分の根拠として再帰利用しない。

---

# 5. Question生成と矛盾分類

矛盾候補はDIRECT_CONTRADICTION、TEMPORAL_DIFFERENCE、SCOPE_DIFFERENCE、PERSPECTIVE_DIFFERENCE、MODAL_DIFFERENCE、EVIDENCE_CONFLICT、INTERPRETATION_CONFLICT等に分類する。時間・Scope・話者・様相の違いで両立可能なら、直ちに事実矛盾Questionを生成しない。分類不確実性自体をQuestion化してよい。

生成前に既存Question、根本原因、Evidence追加可否、親子/関連表現を確認する。重複生成を防ぐ決定的deduplication_keyを用いる。再オープンは単なるスコア変動ではなく、未解決状態の再成立と根拠を要求する。

---

# 6. Interest・Focus選択規則

Interest Evaluatorは評価を行い、Focus Managerは選択する。Interest順位をそのままFocus順位にしない。

選択順序:
1. 明示的Focus指示とScope確認
2. 現在Focusの有効性確認
3. Scope・階層制約適用
4. 明示指示に反しない候補のInterest比較
5. 影響範囲・未解決度・Recency等の比較
6. ヒステリシスと最低維持条件
7. KEEP / DESCEND / ASCEND / SWITCH / REACTIVATE / MULTI / DEFER
8. 遷移理由記録

明示的な話題転換・注目指示はヒステリシスで阻害しない。時間経過のみでQuestionやEvidenceを無効化しない。再発・新Evidence・明示指示・Dependency ImpactによりInterestを再活性化できる。

---

# 7. Dependencyと再評価収束

Dependency Graphは有向グラフとし、trigger_conditionを満たす下流ノードのみImpact Setへ追加する。Impact Setは起点、到達経路、発火条件、理由を保持する。

再評価Jobはreconciliation_id、base_state_version、generation、root_change_refs、scheduled_node_refs、visited_node_refs、cycle_group_refs、iteration_count、statusを持つ。同一generation内で同じnode + policy_version + effective_input_fingerprintを重複評価しない。

Dependency GraphのSCCを循環グループとし、SCC内部は固定点評価単位とする。固定点は意味状態・有効性・依存出力が不変で新たなImpactが生じない状態。文章表現の差だけは状態変化とみなさない。

停止条件:
- 固定点到達
- 影響対象なし
- 設定上限到達
- 同一状態反復
- 入力/State Version変更による再起動
- 検証不能・循環異常

上限到達・反復時はRECONCILIATION_STALLED等で保留し、暫定状態を確定扱いしない。再評価はroot change、base version、policy version、effective input fingerprintに対して冪等とする。

---

# 8. Streamingと時間モデル

時間属性はevent_time、observed_at、valid_from、valid_until、revised_at、committed_atを区別する。UTC機械時刻と元タイムゾーンを保持可能とし、不明はUNKNOWNで表す。source_sequenceは同一Source内順序であり、異なるSource間の全順序を仮定しない。因果参照があれば因果順序を優先する。

部分入力はPARTIAL Eventまたは同一論理入力のrevisionで表す。暫定Interpretation/Unitにはprovisional marker、base version、根拠範囲を付ける。確定入力は暫定状態を上書きせず、新Eventと再評価で反映する。

粒度収束は原情報順序、情報完全性、finalization境界、Adapter位置/因果情報、Recognition Policy/Model Versionが同等な場合の最終意味状態を比較する。内部IDやイベント数の一致は要求しない。

遅着Eventはobserved_atとevent_timeを分け、順序復元可能な範囲だけ再評価する。復元不能な順序を捏造しない。

---

# 9. INIT / UPDATE / RECONCILE

## 9.1 INIT
Event受信 -> Evidence登録 -> Semantic Proposal -> Unit/Entity/Cluster/Relation候補 -> Question/Interest/Focus評価 -> Dependency Impact -> Validation -> Commit -> History更新。INITは逐次的初期化である。

## 9.2 UPDATE
新Eventを既存Context Viewと照合し、候補生成、統合、Focus評価、Dependency Impact、State Delta、Validation、Commitを行う。影響範囲を限定する。

## 9.3 RECONCILE
Root ChangeとBase Versionを確定し、Impact Set算出、SCC特定、再評価順序構築、世代内重複排除、意味状態比較、派生Question/Interest/Focus評価、Validation/Commit、停止理由・未解決範囲・再開条件の記録を行う。全文脈の無条件再構築ではない。

---

# 10. Recognition Engine / Core責務分離

Recognition Engine/LLMは意味認識、仮説、参照/Entity/Relation/Cluster/Question/Interest/Trust候補、State Delta候補を生成する。Coreは入力整合性、Evidence、データ構造、状態遷移、Dependency、Uncertainty、Transaction、Commit、History、Recoveryを担当する。

モデル出力はProposalであり確定権限を持たない。Proposalはproducer/version、input refs、context snapshot version、candidate objects、uncertainty、evidence/provenance refs、requested operations、processing metadataを持つ。Coreは一部採用・拒否・保留できる。

---

# 11. Core Transaction / Commit / Recovery

Commit前にBase Version、read_set/write_set、参照先、Evidence/Provenance、状態遷移、Invariant、Dependency Impact、重複イベント、Question/Interest/Focus参照、Source Lineageを検証する。

Optimistic Concurrencyではread_setのVersion一致を要求する。競合Deltaは保留し、現在StateからProposal再生成または安全な非競合分離後に再検証する。古いDeltaの強制上書きを禁止する。

1 CommitはState Deltaと履歴イベントを原子的に記録する。Event Logを再構成基準、Snapshotを派生高速復旧物とする。SnapshotはEvent Offset、State Version、Schema Version、Digestを持つ。RecoveryではSnapshot検証後に後続Eventを再生し、Commit履歴とVersionを照合する。不整合状態は正常として公開しない。

確率的モデルの再実行結果を同一と仮定しない。実際にCommitしたProposal、モデル/ルールVersion、入力参照、検証結果を保存する。

---

# 12. 共通処理パイプライン

1. Event形式・識別子・順序の検証
2. Idempotency判定
3. Evidence登録
4. Context Viewを用いたRecognition Proposal生成
5. Unit/Entity/Cluster/Relation候補の統合
6. 矛盾/Gap分類
7. Question生成・統合
8. Interest評価
9. Focus選択
10. Dependency Impact算出
11. State Validation
12. Transaction Commit
13. Commit Eventを起点とする非同期RECONCILE

各段階はProposal/Deltaを生成し、確定状態はTransaction Coordinatorのみが変更する。

---

# 13. Context Integrity / 不変条件

全ての確定状態は根拠、関係、時間、不確実性、変更履歴を追跡可能とする。

| ID | 不変条件 |
|---|---|
| I-01 | Evidence Immutability |
| I-02 | Granularity Independence |
| I-03 | Focus-Structure Separation |
| I-04 | Uncertainty Preservation |
| I-05 | Temporal Separation |
| I-06 | Relation-Dependency Separation |
| I-07 | Provenance Traceability |
| I-08 | Non-destructive Cluster Evolution |
| I-09 | Idempotency |
| I-10 | State Reconstructability |
| I-11 | Core Authority |
| I-12 | Retrospective Reevaluation |
| I-13 | Transactional Integrity |
| I-14 | Uncertainty Propagation Control |
| I-15 | Entity Identity Preservation |
| I-16 | Context Integrity |
| I-17 | Question Traceability |
| I-18 | Interest-Focus Separation |
| I-19 | No External Action Requirement |
| I-20 | Trust-Growth Separation |
| I-21 | Contextual Trust |
| I-22 | Question Non-destructiveness |
| I-23 | Dependency Convergence and Bounded Reconciliation |
| I-24 | Source Lineage Independence |
| I-25 | Provisional / Committed State Separation |

加えて、詳細設計Invariant D-01〜D-20をCore検証規則として適用する。D-01〜D-20の説明と検証方法はDETAILED_DESIGN_v0.1.mdを参照する。

---

# 14. 非機能要件

- 拡張性: Adapter、Recognition Engine、Repositoryを独立して交換可能
- 再現性: 同じEvent履歴とCommit判断から同等状態を再構成
- 耐障害性: Event Log、State History、SnapshotによるRecovery
- 監査性: 根拠、処理経路、状態変更履歴の追跡
- 低遅延性: 暫定結果を逐次生成し確定状態と分離
- 言語拡張性: 日本語優先、Coreデータ構造は言語非依存
- 収束性: Dependency再評価は固定点または明示的停止理由に到達
- 整合性: 全確定状態変更はCore Transactionを経由

---

# 15. 適合性試験

少なくとも以下を検証する: Granularity Convergence、Focus遷移、Reference Resolution、Temporal Update、Dependency Impact/SCC、Cluster Merge/Split、Parallel Input、Transaction Conflict、Uncertainty Propagation、Entity Identity、Recovery、Provenance、Question Lifecycle/Dedup/Reopen、Interest-to-Focus、Source Trust/Lineage、Partial/Final Streaming。

詳細設計の必須30シナリオに加え、50事例思考実験を回帰テスト候補として維持する。期待結果は自然言語出力だけでなく、状態グラフの意味的条件で評価する。思考実験の評価結果は実装適合性の証明とはみなさない。

測定指標: Semantic Convergence Rate、Focus Transition Precision/Recall、Reference/Entity Resolution Accuracy、Cluster Purity/Fragmentation、Dependency Propagation Accuracy、State Consistency、Uncertainty Preservation、Provenance Completeness、Question Lifecycle Correctness、Interest-to-Focus Alignment、Trust Calibration、Recovery Correctness、Processing Latency。

---

# 16. 実装段階

A. Event/Evidence/Transaction Foundation  
B. Semantic Context (Interpretation/Unit/Claim/Entity/Cluster/Relation)  
C. Question/Dependency (Lifecycle/Dedup/SCC/Convergence)  
D. Interest/Focus (Scope/Selection/Hysteresis/Reactivation)  
E. Trust/Lineage (Source Lineage/Independence/Calibration)  
F. Streaming Convergence (Partial/Final/Late Arrival/Granularity Tests)

各Stageの完了は、対応するInvariantと必須テストの合格によって判定する。

---

# 17. JSON Schema / Core API適合契約

## 17.1 JSON Schema

規範Schemaは `schemas/surge-v0.5.schema.json` とする。JSON Schema Draft 2020-12を使用する。Schemaはデータ形状・型・必須属性・enum・参照形式を検証する。状態遷移、参照整合性、Evidenceの実在性、read/write競合、SCC収束等の文脈依存制約はCore Validatorの責務とする。

全レコードはschema_versionを持つ。未知フィールドは規範オブジェクトでは拒否し、拡張データはextensionsに格納する。IDは不透明な非空文字列であり、CoreはID文字列の意味に依存しない。

## 17.2 Core API

規範API契約は `docs/surge/CORE_API_v0.2.md` とする。APIはTransport-neutralな操作契約であり、HTTP、RPC、in-process ABIのいずれにも写像できる。

主要操作:
- ingest_event
- get_context
- submit_proposal
- validate_delta
- commit_delta
- reconcile
- get_question
- get_focus
- create_snapshot
- restore_snapshot

Recognition EngineはProposalを提出するだけで、Commit権限を持たない。CommitはCore Transaction Coordinatorを経由する。全書き込み操作はIdempotencyとBase State Versionを扱う。

---

# 18. バージョン・互換性

v0.4はv0.3のアーキテクチャ原則を維持し、DETAILED_DESIGN_v0.1のデータ契約、状態遷移、Question統合、Interest/Focus選択、Dependency収束、Source Lineage、Streaming Convergence、Transaction/Recoveryを正式基礎仕様へ統合する。

v0.3は履歴資料として保持し、上書きしない。v0.4以降の規範的な実装契約は本書、JSON Schema、Core APIの三者とする。三者間に矛盾がある場合、互換性を壊す暗黙解釈を行わず、仕様改訂で解決する。

**結論:** SURGE v0.4は、Evidenceを不変の根拠基盤とし、Interpretationと文脈状態を非破壊的に進化させ、Question/Interest/Focusを認知制御として分離し、Dependency収束制御とCore Transactionによって継続的な文脈整合性を保証する。


# 15. v0.5 詳細設計強化要件

v0.5はv0.4の基本アーキテクチャを維持し、DETAILED_DESIGN_v0.2.mdの判定契約を規範要件として追加する。

## 15.1 Semantic Equivalence
粒度収束・重複判定・Dependency固定点はSemantic Projectionとバージョン付きCanonicalizationを用いる。比較結果はEXACT/EQUIVALENT/COMPATIBLE/CONFLICTING/INCOMPARABLEに分類し、EXACT/EQUIVALENTのみを収束成功とする。

## 15.2 Event Correction
Event/Evidenceは不変とし、訂正はAMEND/SUPERSEDE/RETRACT/CLARIFYの新Eventで表す。Revision Groupと分岐を保持し、受信時刻だけで競合訂正を決着させない。

## 15.3 Focus Arbitration
Focus選択はScope Policyに従う階層的決定とする。Scope間Interest比較を禁止し、解消不能な指示競合・同順位はMULTI/DEFERを許容する。Focus変更はCluster lifecycleを変更しない。

## 15.4 Temporal Semantics
時間区間は原則半開区間[valid_from, valid_until)。UNKNOWN、OPEN_BOUNDARY、point eventを区別し、比較不能な時間を矛盾確定しない。

## 15.5 Lineage / Independence
Source Lineageは循環を許さない有向グラフとする。複数起源・部分独立はEvidence Component単位で評価し、同一Componentの複数経路到達は一度だけ集計する。UNKNOWNを独立裏付けに加算しない。

## 15.6 Recognition Interoperability
Producerはsemantic contract、canonicalization、capability、confidence calibration等のDescriptorをProposalに付与する。未対応Semantic TypeはUNMAPPEDとして保持し、異なる尺度のconfidenceを無校正で比較しない。

## 15.7 Context Lifecycle / Resource Budget
Cluster lifecycleとFocus roleは独立状態とする。Recency減衰やFocus解除だけでClusterをArchiveしない。Multi-FocusはScope PolicyのBudgetと公平性規則に従い、超過対象を削除せずDeferできる。

## 15.8 Conformance
Schema検証に加え、詳細設計v0.2のCross-record/意味検証と追加テスト31〜50を満たす。Schema適合のみをCore適合の証明としてはならない。

## 16. v0.5文書構成
- 基礎仕様: SPECIFICATION_v0.5.md
- 詳細設計: DETAILED_DESIGN_v0.2.md
- JSON Schema: schemas/surge-v0.5.schema.json
- Core API: CORE_API_v0.2.md

v0.4文書は履歴として保持し、既存データは明示的Schema Migrationなしにv0.5として読み替えない。


### 0.2 Durable spatial focus state and bounded execution

Spatial focus state is durable ContextGraph state. It consists of an optional logical
region, persisted 3D positions for clusters, and independent per-cluster focus depths.
These are three different state dimensions and are updated by different journal event
families:

- `FOCUS_REGION_SET_V1`: initialize or replace the region descriptor.
- `FOCUS_REGION_MOVE_V1`: change only the region center.
- `FOCUS_REGION_RESIZE_V1`: change only the region half-extents.
  Move and resize events never change cluster depth or cluster coordinates.
- `CLUSTER_SPATIAL_POSITION_SET_V1`: update one cluster's position. It never moves
  or resizes the focus region.
- `CLUSTER_FOCUS_DEPTH_SET_V1`: update one cluster's depth. It never changes region
  geometry or cluster position.

Each event participates in SemanticDelta idempotency, optimistic state-version
checking, journal replay and the graph snapshot. New snapshots use `CGS2`; the
restorer continues to accept existing `CGS1` snapshots without spatial state.
Malformed spatial state, non-finite coordinates, negative extents, references to
non-cluster nodes, and excessive depth are rejected during apply or restore.

The logical region and its membership are distinct from bounded execution:

- `logical_region_cluster_ids` records every positioned cluster geometrically inside
  the region, regardless of whether the execution budget permits processing it.
- `processed_region_cluster_ids` records roots whose configured traversal completed.
- `deferred_region_cluster_ids` records roots not fully processed due to budget.
- The region descriptor and logical membership are never contracted to hide work
  omitted by a budget limit.

The observation reports position checks, clusters considered/in-region/scheduled/
deferred, nodes processed, relations examined/emitted, work units, elapsed
microseconds, configured limits and `budget_exhausted`. Relation inspection is
capped before traversal and the full graph is not copied as an intermediate.
Work units are position checks + relation records examined + nodes emitted;
elapsed microseconds are measured wall-clock time for the projection. Neither metric is a correctness score.

The current geometry is an axis-aligned 3D box. Graph topology and Euclidean
coordinates remain separate: Contains traversal is controlled by per-cluster depth,
not inferred from geometric distance.
