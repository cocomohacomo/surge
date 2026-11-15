# SURGE v0.6 Conformance Test Catalogue

**Specification:** 0.6.0  
**Test type:** Model-based design conformance  
**Count:** 100

設計上の期待結果が100ケース全てで定義済み。PASSは仕様レベルの設計適合を示し、実装自動テストの実行を意味しない。

| ID | ケース | 期待結果 |
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

## 集計
- 設計適合: 100/100 PASS
- 未定義の成功経路: 0
- 実装自動テスト: 未実行
- 要実測: Clock Mapping誤差伝播、SCC収束、DB Transaction、並列Delta競合、JSON Schema Validator実装。
