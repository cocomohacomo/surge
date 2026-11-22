"""ctypes access to the journal-backed C++ ContextGraph SemanticDelta API."""
from __future__ import annotations
import ctypes
import json
from typing import Any
from surge_adapter.packet_ingest import Admission, CppCoreBridge, PacketRejected, normalize_source

class _Packet(ctypes.Structure):
    _fields_ = [("packet_id", ctypes.c_char_p), ("idempotency_key", ctypes.c_char_p),
                ("context_id", ctypes.c_char_p), ("source_id", ctypes.c_char_p),
                ("source_generation", ctypes.c_char_p), ("block_ref", ctypes.c_char_p),
                ("offset_basis", ctypes.c_char_p), ("start_offset", ctypes.c_uint64),
                ("end_offset", ctypes.c_uint64), ("source_sequence", ctypes.c_uint64),
                ("source_snapshot_digest", ctypes.c_char_p), ("source_range_digest", ctypes.c_char_p),
                ("canonical_request_digest", ctypes.c_char_p), ("revision_of_packet_id", ctypes.c_char_p),
                ("revision", ctypes.c_int), ("revision_sequence", ctypes.c_uint64)]

class _Node(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
                ("content", ctypes.c_char_p),
                ("references", ctypes.POINTER(ctypes.c_char_p)),
                ("reference_count", ctypes.c_size_t)]

class _Proposal(ctypes.Structure):
    _fields_ = [("proposal_id", ctypes.c_char_p), ("packet_ref", ctypes.c_char_p),
                ("base_state_version", ctypes.c_uint64),
                ("context_hint_state_version", ctypes.c_uint64),
                ("packet_source_generation", ctypes.c_char_p),
                ("packet_source_sequence", ctypes.c_uint64),
                ("serialized_json", ctypes.c_char_p)]

class _EvidenceProvenance(ctypes.Structure):
    _fields_ = [("evidence_id", ctypes.c_char_p),
                ("observed_at", ctypes.c_char_p),
                ("source_published_at", ctypes.c_char_p),
                ("source_retrieved_at", ctypes.c_char_p),
                ("revision", ctypes.c_uint64)]

class _Relation(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
                ("from_id", ctypes.c_char_p), ("to_id", ctypes.c_char_p)]

class _Anchor(ctypes.Structure):
    _fields_ = [("anchor_id", ctypes.c_char_p), ("source_digest", ctypes.c_char_p),
                ("offset_basis", ctypes.c_char_p), ("start_offset", ctypes.c_uint64),
                ("end_offset", ctypes.c_uint64), ("exact_text", ctypes.c_char_p),
                ("packet_ref", ctypes.c_char_p)]

class _Scope(ctypes.Structure):
    _fields_ = [("span_id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
                ("source_digest", ctypes.c_char_p), ("offset_basis", ctypes.c_char_p),
                ("opener", _Anchor), ("has_closer", ctypes.c_int), ("closer", _Anchor),
                ("target_refs", ctypes.POINTER(ctypes.c_char_p)), ("target_count", ctypes.c_size_t),
                ("antecedent_refs", ctypes.POINTER(ctypes.c_char_p)), ("antecedent_count", ctypes.c_size_t),
                ("attribution_ref", ctypes.c_char_p), ("modality_kind", ctypes.c_char_p),
                ("temporal_role", ctypes.c_char_p), ("parent_ref", ctypes.c_char_p),
                ("packet_refs", ctypes.POINTER(ctypes.c_char_p)), ("packet_count", ctypes.c_size_t)]

class _ChaosProvenance(ctypes.Structure):
    _fields_ = [("producer_id", ctypes.c_char_p), ("producer_version", ctypes.c_char_p),
                ("policy_version", ctypes.c_char_p), ("recorded_at", ctypes.c_char_p)]

class _ChaosMember(ctypes.Structure):
    _fields_ = [("member_ref", ctypes.c_char_p), ("event_ref", ctypes.c_char_p),
                ("admission_reason", ctypes.c_char_p), ("admitted_at", ctypes.c_char_p),
                ("source_digest", ctypes.c_char_p)]

class _ChaosAssociation(ctypes.Structure):
    _fields_ = [("association_id", ctypes.c_char_p), ("from_member_ref", ctypes.c_char_p),
                ("to_member_ref", ctypes.c_char_p), ("association_type", ctypes.c_char_p), ("valid_from_utc", ctypes.c_char_p),
                ("valid_until_utc", ctypes.c_char_p)]

class _ChaosHypothesisReview(ctypes.Structure):
    _fields_ = [("status", ctypes.c_char_p), ("epistemic_state", ctypes.c_char_p),
                ("excluded_member_refs", ctypes.POINTER(ctypes.c_char_p)),
                ("exclusion_reasons", ctypes.POINTER(ctypes.c_char_p)),
                ("exclusion_count", ctypes.c_size_t)]


class _ChaosScoreObservation(ctypes.Structure):
    _fields_ = [("value", ctypes.c_double), ("scorer_id", ctypes.c_char_p),
                ("scorer_version", ctypes.c_char_p), ("policy_version", ctypes.c_char_p),
                ("observed_at", ctypes.c_char_p)]


class _ChaosPatternHypothesis(ctypes.Structure):
    _fields_ = [("hypothesis_id", ctypes.c_char_p), ("statement", ctypes.c_char_p),
                ("epistemic_state", ctypes.c_char_p), ("status", ctypes.c_char_p),
                ("member_refs", ctypes.POINTER(ctypes.c_char_p)), ("member_count", ctypes.c_size_t),
                ("excluded_member_refs", ctypes.POINTER(ctypes.c_char_p)), ("excluded_member_count", ctypes.c_size_t),
                ("counterexample_refs", ctypes.POINTER(ctypes.c_char_p)), ("counterexample_count", ctypes.c_size_t),
                ("alternative_hypothesis_refs", ctypes.POINTER(ctypes.c_char_p)), ("alternative_hypothesis_count", ctypes.c_size_t)]

class _JournalEvent(ctypes.Structure):
    _fields_ = [("event_id", ctypes.c_char_p), ("kind", ctypes.c_char_p),
                ("payload", ctypes.c_char_p)]


class SemanticCoreBridge(CppCoreBridge):
    """Packet admission + typed SemanticDelta commit over one shared journal."""

    def __init__(self, journal_path, library_path=None, resource_limits=None):
        super().__init__(journal_path, library_path, resource_limits=resource_limits)
        lib = self._lib
        lib.surge_retention_journal_commit.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_JournalEvent), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_retention_journal_commit.restype = ctypes.c_int
        lib.surge_retention_journal_get_events.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_retention_journal_get_events.restype = ctypes.c_int
        lib.surge_packet_core_admit_and_apply_with_proposal.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(_Packet), ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.POINTER(_Scope), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(_Proposal),
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_packet_core_admit_and_apply_with_proposal.restype = ctypes.c_int
        lib.surge_classification_proposal_get.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_classification_proposal_get.restype = ctypes.c_int
        lib.surge_packet_core_admit_and_apply_with_metadata.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(_Packet), ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.POINTER(_Scope), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(_Proposal),
            ctypes.POINTER(_EvidenceProvenance),
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_packet_core_admit_and_apply_with_metadata.restype = ctypes.c_int
        lib.surge_evidence_provenance_get.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_evidence_provenance_get.restype = ctypes.c_int
        lib.surge_context_graph_apply_scoped.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.POINTER(_Scope), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply_scoped.restype = ctypes.c_int
        lib.surge_context_graph_scope_count.argtypes = [ctypes.c_void_p]
        lib.surge_context_graph_scope_count.restype = ctypes.c_size_t
        lib.surge_question_aspect_set_status_batch.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_char_p),
            ctypes.c_size_t, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_aspect_set_status_batch.restype = ctypes.c_int
        lib.surge_question_aspect_set_status.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_aspect_set_status.restype = ctypes.c_int
        lib.surge_question_aspect_get_status.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_aspect_get_status.restype = ctypes.c_int
        lib.surge_question_get_aggregate_status.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_get_aggregate_status.restype = ctypes.c_int
        lib.surge_question_memory_set_state.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint64,ctypes.POINTER(ctypes.c_char),ctypes.c_size_t]
        lib.surge_question_memory_set_state.restype=ctypes.c_int
        lib.surge_question_memory_record_reference.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_uint64,ctypes.POINTER(ctypes.c_int),ctypes.POINTER(ctypes.c_uint64),ctypes.POINTER(ctypes.c_char),ctypes.c_size_t]
        lib.surge_question_memory_record_reference.restype=ctypes.c_int
        lib.surge_question_memory_record_reference_with_dependents.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_uint64,ctypes.c_size_t,ctypes.c_size_t,ctypes.POINTER(ctypes.c_int),ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_char),ctypes.c_size_t]
        lib.surge_question_memory_record_reference_with_dependents.restype=ctypes.c_int
        lib.surge_question_memory_get_state.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.POINTER(ctypes.c_char),ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_char),ctypes.c_size_t]
        lib.surge_question_memory_get_state.restype=ctypes.c_int
        lib.surge_question_memory_recommendation.argtypes=[ctypes.c_double,ctypes.c_double,ctypes.c_double,ctypes.c_double,ctypes.c_double,ctypes.c_double,ctypes.POINTER(ctypes.c_char),ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_char),ctypes.c_size_t]
        lib.surge_question_memory_recommendation.restype=ctypes.c_int
        lib.surge_question_memory_plan.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_char),ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_char),ctypes.c_size_t]
        lib.surge_question_memory_plan.restype=ctypes.c_int
        lib.surge_question_memory_enforce_capacity.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_char_p),ctypes.POINTER(ctypes.c_double),ctypes.c_size_t,ctypes.c_size_t,ctypes.c_uint64,ctypes.POINTER(ctypes.c_char),ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_char),ctypes.c_size_t]
        lib.surge_question_memory_enforce_capacity.restype=ctypes.c_int
        lib.surge_question_get_context_bundle.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_get_context_bundle.restype = ctypes.c_int
        lib.surge_question_get_reevaluation_plan.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_get_reevaluation_plan.restype = ctypes.c_int
        lib.surge_question_get_reevaluation_candidates.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_get_reevaluation_candidates.restype = ctypes.c_int
        lib.surge_question_route_contradiction.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_route_contradiction.restype = ctypes.c_int
        lib.surge_question_route_contradiction_scoped.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_question_route_contradiction_scoped.restype = ctypes.c_int
        lib.surge_context_graph_dependency_evaluation.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_dependency_evaluation.restype = ctypes.c_int
        lib.surge_context_graph_apply.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.POINTER(_Node), ctypes.c_size_t,
            ctypes.POINTER(_Relation), ctypes.c_size_t,
            ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply.restype = ctypes.c_int
        lib.surge_context_graph_apply_claim_validity.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply_claim_validity.restype = ctypes.c_int
        lib.surge_context_graph_apply_claim_validity_reviewed.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_apply_claim_validity_reviewed.restype = ctypes.c_int
        lib.surge_context_graph_claim_validity_at.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_claim_validity_at.restype = ctypes.c_int

        lib.surge_temporal_boundary_add.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_boundary_add.restype = ctypes.c_int
        lib.surge_temporal_boundary_resolve_candidate.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_boundary_resolve_candidate.restype = ctypes.c_int
        lib.surge_temporal_boundary_get.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_boundary_get.restype = ctypes.c_int
        lib.surge_temporal_boundary_clear.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_uint64, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_boundary_clear.restype = ctypes.c_int
        lib.surge_temporal_boundary_pending_count.argtypes = [ctypes.c_void_p]
        lib.surge_temporal_boundary_pending_count.restype = ctypes.c_size_t
        lib.surge_temporal_question_get.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_temporal_question_get.restype = ctypes.c_int
        lib.surge_temporal_question_count.argtypes = [ctypes.c_void_p]
        lib.surge_temporal_question_count.restype = ctypes.c_size_t
        lib.surge_context_graph_focus_get.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_focus_get.restype = ctypes.c_int
        lib.surge_context_graph_focus_set_chaos.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_context_graph_focus_set_chaos.restype = ctypes.c_int

        lib.surge_chaos_cluster_create.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.POINTER(_ChaosProvenance), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_create.restype = ctypes.c_int
        lib.surge_chaos_cluster_admit_member.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.POINTER(_ChaosMember), ctypes.POINTER(_ChaosProvenance),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_admit_member.restype = ctypes.c_int
        lib.surge_chaos_cluster_propose_association.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.POINTER(_ChaosAssociation), ctypes.POINTER(_ChaosProvenance),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_propose_association.restype = ctypes.c_int
        lib.surge_chaos_cluster_propose_association_temporal.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.POINTER(_ChaosAssociation), ctypes.POINTER(_ChaosProvenance),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_propose_association_temporal.restype = ctypes.c_int
        lib.surge_chaos_cluster_review_association.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(_ChaosProvenance),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_review_association.restype = ctypes.c_int
        lib.surge_chaos_cluster_expire_due_associations.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(_ChaosProvenance),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_expire_due_associations.restype = ctypes.c_int
        lib.surge_chaos_cluster_merge.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_size_t,
            ctypes.POINTER(_ChaosProvenance), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_merge.restype = ctypes.c_int
        lib.surge_chaos_cluster_add_pattern_hypothesis.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.POINTER(_ChaosPatternHypothesis), ctypes.POINTER(_ChaosProvenance),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_add_pattern_hypothesis.restype = ctypes.c_int
        lib.surge_chaos_cluster_review_pattern_hypothesis.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(_ChaosHypothesisReview),
            ctypes.POINTER(_ChaosProvenance), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_review_pattern_hypothesis.restype = ctypes.c_int
        lib.surge_chaos_cluster_record_observation.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(_ChaosScoreObservation),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_record_observation.restype = ctypes.c_int
        lib.surge_chaos_cluster_record_entity_observation.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
            ctypes.POINTER(_ChaosScoreObservation), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_record_entity_observation.restype = ctypes.c_int
        lib.surge_chaos_cluster_revise_membership.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int,
            ctypes.POINTER(_ChaosProvenance), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_revise_membership.restype = ctypes.c_int
        lib.surge_chaos_cluster_transition.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(_ChaosProvenance),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_transition.restype = ctypes.c_int
        lib.surge_chaos_cluster_promote_subset.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64,
            ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_char_p),
            ctypes.c_size_t, ctypes.POINTER(_ChaosProvenance), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_promote_subset.restype = ctypes.c_int
        lib.surge_chaos_cluster_get.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_get.restype = ctypes.c_int
        lib.surge_chaos_cluster_get_bounded.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t,
            ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t, ctypes.POINTER(ctypes.c_char),
            ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.surge_chaos_cluster_get_bounded.restype = ctypes.c_int


    @property
    def scope_count(self) -> int:
        return int(self._lib.surge_context_graph_scope_count(self._handle))


    def admit_and_apply(self, packet, source, snapshot_digest, range_digest,
                        request_digest, revision, revision_sequence, revision_ref,
                        delta):
        pos = packet["source_position"]
        packet_input = _Packet(
            packet["packet_id"].encode(), packet["idempotency_key"].encode(),
            packet["context_id"].encode(), packet["source_id"].encode(),
            pos["source_generation"].encode(), pos["block_ref"].encode(),
            pos["offset_basis"].encode(), int(pos["start_offset"]),
            int(pos["end_offset"]), int(pos["source_sequence"]),
            snapshot_digest.encode(), range_digest.encode(), request_digest.encode(),
            (revision_ref or "").encode(), 1 if revision else 0, int(revision_sequence))
        keepalive = []
        nodes = []
        for item in delta["nodes"]:
            refs = [str(v).encode() for v in item.get("references", [])]
            ref_array = (ctypes.c_char_p * len(refs))(*refs) if refs else None
            if ref_array is not None:
                keepalive.append(ref_array)
            nodes.append(_Node(str(item["id"]).encode(), str(item["kind"]).encode(),
                str(item["content"]).encode(),
                ctypes.cast(ref_array, ctypes.POINTER(ctypes.c_char_p)) if ref_array is not None else None,
                len(refs)))
        node_array = (_Node * len(nodes))(*nodes) if nodes else None
        relations = [_Relation(str(r["id"]).encode(), str(r["kind"]).encode(),
                               str(r["from"]).encode(), str(r["to"]).encode())
                     for r in delta["relations"]]
        relation_array = (_Relation * len(relations))(*relations) if relations else None
        scopes = []
        for item in delta.get("scope_spans", []):
            def anchor(value):
                return _Anchor(str(value["anchor_id"]).encode(),
                               str(value["source_digest"]).encode(),
                               str(value["offset_basis"]).encode(),
                               int(value["start_offset"]), int(value["end_offset"]),
                               str(value["exact_text"]).encode(),
                               str(value["packet_ref"]).encode())
            opener = anchor(item["opener"])
            closer_value = item.get("closer")
            closer = anchor(closer_value) if closer_value else _Anchor()
            target = [str(v).encode() for v in item.get("target_refs", [])]
            antecedents = [str(v).encode() for v in item.get("antecedent_refs", [])]
            packets = [str(v).encode() for v in item.get("packet_refs", [])]
            target_array = (ctypes.c_char_p * len(target))(*target) if target else None
            antecedent_array = (ctypes.c_char_p * len(antecedents))(*antecedents) if antecedents else None
            packet_array = (ctypes.c_char_p * len(packets))(*packets) if packets else None
            keepalive.extend([x for x in (target_array, antecedent_array, packet_array) if x is not None])
            scopes.append(_Scope(str(item["span_id"]).encode(), str(item["kind"]).encode(),
                str(item["source_digest"]).encode(), str(item["offset_basis"]).encode(),
                opener, 1 if closer_value else 0, closer,
                ctypes.cast(target_array, ctypes.POINTER(ctypes.c_char_p)) if target_array is not None else None,
                len(target), ctypes.cast(antecedent_array, ctypes.POINTER(ctypes.c_char_p)) if antecedent_array is not None else None,
                len(antecedents), str(item.get("attribution_ref", "")).encode(),
                str(item.get("modality_kind", "")).encode(), str(item.get("temporal_role", "")).encode(),
                str(item.get("parent_ref", "")).encode(),
                ctypes.cast(packet_array, ctypes.POINTER(ctypes.c_char_p)) if packet_array is not None else None,
                len(packets)))
        scope_array = (_Scope * len(scopes))(*scopes) if scopes else None
        snapshot_buffer = (ctypes.c_ubyte * len(source)).from_buffer_copy(source)
        focus = delta.get("focus")
        focus_cluster = str(focus["cluster_id"]).encode() if focus else None
        focus_interest = str(focus.get("interest_id", "")).encode() if focus else None
        proposal_value = delta.get("classification_proposal")
        proposal_input = None
        proposal_json = None
        if proposal_value is not None:
            proposal_json = json.dumps(proposal_value, ensure_ascii=False,
                                       separators=(",", ":"), sort_keys=True).encode("utf-8")
            proposal_input = _Proposal(
                str(proposal_value["proposal_id"]).encode("utf-8"),
                str(proposal_value["packet_ref"]).encode("utf-8"),
                int(proposal_value["base_state_version"]),
                int(proposal_value["context_hint_state_version"]),
                str(proposal_value["packet_source_generation"]).encode("utf-8"),
                int(proposal_value["packet_source_sequence"]), proposal_json)
        provenance_value = delta.get("evidence_provenance")
        provenance_input = None
        provenance_keepalive = []
        if provenance_value is not None:
            def optional_timestamp(key):
                value = provenance_value.get(key)
                if value is None:
                    return None
                if not isinstance(value, str) or not value:
                    raise PacketRejected("EVIDENCE_PROVENANCE_TIMESTAMP_INVALID")
                encoded = value.encode("utf-8")
                provenance_keepalive.append(encoded)
                return encoded
            evidence_id = provenance_value.get("evidence_id")
            if not isinstance(evidence_id, str) or not evidence_id:
                raise PacketRejected("EVIDENCE_PROVENANCE_ID_REQUIRED")
            evidence_id_bytes = evidence_id.encode("utf-8")
            provenance_keepalive.append(evidence_id_bytes)
            provenance_input = _EvidenceProvenance(
                evidence_id_bytes, optional_timestamp("observed_at"),
                optional_timestamp("source_published_at"),
                optional_timestamp("source_retrieved_at"),
                int(provenance_value.get("revision", 1)))
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        status, error = ctypes.create_string_buffer(128), ctypes.create_string_buffer(1024)
        ok = self._lib.surge_packet_core_admit_and_apply_with_metadata(
            self._handle, ctypes.byref(packet_input), snapshot_buffer, len(source),
            str(delta["delta_id"]).encode(), str(delta["idempotency_key"]).encode(),
            int(delta["expected_state_version"]), node_array, len(nodes),
            relation_array, len(relations), scope_array, len(scopes),
            focus_cluster, focus_interest,
            ctypes.byref(proposal_input) if proposal_input is not None else None,
            ctypes.byref(provenance_input) if provenance_input is not None else None,
            ctypes.byref(duplicate), ctypes.byref(version), status, len(status), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "ATOMIC_SEMANTIC_COMMIT_FAILED")
        range_key = "|".join((packet["context_id"], packet["source_id"],
            pos["source_generation"], pos["block_ref"], pos["offset_basis"],
            str(pos["start_offset"]), str(pos["end_offset"])))
        admission = Admission(status=status.value.decode("utf-8") or "DUPLICATE",
            packet_id=packet["packet_id"], context_id=packet["context_id"],
            source_id=packet["source_id"], source_generation=pos["source_generation"],
            source_sequence=int(pos["source_sequence"]), range_key=range_key,
            range_digest=range_digest, canonical_request_digest=request_digest)
        return admission, {"committed": True, "duplicate": bool(duplicate.value),
                           "state_version": int(version.value)}


    def get_evidence_provenance(self, evidence_id: str) -> dict[str, Any]:
        """Load the append-only provenance history for one Evidence node."""
        if not isinstance(evidence_id, str) or not evidence_id:
            raise PacketRejected("EVIDENCE_PROVENANCE_ID_REQUIRED")
        required = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(256)
        fn = self._lib.surge_evidence_provenance_get
        ok = fn(self._handle, evidence_id.encode("utf-8"), None, 0,
                ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != \
                "EVIDENCE_PROVENANCE_BUFFER_TOO_SMALL":
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "EVIDENCE_PROVENANCE_SIZE_QUERY_FAILED")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(256)
        ok = fn(self._handle, evidence_id.encode("utf-8"), output, len(output),
                ctypes.byref(required), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "EVIDENCE_PROVENANCE_READ_FAILED")
        try:
            value = json.loads(output.value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("EVIDENCE_PROVENANCE_JSON_INVALID") from exc
        if not isinstance(value, dict) or value.get("evidence_id") != evidence_id:
            raise PacketRejected("EVIDENCE_PROVENANCE_RESPONSE_INVALID")
        return value


    def get_classification_proposal(self, proposal_id: str) -> dict[str, Any]:
        """Load a committed ClassificationProposal from EventJournal after restart."""
        if not isinstance(proposal_id, str) or not proposal_id:
            raise PacketRejected("CLASSIFICATION_PROPOSAL_ID_REQUIRED")
        required = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(256)
        fn = self._lib.surge_classification_proposal_get
        ok = fn(self._handle, proposal_id.encode("utf-8"), None, 0,
                ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != \
                "CLASSIFICATION_PROPOSAL_BUFFER_TOO_SMALL":
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CLASSIFICATION_PROPOSAL_SIZE_QUERY_FAILED")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(256)
        ok = fn(self._handle, proposal_id.encode("utf-8"), output, len(output),
                ctypes.byref(required), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CLASSIFICATION_PROPOSAL_READ_FAILED")
        try:
            proposal = json.loads(output.value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("CLASSIFICATION_PROPOSAL_JSON_INVALID") from exc
        if not isinstance(proposal, dict) or proposal.get("proposal_id") != proposal_id:
            raise PacketRejected("CLASSIFICATION_PROPOSAL_RESPONSE_INVALID")
        return proposal


    def dependency_evaluation(self, delta_id: str) -> dict[str, Any]:
        """Return the durable dependency evaluation for one committed SemanticDelta."""
        required = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(256)
        fn = self._lib.surge_context_graph_dependency_evaluation
        ok = fn(self._handle, str(delta_id).encode("utf-8"), None, 0,
                ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != \
                "DEPENDENCY_EVALUATION_BUFFER_TOO_SMALL":
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "DEPENDENCY_EVALUATION_READ_FAILED")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(256)
        ok = fn(self._handle, str(delta_id).encode("utf-8"), output, len(output),
                ctypes.byref(required), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "DEPENDENCY_EVALUATION_READ_FAILED")
        try:
            value = json.loads(output.value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("DEPENDENCY_EVALUATION_JSON_INVALID") from exc
        if not isinstance(value, dict) or value.get("delta_id") != delta_id:
            raise PacketRejected("DEPENDENCY_EVALUATION_RESPONSE_INVALID")
        return value

    def apply_claim_validity(self, *, delta_id: str, idempotency_key: str,
                             expected_state_version: int, claim_id: str,
                             valid_from: str, valid_until: str | None,
                             basis_evidence_id: str, revision: int) -> dict[str, Any]:
        """Append a valid-time interval revision without overwriting prior history."""
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_context_graph_apply_claim_validity(
            self._handle, delta_id.encode("utf-8"), idempotency_key.encode("utf-8"),
            int(expected_state_version), claim_id.encode("utf-8"),
            valid_from.encode("utf-8"), (valid_until or "").encode("utf-8"),
            basis_evidence_id.encode("utf-8"), int(revision),
            ctypes.byref(duplicate), ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CLAIM_VALIDITY_REJECTED")
        return {"committed": True, "duplicate": bool(duplicate.value),
                "state_version": int(version.value)}

    def apply_approved_temporal_binding(self, binding: dict[str, Any], source_text: str, *,
                                        approved: bool, reviewer_id: str,
                                        review_evidence_id: str, delta_id: str,
                                        idempotency_key: str, expected_state_version: int,
                                        revision: int) -> dict[str, Any]:
        """Commit a reviewed temporal binding with durable review provenance."""
        import unicodedata
        if approved is not True:
            raise PacketRejected("CLAIM_VALIDITY_REVIEW_NOT_APPROVED")
        if not isinstance(binding, dict) or binding.get("binding_status") not in ("BOUND_PENDING_CORE_REVIEW", "CORRECTION_REVIEW_REQUIRED"):
            raise PacketRejected("TEMPORAL_BINDING_NOT_ELIGIBLE")
        if not isinstance(reviewer_id, str) or not reviewer_id.strip() or not isinstance(review_evidence_id, str) or not review_evidence_id.strip():
            raise PacketRejected("CLAIM_VALIDITY_REVIEW_PROVENANCE_REQUIRED")
        span = binding.get("source_span") or {}
        normalized = unicodedata.normalize("NFC", source_text).encode("utf-8")
        start, end = span.get("start_offset"), span.get("end_offset")
        if (span.get("offset_basis") != "UTF8_BYTE_NFC" or
                not isinstance(start, int) or isinstance(start, bool) or
                not isinstance(end, int) or isinstance(end, bool) or
                start < 0 or end <= start or end > len(normalized)):
            raise PacketRejected("CLAIM_VALIDITY_SOURCE_SPAN_INVALID")
        try:
            exact = normalized[start:end].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise PacketRejected("CLAIM_VALIDITY_SOURCE_SPAN_INVALID") from exc
        if exact != span.get("exact_text"):
            raise PacketRejected("CLAIM_VALIDITY_SOURCE_SPAN_MISMATCH")
        candidate = binding.get("temporal_candidate") or {}
        if (candidate.get("status") != "RESOLVED" or
                candidate.get("role") not in ("VALID_FROM", "INTERVAL") or
                not isinstance(candidate.get("valid_from"), str) or not candidate.get("valid_from")):
            raise PacketRejected("CLAIM_VALIDITY_CANDIDATE_NOT_COMMITTABLE")
        claim_id, evidence_id = binding.get("claim_ref"), binding.get("evidence_ref")
        if not isinstance(claim_id, str) or not claim_id or not isinstance(evidence_id, str) or not evidence_id:
            raise PacketRejected("CLAIM_VALIDITY_BINDING_REFERENCES_REQUIRED")
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_context_graph_apply_claim_validity_reviewed(
            self._handle, delta_id.encode("utf-8"), idempotency_key.encode("utf-8"),
            int(expected_state_version), claim_id.encode("utf-8"),
            candidate["valid_from"].encode("utf-8"),
            (candidate.get("valid_until") or "").encode("utf-8"),
            evidence_id.encode("utf-8"), int(revision),
            review_evidence_id.encode("utf-8"), reviewer_id.encode("utf-8"),
            ctypes.byref(duplicate), ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CLAIM_VALIDITY_REVIEWED_COMMIT_FAILED")
        return {"committed": True, "duplicate": bool(duplicate.value),
                "state_version": int(version.value), "claim_id": claim_id,
                "revision": int(revision), "review_evidence_id": review_evidence_id,
                "reviewer_id": reviewer_id}

    def claim_validity_at(self, claim_id: str, instant: str) -> str:
        """Evaluate the latest valid-time revision at an explicit RFC3339 instant."""
        output = ctypes.create_string_buffer(64)
        error = ctypes.create_string_buffer(256)
        ok = self._lib.surge_context_graph_claim_validity_at(
            self._handle, claim_id.encode("utf-8"), instant.encode("utf-8"),
            output, len(output), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "CLAIM_VALIDITY_QUERY_FAILED")
        return output.value.decode("ascii")

    def apply_semantic_delta(self, delta: dict[str, Any]) -> dict[str, Any]:
        required = {"delta_id", "idempotency_key", "expected_state_version",
                    "nodes", "relations"}
        if not isinstance(delta, dict) or not required.issubset(delta):
            raise PacketRejected("SEMANTIC_DELTA_SHAPE_INVALID")
        # Core checks idempotency before version conflicts, allowing exact retries.
        keepalive: list[Any] = []
        nodes = []
        for item in delta["nodes"]:
            refs = [str(v).encode("utf-8") for v in item.get("references", [])]
            ref_array = (ctypes.c_char_p * len(refs))(*refs) if refs else None
            if ref_array is not None:
                keepalive.append(ref_array)
            nodes.append(_Node(str(item["id"]).encode(), str(item["kind"]).encode(),
                               str(item["content"]).encode(),
                               ctypes.cast(ref_array, ctypes.POINTER(ctypes.c_char_p)) if ref_array is not None else None,
                               len(refs)))
        node_array = (_Node * len(nodes))(*nodes) if nodes else None
        relations = [_Relation(str(r["id"]).encode(), str(r["kind"]).encode(),
                               str(r["from"]).encode(), str(r["to"]).encode())
                     for r in delta["relations"]]
        relation_array = (_Relation * len(relations))(*relations) if relations else None
        scopes = []
        for item in delta.get("scope_spans", []):
            def anchor(value):
                return _Anchor(str(value["anchor_id"]).encode(),
                               str(value["source_digest"]).encode(),
                               str(value["offset_basis"]).encode(),
                               int(value["start_offset"]), int(value["end_offset"]),
                               str(value["exact_text"]).encode(),
                               str(value["packet_ref"]).encode())
            opener = anchor(item["opener"])
            closer_value = item.get("closer")
            closer = anchor(closer_value) if closer_value else _Anchor()
            target = [str(v).encode() for v in item.get("target_refs", [])]
            antecedents = [str(v).encode() for v in item.get("antecedent_refs", [])]
            packets = [str(v).encode() for v in item.get("packet_refs", [])]
            target_array = (ctypes.c_char_p * len(target))(*target) if target else None
            antecedent_array = (ctypes.c_char_p * len(antecedents))(*antecedents) if antecedents else None
            packet_array = (ctypes.c_char_p * len(packets))(*packets) if packets else None
            keepalive.extend([x for x in (target_array, antecedent_array, packet_array) if x is not None])
            scopes.append(_Scope(str(item["span_id"]).encode(), str(item["kind"]).encode(),
                str(item["source_digest"]).encode(), str(item["offset_basis"]).encode(),
                opener, 1 if closer_value else 0, closer,
                ctypes.cast(target_array, ctypes.POINTER(ctypes.c_char_p)) if target_array is not None else None,
                len(target), ctypes.cast(antecedent_array, ctypes.POINTER(ctypes.c_char_p)) if antecedent_array is not None else None,
                len(antecedents), str(item.get("attribution_ref", "")).encode(),
                str(item.get("modality_kind", "")).encode(), str(item.get("temporal_role", "")).encode(),
                str(item.get("parent_ref", "")).encode(),
                ctypes.cast(packet_array, ctypes.POINTER(ctypes.c_char_p)) if packet_array is not None else None,
                len(packets)))
        scope_array = (_Scope * len(scopes))(*scopes) if scopes else None
        source_snapshot = normalize_source(str(delta.get("source_snapshot", "")))
        focus = delta.get("focus")
        focus_cluster = str(focus["cluster_id"]).encode() if focus else None
        focus_interest = str(focus.get("interest_id", "")).encode() if focus else None
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(1024)
        ok = self._lib.surge_context_graph_apply_scoped(
            self._handle, str(delta["delta_id"]).encode(),
            str(delta["idempotency_key"]).encode(),
            int(delta["expected_state_version"]),
            node_array, len(nodes), relation_array, len(relations),
            scope_array, len(scopes), source_snapshot, len(source_snapshot),
            focus_cluster, focus_interest, ctypes.byref(duplicate),
            ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "SEMANTIC_DELTA_REJECTED")
        return {"committed": True, "duplicate": bool(duplicate.value),
                "state_version": int(version.value)}


    @staticmethod
    def _decode_temporal_ledger_json(payload: bytes) -> dict[str, Any]:
        try:
            value = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("TEMPORAL_LEDGER_RESPONSE_INVALID") from exc
        if not isinstance(value, dict):
            raise PacketRejected("TEMPORAL_LEDGER_RESPONSE_INVALID")
        return value

    def _temporal_ledger_call(self, function, args: tuple[Any, ...],
                              buffer_error: str = "TEMPORAL_LEDGER_BUFFER_TOO_SMALL") -> dict[str, Any]:
        required = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(512)
        ok = function(*args, None, 0, ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != buffer_error:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "TEMPORAL_LEDGER_READ_FAILED")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(512)
        function(*args, output, len(output), ctypes.byref(required), error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if detail:
            raise PacketRejected(detail)
        return self._decode_temporal_ledger_json(output.value)

    def add_temporal_boundary(self, *, transaction_id: str, idempotency_key: str,
                              event_id: str, expected_state_version: int,
                              claim_id: str, role: str, valid_from: str = "",
                              valid_until: str = "", evidence_id: str,
                              packet_id: str) -> dict[str, Any]:
        if any(not isinstance(v, str) or not v.strip() for v in
               (transaction_id, idempotency_key, event_id, claim_id, role, evidence_id, packet_id)):
            raise PacketRejected("TEMPORAL_LEDGER_ARGUMENT_INVALID")
        args = (self._handle, transaction_id.encode(), idempotency_key.encode(),
                event_id.encode(), int(expected_state_version), claim_id.encode(),
                role.encode(), valid_from.encode(), valid_until.encode(),
                evidence_id.encode(), packet_id.encode())
        result = self._temporal_ledger_call(self._lib.surge_temporal_boundary_add, args)
        if not result.get("committed"):
            raise PacketRejected(result.get("status") or result.get("error") or
                                 "TEMPORAL_LEDGER_ADD_REJECTED")
        return result

    def get_temporal_boundary(self, claim_id: str) -> dict[str, Any] | None:
        if not isinstance(claim_id, str) or not claim_id.strip():
            raise PacketRejected("CLAIM_ID_REQUIRED")
        args = (self._handle, claim_id.encode())
        result = self._temporal_ledger_call(self._lib.surge_temporal_boundary_get, args)
        return result.get("pending") if result.get("found") else None

    def resolve_temporal_boundary_candidate(self, *, transaction_id: str, idempotency_key: str,
            event_id: str, expected_state_version: int, claim_id: str, role: str,
            candidate_id: str) -> dict[str, Any]:
        if any(not isinstance(v, str) or not v.strip() for v in
               (transaction_id, idempotency_key, event_id, claim_id, role, candidate_id)):
            raise PacketRejected("TEMPORAL_LEDGER_ARGUMENT_INVALID")
        args = (self._handle, transaction_id.encode(), idempotency_key.encode(),
                event_id.encode(), int(expected_state_version), claim_id.encode(),
                role.encode(), candidate_id.encode())
        result = self._temporal_ledger_call(self._lib.surge_temporal_boundary_resolve_candidate, args)
        if not result.get("committed"):
            raise PacketRejected(result.get("status") or result.get("error") or "TEMPORAL_LEDGER_RESOLUTION_REJECTED")
        return result

    def clear_temporal_boundary(self, *, transaction_id: str, idempotency_key: str,
                                event_id: str, expected_state_version: int,
                                claim_id: str) -> dict[str, Any]:
        if any(not isinstance(v, str) or not v.strip() for v in
               (transaction_id, idempotency_key, event_id, claim_id)):
            raise PacketRejected("TEMPORAL_LEDGER_ARGUMENT_INVALID")
        args = (self._handle, transaction_id.encode(), idempotency_key.encode(),
                event_id.encode(), int(expected_state_version), claim_id.encode())
        result = self._temporal_ledger_call(self._lib.surge_temporal_boundary_clear, args)
        if not result.get("committed"):
            raise PacketRejected(result.get("status") or result.get("error") or
                                 "TEMPORAL_LEDGER_CLEAR_REJECTED")
        return result

    @property
    def temporal_boundary_pending_count(self) -> int:
        return int(self._lib.surge_temporal_boundary_pending_count(self._handle))

    def get_temporal_questions(self, claim_id: str) -> list[dict[str, Any]]:
        if not isinstance(claim_id, str) or not claim_id.strip():
            raise PacketRejected("CLAIM_ID_REQUIRED")
        result = self._temporal_ledger_call(
            self._lib.surge_temporal_question_get, (self._handle, claim_id.encode()),
            "TEMPORAL_QUESTION_BUFFER_TOO_SMALL")
        return result.get("questions", [])

    @property
    def temporal_question_count(self) -> int:
        return int(self._lib.surge_temporal_question_count(self._handle))

    def get_question_aspect(self, aspect_id: str) -> dict[str, Any]:
        """Return the durable lifecycle status for a QuestionAspect."""
        if not isinstance(aspect_id, str) or not aspect_id.strip():
            raise PacketRejected("QUESTION_ASPECT_ID_REQUIRED")
        return self._temporal_ledger_call(
            self._lib.surge_question_aspect_get_status,
            (self._handle, aspect_id.encode()),
            "QUESTION_ASPECT_BUFFER_TOO_SMALL")


    def get_question_aggregate(self, question_id: str) -> dict[str, Any]:
        """Return a read-only aggregate derived from all durable QuestionAspects."""
        if not isinstance(question_id, str) or not question_id.strip():
            raise PacketRejected("QUESTION_ID_REQUIRED")
        return self._temporal_ledger_call(
            self._lib.surge_question_get_aggregate_status,
            (self._handle, question_id.encode()),
            "QUESTION_AGGREGATE_BUFFER_TOO_SMALL")


    def get_question_memory_state(self, question_id: str) -> dict[str, Any]:
        if not isinstance(question_id,str) or not question_id.strip(): raise PacketRejected("QUESTION_ID_REQUIRED")
        return self._temporal_ledger_call(self._lib.surge_question_memory_get_state,(self._handle,question_id.encode()),"QUESTION_MEMORY_BUFFER_TOO_SMALL")

    def record_question_reference(self, question_id: str, expected_state_version: int | None = None) -> dict[str, Any]:
        """Persist a reference and reactivate a dormant Question; archived Questions stay archived."""
        if not isinstance(question_id, str) or not question_id.strip():
            raise PacketRejected("QUESTION_ID_REQUIRED")
        expected = self.state_version if expected_state_version is None else int(expected_state_version)
        reactivated = ctypes.c_int(0)
        count = ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_question_memory_record_reference(
            self._handle, question_id.encode("utf-8"), expected,
            ctypes.byref(reactivated), ctypes.byref(count), error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if not ok or detail:
            raise PacketRejected(detail or "QUESTION_REFERENCE_REJECTED")
        return {"question_id": question_id, "reference_count": count.value,
                "reactivated": bool(reactivated.value), "memory_state": "ACTIVE",
                "state_version": self.state_version}

    def record_question_reference_with_dependents(self, question_id: str,
                                                  expected_state_version: int | None = None,
                                                  max_depth: int = 8, max_nodes: int = 256) -> dict[str, Any]:
        """Record a reference and atomically reactivate dormant dependent Questions."""
        if not isinstance(question_id, str) or not question_id.strip():
            raise PacketRejected("QUESTION_ID_REQUIRED")
        if not isinstance(max_depth, int) or isinstance(max_depth, bool) or not 1 <= max_depth <= 64:
            raise PacketRejected("QUESTION_REACTIVATION_DEPTH_INVALID")
        if not isinstance(max_nodes, int) or isinstance(max_nodes, bool) or not 1 <= max_nodes <= 4096:
            raise PacketRejected("QUESTION_REACTIVATION_NODE_LIMIT_INVALID")
        expected = self.state_version if expected_state_version is None else int(expected_state_version)
        root_reactivated = ctypes.c_int(0)
        dependent_count = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_question_memory_record_reference_with_dependents(
            self._handle, question_id.encode("utf-8"), expected, max_depth, max_nodes,
            ctypes.byref(root_reactivated), ctypes.byref(dependent_count), error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if not ok or detail:
            raise PacketRejected(detail or "QUESTION_DEPENDENCY_REACTIVATION_REJECTED")
        return {"question_id": question_id, "root_reactivated": bool(root_reactivated.value),
                "reactivated_dependent_count": dependent_count.value,
                "state_version": self.state_version}

    def set_question_memory_state(self, question_id: str, state: str, expected_state_version: int | None = None) -> int:
        if not isinstance(question_id,str) or not question_id.strip(): raise PacketRejected("QUESTION_ID_REQUIRED")
        if state not in ("ACTIVE","DORMANT","ARCHIVED"): raise PacketRejected("QUESTION_MEMORY_STATE_INVALID")
        expected=self.state_version if expected_state_version is None else int(expected_state_version); error=ctypes.create_string_buffer(512)
        ok=self._lib.surge_question_memory_set_state(self._handle,question_id.encode(),state.encode(),expected,error,len(error)); detail=error.value.decode("utf-8",errors="replace")
        if not ok or detail: raise PacketRejected(detail or "QUESTION_MEMORY_STATE_UPDATE_REJECTED")
        return self.state_version

    def recommend_question_memory(self, contradiction_strength: float, focus_relevance: float, unresolved_importance: float, recency: float, recurrence: float, dormant_threshold: float=0.35) -> dict[str,Any]:
        values=(contradiction_strength,focus_relevance,unresolved_importance,recency,recurrence,dormant_threshold)
        if any(isinstance(v,bool) or not isinstance(v,(int,float)) or not 0<=v<=1 for v in values): raise PacketRejected("QUESTION_MEMORY_FACTOR_OUT_OF_RANGE")
        return self._temporal_ledger_call(self._lib.surge_question_memory_recommendation,values,"QUESTION_MEMORY_BUFFER_TOO_SMALL")

    def plan_question_memory_capacity(self, max_active: int) -> dict[str, Any]:
        """Build a read-only, explainable retention plan from durable Core graph state."""
        if not isinstance(max_active, int) or isinstance(max_active, bool) or not 0 <= max_active <= 4096:
            raise PacketRejected("QUESTION_MEMORY_CAPACITY_LIMIT")
        return self._temporal_ledger_call(
            self._lib.surge_question_memory_plan, (self._handle, max_active),
            "QUESTION_MEMORY_PLAN_BUFFER_TOO_SMALL")

    def enforce_question_memory_capacity(self, retention_scores: dict[str,float], max_active: int, expected_state_version: int | None = None) -> dict[str,Any]:
        if not isinstance(retention_scores,dict) or len(retention_scores)>4096: raise PacketRejected("QUESTION_MEMORY_CAPACITY_ARGUMENT_INVALID")
        if not isinstance(max_active,int) or isinstance(max_active,bool) or not 0<=max_active<=4096: raise PacketRejected("QUESTION_MEMORY_CAPACITY_LIMIT")
        items=sorted(retention_scores.items()); ids=[]; scores=[]
        for question_id,score in items:
            if not isinstance(question_id,str) or not question_id.strip() or isinstance(score,bool) or not isinstance(score,(int,float)) or not 0<=score<=1: raise PacketRejected("QUESTION_MEMORY_SCORE_INVALID")
            ids.append(question_id.encode("utf-8"));scores.append(float(score))
        id_array=(ctypes.c_char_p*len(ids))(*ids) if ids else None;score_array=(ctypes.c_double*len(scores))(*scores) if scores else None
        expected=self.state_version if expected_state_version is None else int(expected_state_version)
        output_capacity=128+sum(6*len(item)+16 for item in ids)
        output=ctypes.create_string_buffer(output_capacity);required=ctypes.c_size_t(0);error=ctypes.create_string_buffer(512)
        ok=self._lib.surge_question_memory_enforce_capacity(self._handle,id_array,score_array,len(ids),max_active,expected,output,len(output),ctypes.byref(required),error,len(error))
        detail=error.value.decode("utf-8",errors="replace")
        if not ok or detail: raise PacketRejected(detail or "QUESTION_MEMORY_CAPACITY_REJECTED")
        return self._decode_temporal_ledger_json(output.value)

    def get_question_context_bundle(self, question_id: str,
                                    max_depth: int = 8, max_nodes: int = 256) -> dict[str, Any]:
        """Read-only bounded restoration view of dependent Questions, aspects, Claims and Evidence."""
        if not isinstance(question_id, str) or not question_id.strip():
            raise PacketRejected("QUESTION_ID_REQUIRED")
        if not isinstance(max_depth, int) or isinstance(max_depth, bool) or not 1 <= max_depth <= 64:
            raise PacketRejected("QUESTION_CONTEXT_DEPTH_INVALID")
        if not isinstance(max_nodes, int) or isinstance(max_nodes, bool) or not 1 <= max_nodes <= 4096:
            raise PacketRejected("QUESTION_CONTEXT_NODE_LIMIT_INVALID")
        return self._temporal_ledger_call(
            self._lib.surge_question_get_context_bundle,
            (self._handle, question_id.encode(), max_depth, max_nodes),
            "QUESTION_CONTEXT_BUFFER_TOO_SMALL")

    def plan_question_reevaluation(self, question_id: str,
                                   max_depth: int = 8, max_nodes: int = 256) -> dict[str, Any]:
        """Build a bounded, read-only evidence-based QuestionAspect reevaluation proposal."""
        if not isinstance(question_id, str) or not question_id.strip():
            raise PacketRejected("QUESTION_ID_REQUIRED")
        if not isinstance(max_depth, int) or isinstance(max_depth, bool) or not 1 <= max_depth <= 64:
            raise PacketRejected("QUESTION_REEVALUATION_DEPTH_INVALID")
        if not isinstance(max_nodes, int) or isinstance(max_nodes, bool) or not 1 <= max_nodes <= 4096:
            raise PacketRejected("QUESTION_REEVALUATION_NODE_LIMIT_INVALID")
        return self._temporal_ledger_call(
            self._lib.surge_question_get_reevaluation_plan,
            (self._handle, question_id.encode(), max_depth, max_nodes),
            "QUESTION_REEVALUATION_PLAN_BUFFER_TOO_SMALL")

    def get_question_reevaluation_candidates(self, question_id: str,
                                             max_depth: int = 8,
                                             max_nodes: int = 256) -> dict[str, Any]:
        """Discover dependent Questions without changing their lifecycle state."""
        if not isinstance(question_id, str) or not question_id.strip():
            raise PacketRejected("QUESTION_ID_REQUIRED")
        if not isinstance(max_depth, int) or isinstance(max_depth, bool) or not 1 <= max_depth <= 64:
            raise PacketRejected("QUESTION_REEVALUATION_DEPTH_INVALID")
        if not isinstance(max_nodes, int) or isinstance(max_nodes, bool) or not 1 <= max_nodes <= 4096:
            raise PacketRejected("QUESTION_REEVALUATION_NODE_LIMIT_INVALID")
        return self._temporal_ledger_call(
            self._lib.surge_question_get_reevaluation_candidates,
            (self._handle, question_id.encode(), max_depth, max_nodes),
            "QUESTION_REEVALUATION_BUFFER_TOO_SMALL")

    def apply_question_reevaluation_plan(self, plan: dict[str, Any],
                                         aspect_ids: list[str] | None = None) -> dict[str, Any]:
        """Atomically apply selected proposals after Core revalidates current evidence."""
        if not isinstance(plan, dict) or not plan.get("read_only"):
            raise PacketRejected("QUESTION_REEVALUATION_PLAN_INVALID")
        expected = plan.get("state_version")
        if not isinstance(expected, int) or isinstance(expected, bool):
            raise PacketRejected("QUESTION_REEVALUATION_PLAN_VERSION_INVALID")
        proposals = plan.get("aspect_proposals")
        if not isinstance(proposals, list) or not proposals:
            raise PacketRejected("QUESTION_REEVALUATION_PLAN_EMPTY")
        selected = set(aspect_ids) if aspect_ids is not None else None
        updates, seen = [], set()
        for proposal in proposals:
            if not isinstance(proposal, dict):
                raise PacketRejected("QUESTION_REEVALUATION_PROPOSAL_INVALID")
            aspect_id, status = proposal.get("aspect_id"), proposal.get("proposed_status")
            if not isinstance(aspect_id, str) or not aspect_id or aspect_id in seen:
                raise PacketRejected("QUESTION_REEVALUATION_PROPOSAL_INVALID")
            seen.add(aspect_id)
            if selected is not None and aspect_id not in selected:
                continue
            if status not in ("OPEN", "RESOLVED", "DEFERRED"):
                raise PacketRejected("QUESTION_REEVALUATION_PROPOSAL_INVALID")
            updates.append((aspect_id, status))
        if selected is not None and selected - seen:
            raise PacketRejected("QUESTION_REEVALUATION_ASPECT_NOT_IN_PLAN")
        if not updates:
            raise PacketRejected("QUESTION_REEVALUATION_PLAN_EMPTY")
        ids = (ctypes.c_char_p * len(updates))(*(x[0].encode("utf-8") for x in updates))
        statuses = (ctypes.c_char_p * len(updates))(*(x[1].encode("ascii") for x in updates))
        error = ctypes.create_string_buffer(512)
        committed_version = ctypes.c_uint64(0)
        ok = self._lib.surge_question_aspect_set_status_batch(
            self._handle, ids, statuses, len(updates), expected,
            ctypes.byref(committed_version), error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if not ok or detail:
            raise PacketRejected(detail or "QUESTION_REEVALUATION_APPLY_REJECTED")
        return {"applied_aspect_ids": [x[0] for x in updates],
                "state_version": committed_version.value, "atomic": True}

    def set_question_aspect_status(self, aspect_id: str, status: str,
                                   expected_state_version: int | None = None) -> int:
        """Set OPEN/RESOLVED/DEFERRED using optimistic state-version control."""
        if not isinstance(aspect_id, str) or not aspect_id.strip():
            raise PacketRejected("QUESTION_ASPECT_ID_REQUIRED")
        if status not in ("OPEN", "RESOLVED", "DEFERRED"):
            raise PacketRejected("QUESTION_ASPECT_STATUS_INVALID")
        expected = self.state_version if expected_state_version is None else int(expected_state_version)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_question_aspect_set_status(
            self._handle, aspect_id.encode(), status.encode(), expected, error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if not ok or detail:
            raise PacketRejected(detail or "QUESTION_ASPECT_STATUS_UPDATE_REJECTED")
        return self.state_version

    def route_contradiction(self, relation_id: str,
                            policy_version: str = "question-router-v1") -> dict[str, Any]:
        """Create or retrieve the durable Question route for an accepted contradiction."""
        if not isinstance(relation_id, str) or not relation_id.strip():
            raise PacketRejected("CONTRADICTION_RELATION_ID_REQUIRED")
        if not isinstance(policy_version, str) or not policy_version.strip():
            raise PacketRejected("QUESTION_POLICY_VERSION_REQUIRED")
        before_version = self.state_version
        required = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(256)
        fn = self._lib.surge_question_route_contradiction
        ok = fn(self._handle, relation_id.encode("utf-8"), policy_version.encode("utf-8"),
                None, 0, ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != "QUESTION_ROUTE_BUFFER_TOO_SMALL":
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "QUESTION_ROUTE_READ_FAILED")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(256)
        ok = fn(self._handle, relation_id.encode("utf-8"), policy_version.encode("utf-8"),
                output, len(output), ctypes.byref(required), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "QUESTION_ROUTE_FAILED")
        result = json.loads(output.value.decode("utf-8"))
        # The sizing probe may have committed the route; report whether it
        # existed before this method call rather than the retry's duplicate bit.
        if int(result.get("state_version", before_version)) > before_version:
            result["duplicate"] = False
        return result

    def route_contradiction_scoped(self, relation_id: str, scope_key: str,
                                   policy_version: str = "question-router-v2") -> dict[str, Any]:
        """Group contradictions by unordered Claim pair and explicit scope; retain each event as an aspect."""
        if not isinstance(relation_id, str) or not relation_id.strip(): raise PacketRejected("CONTRADICTION_RELATION_ID_REQUIRED")
        if not isinstance(scope_key, str) or not scope_key.strip(): raise PacketRejected("QUESTION_SCOPE_KEY_REQUIRED")
        if not isinstance(policy_version, str) or not policy_version.strip(): raise PacketRejected("QUESTION_POLICY_VERSION_REQUIRED")
        before_version = self.state_version; required = ctypes.c_size_t(0); error = ctypes.create_string_buffer(256)
        fn = self._lib.surge_question_route_contradiction_scoped
        args = (self._handle, relation_id.encode("utf-8"), policy_version.encode("utf-8"), scope_key.encode("utf-8"))
        ok = fn(*args, None, 0, ctypes.byref(required), error, len(error))
        if ok or error.value.decode("utf-8", errors="replace") != "QUESTION_ROUTE_BUFFER_TOO_SMALL": raise PacketRejected(error.value.decode("utf-8", errors="replace") or "QUESTION_ROUTE_READ_FAILED")
        output = ctypes.create_string_buffer(required.value); error = ctypes.create_string_buffer(256)
        ok = fn(*args, output, len(output), ctypes.byref(required), error, len(error))
        if not ok: raise PacketRejected(error.value.decode("utf-8", errors="replace") or "QUESTION_ROUTE_FAILED")
        result = json.loads(output.value.decode("utf-8"))
        if int(result.get("state_version", before_version)) > before_version: result["duplicate"] = False
        return result
    @staticmethod
    def _chaos_provenance(value: dict[str, Any]) -> _ChaosProvenance:
        if not isinstance(value, dict):
            raise PacketRejected("CHAOS_PROVENANCE_REQUIRED")
        fields = ("producer_id", "producer_version", "policy_version", "recorded_at")
        encoded = []
        for field in fields:
            item = value.get(field)
            if not isinstance(item, str) or not item.strip():
                raise PacketRejected("CHAOS_PROVENANCE_INVALID")
            encoded.append(item.encode("utf-8"))
        return _ChaosProvenance(*encoded)

    def _chaos_mutation(self, function, args) -> dict[str, Any]:
        version = ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(512)
        ok = function(self._handle, *args, ctypes.byref(version), error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if not ok:
            raise PacketRejected(detail or "CHAOS_CLUSTER_MUTATION_REJECTED")
        return {"committed": True, "state_version": int(version.value)}

    def create_chaos_cluster(self, *, operation_id: str, idempotency_key: str,
                             expected_state_version: int, cluster_id: str,
                             provenance: dict[str, Any]) -> dict[str, Any]:
        """Create a durable open-world exploration cluster."""
        values = (operation_id, idempotency_key, cluster_id)
        if any(not isinstance(x, str) or not x.strip() for x in values):
            raise PacketRejected("CHAOS_CLUSTER_IDENTITY_REQUIRED")
        p = self._chaos_provenance(provenance)
        return self._chaos_mutation(self._lib.surge_chaos_cluster_create,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), ctypes.byref(p)))

    def admit_chaos_member(self, *, operation_id: str, idempotency_key: str,
                           expected_state_version: int, cluster_id: str,
                           member: dict[str, Any],
                           provenance: dict[str, Any]) -> dict[str, Any]:
        """Retain an immutable event reference without asserting a shared topic."""
        if not isinstance(member, dict):
            raise PacketRejected("CHAOS_MEMBER_INVALID")
        fields = ("member_ref", "event_ref", "admission_reason", "admitted_at")
        values = [member.get(k) for k in fields]
        if any(not isinstance(x, str) or not x.strip() for x in values):
            raise PacketRejected("CHAOS_MEMBER_INVALID")
        digest = member.get("source_digest")
        if digest is not None and not isinstance(digest, str):
            raise PacketRejected("CHAOS_MEMBER_INVALID")
        p = self._chaos_provenance(provenance)
        item = _ChaosMember(*(x.encode("utf-8") for x in values),
                            digest.encode("utf-8") if digest else None)
        return self._chaos_mutation(self._lib.surge_chaos_cluster_admit_member,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), ctypes.byref(item), ctypes.byref(p)))

    def propose_chaos_association(self, *, operation_id: str, idempotency_key: str,
                                  expected_state_version: int, cluster_id: str,
                                  association: dict[str, Any],
                                  provenance: dict[str, Any]) -> dict[str, Any]:
        """Add a provisional association; association never becomes a core relation implicitly."""
        if not isinstance(association, dict):
            raise PacketRejected("CHAOS_ASSOCIATION_INVALID")
        fields = ("association_id", "from_member_ref", "to_member_ref", "association_type")
        values = [association.get(k) for k in fields]
        if any(not isinstance(x, str) or not x.strip() for x in values):
            raise PacketRejected("CHAOS_ASSOCIATION_INVALID")
        valid_from = association.get("valid_from_utc") or ""
        valid_until = association.get("valid_until_utc") or ""
        for timestamp in (valid_from, valid_until):
            if timestamp and (not isinstance(timestamp, str) or len(timestamp) != 20
                              or timestamp[4] != "-" or timestamp[7] != "-"
                              or timestamp[10] != "T" or timestamp[13] != ":"
                              or timestamp[16] != ":" or timestamp[19] != "Z"):
                raise PacketRejected("CHAOS_ASSOCIATION_VALID_TIME_INVALID")
        if valid_from and valid_until and valid_from >= valid_until:
            raise PacketRejected("CHAOS_ASSOCIATION_VALID_TIME_INVALID")
        p = self._chaos_provenance(provenance)
        item = _ChaosAssociation(*(x.encode("utf-8") for x in values),
                                 valid_from.encode("ascii") if valid_from else None,
                                 valid_until.encode("ascii") if valid_until else None)
        return self._chaos_mutation(self._lib.surge_chaos_cluster_propose_association_temporal,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), ctypes.byref(item), ctypes.byref(p)))

    def merge_chaos_clusters(
        self, *, operation_id: str, idempotency_key: str,
        expected_state_version: int, target_cluster_id: str,
        source_cluster_ids: list[str], provenance: dict[str, Any],
    ) -> dict[str, Any]:
        """Atomically merge reviewable/dormant sources into a retained target."""
        if (not isinstance(source_cluster_ids, list) or not source_cluster_ids
                or len(source_cluster_ids) > 1000
                or any(not isinstance(item, str) or not item.strip() for item in source_cluster_ids)
                or len(set(source_cluster_ids)) != len(source_cluster_ids)):
            raise PacketRejected("CHAOS_MERGE_SOURCE_INVALID")
        if not isinstance(target_cluster_id, str) or not target_cluster_id.strip():
            raise PacketRejected("CHAOS_MERGE_TARGET_INVALID")
        refs = [item.encode("utf-8") for item in source_cluster_ids]
        array = (ctypes.c_char_p * len(refs))(*refs)
        p = self._chaos_provenance(provenance)
        return self._chaos_mutation(self._lib.surge_chaos_cluster_merge,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             target_cluster_id.encode(), array, len(refs), ctypes.byref(p)))

    def expire_due_chaos_associations(
        self, *, operation_id: str, idempotency_key: str,
        expected_state_version: int, cluster_id: str, as_of_utc: str,
        provenance: dict[str, Any],
    ) -> dict[str, Any]:
        """Expire nonterminal associations whose valid_until is due."""
        if (not isinstance(as_of_utc, str) or len(as_of_utc) != 20
                or as_of_utc[4:5] != "-" or as_of_utc[7:8] != "-"
                or as_of_utc[10:11] != "T" or as_of_utc[13:14] != ":"
                or as_of_utc[16:17] != ":" or not as_of_utc.endswith("Z")):
            raise PacketRejected("CHAOS_ASSOCIATION_EXPIRY_INPUT_INVALID")
        p = self._chaos_provenance(provenance)
        return self._chaos_mutation(self._lib.surge_chaos_cluster_expire_due_associations,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), as_of_utc.encode("ascii"), ctypes.byref(p)))

    def revise_chaos_membership(
        self, *, operation_id: str, idempotency_key: str,
        expected_state_version: int, cluster_id: str, member_ref: str,
        status: str = "UNCLASSIFIED", active: bool = True,
        provenance: dict[str, Any],
    ) -> dict[str, Any]:
        """Append a membership status or unmembership/reinstatement revision."""
        if status not in {"UNCLASSIFIED", "PROVISIONAL", "REVIEWED"}:
            raise PacketRejected("CHAOS_MEMBERSHIP_STATUS_INVALID")
        if not isinstance(active, bool):
            raise PacketRejected("CHAOS_MEMBERSHIP_ACTIVE_INVALID")
        identity = (operation_id, idempotency_key, cluster_id, member_ref)
        if any(not isinstance(item, str) or not item.strip() for item in identity):
            raise PacketRejected("CHAOS_MEMBERSHIP_IDENTITY_INVALID")
        p = self._chaos_provenance(provenance)
        return self._chaos_mutation(self._lib.surge_chaos_cluster_revise_membership,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), member_ref.encode(), status.encode("ascii"),
             1 if active else 0, ctypes.byref(p)))

    def review_chaos_pattern_hypothesis(
        self, *, operation_id: str, idempotency_key: str,
        expected_state_version: int, cluster_id: str, hypothesis_id: str,
        status: str, epistemic_state: str,
        exclusion_reasons: dict[str, str] | None = None,
        provenance: dict[str, Any],
    ) -> dict[str, Any]:
        """Append an auditable PatternHypothesis review with per-member reasons."""
        allowed_status = {"PROVISIONAL", "REVIEWABLE", "REJECTED", "PROMOTED_TO_EVIDENCE_REVIEW"}
        allowed_epistemic = {"HYPOTHETICAL", "UNVERIFIED", "CONTRADICTED", "REJECTED"}
        if status not in allowed_status or epistemic_state not in allowed_epistemic:
            raise PacketRejected("PATTERN_HYPOTHESIS_REVIEW_INVALID")
        reasons = exclusion_reasons or {}
        if not isinstance(reasons, dict) or any(
            not isinstance(ref, str) or not ref or not isinstance(reason, str) or not reason.strip()
            for ref, reason in reasons.items()
        ):
            raise PacketRejected("PATTERN_HYPOTHESIS_EXCLUSION_REASON_INVALID")
        refs = [ref.encode("utf-8") for ref in reasons]
        values = [reasons[ref].encode("utf-8") for ref in reasons]
        ref_array = (ctypes.c_char_p * len(refs))(*refs) if refs else None
        value_array = (ctypes.c_char_p * len(values))(*values) if values else None
        item = _ChaosHypothesisReview(status.encode("ascii"), epistemic_state.encode("ascii"),
            ctypes.cast(ref_array, ctypes.POINTER(ctypes.c_char_p)) if ref_array is not None else None,
            ctypes.cast(value_array, ctypes.POINTER(ctypes.c_char_p)) if value_array is not None else None,
            len(refs))
        p = self._chaos_provenance(provenance)
        return self._chaos_mutation(self._lib.surge_chaos_cluster_review_pattern_hypothesis,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), hypothesis_id.encode(), ctypes.byref(item), ctypes.byref(p)))

    def record_chaos_entity_observation(
        self, *, operation_id: str, idempotency_key: str,
        expected_state_version: int, cluster_id: str, entity_kind: str,
        entity_ref: str, observation: dict[str, Any],
    ) -> dict[str, Any]:
        """Append member salience, association confidence or hypothesis confidence."""
        dimensions = {"MEMBER": "salience", "ASSOCIATION": "confidence",
                      "HYPOTHESIS": "confidence"}
        if entity_kind not in dimensions or not isinstance(observation, dict):
            raise PacketRejected("CHAOS_SCORE_ENTITY_INVALID")
        value = observation.get("value")
        import math
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(float(value)) or not 0 <= float(value) <= 1:
            raise PacketRejected("CHAOS_SCORE_OBSERVATION_INVALID")
        fields = ("scorer_id", "scorer_version", "policy_version", "observed_at")
        metadata = [observation.get(name) for name in fields]
        if any(not isinstance(item, str) or not item.strip() for item in metadata):
            raise PacketRejected("CHAOS_SCORE_OBSERVATION_INVALID")
        identity = (operation_id, idempotency_key, cluster_id, entity_ref)
        if any(not isinstance(item, str) or not item.strip() for item in identity):
            raise PacketRejected("CHAOS_SCORE_OBSERVATION_IDENTITY_INVALID")
        item = _ChaosScoreObservation(float(value), *(item.encode("utf-8") for item in metadata))
        return self._chaos_mutation(self._lib.surge_chaos_cluster_record_entity_observation,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), entity_kind.encode("ascii"), entity_ref.encode(),
             ctypes.byref(item)))

    def record_chaos_score_observation(
        self, *, operation_id: str, idempotency_key: str,
        expected_state_version: int, cluster_id: str, dimension: str,
        observation: dict[str, Any],
    ) -> dict[str, Any]:
        """Append a scorer-bound score observation without collapsing UNKNOWN to zero."""
        allowed = {"organization", "salience", "recognition_confidence", "novelty",
                   "diversity", "ambiguity", "utility"}
        if dimension not in allowed or not isinstance(observation, dict):
            raise PacketRejected("CHAOS_SCORE_OBSERVATION_INVALID")
        value = observation.get("value")
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise PacketRejected("CHAOS_SCORE_OBSERVATION_INVALID")
        import math
        if not math.isfinite(float(value)) or not 0.0 <= float(value) <= 1.0:
            raise PacketRejected("CHAOS_SCORE_OBSERVATION_INVALID")
        fields = ("scorer_id", "scorer_version", "policy_version", "observed_at")
        metadata = [observation.get(name) for name in fields]
        if any(not isinstance(item, str) or not item.strip() for item in metadata):
            raise PacketRejected("CHAOS_SCORE_OBSERVATION_INVALID")
        identity = (operation_id, idempotency_key, cluster_id)
        if any(not isinstance(item, str) or not item.strip() for item in identity):
            raise PacketRejected("CHAOS_SCORE_OBSERVATION_IDENTITY_INVALID")
        item = _ChaosScoreObservation(float(value), *(item.encode("utf-8") for item in metadata))
        return self._chaos_mutation(self._lib.surge_chaos_cluster_record_observation,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), dimension.encode("ascii"), ctypes.byref(item)))

    def transition_chaos_cluster(self, *, operation_id: str, idempotency_key: str,
                                 expected_state_version: int, cluster_id: str,
                                 target_lifecycle: str, reason: str,
                                 provenance: dict[str, Any]) -> dict[str, Any]:
        """Apply a version-pinned lifecycle transition."""
        if not all(isinstance(x, str) and x.strip() for x in
                   (operation_id, idempotency_key, cluster_id, target_lifecycle, reason)):
            raise PacketRejected("CHAOS_TRANSITION_INVALID")
        p = self._chaos_provenance(provenance)
        return self._chaos_mutation(self._lib.surge_chaos_cluster_transition,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), target_lifecycle.encode(), reason.encode(), ctypes.byref(p)))

    def promote_chaos_subset(self, *, operation_id: str, idempotency_key: str,
                             expected_state_version: int, cluster_id: str,
                             member_targets: dict[str, str],
                             provenance: dict[str, Any]) -> dict[str, Any]:
        """Promote only explicitly selected members and preserve the residual cluster."""
        if not isinstance(member_targets, dict) or not member_targets or len(member_targets) > 4096:
            raise PacketRejected("CHAOS_PROMOTION_SELECTION_INVALID")
        if any(not isinstance(k, str) or not k.strip() or
               not isinstance(v, str) or not v.strip() for k, v in member_targets.items()):
            raise PacketRejected("CHAOS_PROMOTION_SELECTION_INVALID")
        p = self._chaos_provenance(provenance)
        refs = (ctypes.c_char_p * len(member_targets))(*(k.encode("utf-8") for k in member_targets))
        targets = (ctypes.c_char_p * len(member_targets))(*(v.encode("utf-8") for v in member_targets.values()))
        return self._chaos_mutation(self._lib.surge_chaos_cluster_promote_subset,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), refs, targets, len(member_targets), ctypes.byref(p)))

    def review_chaos_association(self, *, operation_id: str, idempotency_key: str,
                                 expected_state_version: int, cluster_id: str,
                                 association_id: str, status: str,
                                 provenance: dict[str, Any]) -> dict[str, Any]:
        """Append a provenance-bearing association status revision."""
        if status not in {"PROVISIONAL", "REVIEWED", "REJECTED", "EXPIRED"}:
            raise PacketRejected("CHAOS_ASSOCIATION_STATUS_INVALID")
        if not all(isinstance(x, str) and x.strip() for x in
                   (operation_id, idempotency_key, cluster_id, association_id)):
            raise PacketRejected("CHAOS_ASSOCIATION_REVIEW_ARGUMENT_INVALID")
        p = self._chaos_provenance(provenance)
        return self._chaos_mutation(self._lib.surge_chaos_cluster_review_association,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), association_id.encode(), status.encode(), ctypes.byref(p)))

    def add_chaos_pattern_hypothesis(self, *, operation_id: str, idempotency_key: str,
                                     expected_state_version: int, cluster_id: str,
                                     hypothesis: dict[str, Any],
                                     provenance: dict[str, Any]) -> dict[str, Any]:
        """Persist a hypothesis separately from Claims, retaining counterexamples and alternatives."""
        if not isinstance(hypothesis, dict):
            raise PacketRejected("CHAOS_PATTERN_HYPOTHESIS_INVALID")
        values = [hypothesis.get("hypothesis_id"), hypothesis.get("statement")]
        if any(not isinstance(x, str) or not x.strip() for x in values):
            raise PacketRejected("CHAOS_PATTERN_HYPOTHESIS_INVALID")
        names = ("member_refs", "excluded_member_refs", "counterexample_refs", "alternative_hypothesis_refs")
        vectors = []
        counts = []
        for name in names:
            value = hypothesis.get(name, [])
            if not isinstance(value, list) or len(value) > 10000 or any(not isinstance(x, str) or not x.strip() for x in value):
                raise PacketRejected("CHAOS_PATTERN_HYPOTHESIS_REFERENCES_INVALID")
            vectors.append((ctypes.c_char_p * len(value))(*(x.encode("utf-8") for x in value)) if value else None)
            counts.append(len(value))
        p = self._chaos_provenance(provenance)
        item = _ChaosPatternHypothesis(values[0].encode(), values[1].encode(),
            str(hypothesis.get("epistemic_state", "HYPOTHETICAL")).encode(),
            str(hypothesis.get("status", "PROVISIONAL")).encode(),
            vectors[0], counts[0], vectors[1], counts[1], vectors[2], counts[2], vectors[3], counts[3])
        return self._chaos_mutation(self._lib.surge_chaos_cluster_add_pattern_hypothesis,
            (operation_id.encode(), idempotency_key.encode(), int(expected_state_version),
             cluster_id.encode(), ctypes.byref(item), ctypes.byref(p)))

    def get_chaos_cluster(self, cluster_id: str, *, max_members: int = 1000,
                          max_associations: int = 1000, max_hypotheses: int = 256,
                          max_history_per_association: int = 100,
                          max_hypothesis_refs: int = 256) -> dict[str, Any]:
        """Read the authoritative Chaos ledger with explicit per-dimension budgets."""
        if not isinstance(cluster_id, str) or not cluster_id.strip():
            raise PacketRejected("CHAOS_CLUSTER_ID_REQUIRED")
        budgets = (max_members, max_associations, max_hypotheses,
                   max_history_per_association, max_hypothesis_refs)
        if any(isinstance(x, bool) or not isinstance(x, int) or x < 0 for x in budgets):
            raise PacketRejected("CHAOS_QUERY_BUDGET_INVALID")
        if max_members > 10000 or max_associations > 10000 or max_hypotheses > 1000 or max_history_per_association > 1000 or max_hypothesis_refs > 1000:
            raise PacketRejected("CHAOS_QUERY_BUDGET_INVALID")
        fn = self._lib.surge_chaos_cluster_get_bounded
        args = (self._handle, cluster_id.encode("utf-8"), *budgets)
        required, error = ctypes.c_size_t(0), ctypes.create_string_buffer(512)
        ok = fn(*args, None, 0, ctypes.byref(required), error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if ok or detail != "CHAOS_CLUSTER_BUFFER_TOO_SMALL":
            raise PacketRejected(detail or "CHAOS_CLUSTER_SIZE_QUERY_FAILED")
        if required.value <= 1 or required.value > 16 * 1024 * 1024:
            raise PacketRejected("CHAOS_CLUSTER_RESPONSE_TOO_LARGE")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(512)
        ok = fn(*args, output, len(output), ctypes.byref(required), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or "CHAOS_CLUSTER_READ_FAILED")
        try:
            value = json.loads(output.value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("CHAOS_CLUSTER_JSON_INVALID") from exc
        if (not isinstance(value, dict) or value.get("cluster_id") != cluster_id
                or not isinstance(value.get("complete"), bool) or not isinstance(value.get("omitted"), dict)):
            raise PacketRejected("CHAOS_CLUSTER_RESPONSE_INVALID")
        return value

    def set_chaos_focus(self, cluster_id: str, *, operation_id: str,
                        idempotency_key: str, expected_state_version: int | None = None,
                        interest_id: str | None = None) -> dict[str, Any]:
        """Focus a durable Chaos Cluster without converting it to an ordinary topic cluster."""
        if not isinstance(cluster_id, str) or not cluster_id.strip():
            raise PacketRejected("CHAOS_FOCUS_TARGET_REQUIRED")
        self.get_chaos_cluster(cluster_id)
        expected = self.state_version if expected_state_version is None else expected_state_version
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_context_graph_focus_set_chaos(
            self._handle, operation_id.encode(), idempotency_key.encode(), int(expected),
            cluster_id.encode(), (interest_id or "").encode(), ctypes.byref(duplicate),
            ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or "CHAOS_FOCUS_SET_FAILED")
        return {"committed": True, "duplicate": bool(duplicate.value),
                "state_version": int(version.value), "cluster_id": cluster_id,
                "target_kind": "CHAOS_CLUSTER"}

    def get_focus(self) -> dict[str, Any] | None:
        result = self._temporal_ledger_call(
            self._lib.surge_context_graph_focus_get, (self._handle,),
            "FOCUS_BUFFER_TOO_SMALL")
        return result if result.get("found") else None

    def observe_cognition(self) -> dict[str, Any]:
        """Return a detached, read-only snapshot of SURGE's committed cognition state."""
        fn = self._lib.surge_context_graph_observe_cognition
        fn.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        fn.restype = ctypes.c_int
        required, error = ctypes.c_size_t(0), ctypes.create_string_buffer(512)
        ok = fn(self._handle, None, 0, ctypes.byref(required), error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if ok or detail != "COGNITION_OBSERVATION_BUFFER_TOO_SMALL":
            raise PacketRejected(detail or "COGNITION_OBSERVATION_SIZE_QUERY_FAILED")
        if required.value <= 1 or required.value > 32 * 1024 * 1024:
            raise PacketRejected("COGNITION_OBSERVATION_RESPONSE_TOO_LARGE")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(512)
        ok = fn(self._handle, output, len(output), ctypes.byref(required), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "COGNITION_OBSERVATION_READ_FAILED")
        try:
            value = json.loads(output.value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("COGNITION_OBSERVATION_JSON_INVALID") from exc
        expected = {"state_version", "nodes", "relations", "memberships",
                    "unresolved_member_ids", "active_question_ids",
                    "active_interest_ids", "current_focus", "focus_history",
                    "unresolved_scope_count", "dependency_evaluation_count"}
        if not isinstance(value, dict) or set(value) != expected:
            raise PacketRejected("COGNITION_OBSERVATION_RESPONSE_INVALID")
        return value



    def recall_cognition_candidates(self, input_member_id: str,
                                    query_terms: list[str], max_candidates: int = 8) -> dict[str, Any]:
        """Retrieve ranked cognition candidates without changing graph membership."""
        if not isinstance(input_member_id, str) or not input_member_id:
            raise PacketRejected("COGNITION_RECALL_INPUT_MEMBER_REQUIRED")
        if (not isinstance(query_terms, list) or len(query_terms) > 4096
                or any(not isinstance(term, str) for term in query_terms)):
            raise PacketRejected("COGNITION_RECALL_TERMS_INVALID")
        if not isinstance(max_candidates, int) or not 0 <= max_candidates <= 1024:
            raise PacketRejected("COGNITION_RECALL_LIMIT_INVALID")
        fn = self._lib.surge_context_graph_recall_candidates
        fn.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char_p), ctypes.c_size_t, ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        fn.restype = ctypes.c_int
        encoded = [term.encode("utf-8") for term in query_terms]
        keepalive = (ctypes.c_char_p * len(encoded))(*encoded) if encoded else None
        required, error = ctypes.c_size_t(0), ctypes.create_string_buffer(512)
        ok = fn(self._handle, input_member_id.encode("utf-8"), keepalive,
                len(encoded), max_candidates, None, 0, ctypes.byref(required),
                error, len(error))
        detail = error.value.decode("utf-8", errors="replace")
        if ok or detail != "COGNITION_RECALL_BUFFER_TOO_SMALL":
            raise PacketRejected(detail or "COGNITION_RECALL_SIZE_QUERY_FAILED")
        if required.value <= 1 or required.value > 8 * 1024 * 1024:
            raise PacketRejected("COGNITION_RECALL_RESPONSE_TOO_LARGE")
        output = ctypes.create_string_buffer(required.value)
        error = ctypes.create_string_buffer(512)
        ok = fn(self._handle, input_member_id.encode("utf-8"), keepalive,
                len(encoded), max_candidates, output, len(output), ctypes.byref(required),
                error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "COGNITION_RECALL_FAILED")
        try:
            value = json.loads(output.value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("COGNITION_RECALL_JSON_INVALID") from exc
        if (not isinstance(value, dict) or set(value) != {"state_version", "candidates"}
                or not isinstance(value["candidates"], list)):
            raise PacketRejected("COGNITION_RECALL_RESPONSE_INVALID")
        for candidate in value["candidates"]:
            if (not isinstance(candidate, dict) or
                set(candidate) != {"cluster_id", "recall_score", "memory_state_version",
                                   "supporting_member_ids", "matched_terms"}):
                raise PacketRejected("COGNITION_RECALL_CANDIDATE_INVALID")
        return value

    def record_cognition_feedback(self, *, feedback_id: str, candidate_cluster_id: str,
                                  input_member_id: str, outcome: str, producer_id: str,
                                  producer_version: str, policy_version: str,
                                  recorded_at: str, feedback_node_id: str,
                                  relation_id: str, notes: str = "",
                                  expected_state_version: int | None = None) -> int:
        """Persist external candidate evaluation; return the committed state version."""
        values = (feedback_id, candidate_cluster_id, input_member_id, outcome,
                  producer_id, producer_version, policy_version, recorded_at,
                  feedback_node_id, relation_id, notes)
        if any(not isinstance(value, str) for value in values):
            raise PacketRejected("COGNITION_FEEDBACK_FIELDS_INVALID")
        expected = self.state_version if expected_state_version is None else expected_state_version
        fn = self._lib.surge_context_graph_record_cognition_feedback
        fn.argtypes = [
            ctypes.c_void_p, *([ctypes.c_char_p] * 9), ctypes.c_uint64,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        fn.restype = ctypes.c_int
        version, error = ctypes.c_uint64(0), ctypes.create_string_buffer(512)
        ok = fn(self._handle, feedback_id.encode(), candidate_cluster_id.encode(),
                input_member_id.encode(), outcome.encode(), producer_id.encode(),
                producer_version.encode(), policy_version.encode(), recorded_at.encode(),
                notes.encode(), int(expected), feedback_node_id.encode(), relation_id.encode(),
                ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "COGNITION_FEEDBACK_COMMIT_FAILED")
        return int(version.value)

    def commit_retention_events(self, transaction_id: str, idempotency_key: str,
                                expected_state_version: int,
                                events: list[dict[str, str]]) -> dict[str, Any]:
        """Append exactly one allowlisted retention event in one transaction."""
        if not isinstance(events, list) or len(events) != 1:
            raise PacketRejected("RETENTION_JOURNAL_EVENTS_INVALID")
        keepalive = []
        rows = []
        for event in events:
            if not isinstance(event, dict):
                raise PacketRejected("RETENTION_JOURNAL_EVENT_INVALID")
            try:
                encoded = [event[key].encode("utf-8") for key in ("event_id", "kind", "payload")]
            except (KeyError, AttributeError):
                raise PacketRejected("RETENTION_JOURNAL_EVENT_INVALID")
            keepalive.extend(encoded)
            rows.append(_JournalEvent(*encoded))
        array = (_JournalEvent * len(rows))(*rows)
        duplicate, version = ctypes.c_int(0), ctypes.c_uint64(0)
        error = ctypes.create_string_buffer(512)
        ok = self._lib.surge_retention_journal_commit(
            self._handle, transaction_id.encode("utf-8"), idempotency_key.encode("utf-8"),
            int(expected_state_version), array, len(array), ctypes.byref(duplicate),
            ctypes.byref(version), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "RETENTION_JOURNAL_COMMIT_FAILED")
        return {"committed": True, "duplicate": bool(duplicate.value),
                "state_version": int(version.value)}

    def get_retention_events(self) -> list[dict[str, str]]:
        """Replay only allowlisted retention events from the authoritative journal."""
        required = ctypes.c_size_t(0)
        error = ctypes.create_string_buffer(512)
        self._lib.surge_retention_journal_get_events(
            self._handle, None, 0, ctypes.byref(required), error, len(error))
        if required.value <= 1:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "RETENTION_JOURNAL_SIZE_QUERY_FAILED")
        output = ctypes.create_string_buffer(required.value)
        ok = self._lib.surge_retention_journal_get_events(
            self._handle, output, len(output), ctypes.byref(required), error, len(error))
        if not ok:
            raise PacketRejected(error.value.decode("utf-8", errors="replace") or
                                 "RETENTION_JOURNAL_READ_FAILED")
        try:
            value = json.loads(output.value.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PacketRejected("RETENTION_JOURNAL_JSON_INVALID") from exc
        if not isinstance(value, list) or any(
            not isinstance(item, dict) or set(item) != {"event_id", "kind", "payload"}
            for item in value
        ):
            raise PacketRejected("RETENTION_JOURNAL_RESPONSE_INVALID")
        return value
