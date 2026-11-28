#pragma once
#include "surge/event_journal.hpp"
#include <cstdint>
#include <map>
#include <string>
namespace surge {
struct MinimalInformationProjection {
  std::string information;
  std::string target_cluster;
  bool has_target_cluster{false};
  double distance{0.0};
};
struct MinimalProjectionEnvelope {
  std::string projection_id, idempotency_key, context_id;
  std::string projector_profile_id, projector_profile_version;
  std::uint64_t inventory_revision{0};
  MinimalInformationProjection projection;
};
struct MinimalProjectionAdmissionResult {
  bool admitted{false}, duplicate{false};
  std::uint64_t state_version{0};
  std::string error;
};
// Durable ingress only. This class never interprets distance or mutates clusters.
class MinimalProjectionAdmission {
 public:
  explicit MinimalProjectionAdmission(EventJournal& journal);
  bool recover();
  MinimalProjectionAdmissionResult admit(const MinimalProjectionEnvelope& envelope);
  std::uint64_t state_version() const;
  std::size_t projection_count() const;
  bool contains_projection(const std::string& projection_id) const;
 private:
  static bool validate(const MinimalProjectionEnvelope&, std::string&);
  static std::string encode(const MinimalProjectionEnvelope&);
  static bool decode(const std::string&, MinimalProjectionEnvelope&);
  bool apply_recovered(const MinimalProjectionEnvelope&, const std::string&);
  EventJournal& journal_;
  std::map<std::string, std::string> projections_, idempotency_;
  bool recovered_{false};
};
}  // namespace surge
