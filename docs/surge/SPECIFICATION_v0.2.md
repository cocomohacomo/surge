# SURGE 基礎設計 v0.2
## Universal Context Cognition Framework
### 正式基礎仕様書

**Version:** 0.2.0  
**Status:** Architecture Specification  
**Design Priority:** Streaming-Native / Model-Independent / Japanese-First  
**Scope:** 汎用文脈認識・統合・進化フレームワーク

---

# 1. 概要

## 1.1 定義

SURGEは、異なる形式・粒度・時間軸で逐次入力される情報を共通のイベント形式に正規化し、意味認識、文脈統合、フォーカス管理、状態更新、履歴管理を継続的に実行する、モデル非依存の汎用文脈認識フレームワークである。

SURGEは、単純な要約、分類、情報抽出、検索を目的とするものではない。

入力された情報を既存の文脈と照合し、情報間の関係、依存、時間的変化、矛盾、不確実性を管理しながら、文脈全体を継続的に構築・維持・再評価することを目的とする。

## 1.2 基本目的

SURGEは次の能力を提供する。

1. 入力粒度に依存しない意味認識
2. 異なる入力形式の共通処理
3. 文脈の逐次構築と更新
4. 話題と意味構造の動的管理
5. 複数・階層型フォーカスの管理
6. 情報間の関係および依存関係の管理
7. 時間経過に伴う状態変化の管理
8. 曖昧性・矛盾・不確実性の保持
9. 過去の解釈と現在の状態の非破壊的な更新
10. 文脈の再評価と履歴からの再構成

## 1.3 設計思想

SURGEの基本思想を以下のように定義する。

**Context is continuously constructed, maintained, and reevaluated.**

文脈は一度の解析で完成するものではない。

新しい情報の到着、時間の経過、情報の訂正、依存関係の変化、過去の解釈の再評価によって、継続的に進化する。

そのため、SURGEはバッチ処理を基本とするのではなく、ストリーミング処理を基本設計とする。

---

# 2. 基本設計原則

## 2.1 Streaming-Native

全ての入力を逐次処理可能な構造として扱う。

入力は完成した文書や会話である必要はない。未完成の単語、文、発言、段落であっても、イベントとして受け付ける。

入力が到着するたびに、既存の文脈との関係を評価し、必要な状態更新を行う。

## 2.2 Granularity Invariance

入力の分割単位と意味認識単位を独立させる。

以下の入力単位を同一の処理体系で扱う。

- 文字
- トークン
- 単語
- 句
- 文
- 発言
- 段落
- 文書
- 会話
- その他の構造化入力

同一の意味内容と同等の情報順序を持つ入力は、分割方法が異なっても、最終的に同等の文脈状態へ収束することを目標とする。

ただし、入力順序が意味に影響する場合、順序不変性は要求しない。

## 2.3 Model Independence

SURGE Coreは特定のLLM、言語モデル、認識モデルに依存してはならない。

モデルは意味認識や解釈の候補を生成する外部エンジンとして利用する。

モデルの交換、追加、削除がSURGE Coreの基本構造に影響しないことを原則とする。

## 2.4 Non-destructive Context Evolution

証拠、解釈、現在状態、履歴を分離する。

過去の証拠を削除せず、解釈や状態の変更を履歴として保持する。

過去の解釈が変更された場合も、当時の解釈と変更後の解釈を追跡可能にする。

## 2.5 Evidence-Based State Management

状態変更には根拠が必要である。

全ての確定状態は、根拠となるEvidence、適用されたInterpretation、関連するRelationおよび変更履歴を追跡できなければならない。

## 2.6 Uncertainty Preservation

意味、参照先、主体、対象、時間、依存関係などが確定していない場合、無理に確定してはならない。

不確実性を明示的に保持し、後続情報による解決を許容する。

## 2.7 Separation of Structure, Focus, and Evolution

以下の3つを独立して管理する。

- Context Structure：情報の意味的なまとまりと関係
- Context Focus：現在注目している情報とその階層
- Context Evolution：情報の追加、変更、再評価、履歴

フォーカスの変更は、必ずしもクラスターの変更を意味しない。

クラスターの変更も、必ずしもフォーカスの変更を意味しない。

---

# 3. 全体アーキテクチャ

SURGEは6つの論理レイヤーと4つの横断的なCore制御機構から構成する。

## 3.1 6つの論理レイヤー

### Layer 1：Input Normalization

入力形式の正規化を担当する。

責務：

- 入力アダプターからのイベント受信
- 入力元情報の管理
- 時刻・位置情報の管理
- 入力順序の管理
- 共通Input Eventへの変換
- 原文・原データの保持
- 入力の重複検知に必要な識別情報の付与

入力元の例：

- 会話
- チャット
- 文書
- 音声認識結果
- 動画
- センサーデータ
- 外部システム

入力アダプターは形式ごとの処理を担当するが、文脈認識の独自実装を持ってはならない。

全ての入力は共通のSURGE Coreへ渡す。

### Layer 2：Semantic Recognition

入力イベントから意味的な情報を抽出する。

責務：

- 意味単位の抽出
- 意味仮説の生成
- 主体・対象・動作の認識
- 発言の談話上の役割の認識
- 参照表現の解決候補生成
- Entity候補の抽出
- 時間表現の認識
- 矛盾候補の検出
- 確信度および不確実性の提示

認識結果は確定情報ではなく、Context Integrationへ渡す候補とする。

### Layer 3：Context Integration

新しい意味情報と既存文脈の統合を担当する。

責務：

- Semantic Unitの統合
- Context Clusterの生成・更新
- Entity Resolution結果の反映
- Relationの生成・更新
- Dependencyの生成・更新
- 参照関係の統合
- 時間的関係の統合
- 矛盾の管理
- 既存文脈との整合性評価

情報の類似性だけでなく、意味的関係、主体、対象、時間、出典を考慮する。

### Layer 4：Dynamic Focus Management

文脈内で現在注目すべき情報を管理する。

責務：

- Global Focusの管理
- Local Focusの管理
- 階層型Focusの管理
- 複数Focusの管理
- Focus遷移の判定
- 過去Focusの履歴管理
- Dormant Contextの再活性化

Focusはクラスターの属性ではなく、独立した実行時状態とする。

### Layer 5：State Evolution

文脈状態の生成・更新・再評価を統括する。

責務：

- INIT
- UPDATE
- RECONCILE
- State Deltaの統合
- 状態変更の検証
- 変更影響範囲の評価
- 競合検知
- Commit制御
- 状態バージョン管理
- 履歴との整合性確認

全ての確定状態変更はState Evolutionを経由する。

### Layer 6：Context Repository

文脈情報の保存・管理を担当する。

責務：

- Evidenceの保存
- Interpretationの保存
- Semantic Unitの保存
- Entityの保存
- Clusterの保存
- Relationの保存
- Dependencyの保存
- Focus Stateの保存
- Context Stateの保存
- Event Historyの保存
- State Historyの保存
- Snapshotの管理
- 状態復元

保存データは履歴から再構成可能な構造とする。

---

# 4. 横断的Core制御機構

横断的制御機構は独立した認識レイヤーではなく、6つのレイヤーが共通利用する制御機能である。

## 4.1 Uncertainty Management

不確実性の保持と伝播を管理する。

対象：

- 意味解釈
- 参照先
- Entity同一性
- 主体・対象
- 時間
- Relation
- Dependency
- 状態変更の妥当性

不確実性は単一の確信度だけで表現してはならない。

少なくとも以下を区別する。

| 状態 | 意味 |
|---|---|
| CONFIRMED | 根拠に基づいて確定 |
| PROVISIONAL | 暫定的に採用 |
| CANDIDATE | 複数の候補が存在 |
| UNRESOLVED | 解決されていない |
| CONFLICTED | 矛盾が存在 |
| INVALIDATED | 無効化された |
| REEVALUATION_REQUIRED | 再評価が必要 |

不確実な情報に依存する情報は、その不確実性の影響を評価しなければならない。

ただし、不確実性を無条件に全ての後続情報へ伝播させてはならない。

依存関係と影響規則に基づいて伝播範囲を決定する。

## 4.2 Dependency Management

情報間の依存関係と変更の波及範囲を管理する。

Dependencyは単なる関連リンクではなく、変更による影響を評価するための構造とする。

依存関係には以下を持たせる。

- Dependency Type
- Source Unit
- Target Unit
- Trigger Condition
- Propagation Rule
- Evaluation Method
- Validity
- Provenance

変更が発生した場合、依存関係を辿って影響対象を特定し、必要な範囲だけを再評価する。

## 4.3 Transaction Management

状態変更の整合性を保証する。

責務：

- State Version管理
- Transaction管理
- Atomic Commit
- Optimistic Concurrency Control
- Conflict Detection
- Retry / Re-evaluation
- Idempotency
- Rollback
- Recovery

認識処理中の新規入力を妨げず、古い状態に基づく処理結果が新しい状態を上書きすることを防止する。

## 4.4 Provenance Management

全ての情報と状態変更の根拠を追跡する。

管理対象：

- 入力元
- 原文
- 発言者
- 観測時刻
- イベント時刻
- 文書位置
- モデル・処理器情報
- 解釈根拠
- 状態変更根拠
- 変更履歴

ProvenanceはEvidenceから現在状態までの追跡可能性を保証するための共通機構とする。

---

# 5. 共通データモデル

SURGEは以下の主要データ構造を採用する。

## 5.1 Input Event

入力を表す最小の管理単位。

主な属性：

- event_id
- source_id
- source_sequence
- event_time
- observation_time
- input_position
- content
- content_type
- speaker_id
- document_id
- metadata
- causal_reference

Eventは原則として不変とする。

## 5.2 Evidence

入力から得られた根拠情報。

主な属性：

- evidence_id
- event_id
- evidence_type
- content
- provenance
- observed_at
- validity
- integrity_hash

Evidenceは原入力との追跡可能性を持つ。

## 5.3 Interpretation

Evidenceに対する意味解釈。

主な属性：

- interpretation_id
- evidence_refs
- hypothesis_type
- semantic_content
- confidence
- uncertainty_status
- alternatives
- model_origin
- created_at
- supersedes

Interpretationは複数存在できる。

後から解釈が変わっても、旧Interpretationを削除しない。

## 5.4 Semantic Unit

文脈を構成する意味単位。

主な属性：

- unit_id
- semantic_type
- subject
- predicate
- object
- attributes
- temporal_scope
- evidence_refs
- interpretation_refs
- status

入力の分割単位とは独立して存在する。

一つの入力イベントから複数のSemantic Unitが生成されてもよい。

複数イベントから一つのSemantic Unitが形成されてもよい。

## 5.5 Entity

実体・主体・対象を管理する。

主な属性：

- entity_id
- entity_type
- canonical_reference
- aliases
- attributes
- identity_status
- evidence_refs
- temporal_scope
- entity_relations

Entity Resolutionは、候補間の同一性を評価する。

判定結果は以下を許容する。

- SAME_ENTITY
- DISTINCT_ENTITY
- RELATED_ENTITY
- UNRESOLVED_ENTITY

未確定のEntity候補を無理に統合してはならない。

## 5.6 Context Cluster

意味的に関連する情報の集合。

主な属性：

- cluster_id
- cluster_type
- parent_cluster_id
- member_units
- child_clusters
- relations
- cluster_status
- created_at
- updated_at
- lineage

Clusterは階層構造を持つことができる。

Clusterの生成・統合・分割は履歴を保持する。

## 5.7 Relation

Semantic Unit、Entity、Cluster間の関係。

基本Relation Type：

- RELATED
- DEPENDS_ON
- DERIVED_FROM
- REFERS_TO
- CONTRADICTS
- SUPERSEDES
- CONTAINS
- PART_OF
- SAME_AS
- CAUSED_BY
- SUPPORTS
- EXCLUDES

Relationは方向性と根拠を保持する。

## 5.8 Dependency

変更の影響を管理する関係。

主な属性：

- dependency_id
- source_ref
- target_ref
- dependency_type
- trigger_condition
- propagation_rule
- evaluation_method
- uncertainty_policy
- validity
- provenance

Dependencyと一般的なRelationは区別する。

全てのRelationがDependencyになるわけではない。

## 5.9 Focus State

現在の注目状態を管理する。

主な属性：

- focus_id
- scope_id
- focus_level
- target_ref
- focus_role
- parent_focus_id
- focus_status
- activation_reason
- activated_at
- previous_focus_ref
- transition_history

Focus ScopeはGlobal、Speaker、Conversation、Taskなどを許容する。

## 5.10 Context State

現在の文脈状態を管理する。

主な属性：

- state_id
- state_version
- active_entities
- active_clusters
- semantic_units
- relations
- dependencies
- focus_states
- unresolved_items
- conflicts
- temporal_context
- provenance_refs
- updated_at

Context Stateは現在有効な状態を表す。

過去の状態はState Historyに保持する。

## 5.11 State Delta

状態変更の提案を表す。

主な属性：

- delta_id
- base_state_version
- source_event_refs
- proposed_operations
- affected_refs
- confidence
- uncertainty_status
- validation_results
- transaction_id

State Deltaは提案であり、Commit前に確定状態として扱ってはならない。

---

# 6. 時間管理

SURGEでは時間を単一のタイムスタンプとして扱わない。

以下を区別する。

| 時間属性 | 定義 |
|---|---|
| Event Time | 情報が発生した時刻 |
| Observation Time | SURGEが情報を受け取った時刻 |
| Valid From | 情報が有効になる時刻 |
| Valid Until | 情報が無効になる時刻 |
| Revision Time | 情報が訂正された時刻 |

時間情報が不明な場合はUNKNOWNとして保持する。

不明と未設定を同一視しない。

相対時間は基準日時、タイムゾーン、対象となる時間表現が確定した場合に解決する。

時間経過だけで情報が自動的に事実でなくなるとは限らない。

必要に応じて有効性の再評価を行う。

---

# 7. フォーカス管理

## 7.1 基本構造

Focusは以下の階層で管理する。

- Global Focus
- Local Focus
- Hierarchical Focus
- Background Context

複数のLocal Focusを同時に保持できる。

FocusはClusterと独立して管理する。

## 7.2 Focus Role

- PRIMARY
- SECONDARY
- DORMANT
- BACKGROUND

これらはFocus Scope内の役割であり、Clusterの重要度を示すものではない。

## 7.3 Focus Transition

基本操作：

- KEEP
- DESCEND
- ASCEND
- SWITCH
- REACTIVATE
- MULTI
- DEFER

遷移の判断材料：

- 明示的な話題転換表現
- 意味的な話題変化
- 参照表現
- 質問と回答の関係
- 話者
- 時間的近接性
- 文書構造
- 既存Clusterとの関係
- 過去のFocus履歴

単一のスコアだけで遷移を決定してはならない。

明示的な遷移指示は強い根拠として扱い、一般的なヒステリシスによって無効化してはならない。

---

# 8. Cluster Management

Cluster操作：

- CREATE
- ATTACH
- DETACH
- MERGE
- SPLIT
- REACTIVATE
- ARCHIVE

原則：

1. 意味的関連性に基づいて構成する。
2. 文書構造や見出しと機械的に同一視しない。
3. 過剰な細分化を避ける。
4. 無関係な情報の過剰統合を避ける。
5. 統合・分割の履歴を保持する。
6. Clusterの再編とFocus変更を独立して処理する。

MERGEとSPLITは非破壊的な操作とする。

旧Clusterの識別情報と構成履歴を保持する。

---

# 9. Reference Resolution

参照表現の解決には以下を使用する。

- Immediate Discourse
- Current Focus
- Focus History
- Semantic Units
- Entity
- Relation
- Speaker
- Temporal Proximity
- Document Position
- Cluster Hierarchy

参照解決結果は確定・暫定・未解決を区別する。

候補が複数存在する場合は、複数候補を保持する。

後続情報によって参照先が確定した場合、関連するInterpretationとDependencyを再評価する。

参照先が未確定の場合、その参照に依存する状態変更は原則として保留する。

ただし、参照先が未確定でも独立して確定可能な情報は確定してよい。

---

# 10. INIT / UPDATE / RECONCILE

## 10.1 INIT

空のContext Stateから文脈を構築する。

処理：

1. Input Event受信
2. Evidence生成
3. Semantic Hypothesis生成
4. Semantic Unit抽出
5. Entity・Cluster候補生成
6. Relation候補生成
7. State Delta生成
8. Core検証
9. Commit
10. Context State更新

INITは一括構築ではなく、逐次的な初期化とする。

## 10.2 UPDATE

既存状態に対して新しい情報を追加・変更する。

処理：

1. Input Event受信
2. 既存状態との照合
3. Semantic Recognition
4. Context Integration
5. Focus評価
6. Dependency Impact Analysis
7. State Delta生成
8. Core検証
9. Commit
10. 履歴更新

UPDATEは影響範囲を限定した保守的な更新とする。

## 10.3 RECONCILE

既存文脈の整合性を再評価する。

対象：

- 未解決の参照
- 矛盾
- 古い解釈
- 依存関係
- Entity同一性
- Cluster構造
- Focus構造
- 時間的整合性
- 不確実性
- 過去状態との整合性

RECONCILEは全体を無条件に再構築する処理ではない。

原則として影響範囲を特定し、必要な箇所だけを再評価する。

広範囲の不整合が検出された場合は、対象範囲を拡張する。

---

# 11. State Transaction

全ての確定状態変更はTransactionを経由する。

## 11.1 基本処理

1. Base State Version取得
2. 処理対象の読み込み
3. State Delta生成
4. Validation
5. Dependency Impact Analysis
6. Version Check
7. Atomic Commit
8. State Version更新
9. History保存

## 11.2 競合処理

Commit時にBase State Versionと現在のState Versionが異なる場合、古い状態を基にした結果をそのまま確定してはならない。

競合時の処理：

- 再評価
- 差分マージ
- 再実行
- Commit保留

どの処理を選択するかは変更内容と影響範囲に基づいて決定する。

## 11.3 Idempotency

同一イベントの重複処理によって、意味情報や状態変更が二重登録されてはならない。

Event IDと処理識別情報を用いて重複を検知する。

---

# 12. LLMとSURGE Coreの責任分離

## 12.1 LLM / Recognition Engine

担当：

- 意味認識
- 仮説生成
- 参照候補抽出
- Entity候補抽出
- Relation候補抽出
- Cluster候補抽出
- Focus候補抽出
- 矛盾候補抽出
- 再評価候補生成

LLMは状態変更の提案者である。

## 12.2 SURGE Core

担当：

- 入力の整合性管理
- Evidence管理
- データ構造管理
- 状態整合性検証
- 依存関係管理
- 不確実性管理
- Transaction管理
- Commit
- History管理
- Recovery

Coreは確定状態の唯一の管理主体とする。

LLMが直接Repositoryの確定状態を書き換えてはならない。

## 12.3 Model Adapter

モデルごとの入出力差異を吸収する。

モデルの種類や性能にかかわらず、SURGE Coreに対して共通の構造化出力を返す。

特定モデルの独自状態や内部表現をSURGEの必須仕様にしてはならない。

---

# 13. 共通処理インターフェース

SURGE内部の処理結果は、共通の構造化形式に変換する。

各処理器は以下を返すことを基本とする。

- Input References
- Candidate Interpretations
- Candidate Relations
- Candidate Entities
- Candidate Focus Transitions
- Proposed State Delta
- Confidence / Uncertainty
- Provenance
- Processing Metadata

処理器は確定状態を直接変更しない。

Coreは処理結果を検証し、採用可能な差分だけをCommitする。

---

# 14. Context Integrity Principle

SURGE全体に適用される基本原則として、Context Integrity Principleを定義する。

**全ての文脈状態は、根拠、関係、時間、不確実性、変更履歴を追跡可能でなければならない。**

これにより、現在の状態だけでなく、なぜその状態になったかを説明・再構成できるようにする。

この原則は、全レイヤーおよびRepositoryに適用する。

---

# 15. 不変条件

以下をSURGEの基本不変条件とする。

**I-01：Evidence Immutability**  
原入力およびEvidenceは原則として変更しない。

**I-02：Granularity Independence**  
入力粒度と意味単位を独立して扱う。

**I-03：Focus-Structure Separation**  
FocusとCluster構造を独立して管理する。

**I-04：Uncertainty Preservation**  
未確定情報を確定事実として扱わない。

**I-05：Temporal Separation**  
過去状態と現在状態を区別する。

**I-06：Relation-Dependency Separation**  
一般的な関係と変更影響を伴う依存関係を区別する。

**I-07：Provenance Traceability**  
状態変更は根拠を追跡可能とする。

**I-08：Non-destructive Cluster Evolution**  
Clusterの統合・分割で履歴を破壊しない。

**I-09：Idempotency**  
重複イベントによる二重更新を防ぐ。

**I-10：State Reconstructability**  
履歴から過去状態を再構成可能とする。

**I-11：Core Authority**  
LLMの提案だけで確定状態を変更しない。

**I-12：Retrospective Reevaluation**  
新しい入力によって過去の解釈を再評価できる。

**I-13：Transactional Integrity**  
競合する状態更新によって整合性を破壊しない。

**I-14：Uncertainty Propagation Control**  
不確実性が依存情報に与える影響を管理する。

**I-15：Entity Identity Preservation**  
同一性が未確定のEntityを無条件に統合しない。

**I-16：Context Integrity**  
全ての確定状態について根拠・時間・関係・履歴を追跡可能とする。

---

# 16. 非機能要件

## 16.1 拡張性

入力アダプター、認識モデル、処理器、保存方式を独立して追加・交換できること。

## 16.2 再現性

同一のイベント履歴、同一の処理仕様、同一の確定済み判断を用いて、同等の状態を再構成できること。

確率的モデルによる再解釈の完全一致は保証対象とせず、モデル出力とCommit履歴を保存する。

## 16.3 耐障害性

イベント履歴、状態履歴、Snapshotを利用して障害から復旧できること。

## 16.4 監査性

状態変更の根拠、処理経路、変更履歴を追跡できること。

## 16.5 低遅延性

入力到着から暫定的な意味認識・文脈更新までを逐次実行可能とする。

ただし、完全な整合性再評価と即時処理の両立が困難な場合は、暫定状態と確定状態を区別する。

## 16.6 言語拡張性

初期実装では日本語を優先する。

意味構造、データ形式、Coreの制御仕様は言語非依存とし、将来的な多言語対応を可能にする。

---

# 17. 適合性試験

SURGEの実装は、少なくとも以下の試験を通過することを目標とする。

| 試験領域 | 検証内容 |
|---|---|| Granularity | 入力分割方法による最終状態の収束 |
| Focus | 話題切替・復帰・複数フォーカス |
| Reference | 代名詞・省略・遅延解決 |
| Temporal | 時間経過・訂正・有効期間 |
| Dependency | 変更の影響範囲と再評価 |
| Cluster | 生成・統合・分割・再活性化 |
| Parallelism | 複数話者・複数議題・同時入力 |
| Transaction | バージョン競合・Commit整合性 |
| Uncertainty | 不確実性の保持と伝播 |
| Entity | 異種入力間の同一性判定 |
| Recovery | 履歴からの状態復元 |
| Provenance | 根拠の完全な追跡 |

評価指標：

- Semantic Convergence Rate
- Focus Transition Precision / Recall
- Reference Resolution Accuracy
- Entity Resolution Accuracy
- Cluster Purity
- Cluster Fragmentation
- Dependency Propagation Accuracy
- State Consistency
- Uncertainty Preservation Rate
- Provenance Completeness
- Recovery Correctness
- Processing Latency

これらは実装後に測定する指標であり、現時点で性能値を保証するものではない。

---

# 18. 実装上の優先順位

実装は次の順序を推奨する。

## Phase 1：Core Foundation

- Input Event
- Evidence
- Semantic Unit
- Context State
- Event History
- State Delta
- Atomic Commit
- INIT / UPDATE

目的：文脈の基本構築と更新を成立させる。

## Phase 2：Context Structure

- Entity
- Cluster
- Relation
- Dependency
- Reference Resolution

目的：意味的な文脈構造を成立させる。

## Phase 3：Dynamic Cognition

- Global / Local Focus
- Hierarchical Focus
- Multi-Focus
- Uncertainty Management
- Retrospective Reevaluation
- RECONCILE

目的：動的な文脈認識を成立させる。

## Phase 4：Robustness

- Versioned State Transaction
- Conflict Resolution
- Recovery
- Provenance
- Idempotency
- Concurrency Control

目的：継続運用に必要な整合性と耐障害性を確立する。

## Phase 5：Input Expansion

- Document Adapter
- Conversation Adapter
- Chat Adapter
- Audio Adapter
- Video Adapter
- External Data Adapter

目的：同一Coreによる異種入力の統合を実現する。

---

# 19. v0.1からv0.2への変更方針

| 項目 | v0.2での変更 |
|---|---|
| Streaming-Native | 維持 |
| Model Independence | 維持 |
| Granularity Invariance | Semantic Convergenceとして明確化 |
| 6層構造 | 維持 |
| Dynamic Focus | 複数スコープ対応 |
| Cluster Management | 系譜・履歴を明確化 |
| Reference Resolution | 遅延確定と不確実性連携 |
| Dependency Graph | Trigger / Propagation / Evaluation追加 |
| Temporal Context | 5種類の時間属性を明確化 |
| INIT / UPDATE / RECONCILE | 維持・拡張 |
| LLM / Core分離 | 全状態変更への適用を明確化 |
| Event History | 再構成・重複排除を強化 |
| Transaction | Versioned State Transaction追加 |
| Uncertainty | 横断的管理機構として定義 |
| Entity Resolution | 明示的な管理機能として追加 |
| Context Integrity | 全体原則として追加 |

---

# 20. v0.2の設計上の位置付け

SURGE v0.2は、汎用文脈認識フレームワークの基礎アーキテクチャを定義するものである。

この仕様によって、入力の正規化、意味認識、文脈統合、フォーカス管理、状態進化、履歴管理の基本責任を明確化する。

一方、以下は後続の詳細設計で定義する。

- 各データ構造の完全な型定義
- APIおよびイベントスキーマ
- 状態遷移の詳細条件
- Dependencyの評価式
- Entity Resolutionの具体的アルゴリズム
- Focus Transitionの判定アルゴリズム
- LLM出力の共通JSON Schema
- Repositoryの物理構造
- SnapshotおよびRecoveryの実装方式
- 性能目標とベンチマーク条件

これらは基礎思想を変更せずに具体化する。

## 最終定義

SURGEは、入力形式や粒度に依存せず、情報をイベントとして逐次受け取り、意味、関係、依存、時間、フォーカス、不確実性を統合管理しながら、文脈状態を継続的に構築・更新・再評価するモデル非依存の汎用文脈認識フレームワークである。

その中核は、Streaming-Nativeな入力処理と、Evidenceに基づく非破壊的なContext Evolutionにある。

SURGE Coreは全ての確定状態を統括し、認識モデルはそのための解釈候補を提供する。

**SURGEは情報を単に記憶するのではなく、情報がどのような関係と経緯によって現在の文脈を形成しているかを継続的に管理する。**