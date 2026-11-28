#include "surge/minimal_projection_admission.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <string>
int main() {
  const std::string path = "minimal_projection_admission_test.journal"; std::remove(path.c_str());
  {
    surge::EventJournal journal(path); assert(journal.open());
    surge::MinimalProjectionAdmission admission(journal); assert(admission.recover());
    surge::MinimalProjectionEnvelope e;
    e.projection_id="projection-1"; e.idempotency_key="idem-1"; e.context_id="ctx";
    e.projector_profile_id="embedding-ja"; e.projector_profile_version="1"; e.inventory_revision=7;
    e.projection.information="柴犬"; e.projection.target_cluster="cluster-dog";
    e.projection.has_target_cluster=true; e.projection.distance=100.0;
    const auto first=admission.admit(e); assert(first.admitted && !first.duplicate);
    const auto duplicate=admission.admit(e); assert(duplicate.admitted && duplicate.duplicate);
    auto conflict=e; conflict.projection.information="秋田犬";
    const auto rejected=admission.admit(conflict); assert(!rejected.admitted && rejected.error=="PROJECTION_ID_REUSED");
    auto invalid=e; invalid.projection_id="nan"; invalid.idempotency_key="nan-key";
    invalid.projection.distance=std::numeric_limits<double>::quiet_NaN();
    assert(admission.admit(invalid).error=="DISTANCE_MUST_BE_FINITE");
    auto fresh=e; fresh.projection_id="projection-2"; fresh.idempotency_key="idem-2";
    fresh.projection.target_cluster.clear(); fresh.projection.has_target_cluster=false;
    fresh.projection.distance=0.0; assert(admission.admit(fresh).admitted);
  }
  {
    surge::EventJournal journal(path); assert(journal.open());
    surge::MinimalProjectionAdmission recovered(journal); assert(recovered.recover());
    assert(recovered.projection_count()==2);
    assert(recovered.contains_projection("projection-1"));
    assert(recovered.contains_projection("projection-2"));
  }
  std::remove(path.c_str()); return 0;
}
