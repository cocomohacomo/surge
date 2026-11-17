#include "test_support.hpp"
#include "surge/core.hpp"
#include <iostream>
using namespace surge;
int main() {
  { SourceSequencer s(9); s.observe(10); s.observe(12); CHECK(s.gaps().count(11)); CHECK(s.packet_status(12)=="READY_BUT_BLOCKED"); CHECK(s.watermark()==10); }
  { SourceSequencer s(10); s.observe(12); CHECK(!s.authorize_skip(11,"NONE","SOURCE_CONFIRMED_LOSS","v1","k")); CHECK(s.authorize_skip(11,"SOURCE_GAP_SKIP","SOURCE_CONFIRMED_LOSS","v1","k")); CHECK(s.watermark()==12); CHECK(s.audit_count()==1); CHECK(s.authorize_skip(11,"SOURCE_GAP_SKIP","SOURCE_CONFIRMED_LOSS","v1","k")); CHECK(s.audit_count()==1); }
  { SourceSequencer s(10); s.observe(12); s.authorize_skip(11,"SOURCE_GAP_SKIP","SOURCE_CONFIRMED_LOSS","v1","k"); CHECK(s.ingest_late(11)); CHECK(s.gaps().at(11).state==GapState::Reopened); }
  { SourceSequencer s(0); s.observe(3); CHECK(s.packet_status(3)=="READY_BUT_BLOCKED"); s.observe(2); s.observe(1); CHECK(s.watermark()==3); }
  { RangeLedger r; CHECK(r.ingest("K","D1","i1").error.empty()); auto d=r.ingest("K","D1","i1"); CHECK(d.duplicate); CHECK(r.evidence_count()==1); }
  { RangeLedger r; r.ingest("K","D1","i1"); CHECK(r.ingest("K","D2","i2").error=="RANGE_CONTENT_CONFLICT"); CHECK(r.evidence_count()==1); }
  { RangeLedger r; r.ingest("K","D1","i1"); CHECK(r.ingest("K","D2","i2",true,2).error.empty()); CHECK(r.revision_count()==1); CHECK(r.evidence_count()==2); }
  { RangeLedger r; r.ingest("K","D1","i1"); CHECK(r.ingest("X","D2","i1").error=="IDEMPOTENCY_KEY_REUSED"); }
  { ScopeSpan s("NEGATION"); CHECK(!s.close(true,false,true)); CHECK(s.close(true,true,true)); CHECK(s.state()==ScopeState::Closed); }
  { ScopeSpan s("CONDITION"); s.mark_unresolved(); CHECK(!s.close(true,true,false)); CHECK(s.close(true,true,true)); }
  { ScopeSpan s("QUOTE"); CHECK(s.invalidate("rev-2")); CHECK(s.state()==ScopeState::Invalidated); }
  { auto t=resolve_ticket(10,11,true); CHECK(t.resolved && !t.stale && t.state_version==11); }
  { auto t=resolve_ticket(10,11,false); CHECK(!t.resolved && t.stale); }
  { CHECK(quality_gate(0,0,0.5)==Gate::Inconclusive); CHECK(quality_gate(100,1,0.5)==Gate::Fail); CHECK(quality_gate(100,90,0.5)==Gate::Pass); }
  { CHECK(performance_gate(0.005,0.01)==Gate::Pass); CHECK(performance_gate(0.02,0.01)==Gate::Fail); }
  { CHECK(overall_gate(Gate::Pass,Gate::Pass,0)==Gate::Pass); CHECK(overall_gate(Gate::Pass,Gate::Inconclusive,0)==Gate::Inconclusive); CHECK(overall_gate(Gate::Pass,Gate::Pass,1)==Gate::Fail); }
  std::cout << "SURGE Core tests: 16 groups passed\n";
}
