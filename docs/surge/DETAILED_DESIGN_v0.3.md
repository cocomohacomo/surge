# SURGE 詳細設計 v0.3
## Detailed Design Specification

**Version:** 0.3.0  
**基準仕様:** SURGE 基礎設計 v0.6.0  
**Status:** Normative Detailed Design for SPECIFICATION_v0.6.md  
**設計方針:** Architecture-preserving / Streaming-native / Evidence-first / Non-destructive

---

## 1. 目的と適用範囲

本書はSURGE v0.3の概念および責務分離を変更せず、実装に必要なデータ契約、状態遷移、処理順序、再評価規則、整合性制約を定義する。

本書は実装アルゴリズムの選択余地を残す。特定LLM、データベース、メッセージブローカー、数値重みを必須化しない。

### 1.1 設計上の優先順位

1. Evidenceと履歴の非破壊性
2. 不確実性を失わないこと
3. 確定状態をCoreのみがCommitすること
4. 影響範囲を限定した再評価
5. 入力粒度が異なっても意味的に同等な最終状態へ収束すること
6. ストリーミング処理の継続性と低遅延性

### 1.2 用語上の区別

- **事実主張 (Claim):** EvidenceからInterpretationを介して表現された、真偽・有効性を評価可能な内容。
- **Evidence:** 入力イベントに由来する不変の根拠。
- **Interpretation:** Evidenceに対する一つ以上の意味仮説。
- **Committed State:** Coreが検証し永続化した状態。
- **Provisional State:** 暫定採用されたが、後続情報により再評価され得る状態。
- **Question:** 文脈内に残る未解決事項。外部への質問や行動命令ではない。
- **Interest:** QuestionまたはClusterへ認知資源を向ける必要性の評価。
- **Focus:** Scope内で実際に選択された認知対象。
- **Relation:** 意味的な関係。
- **Dependency:** 変更時に再評価を必要とする影響関係。

---

## 2. 論理コンポーネントと責務境界

既存の6レイヤーと横断Core機構を維持し、内部コンポーネントを次のように分割する。

| コンポーネント | 主な入力 | 出力 | 禁止事項 |
|---|---|---|---|
| Event Gateway | Adapter Event | 正規化Input Event | 意味解釈、状態Commit |
| Evidence Registrar | Input Event | Evidence | 原入力の上書き |
| Recognition Orchestrator | Event/Evidence/Context View | Recognition Proposal | Repository直接更新 |
| Interpretation Manager | Recognition Proposal | Interpretation候補 | 旧Interpretation削除 |
| Context Integrator | Interpretation候補/Context View | Unit・Entity・Cluster・Relation候補 | 未確定同一性の強制統合 |
| Question Manager | Conflict/Gap/Ambiguity候補 | Question Delta | Questionを自然言語質問や行動命令に限定 |
| Interest Evaluator | Question/Cluster/Scope | Interest Delta | Interestのみで状態確定 |
| Focus Manager | Interest/明示指示/Context View | Focus Delta | Focus変更によるCluster改変 |
| Dependency Planner | Delta/Dependency Graph | Impact Set | Relationを無条件にDependency扱い |
| Reconciliation Engine | Impact Set/Current State | Reconciliation Proposal | 全文脈の無条件再解析 |
| State Validator | Proposal/Invariant Set | Validation Result | 不明点を暗黙に確定 |
| Transaction Coordinator | Validated Delta | Commit/Conflict | 古いBase Versionでの無条件Commit |
| Repository | Committed Events/State | Snapshot/History/Views | LLMからの直接書き込み |

各コンポーネントは候補または差分を返す。確定状態の変更はTransaction Coordinatorを通過する。

---

## 3. 共通識別子と時間モデル

### 3.1 識別子

すべての永続エンティティに型付き一意識別子を付与する。例: `event_id`, `evidence_id`, `interpretation_id`, `unit_id`, `question_id`。

識別子は生成後に再利用しない。統合・分割・置換後も旧IDを保持し、Lineageで新旧関係を表現する。

### 3.2 時間属性

時刻はUTC基準の機械可読値と元タイムゾーン情報を併記できる構造とする。時刻が分からない場合はゼロ値で代用せずUNKNOWNを表現する。

- event_time: 情報が発生した時刻
- observed_at: SURGEが受信した時刻
- valid_from / valid_until: 主張の有効期間
- revised_at: 訂正・改訂が観測された時刻
- committed_at: 状態変更がCommitされた時刻

イベント順序は少なくとも source_id + source_sequence で表す。異なるソース間の全順序を仮定せず、因果参照がある場合は因果順序を優先する。

---

## 4. データ契約

以下は論理スキーマであり、物理DB型やシリアライズ形式を固定しない。全レコードに schema_version、created_at、provenance_refs を付与可能とする。

### 4.1 Input Event

必須属性:
- event_id
- source_id
- source_sequence または source_position
- observed_at
- content_type
- payload_ref または content
- idempotency_key

任意属性:
- event_time
- speaker_id
- document_id
- input_position
- causal_refs
- locale
- adapter_metadata

イベント本文は原則不変。訂正は新しいEventとして追加し、supersedes_event_ref等で関連付ける。

### 4.2 Evidence

必須属性:
- evidence_id
- event_refs
- evidence_kind
- source_lineage_ref
- observed_at
- integrity_digest
- validity_status

Evidenceは原イベントの該当範囲へ逆参照できること。派生Evidenceはderived_from_refsを持ち、原入力と派生結果を混同しない。

### 4.3 Interpretation

必須属性:
- interpretation_id
- evidence_refs
- hypothesis
- interpretation_status
- confidence_record
- created_at
- producer_id
- model_or_rule_version

confidence_recordは単一数値だけでなく、値の尺度・校正情報・評価対象を識別できる形とする。異なるモデルの確信度を無校正で直接比較しない。

Interpretationの状態:
- CANDIDATE
- PROVISIONAL
- ACCEPTED
- SUPERSEDED
- REJECTED
- NEEDS_REEVALUATION

REJECTEDはEvidenceが削除されたことを意味しない。新しい根拠で旧解釈が再び候補化された場合は、新しいInterpretationを生成する。

### 4.4 Semantic Unit / Claim

Semantic Unitは意味的なまとまりを表す。真偽評価可能な主張はClaim表現を持たせる。

Claimの比較キーは少なくとも subject、predicate、object、polarity、modality、temporal_scope、context_scopeを含む。表層文字列一致のみで同一Claimと判定しない。

状態:
- PROPOSED
- PROVISIONAL
- ACTIVE
- DISPUTED
- SUPERSEDED
- INVALIDATED
- ARCHIVED

DISPUTEDはClaimが存在しない状態ではなく、相反するEvidenceまたはInterpretationが共存している状態。

### 4.5 Entity

Entity Identityは次の状態を持つ:
- UNRESOLVED
- PROVISIONAL
- CONFIRMED
- DISTINCT
- MERGED
- SPLIT

MERGED/SPLITは履歴上の操作結果であり、過去参照を書き換えない。Entity Aliasは同一性の根拠ではなく、候補生成の材料として扱う。

### 4.6 Cluster

Cluster状態:
- ACTIVE
- DORMANT
- ARCHIVED
- MERGED
- SPLIT

Clusterにはmember lineage、parent/child lineage、merge/split eventを保持する。Cluster IDを再利用しない。

### 4.7 Relation / Dependency

Relationは意味的関係と根拠を保持する。Dependencyは追加でtrigger_condition、impact_policy、evaluation_policy、cycle_group_refを持つ。

Dependencyの有効状態:
- ACTIVE
- INACTIVE
- SUSPENDED
- INVALIDATED

依存辺は必ず方向性を持つ。AがBに依存する場合、Aの再評価対象化をBの変更が引き起こす向きで格納する。API上のsource/target命名は実装時にこの意味と一致させる。

### 4.8 Question

Question必須属性:
- question_id
- question_type
- unresolved_aspects
- target_refs
- trigger_refs
- status
- created_at
- updated_at
- provenance_refs

Question Type:
- CONTRADICTION
- AMBIGUITY
- KNOWLEDGE_GAP
- REFERENCE_UNRESOLVED
- TEMPORAL_INCONSISTENCY
- DEPENDENCY_INCONSISTENCY
- EVIDENCE_INSUFFICIENT
- SOURCE_INDEPENDENCE_UNCLEAR

Question状態遷移:
- OPEN -> INVESTIGATING | DEFERRED | RESOLVED | SUPERSEDED
- INVESTIGATING -> OPEN | DEFERRED | RESOLVED | SUPERSEDED
- DEFERRED -> OPEN | INVESTIGATING | RESOLVED | SUPERSEDED
- RESOLVED -> OPEN (reopen) のみ新しい根拠または既存根拠の有効性変更を伴う
- SUPERSEDED -> 終端。再利用せず新規Questionを生成

RESOLVEDは「唯一の答えが確定した」ことに限定しない。時間区分による両立、複数候補の保持、矛盾の原因特定などにより、元の未解決事項が解消された場合も含む。未解決のまま保留する場合はDEFERREDとする。

### 4.9 Questionの重複・統合

同一Question候補の判定は次の特徴を使う:
1. 未解決対象の意味的一致
2. 根本原因または矛盾するClaimの一致
3. 対象Scopeの互換性
4. 時間範囲の重なり
5. 既存QuestionとのEvidence/Interpretation参照関係

単一の類似度閾値だけで自動統合しない。確定的な重複は既存Questionにtrigger_refsを追加する。包含関係は親子Questionで表現する。部分的に重なるだけなら別Questionを維持し、related_question_refsを付与する。

統合時はcanonical Questionを選び、統合元をSUPERSEDEDとして残す。統合元の根拠と履歴はcanonical側へ参照統合し、元IDを消去しない。

### 4.10 Interest

Interestはtarget_ref、scope_id、component_vector、priority_band、status、evaluation_version、history_refsを持つ。

component_vectorの基本要素:
- unresolvedness
- impact
- dependency_reach
- recency
- recurrence
- context_relevance
- explicit_attention
- evidence_uncertainty

各要素は値だけでなく、算出根拠・尺度・評価時刻を保持する。異なるScope間で値を無条件に比較しない。

Interest状態:
- ACTIVE
- DECAYING
- DORMANT
- REACTIVATED
- CLOSED

QuestionがRESOLVEDになっても、関連ClusterへのInterestが独立して存在する場合は維持できる。Question状態とInterest状態を同一遷移にしない。

### 4.11 Focus

Focusはscope_id、target_ref、level、role、activation_reason、status、previous_focus_refs、transition_event_refを持つ。

Focus role:
- PRIMARY
- SECONDARY
- BACKGROUND
- DORMANT

Focusは複数保持可能。ScopeごとにPRIMARYの一意性を要求するかはScope Policyで定義する。GlobalとLocalで別々のPRIMARYを許容する。

### 4.12 State Delta

State Deltaは次を持つ:
- delta_id
- base_state_version
- input_event_refs
- read_set
- write_set
- operations
- affected_refs
- dependency_generation
- validation_requirements
- producer_refs

操作はADD、LINK、UPDATE_STATUS、SUPERSEDE、MERGE、SPLIT、REOPEN等の型付き命令とする。削除操作は原則提供せず、無効化・アーカイブ・置換で表現する。

---

## 5. Question生成ポリシー

### 5.1 矛盾の区別

少なくとも以下を区別する:
- DIRECT_CONTRADICTION: 同一Scope・重複時間・両立不能な主張
- TEMPORAL_DIFFERENCE: 時間が異なり両立可能
- SCOPE_DIFFERENCE: 対象範囲が異なり両立可能
- PERSPECTIVE_DIFFERENCE: 発言者・立場・評価軸の違い
- MODAL_DIFFERENCE: 事実、予定、希望、仮説等の様相の違い
- EVIDENCE_CONFLICT: 根拠の内容または信頼性が競合
- INTERPRETATION_CONFLICT: 同一Evidenceに複数解釈がある

Question生成は矛盾候補の分類後に行う。時間・Scope・話者・様相の違いで説明可能なら、直ちに事実矛盾Questionを生成せず、分類結果を記録する。分類自体が不確実なら分類不確実性をQuestion化できる。

### 5.2 生成抑制

Questionを生成する前に:
1. 既存Questionを検索
2. 同じ根本原因か判定
3. 既存QuestionにEvidenceを追加できるか判定
4. 親子関係または関連Questionで表現できるか判定
5. それでも独立した未解決事項なら新規生成

認識器の再実行だけで同一Questionを増殖させない。生成要求は決定的なdeduplication_keyと入力参照を持つ。

### 5.3 解消と再オープン

Questionの解消にはresolution_basisを必須とする。resolution_basisは根拠、適用Interpretation、解消理由、対象時間/Scopeを記録する。

既存Evidenceの再評価だけでQuestionを再オープンする場合も、再評価イベントと変更された評価を記録する。単なるスコア変動ではなく、未解決状態が再成立したことを要件とする。

---

## 6. Interest・Focus選択規則

### 6.1 評価と選択の分離

Interest Evaluatorは評価状態を生成する。Focus ManagerはScope Policy、明示指示、既存Focus、Interest、文脈構造を用いて選択する。Interest順位をそのままFocus順位に変換しない。

### 6.2 選択順序

Focus Managerは以下の順序で判断する:
1. 明示的Focus指示と適用Scopeを確認
2. 現在Focusの有効性を確認
3. Scope・階層制約を適用
4. 明示指示に反しない候補間でInterestを比較
5. 影響範囲・未解決度・Recency等の要素を比較
6. ヒステリシスと最低維持条件を適用
7. KEEP / DESCEND / ASCEND / SWITCH / REACTIVATE / MULTI / DEFERを決定
8. 遷移理由を記録

### 6.3 ヒステリシス

Focus切替は新候補が一時的に上位になっただけでは実行しない。現在Focusの維持価値、候補の優位性、明示指示、緊急性、Scopeを考慮する。

明示的な話題転換・注目指示はヒステリシスの対象外とする。ヒステリシスは明示指示を抑制してはならない。

### 6.4 Interest減衰

時間経過のみでQuestionやEvidenceを無効化しない。Recency成分の減衰とQuestionの解消は別処理である。再発・新Evidence・明示指示・Dependency ImpactによりInterestを再活性化できる。

---

## 7. Dependencyと再評価の収束制御

### 7.1 影響グラフ

Dependency Graphは有向グラフとして管理する。変更されたノードから、trigger_conditionを満たす下流ノードのみをImpact Setへ追加する。

Impact Setには起点、到達経路、発火条件、評価理由を含める。Relationだけを辿って無制限に波及させない。

### 7.2 再評価世代

各Commitにstate_versionを割り当てる。再評価ジョブは以下を持つ:
- reconciliation_id
- base_state_version
- generation
- root_change_refs
- scheduled_node_refs
- visited_node_refs
- cycle_group_refs
- iteration_count
- status

同一generation内で同じnode + evaluation_policy_version + effective_input_fingerprintを重複評価しない。

### 7.3 循環依存

Dependency Graphの強連結成分(SCC)を循環グループとして扱う。SCC内は固定点評価単位として処理し、外部へは収束した差分のみを伝播する。

固定点は、対象ノードの意味状態・有効性・依存出力が前回評価と同一で、新たなImpact Setが生じない状態とする。モデルの文章出力が異なるだけでは状態変化とみなさない。

### 7.4 収束制御

以下のいずれかで停止:
- 固定点に到達
- 影響対象がなくなった
- 設定上限に到達
- 同一状態の反復を検出
- 入力・状態Versionが変わり再起動が必要
- 検証不能または循環異常を検出

上限到達・反復検出時はQuestionまたはRECONCILIATION_STALLED状態を記録し、暫定状態を確定扱いしない。上限値は実装設定であり、基礎意味論には埋め込まない。

### 7.5 再評価の冪等性

同じroot change、base version、policy version、effective input fingerprintに対する再評価は同じ論理差分に収束させる。重複ジョブの実行は重複Commitを発生させない。

---

## 8. Source LineageとEvidence独立性

### 8.1 Source Lineage

Source Lineageは情報の出所関係を有向グラフで表す。関係種別:
- ORIGINAL
- QUOTES
- REPOSTS
- DERIVED_FROM
- SYNDICATED_FROM
- UNKNOWN_ORIGIN

出所不明は独立と仮定しない。UNKNOWN_ORIGIN同士を同一起源と断定もしない。

### 8.2 独立性評価

Evidenceの独立性はSource IDの相違だけで判定しない。共通原文、引用、転載、配信元、生成元、同一データセット等を参照する。

独立性状態:
- INDEPENDENT
- DEPENDENT
- PARTIALLY_DEPENDENT
- UNKNOWN

INDEPENDENTのみを独立裏付けとして数える。UNKNOWNは独立裏付け数へ加算せず、評価確度を下げる。PARTIALLY_DEPENDENTは独立成分と共有成分を分離できる場合のみ独立成分を評価対象にする。

### 8.3 Source Trust

Source Trustはdomain/scope別に評価し、Evidence自体の有効性と分離する。評価には評価値と評価確度を別々に持たせる。

- 十分な実績がない: UNKNOWN / LOW_CONFIDENCE_ASSESSMENT
- 独立裏付けがある: corroboration evidenceとして加味
- 反証・訂正がある: 対象領域と期間に限定して履歴へ反映
- 新規ソース: 未知として扱い、既定で高信頼/低信頼に分類しない

Trustは解釈候補の比較を補助するだけで、単独でClaimを確定・無効化しない。Trust更新が同じEvidenceの再評価を誘発しても、同じTrust評価を再帰的に自分の根拠として使わない。Trust評価入力はTrust自身から独立したEvidence/評価履歴に限定する。

### 8.4 Cluster Growthとの分離

Cluster Growthは少なくとも以下へ分解:
- 新規情報量
- 意味構造の追加
- 独立裏付け
- 時間的安定性
- 反証・訂正
- 情報源の多様性

量的増加のみでTrustを上昇させない。反復・転載による件数増加は独立裏付けの増加ではない。

---

## 9. StreamingとGranularity Convergence

### 9.1 部分入力

未完了入力はPARTIAL Eventまたは同一論理入力のrevisionとして表現する。部分文字列を確定Evidenceと同じ扱いにしない。入力確定時にfinalization eventを発行し、暫定解釈との関係を記録する。

### 9.2 暫定結果

低遅延のため暫定Interpretation/Unitを許容する。暫定状態にはprovisional marker、base version、根拠範囲を付与する。確定入力が到着したら既存の暫定状態を上書きせず、SUPERSEDED等の履歴を残して再評価する。

### 9.3 粒度差の収束

同じ情報列を異なる分割単位で入力した場合、次の条件を満たすことを収束目標とする:
- 原情報の順序が等価
- 情報欠落がない
- finalization境界が同等
- Adapterが同等の位置・因果情報を保持
- 同じRecognition Policy/Model Versionを使用

比較は内部IDやイベント数ではなく、正規化された意味状態、Claim、Entity、Cluster、Question、Focusの意味的同値性で行う。

文字単位ストリームと文単位バッチの中間状態は一致を要求しない。確定境界後の状態同値性を評価する。

### 9.4 遅延・順序逆転

遅着イベントはobserved_atとevent_timeを分けて保持する。source_sequenceまたはcausal_refsで順序を復元できる場合は該当範囲を再評価する。復元不能な場合は順序不確実性を保持し、全体順序を捏造しない。

---

## 10. Transaction、Commit、Recovery

### 10.1 Commit前検証

Commit前に以下を検証:
- Base State Versionの整合
- 参照先IDの存在
- Evidence/Provenanceの追跡可能性
- 状態遷移の許可
- 不変条件
- Dependency Impactの妥当性
- 同一イベント・同一差分の重複
- Question/Interest/Focus間の参照整合
- Source Lineageの循環・重複起源の扱い

### 10.2 Optimistic Concurrency

Deltaはread_setとwrite_setを宣言する。Commit時にread_setの対象Versionが変わっていない場合にCommit可能とする。

競合時:
1. Deltaを保留
2. 競合対象を特定
3. 現在StateからProposalを再生成、または非競合部分を安全に分離
4. 再検証
5. 再Commit

古いDeltaの強制上書きは禁止。リトライ上限到達時はCONFLICT_PENDINGとして保持する。

### 10.3 原子的Commit

1回のCommitは関連するState Deltaと履歴イベントを原子的に記録する。部分的な成功を外部へCommittedとして見せない。

### 10.4 Event LogとSnapshot

Event Logを再構成の基準とし、Snapshotは高速復旧用の派生物とする。Snapshotには適用済みEvent Offset、State Version、Schema Version、Integrity Digestを記録する。

Recovery:
1. 最新の検証可能Snapshotを選択
2. SnapshotのDigest/Schemaを検証
3. Snapshot以降のEventを順序規則に従って再生
4. Commit履歴とState Versionを照合
5. 不整合があれば隔離し、正常状態として公開しない

### 10.5 再現性

確率的モデルの再実行結果を同一と仮定しない。実際にCommitされたProposal、モデル/ルールVersion、入力参照、検証結果を保存し、過去の確定状態を再構成する。

---

## 11. 共通処理パイプライン

### 11.1 Event処理

1. Event Gatewayが形式・識別子・順序情報を検証
2. Idempotency Keyで重複検知
3. Evidence RegistrarがEvidenceを登録
4. Recognition OrchestratorがContext Viewを読み意味候補を生成
5. Context IntegratorがSemantic Unit/Entity/Cluster/Relation候補を構成
6. Contradiction/Gap Detectorが未解決候補を分類
7. Question ManagerがQuestion Deltaを生成・統合
8. Interest EvaluatorがInterest Deltaを評価
9. Focus ManagerがFocus Deltaを生成
10. Dependency PlannerがImpact Setを生成
11. State Validatorが全Deltaを検証
12. Transaction CoordinatorがCommit
13. Commit Eventを起点に必要なRECONCILEを非同期起動

順序依存がある場合は、各段階の出力をProposalとして保持し、同一Transaction内で整合性を検証する。

### 11.2 RECONCILE処理

1. root changeとbase versionを確定
2. Dependency GraphからImpact Setを算出
3. SCC/循環グループを特定
4. 再評価順序を構築
5. 各対象を同じgeneration内で一度評価
6. 意味状態の変化を比較
7. 新たなImpactがあればキューへ追加
8. 固定点または停止条件まで繰り返す
9. Question/Interest/Focusへの派生影響を評価
10. 一貫したState Deltaとして検証・Commit
11. 停止理由・未解決範囲・再開条件を記録

---

## 12. 共通Proposal契約

Recognition Engine/LLMからCoreへ返すProposalは、少なくとも以下を持つ。

- proposal_id
- producer_id
- producer_version
- input_event_refs
- context_snapshot_version
- candidate_objects
- candidate_relations
- candidate_questions
- candidate_interests
- candidate_focus_transitions
- uncertainty_annotations
- evidence_refs
- provenance_refs
- requested_operations
- processing_metadata

Proposalには「確定」の権限を持たせない。CoreはProposalの一部だけを採用してもよい。拒否・保留された候補も、監査要件に応じて処理履歴に残す。

---

## 13. 不変条件と検証ルール

以下を実装時の機械検証可能なInvariantとして扱う。

| ID | 不変条件 | 検証 |
|---|---|---|
| D-01 | Evidenceは上書きされない | update/delete禁止、digest照合 |
| D-02 | Committed StateはEvidence/Provenanceへ追跡可能 | 参照閉包検証 |
| D-03 | Interpretationの変更は履歴化 | supersedes/created event検証 |
| D-04 | 未確定Entityは自動統合されない | identity_status検証 |
| D-05 | RelationとDependencyは別型 | schema/type検証 |
| D-06 | Questionの状態遷移は許可表に従う | transition validator |
| D-07 | Question解消にはresolution_basisがある | commit validation |
| D-08 | Interest単独で確定状態を変更しない | write-set policy |
| D-09 | 明示Focus指示をヒステリシスで阻害しない | transition test |
| D-10 | Trustは独立したEvidenceを根拠に評価 | lineage audit |
| D-11 | Unknown lineageを独立根拠に加算しない | trust validation |
| D-12 | 同一generationの同一対象を重複評価しない | evaluation key |
| D-13 | 循環依存は収束条件または停止理由を持つ | reconciliation audit |
| D-14 | 古いBase VersionのDeltaを無条件Commitしない | version check |
| D-15 | Event重複で二重Commitしない | idempotency check |
| D-16 | SnapshotはEvent Offset/Version/Digestを持つ | recovery validation |
| D-17 | 暫定結果と確定結果を区別する | provisional marker |
| D-18 | 遅着情報の順序不明を捏造しない | causal/order validation |
| D-19 | Question統合・置換で履歴を失わない | lineage validation |
| D-20 | Focus変更はCluster構造を暗黙変更しない | write-set validation |

---

## 14. 障害・例外処理

| 状況 | 処理 |
|---|---|
| Recognition Engine失敗 | Event/Evidenceを保持し、認識を再試行可能にする |
| Proposal形式不正 | Proposalを拒否し、理由を記録 |
| Evidence参照欠落 | Commit保留 |
| Entity同一性競合 | 未解決候補として保持、統合しない |
| Question重複判定が曖昧 | 統合せず関連付け |
| Dependency循環非収束 | 対象SCCを保留し、Question/停止理由を記録 |
| Commit Version競合 | Deltaを再生成または保留 |
| Snapshot破損 | 直近の検証可能Snapshotへ戻りEvent Logから再生 |
| Event順序不明 | 順序不確実性を保持 |
| Trustデータ不足 | UNKNOWNと低い評価確度を保持 |
| Model Version変更 | 過去Proposalを上書きせず、必要範囲を新規再評価 |
| 部分入力の撤回 | 新Eventで訂正・撤回を表し履歴を保持 |

---

## 15. 性能・スケーラビリティ設計

本設計では性能数値を未確定とする。初期実装では次の測定点を必須とする。

- Event受信からEvidence登録までの遅延
- Event受信からProvisional State生成までの遅延
- Commit遅延と競合率
- Impact Setサイズ
- Dependency再評価ノード数・世代数
- SCCあたりの反復回数
- Question重複統合率
- Focus切替頻度と誤切替率
- Repository読み書き量
- Snapshot生成時間・Recovery時間
- 入力粒度別の最終Semantic Convergence

大規模再評価は優先度キューと影響範囲分割を利用できる。ただし、優先度の低い再評価を永久に飢餓状態にしないため、滞留時間を監視する。再評価が遅延している対象は状態上でRECONCILIATION_PENDINGとして識別可能にする。

---

## 16. 実装段階と完了条件

### Stage A: Event/Evidence/Transaction Foundation
- Event正規化
- Evidence不変性
- Event Log
- State Delta
- Versioned Commit
- Idempotency
- Snapshot/Recoveryの最小実装

**完了条件:** 重複イベント、並行更新、Commit障害から状態を破壊せず復旧できる。

### Stage B: Semantic Context
- Interpretation
- Semantic Unit/Claim
- Entity
- Cluster
- Relation
- Reference Resolution

**完了条件:** 根拠追跡可能な意味構造を構築し、未確定Entityを保持できる。

### Stage C: Question/Dependency
- Contradiction Classification
- Question Lifecycle
- Deduplication/Merge/Parent-Child
- Dependency Impact Analysis
- SCC/Convergence

**完了条件:** Questionの重複増殖を抑え、依存再評価が停止条件を満たす。

### Stage D: Interest/Focus
- Interest component vector
- Scope policy
- Focus transition
- Hysteresis
- Reactivation

**完了条件:** InterestとFocusを分離し、明示指示・複数Scope・再活性化を正しく処理できる。

### Stage E: Trust/Lineage
- Source Lineage
- Evidence Independence
- Contextual Trust
- Trust calibration/history

**完了条件:** 転載の重複計上を避け、未知のソースを未知として保持できる。

### Stage F: Streaming Convergence
- Partial/final event
- Late arrival
- Granularity test harness
- Cross-adapter integration

**完了条件:** 同等の情報列で、分割方法に依存しない意味的最終状態への収束を確認できる。

---

## 17. 必須テストシナリオ

1. 同一Claimへの支持Evidence追加
2. 同一Claimへの反証Evidence追加
3. 同一発言者の意見変更
4. 異なる発言者の意見相違
5. 時間範囲の異なる主張
6. Scopeの異なる主張
7. 参照先が後続入力で確定
8. Entity候補の誤統合防止
9. Questionの重複候補統合
10. Questionの親子分割
11. 解消済みQuestionの再オープン
12. Interest競合とScope分離
13. 明示Focus指示とヒステリシス
14. 複数FocusとDormant再活性化
15. Dependencyの単純連鎖
16. Dependencyの循環SCC
17. Dependencyの非収束・上限到達
18. 同一イベントの重複配送
19. Eventの順序逆転・遅着
20. Commit時のVersion競合
21. Commit途中障害とRecovery
22. Snapshot破損
23. 転載・引用・共通起源Evidence
24. 新規ソースのTrust評価
25. 高Trustソースの反証
26. 部分入力の暫定解釈と確定化
27. 文字単位と文単位の収束比較
28. Model Version変更による再評価
29. 大規模Impact Setの分割処理
30. Trust評価の自己参照ループ防止

各テストは入力イベント、期待Proposal、許容される中間状態、期待最終状態、必須Invariantを定義する。期待結果は単一の文章出力ではなく、状態グラフの意味的条件で判定する。

---

## 18. 未確定事項と設計判断の保留

以下は本書で数値・実装方式を固定しない。

- Interest componentの正規化・重み・閾値
- Focus切替の具体的ヒステリシス値
- Question類似度のモデルと閾値
- Source Trustの統計モデル・校正方式
- Evidence独立性の推定器
- Dependency再評価上限とスケジューリング値
- Event Log/Repositoryの物理DB
- Snapshot間隔と保持期間
- LLM/Recognition Engineの具体的構成
- 初期性能目標

これらは代表データセット、50事例の再現テスト、追加の異常系テストを用いて測定・比較した後に決定する。未確定値を便宜的に固定し、基礎仕様上の必須要件として扱ってはならない。

---

## 19. 基礎仕様との整合性

本書はSURGE v0.3の以下の原則を維持する。

- Streaming-Native
- Granularity Invariance
- Model Independence
- Non-destructive Context Evolution
- Evidence-Based State Management
- Uncertainty Preservation
- Context Structure / Focus / Evolution Separation
- Question -> Interest -> Focusの認知的接続
- 外部行動を要求しないQuestion/Interest
- Contextual Source Trust
- Coreによる唯一のCommit権限
- Context Integrity

本書で定義した状態名・型・処理規則は実装可能な詳細化であり、基礎仕様の意味を変更するものではない。実装検証で矛盾が見つかった場合は、本書だけを暗黙に変更せず、変更理由と基礎仕様への影響を記録して改訂する。

**結論:** SURGEは、Evidenceを不変の基盤とし、Interpretationと状態を非破壊的に進化させ、Question/Interest/Focusを認知制御として分離管理し、DependencyとTransactionによって文脈の再評価と確定状態の整合性を保証する。


---

## v0.4統合記録

本書のデータ契約、状態遷移、Question統合、Interest/Focus選択、Dependency収束、Source Lineage、Streaming Convergence、Transaction/Recoveryに関する規範要件は、SURGE 基礎設計 v0.4.0へ統合された。JSON構造の機械可読契約は `schemas/surge-v0.4.schema.json`、Core操作契約は `docs/surge/CORE_API_v0.1.md` を参照する。本書は詳細設計の由来と設計判断を追跡するために保持し、v0.4以降の規範的な適合判定では基礎仕様・Schema・Core APIを優先する。


# v0.2 強化設計: SURGE v0.5

本章はv0.4のアーキテクチャを維持し、思考実験で抽出した境界条件を実装可能な規範規則へ具体化する。MUST/MUST NOT/SHOULDは規範要件である。

## 20. Semantic Equivalence Contract

### 20.1 Semantic Projection
粒度収束・重複判定・Dependency固定点は文章文字列ではなくSemantic Projectionを比較する。ProjectionはClaim(subject identity state, predicate, object, polarity, modality, temporal scope, context scope)、Entity(identity statusと確定/未確定link)、Cluster(member/parent/child lineageとlifecycle)、Question(type, unresolved aspects, targets, status, resolution basis)、Relation/Dependency(type, direction, validity, policy version)、Focus(scope, target, role, reason class)、Uncertainty(未解決次元と対象範囲)を含む。内部ID、イベント数、生成時刻、文章表現は比較キーに含めない。ただし意味を構成する時間・Scope・Provenanceは保持する。

### 20.2 同値分類
比較結果はEXACT、EQUIVALENT、COMPATIBLE、CONFLICTING、INCOMPARABLEのいずれか。EXACT/EQUIVALENTのみを収束成功とする。COMPATIBLEは矛盾がないだけで情報不足が残る状態、INCOMPARABLEは比較前提不足であり、いずれも成功扱いしない。曖昧なEntity/時間/Scopeを既定値で埋めて同値化してはならない。

### 20.3 Canonicalization
正規化は決定的かつバージョン付きとする。Fingerprintはcanonicalization_versionと正規化構造から生成する。Version変更時は旧Fingerprint/評価履歴を保持する。モデル文章の揺れのみを状態差分としてはならない。

## 21. Event Correction and Revision

訂正は既存Eventの変更ではなく新Eventとし、AMEND、SUPERSEDE、RETRACT、CLARIFYを区別する。訂正Eventはsupersedes_event_refまたはclarifies_event_refs、revision_group_ref、revision_sequenceを持つ。Revision Group内の順序はGroup内だけで有効であり、別Group間の全順序を意味しない。複数訂正が同一Eventを参照する場合、順序・権限・根拠から優先関係を確定できなければ分岐を保持しQuestionを生成する。最終受信時刻だけで勝者を決めない。旧Evidenceは削除せず、訂正種別に従いValidity/Interpretation/Claim/Dependencyを再評価する。CLARIFYは旧Claimを自動無効化しない。

## 22. Deterministic Focus Arbitration

FocusはScopeごとに選択し、異なるScopeのInterestを直接比較しない。Scope Policyはprimary_cardinality(ONE/MULTIPLE)、allowed_roles、hierarchy_policy、explicit_instruction_policy、tie_policy、hysteresis_policy_version、focus_budgetを定義できる。優先順位は、(1)適用Scopeの明示指示、(2)適格性、(3)現Focusの有効性/明示維持、(4)未解決QuestionとImpactの緊急性、(5)同一Scope内Interest、(6)影響範囲/未解決度/Recency/再発性、(7)ヒステリシス、(8)Policy指定Tie Break。競合する明示指示や解消不能な同順位はMULTI/DEFERとし、内部ID順だけで勝者を決めない。遷移にはpolicy_version、decision_reason_codes、considered_candidate_refs、selected_target_refs、deferred_candidate_refsを記録する。

## 23. Temporal Boundary Policy

時間区間は原則半開区間[valid_from, valid_until)とし、隣接区間の境界一致は重複としない。瞬間的事象はpoint eventとして区別する。UNKNOWNは不明値であり無限境界ではない。OPEN_BOUNDARY、UNKNOWN、point eventを区別し、時刻精度と元タイムゾーンを保持する。時間重複を比較できない精度差・TZ不明等はINCOMPARABLEとし、時間矛盾を確定しない。event_timeとobserved_atを混同せず、受信順を発生順へ読み替えない。

## 24. Source Lineage Graph Integrity

Source Lineageは有向グラフで、循環を通常の出所関係として許可しない。循環更新は拒否または隔離し、既存有効グラフを保持する。UNKNOWN_ORIGINは親未特定であり、独立とも同一起源とも推定しない。複数起源のEvidenceは必要に応じComponent単位に分解し、source/evidence refs、shared_origin_refs、independence_statusを記録する。起源が特定できない成分はUNKNOWN/PARTIALLY_DEPENDENTのまま保持する。

## 25. Partial Evidence Independence

一つのEvidenceに複数の主張・データ要素がある場合、Evidence Componentへ分割可能とする。各Componentはsource range、claim target、origin refs、independence status、component fingerprintを持つ。同一Claimの同一側面を支持するINDEPENDENT Componentのみ独立裏付けに数える。DEPENDENT/UNKNOWNは加算しない。PARTIALLY_DEPENDENTは共有成分を除外し、独立と立証できた成分のみ別Componentとして数える。同一Componentが複数経路で到達しても一度だけ集計する。分割不能な複合Evidenceは独立数を水増ししない。

## 26. Recognition Interoperability

Producer Descriptorはproducer_id/version、semantic_contract_version、canonicalization_version、supported_semantic_types、modalities、temporal_precision、uncertainty_dimensions、confidence_calibration_ref、language capabilities、deterministic_modeを含む。CoreはProducer固有ラベルをCore Semantic Contractへ明示的にmappingする。mapping不能な候補はUNMAPPED/拡張候補として保持し、暗黙に破棄・確定しない。異なるProducerのconfidenceは共通尺度・校正情報がない限り比較不能とする。Version変更は新Proposal/履歴として扱い、旧Commitを上書きしない。

## 27. Context / Cluster Lifecycle

Cluster lifecycleとFocus roleは独立状態である。Focus解除だけを理由にClusterをDORMANT/ARCHIVEDへ遷移させない。ARCHIVEDは保持Policy、参照中Dependency、未解決Question、外部参照、明示Archive条件を検証して決める。Merge/Splitは新Lineage Eventとし、旧IDと帰属履歴を保持する。分割帰属が未確定なら候補またはUNRESOLVEDを保持する。

## 28. Multi-Focus Resource Policy

MULTI FocusはScope Policyのfocus_budget、minimum_service_level、defer_policy、reactivation_policyに従う。Budget超過時は対象を削除せずSECONDARY/BACKGROUND/DEFERREDとして保持する。評価滞留時間、最終評価時刻、未処理Impactを監視し、永久飢餓を避ける公平性補正を行う。公平性補正は明示Focus指示や安全なCommit条件を上書きしない。再活性化は新Evidence、Dependency Impact、明示指示、再発等の根拠を記録し、タイマー満了だけで意味的有効性を復元しない。

## 29. Conformance and Acceptance

Schema適合だけではCore適合としない。意味Fingerprint、Cross-record Validator、Lineage DAG、Lifecycle、Transaction検証を併用する。v0.5追加テスト:
31 表現だけが異なる同一ClaimのFingerprint一致
32 未解決Entityを含むClaimの同値性保留
33 接する半開区間の非重複
34 精度差のある時間区間の比較保留
35 AMEND/SUPERSEDE/RETRACT/CLARIFYの区別
36 Revision Group内の競合訂正分岐
37 明示Focus指示の競合
38 同順位FocusのMULTI/DEFER
39 Source Lineage循環の拒否/隔離
40 複数起源EvidenceのComponent分解
41 部分独立Componentの二重計上防止
42 未校正Producer Confidenceの比較禁止
43 未対応Semantic TypeのUNMAPPED保持
44 Focus Budget超過時の非破壊Defer
45 Focus解除とCluster lifecycleの独立性
46 未解決Dependencyを持つCluster Archiveの保留
47 Fingerprint Version変更時の旧履歴保持
48 同一Componentの複数経路到達時の一度だけの集計
49 意味状態反復によるReconciliation停止
50 COMPATIBLE/INCOMPARABLEを収束成功と誤判定しない

v0.5は基本認知モデル、責務分離、Event/Evidence/Interpretation/Stateの非破壊構造を変更せず、判定境界を明示する。重み・閾値・DB・LLMは引き続き実装評価に委ねる。


# 30. Core認知モデルの形式化

## 30.1 Context State
Context Stateを型付き有向多重グラフ G=(V,E,A,T,P) と定義する。VはEvidence、Interpretation、Semantic Unit、Entity、Cluster、Question Aspect、Observation Frame等のノード集合、EはRelation/Dependency/Lineage、Aは型付き属性、Tは有効時間・観測時間、PはProvenanceである。各CommitはState Versionを持つ。Event Logが正本であり、StateはEvent/CommitをPolicy Versionに従って射影したビューである。

## 30.2 非混同の原則
Evidence validity、Interpretation status、Claim status、Question Aspect status、Cluster lifecycle、Focus roleは独立状態軸である。一軸の変更から他軸を同値へ自動変更しない。Evidence無効化はEvidence削除ではなく、依存Interpretation/Claimの再評価を起動する。

## 30.3 Coreの二段階
認識段階ではProducerが候補をProposalとして不変保存する。整合段階ではCoreが参照閉包、型、Lifecycle、Aspect集約、Dependency、時間/同期、Provenance、Versionを検証しDeltaをCommitする。ProducerはCommitted Stateを直接作れない。

# 31. Question Aspect実装契約

## 31.1 レコードと状態遷移
Aspectはaspect_id、question_ref、aspect_key、description、target_refs、trigger_refs、evidence_refs、interpretation_refs、dependency_refs、status、resolution、lineageを持つ。遷移はOPEN->INVESTIGATING/DEFERRED/RESOLVED/INVALIDATED、INVESTIGATING->OPEN/DEFERRED/RESOLVED/INVALIDATED、DEFERRED->OPEN/INVESTIGATING/RESOLVED/INVALIDATEDとする。RESOLVED/INVALIDATEDからOPENへの遷移は根拠失効または新Evidenceによる未解決条件再成立時のみ許可する。

## 31.2 Question集約関数
Coreはaspect_aggregation_versionを指定し、Question.statusを導出する。優先順位: (1)置換されたQuestionはSUPERSEDED、(2)全required Aspect RESOLVEDならRESOLVED、(3)全required Aspect INVALIDATEDならINVALIDATED、(4)RESOLVED/INVALIDATEDと未解決Aspect混在ならPARTIALLY_RESOLVED、(5)未解決AspectにINVESTIGATINGがあればINVESTIGATING、(6)全未解決Aspect DEFERREDならDEFERRED、(7)その他OPEN。required Aspectが0件になるDeltaは禁止。ProducerのQuestion.status指定は禁止。

## 31.3 Resolution validator
RESOLVEDにはresolution_kind、reason、resolved_atを要求する。ANSWERED/DISAMBIGUATED/ATTRIBUTEDはEvidenceまたは検証済Interpretationを要求する。PARTITIONED/SCOPEDは分割先・Scope・Lineageを要求する。NO_LONGER_APPLICABLEは前提不成立根拠を要求する。OTHERは理由とProvenanceを要求する。自由文だけで解消判定しない。

## 31.4 Split/Merge/Dependency
Splitは親Aspectと複数子Aspectを一つのAtomic Deltaで登録する。Mergeは複数親から新Aspectを作る。旧IDを再利用しない。Aspect DependencyはImpact Setの最小単位であり、同一generation/effective_input_fingerprintで一度だけ評価する。Question集約はAspect更新後に実行する。

# 32. Observation Frame詳細設計

## 32.1 Clock Mapping
Clock Mappingはsource_clock_domain、target_clock_domain、transform_version、mapping_function_ref、calibration_evidence_refs、validity_interval、max_error_msを保持する。変換誤差は保守的に伝播し、校正期間外のMappingは適用しない。

## 32.2 同期アルゴリズム
1. ObservationごとのClock Domainと時間精度を取得。
2. 同一Domainなら共通時計として比較し、異なる場合は有効Clock Mappingを検証。
3. 時間区間を共通時計へ変換し誤差上限を伝播。
4. Policy toleranceと比較しALIGNED/PARTIALLY_ALIGNED/UNALIGNED/INCOMPARABLEを決定。
5. 空間参照系は別途変換・比較。
6. Mapping Versionとalignment provenanceを保存。
7. Frame依存Interpretation/ClaimをImpact Setへ登録。

## 32.3 Entity同一性とRevision
Frame内entity_hypothesis_refsはIdentity Proposalへの参照に限る。ALIGNEDでもEntityがUNRESOLVEDなら維持する。Identity確定には独立EvidenceとIdentity Validatorを要求する。FrameはAppend-onlyで、訂正はrevision_of_refを持つ新Frameを作る。一Observationは複数Frameから参照可能。

# 33. Dependency/Transaction接続
AspectまたはObservation Frameの変更はRoot Changeとなる。Dependency Plannerは明示Edgeとtrigger_conditionからImpact Setを算出する。Impact Set外のノードを便宜的に再評価しない。複数Root Changeが同一ノードへ到達した場合、同一generation/fingerprintで一度だけ処理する。Commit時はAspect群、Question集約、Frame Revision、依存Deltaを原子的に検証する。

# 34. Migration
v0.5レコードを原文保存しDigestを付与。Migration Eventにsource/target schema、policy version、source refを記録。各Questionのunresolved_aspectsをAspect候補へ変換し、重複文字列を同一性確認なしに統合しない。旧RESOLVEDでresolution_basis欠落時はReview Aspectを生成し、解消を捏造しない。Observation Frameを遡及生成しない。Migration後に参照閉包・Invariant・Semantic Projectionを検証し、失敗時は旧Stateを公開し続ける。

# 35. v0.6 100-case Conformance Catalogue

本カタログは仕様レベルのモデルベース適合テストである。PASSは設計上の期待結果が一意であり、安全な拒否/保留動作が定義済みであることを意味し、実装実行結果を意味しない。

| ID | ケース | 期待結果 / 必須条件 |
|---|---|---|
| T-001 | 同一Event再配送 | 重複Evidence/Commitを作らず同一論理結果を返す。 |
| T-002 | Evidence上書き拒否 | 不変性違反として拒否し既存Digestを保持する。 |
| T-003 | Event撤回 | 新Eventとして記録し旧Evidenceを残す。 |
| T-004 | Event AMEND訂正 | 訂正種別とLineageを保持し依存先のみ再評価する。 |
| T-005 | Event SUPERSEDE置換 | 受信時刻だけで勝者を決めず分岐を保持する。 |
| T-006 | Event CLARIFY補足 | 補足として追加し旧Claimを自動無効化しない。 |
| T-007 | Source内順序 | 同一Sourceのsequence順だけを適用する。 |
| T-008 | 異Source順序不明 | 全順序を捏造せず部分順序/UNKNOWNを保持する。 |
| T-009 | 遅着Event | 観測時刻と受信時刻を分離して限定再評価する。 |
| T-010 | 部分入力の確定 | 暫定状態を保持しFINAL Event後に再評価する。 |
| T-011 | 同一Claim支持 | 同一Claimへの独立根拠を追加し履歴を残す。 |
| T-012 | 同一Claim反証 | 競合EvidenceとInterpretationを保持しQuestionを生成する。 |
| T-013 | 複数Interpretation候補 | 候補を併存させ、Core検証前に単一化しない。 |
| T-014 | 未校正Confidence比較 | 共通Calibrationなしの数値比較を拒否する。 |
| T-015 | 時間Scope差 | 時間範囲が異なるClaimを即時矛盾としない。 |
| T-016 | 様相差 | FACT/PLAN等の様相差を比較Projectionに保持する。 |
| T-017 | 文言揺れ | 意味Projectionが同じなら表現差を状態差分にしない。 |
| T-018 | 未確定Entity Claim | Entity未確定を伝播し同値確定を保留する。 |
| T-019 | Interpretation再評価 | 旧Interpretationを残し新Version候補を登録する。 |
| T-020 | Claim無効化履歴 | Claimを削除せずInvalidation Eventと履歴を残す。 |
| T-021 | 単一Aspect生成 | 固有IDと親Question参照を持つAspectを作る。 |
| T-022 | 複数Aspect生成 | 各Aspectに独立の対象・根拠・状態を割り当てる。 |
| T-023 | Aspect重複候補 | 根本原因が確定しなければ統合せず関連付ける。 |
| T-024 | Aspect部分重複 | 部分重複はAspect分割/関連付けで表し履歴を残す。 |
| T-025 | 親子Question | 親子QuestionをLineageで結び旧IDを保持する。 |
| T-026 | Aspect target参照欠落 | 存在しない参照を含むDeltaを拒否する。 |
| T-027 | Evidence参照欠落 | 根拠参照欠落をCommit前に拒否する。 |
| T-028 | Aspect provenance欠落 | Provenance欠落をCommit前に拒否する。 |
| T-029 | Aspect ID再利用拒否 | ID再利用を拒否し旧参照の一意性を保つ。 |
| T-030 | Question空Aspect拒否 | 一つ以上のAspectを要求し不正Questionを拒否する。 |
| T-031 | OPENからINVESTIGATING | 開始根拠とJob参照を記録して遷移する。 |
| T-032 | INVESTIGATINGからOPEN | 未解決状態を維持し理由を記録する。 |
| T-033 | OPENからDEFERRED | 停止理由と再開条件を伴うDEFERREDを記録する。 |
| T-034 | DEFERRED再開 | 再開条件成立時だけOPEN/INVESTIGATINGへ遷移する。 |
| T-035 | OPENからRESOLVED | Resolutionと根拠を同一Deltaで検証して解消する。 |
| T-036 | OPENからINVALIDATED | 前提不成立の根拠を検証してINVALIDATEDにする。 |
| T-037 | Resolution根拠なし | Resolution basis欠落を拒否する。 |
| T-038 | RESOLVED再オープン新Evidence | 新Evidenceにより該当Aspectのみ再オープンする。 |
| T-039 | 根拠失効再オープン | 失効したResolutionに依存するAspectだけ再評価する。 |
| T-040 | SUPERSEDED終端 | SUPERSEDEDからの直接再利用を拒否する。 |
| T-041 | 全Aspect RESOLVED | QuestionをRESOLVEDと導出する。 |
| T-042 | 全Aspect INVALIDATED | QuestionをINVALIDATEDと導出する。 |
| T-043 | RESOLVEDとOPEN混在 | PARTIALLY_RESOLVEDと導出し未解決Aspectを保持する。 |
| T-044 | RESOLVEDとDEFERRED混在 | PARTIALLY_RESOLVEDと導出しDeferred Aspectを保持する。 |
| T-045 | INVESTIGATINGを含む集約 | 部分解消条件に従い集約しInvestigatingを反映する。 |
| T-046 | 全未解決Aspect DEFERRED | QuestionをDEFERREDと導出する。 |
| T-047 | optional Aspect未解消 | optional Aspectは履歴に残しrequired判定から除外する。 |
| T-048 | required Aspectなし拒否 | required Aspectが空になるDeltaを拒否する。 |
| T-049 | Question状態上書き拒否 | Producer指定statusを無視/拒否しCoreで再計算する。 |
| T-050 | 集約Version変更 | 集約規則Versionを記録し旧履歴を維持する。 |
| T-051 | Aspect 1対2分割 | 子Aspect作成と親LineageをAtomicに記録する。 |
| T-052 | Aspect 2対1統合 | 新Aspectを作り全親Lineageを保持する。 |
| T-053 | 分割後旧ID参照 | 旧Aspect IDの参照解決を維持する。 |
| T-054 | 統合後旧ID参照 | 旧Aspect IDを消去せずResolverで追跡する。 |
| T-055 | Aspect間Dependency | 明示Edgeとtrigger条件に従いAspect単位で伝播する。 |
| T-056 | 必須Dependency未解決 | 前提Aspect未解決の解消Deltaを拒否する。 |
| T-057 | Dependency循環 | SCC固定点評価またはSTALLEDで停止する。 |
| T-058 | 複数経路Impact重複 | 同一generation/fingerprintの評価を一度に限定する。 |
| T-059 | 独立Aspect同時更新 | write-set競合を検知し再検証する。 |
| T-060 | Aspect Lineage欠落 | 旧AspectとDependencyのLineageを保持する。 |
| T-061 | 同一Clock Domain | 共通Clock Domainで誤差付き比較を行う。 |
| T-062 | 異Clock Domain校正済み | Mappingと校正根拠を検証して整列する。 |
| T-063 | 異Clock Domain校正なし | INCOMPARABLEとしALIGNEDを返さない。 |
| T-064 | Clock Mapping期限切れ | 期限切れMappingを適用せず拒否/保留する。 |
| T-065 | Clock Mapping誤差超過 | 許容差超過としてUNALIGNEDを返す。 |
| T-066 | 時計ドリフト | Mapping Version/誤差範囲を再評価する。 |
| T-067 | 時間精度不一致 | 精度差を保持し比較不能ならINCOMPARABLEにする。 |
| T-068 | 未知時刻 | 時刻を捏造せずUNKNOWNを保持する。 |
| T-069 | observed_atとevent_time相違 | event_timeとobserved_atを別属性で保持する。 |
| T-070 | 変換誤差伝播 | 保守的な誤差伝播とProvenanceを記録する。 |
| T-071 | 全Observation整列 | 全必須Observationが許容差内ならALIGNED。 |
| T-072 | 一部Observation整列 | 整列可能な部分だけPARTIALLY_ALIGNEDにする。 |
| T-073 | 時間許容差超過 | UNALIGNEDを返し同時刻へ丸めない。 |
| T-074 | 時間区間重複なし | 比較可能な時間範囲で非重複を保持する。 |
| T-075 | 時間境界接触 | 半開区間の接触を重複と判定しない。 |
| T-076 | 空間参照系一致 | 同一空間参照系でのみ位置比較を行う。 |
| T-077 | 空間参照系変換なし | 変換根拠不足として空間比較を保留する。 |
| T-078 | Observation 1件Frame拒否 | Observation最小2件制約でSchema拒否する。 |
| T-079 | 一Observation複数Frame | 複数Frame参照を許可し元Observationを保持する。 |
| T-080 | Frameから同一性推論拒否 | 同期とIdentityを別軸としてIdentityを確定しない。 |
| T-081 | Entity仮説のみ | 候補として保持しCONFIRMEDにしない。 |
| T-082 | 独立Identity Evidence | 独立Identity EvidenceをValidatorで審査する。 |
| T-083 | 同時刻別Entity | 時刻近接だけで別Entityを統合しない。 |
| T-084 | 同一Entity複数Frame | Frame間参照は許可しIdentityは別途判定する。 |
| T-085 | Frame Revision | 旧Frameを保持し新Revisionを作成する。 |
| T-086 | Clock Mapping訂正 | 旧Frameを変更せず依存先を再評価する。 |
| T-087 | Frame統合Lineage | 元Frameと統合FrameのLineageを保持する。 |
| T-088 | 同期状態悪化 | 旧Alignmentを維持せず新状態で依存先を再評価する。 |
| T-089 | Frame依存Interpretation再評価 | Frame依存InterpretationのみImpact Setに含める。 |
| T-090 | Entity ID未確定保持 | EntityをUNRESOLVEDのまま保持する。 |
| T-091 | 同時Delta競合 | Version/read-set競合を検知し古いDeltaを拒否する。 |
| T-092 | 非競合Delta再検証 | 意味検証後に限定的なrebaseを許可する。 |
| T-093 | AspectとQuestion集約Atomic Commit | Aspect更新と集約結果を単一Commitで公開する。 |
| T-094 | Frameと依存Delta Atomic Commit | Frame Revisionと依存DeltaをAtomicに公開する。 |
| T-095 | 重複Commit Idempotency | 同じIdempotency Keyの再送は同一結果を返す。 |
| T-096 | Commit途中障害 | 部分Commitを公開せずRecoveryで整合性を検証する。 |
| T-097 | Snapshot Recovery | Event Logから検証可能なStateを再構成する。 |
| T-098 | v0.5 Question Migration | 各旧Aspectを変換し未解決Mappingを報告する。 |
| T-099 | 旧RESOLVED根拠欠落 | Resolutionを捏造せずReview Aspectを生成する。 |
| T-100 | Migration失敗時旧State保持 | 新Generationを公開せず旧Stateを保持する。 |
| T-101 | RESOLVEDとINVALIDATEDの終端混在 | 全required Aspectが終端し少なくとも一つRESOLVEDならQuestionはRESOLVED、Aspect個別の結果は保持する。 |

## 35.1 Exit Criteria
100ケース全てについて期待結果、適用Policy、Validator責務が一意であること。反例が見つかった場合は規則を修正し影響ケースを再検証する。実装適合を宣言するには同Catalogueを自動テスト化し、Fixture、実行ログ、Core Versionを保存する。

## 36. Completion Gate
Question状態の正本はAspectレコード、Question.statusは派生値。部分解消・再オープン・分割・統合・Dependency伝播を規定。Observation FrameのClock Domain、Mapping、誤差伝播、Alignment、Identity分離、Revisionを規定。Schema/API/Validator/Transactionの責務境界とMigrationを規定。


## 37. Detailed Design Invariants D-21 through D-34

| ID | 不変条件 | Core Validator |
|---|---|---|
| D-21 | Questionは一つ以上のrequired Aspectを持つ | Aspect cardinality |
| D-22 | RESOLVED Aspectはresolution_kind/reason/resolved_atと適切な根拠を持つ | Resolution validation |
| D-23 | Question.statusはAspectから決定的に導出される | Aggregation function/version |
| D-24 | 必須Dependencyが未解決なら依存AspectをRESOLVEDにできない | Aspect dependency validation |
| D-25 | Aspect split/mergeは旧IDとLineageを保持する | Lineage closure |
| D-26 | RESOLVED/INVALIDATEDからの再オープンは再成立根拠を要する | Lifecycle transition |
| D-27 | Dependency ImpactはAspect単位でtrigger条件に従い伝播する | Impact planner |
| D-28 | 異Clock DomainのALIGNED判定は有効Clock Mappingを要する | Clock mapping validation |
| D-29 | 同期誤差は保守的に伝播し許容差超過を隠さない | Uncertainty propagation |
| D-30 | ALIGNED/PARTIALLY_ALIGNED等は時間精度とPolicyから導出する | Alignment validator |
| D-31 | Observation Frame membershipはEntity同一性を確定しない | Identity separation |
| D-32 | Observationと既存FrameはAppend-onlyである | Immutability validator |
| D-33 | Frame Revisionは依存先を再評価し旧Frameを保持する | Revision/Impact validator |
| D-34 | MigrationはProvenance付きAtomic Generationとして公開する | Migration validator |
