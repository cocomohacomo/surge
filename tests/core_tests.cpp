#include "surge/core.hpp"
#include <cassert>
#include <iostream>
using namespace surge;
int main() {
  { SourceSequencer s(9); s.observe(10); s.observe(12); assert(s.gaps().count(11)); assert(s.packet_status(12)=="BLOCKED_BY_GAP"); assert(s.watermark()==10); }
  { SourceSequencer s(10); s.observe(12); assert(!s.authorize_skip(11,"NONE","SOURCE_CONFIRMED_LOSS","v1","k")); assert(s.authorize_skip(11,"SOURCE_GAP_SKIP","SOURCE_CONFIRMED_LOSS","v1","k")); assert(s.watermark()==12); assert(s.audit_count()==1); assert(s.authorize_skip(11,"SOURCE_GAP_SKIP","SOURCE_CONFIRMED_LOSS","v1","k")); assert(s.audit_count()==1); }
  { SourceSequencer s(10); s.observe(12); s.authorize_skip(11,"SOURCE_GAP_SKIP","SOURCE_CONFIRMED_LOSS","v1","k"); assert(s.ingest_late(11)); assert(s.gaps().at(11).state==GapState::Reopened); }
  { SourceSequencer s(0); s.observe(3); assert(s.packet_status(3)=="READY_BUT_BLOCKED"); s.observe(2); s.observe(1); assert(s.watermark()==3); }
  { RangeLedger r; assert(r.ingest("K","D1","i1").error.empty()); auto d=r.ingest("K","D1","i1"); assert(d.duplicate); assert(r.evidence_count()==1); }
  { RangeLedger r; r.ingest("K","D1","i1"); assert(r.ingest("K","D2","i2").error=="RANGE_CONTENT_CONFLICT"); assert(r.evidence_count()==1); }
  { RangeLedger r; r.ingest("K","D1","i1"); assert(r.ingest("K","D2","i2",true,2).error.empty()); assert(r.revision_count()==1); assert(r.evidence_count()==2); }
  { RangeLedger r; r.ingest("K","D1","i1"); assert(r.ingest("X","D2","i1").error=="IDEMPOTENCY_KEY_REUSED"); }
  { ScopeSpan s("NEGATION"); assert(!s.close(true,false,true)); assert(s.close(true,true,true)); assert(s.state()==ScopeState::Closed); }
  { ScopeSpan s("CONDITION"); s.mark_unresolved(); assert(!s.close(true,true,false)); assert(s.close(true,true,true)); }
  { ScopeSpan s("QUOTE"); assert(s.invalidate("rev-2")); assert(s.state()==ScopeState::Invalidated); }
  { auto t=resolve_ticket(10,11,true); assert(t.resolved && !t.stale && t.state_version==11); }
  { auto t=resolve_ticket(10,11,false); assert(!t.resolved && t.stale); }
  { assert(quality_gate(0,0,0.5)==Gate::Inconclusive); assert(quality_gate(100,1,0.5)==Gate::Fail); assert(quality_gate(100,90,0.5)==Gate::Pass); }
  { assert(performance_gate(0.005,0.01)==Gate::Pass); assert(performance_gate(0.02,0.01)==Gate::Fail); }
  { assert(overall_gate(Gate::Pass,Gate::Pass,0)==Gate::Pass); assert(overall_gate(Gate::Pass,Gate::Inconclusive,0)==Gate::Inconclusive); assert(overall_gate(Gate::Pass,Gate::Pass,1)==Gate::Fail); }
  std::cout << "SURGE Core tests: 16 groups passed\n";
}
