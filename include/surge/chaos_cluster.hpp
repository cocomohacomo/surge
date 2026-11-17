#pragma once
#include "surge/event_journal.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace surge {
enum class ChaosLifecycle { Open, Accumulating, Emerging, Reviewable, Promoted, Forked, Dormant, Archived };
enum class ChaosOrganization { Unordered, Provisional, MultiPattern, PartiallyStructured };
enum class ChaosAssociationType { CoOccurs, Analogy, Hypothesis, Question, Contradiction, TemporalNearness, Unclassified, CreativeSynthesis };
enum class ChaosAssociationStatus { Provisional, Reviewed, Rejected, Expired };
struct ChaosProvenance { std::string producer_id, producer_version, policy_version, recorded_at; };
struct ChaosScoreObservation {
 double value{0.0};
 std::string scorer_id, scorer_version, policy_version, observed_at;
};
struct ChaosStatusRevision { ChaosAssociationStatus status{ChaosAssociationStatus::Provisional}; ChaosProvenance provenance; };
struct ChaosMember {
 std::string member_ref, event_ref, admission_reason, admitted_at, source_digest;
 ChaosProvenance provenance;
};
struct ChaosAssociation {
 std::string association_id, from_ref, to_ref;
 ChaosAssociationType type{ChaosAssociationType::Unclassified};
 ChaosAssociationStatus status{ChaosAssociationStatus::Provisional};
 ChaosProvenance provenance;
 std::vector<ChaosStatusRevision> history;
};
struct ChaosPatternHypothesis {
 std::string hypothesis_id, statement, epistemic_state{"HYPOTHETICAL"}, status{"PROVISIONAL"};
 std::vector<std::string> member_refs, excluded_member_refs, counterexample_refs, alternative_hypothesis_refs;
 ChaosProvenance provenance;
};
struct ChaosCluster {
 std::string cluster_id;
 ChaosLifecycle lifecycle{ChaosLifecycle::Open};
 ChaosOrganization organization{ChaosOrganization::Unordered};
 std::uint64_t state_version{0};
 ChaosProvenance creation_provenance;
 std::map<std::string,ChaosMember> members;
 std::map<std::string,ChaosAssociation> associations;
 std::map<std::string,ChaosPatternHypothesis> pattern_hypotheses;
 std::vector<std::string> parent_refs,child_refs;
 std::map<std::string,std::string> promoted_member_targets;
 std::map<std::string,std::vector<ChaosScoreObservation>> observations;
};
struct ChaosResult { bool committed{false},duplicate{false}; std::uint64_t state_version{0}; std::string error; };
struct ChaosSplitPart { std::string child_cluster_id; std::vector<std::string> member_refs; };
class ChaosClusterLedger {
 public:
 explicit ChaosClusterLedger(std::string journal_path);
 explicit ChaosClusterLedger(EventJournal& journal);
 bool open();
 ChaosResult create(const std::string&,const std::string&,std::uint64_t,const std::string&,const ChaosProvenance&);
 ChaosResult admit_member(const std::string&,const std::string&,std::uint64_t,const std::string&,const ChaosMember&);
 ChaosResult propose_association(const std::string&,const std::string&,std::uint64_t,const std::string&,const ChaosAssociation&);
 ChaosResult review_association(const std::string&,const std::string&,std::uint64_t,const std::string&,const std::string&,ChaosAssociationStatus,const ChaosProvenance&);
 ChaosResult add_pattern_hypothesis(const std::string&,const std::string&,std::uint64_t,const std::string&,const ChaosPatternHypothesis&);
 ChaosResult record_observation(const std::string&,const std::string&,std::uint64_t,const std::string&,const std::string&,const ChaosScoreObservation&);
 ChaosResult transition(const std::string&,const std::string&,std::uint64_t,const std::string&,ChaosLifecycle,const std::string&,const ChaosProvenance&);
 ChaosResult promote_subset(const std::string&,const std::string&,std::uint64_t,const std::string&,const std::map<std::string,std::string>&,const ChaosProvenance&);
 ChaosResult fork_subset(const std::string&,const std::string&,std::uint64_t,const std::string&,const std::string&,const std::vector<std::string>&,const ChaosProvenance&);
 ChaosResult split_subsets(const std::string&,const std::string&,std::uint64_t,const std::string&,const std::vector<ChaosSplitPart>&,const ChaosProvenance&);
 bool get(const std::string&,ChaosCluster&) const;
 std::vector<ChaosCluster> list() const;
 std::uint64_t state_version() const;
 private:
 using State=std::map<std::string,ChaosCluster>;
 ChaosResult commit_snapshot(const std::string&,const std::string&,std::uint64_t,State&&,const std::string&);
 bool replay();
 static std::string encode(const State&);
 static bool decode(const std::string&,State&);
 static bool allowed_transition(ChaosLifecycle,ChaosLifecycle);
 std::unique_ptr<EventJournal> owned_journal_;
 EventJournal* journal_{nullptr};
 mutable std::mutex mutex_;
 State state_;
};
} // namespace surge
