#include "surge/minimal_projection_admission.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
namespace surge {
namespace {
constexpr const char* kEventKind = "MINIMAL_PROJECTION_ADMITTED";
std::string event_id(const std::string& id) { return "minimal-projection:" + id; }
}
MinimalProjectionAdmission::MinimalProjectionAdmission(EventJournal& journal) : journal_(journal) {}
bool MinimalProjectionAdmission::validate(const MinimalProjectionEnvelope& e, std::string& error) {
  if (e.projection_id.empty() || e.idempotency_key.empty() || e.context_id.empty() ||
      e.projector_profile_id.empty() || e.projector_profile_version.empty()) {
    error = "MISSING_REQUIRED_ENVELOPE_METADATA"; return false;
  }
  if (e.projection.information.empty() ||
      e.projection.information.find_first_not_of(" \\t\\r\\n") == std::string::npos) {
    error = "INFORMATION_MUST_NOT_BE_BLANK"; return false;
  }
  if (e.projection.has_target_cluster && e.projection.target_cluster.empty()) {
    error = "TARGET_CLUSTER_MUST_BE_NONEMPTY_OR_NULL"; return false;
  }
  if (!e.projection.has_target_cluster && !e.projection.target_cluster.empty()) {
    error = "NULL_TARGET_MUST_NOT_CARRY_CLUSTER_ID"; return false;
  }
  if (!std::isfinite(e.projection.distance)) { error = "DISTANCE_MUST_BE_FINITE"; return false; }
  error.clear(); return true;
}
std::string MinimalProjectionAdmission::encode(const MinimalProjectionEnvelope& e) {
  std::ostringstream out;
  out << "MPI1 " << std::quoted(e.projection_id) << ' ' << std::quoted(e.idempotency_key)
      << ' ' << std::quoted(e.context_id) << ' ' << std::quoted(e.projector_profile_id)
      << ' ' << std::quoted(e.projector_profile_version) << ' ' << e.inventory_revision
      << ' ' << std::quoted(e.projection.information) << ' '
      << (e.projection.has_target_cluster ? 1 : 0) << ' '
      << std::quoted(e.projection.target_cluster) << ' ' << std::setprecision(17)
      << e.projection.distance;
  return out.str();
}
bool MinimalProjectionAdmission::decode(const std::string& payload, MinimalProjectionEnvelope& e) {
  std::istringstream in(payload); std::string magic, extra; int has_target = 0;
  if (!(in >> magic) || magic != "MPI1" ||
      !(in >> std::quoted(e.projection_id) >> std::quoted(e.idempotency_key)
        >> std::quoted(e.context_id) >> std::quoted(e.projector_profile_id)
        >> std::quoted(e.projector_profile_version) >> e.inventory_revision
        >> std::quoted(e.projection.information) >> has_target
        >> std::quoted(e.projection.target_cluster) >> e.projection.distance) ||
      (in >> extra) || (has_target != 0 && has_target != 1)) return false;
  e.projection.has_target_cluster = has_target == 1;
  std::string error; return validate(e, error);
}
bool MinimalProjectionAdmission::apply_recovered(const MinimalProjectionEnvelope& e, const std::string& payload) {
  const auto old = projections_.find(e.projection_id);
  if (old != projections_.end()) return old->second == payload;
  const auto idem = idempotency_.find(e.idempotency_key);
  if (idem != idempotency_.end()) return idem->second == e.projection_id;
  projections_.emplace(e.projection_id, payload);
  idempotency_.emplace(e.idempotency_key, e.projection_id);
  return true;
}
bool MinimalProjectionAdmission::recover() {
  // A failed retry must never leave a previously recovered index writable.
  recovered_ = false;
  projections_.clear(); idempotency_.clear();
  for (const auto& event : journal_.committed_events()) {
    if (event.kind != kEventKind) continue;
    MinimalProjectionEnvelope e;
    if (!decode(event.payload, e) || event.event_id != event_id(e.projection_id) ||
        !apply_recovered(e, event.payload)) return false;
  }
  recovered_ = true; return true;
}
MinimalProjectionAdmissionResult MinimalProjectionAdmission::admit(const MinimalProjectionEnvelope& e) {
  MinimalProjectionAdmissionResult result; result.state_version = journal_.state_version();
  if (!recovered_) { result.error = "RECOVERY_REQUIRED"; return result; }
  if (!validate(e, result.error)) return result;
  const auto payload = encode(e);
  const auto old = projections_.find(e.projection_id);
  if (old != projections_.end()) {
    if (old->second != payload) { result.error = "PROJECTION_ID_REUSED"; return result; }
    result.admitted = true; result.duplicate = true; return result;
  }
  const auto idem = idempotency_.find(e.idempotency_key);
  if (idem != idempotency_.end()) { result.error = "IDEMPOTENCY_KEY_REUSED"; return result; }
  JournalEvent event; event.event_id = event_id(e.projection_id);
  event.kind = kEventKind; event.payload = payload;
  const auto committed = journal_.commit("minimal-projection-tx:" + e.projection_id,
      e.idempotency_key, journal_.state_version(), {event});
  result.state_version = committed.state_version;
  if (!committed.committed) { result.error = committed.error; return result; }
  if (!apply_recovered(e, payload)) { result.error = "POST_COMMIT_INDEX_INCONSISTENCY"; return result; }
  result.admitted = true; result.duplicate = committed.duplicate; return result;
}
std::uint64_t MinimalProjectionAdmission::state_version() const { return journal_.state_version(); }
std::size_t MinimalProjectionAdmission::projection_count() const { return projections_.size(); }
bool MinimalProjectionAdmission::contains_projection(const std::string& id) const { return projections_.count(id) != 0; }
}  // namespace surge
