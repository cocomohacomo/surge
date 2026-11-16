#!/usr/bin/env python3
"""Verify GitHub public-release protection settings via the gh CLI.

Exit codes:
  0 = all required checks passed
  1 = one or more required checks failed
  2 = usage / tooling error
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from typing import Any


def run_gh(args: list[str]) -> tuple[int, str, str]:
    proc = subprocess.run(
        ["gh", *args],
        capture_output=True,
        text=True,
    )
    return proc.returncode, proc.stdout, proc.stderr


def gh_json(args: list[str]) -> Any:
    code, out, err = run_gh(args)
    if code != 0:
        raise RuntimeError(err.strip() or out.strip() or f"gh failed: {' '.join(args)}")
    return json.loads(out) if out.strip() else None


def check(name: str, ok: bool, detail: str, required: bool, rows: list[dict[str, Any]]) -> None:
    rows.append({"name": name, "ok": ok, "required": required, "detail": detail})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--owner", required=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument(
        "--required-check",
        action="append",
        default=[],
        dest="required_checks",
        help="Exact required status-check context/job name (repeatable)",
    )
    parser.add_argument(
        "--allow-extra-checks",
        action="store_true",
        help="Do not fail when ruleset requires checks beyond --required-check",
    )
    args = parser.parse_args()
    if not args.required_checks:
        print("error: pass at least one --required-check", file=sys.stderr)
        return 2

    slug = f"{args.owner}/{args.repo}"
    rows: list[dict[str, Any]] = []

    try:
        repo = gh_json(["api", f"repos/{slug}"])
    except RuntimeError as exc:
        print(f"error: cannot read repository: {exc}", file=sys.stderr)
        return 2

    visibility = (repo or {}).get("visibility")
    private = bool((repo or {}).get("private"))
    check(
        "visibility_public",
        (not private) and visibility == "public",
        f"visibility={visibility} private={private}",
        True,
        rows,
    )

    security = (repo or {}).get("security_and_analysis") or {}
    if not security:
        # Some tokens omit the block on REST; fall back to GraphQL-unrelated null.
        check(
            "security_and_analysis_readable",
            False,
            "security_and_analysis missing from REST payload; use a token that can read admin fields or inspect the UI",
            False,
            rows,
        )
    else:
        secret = ((security.get("secret_scanning") or {}).get("status")) == "enabled"
        push = ((security.get("secret_scanning_push_protection") or {}).get("status")) == "enabled"
        dependabot = ((security.get("dependabot_security_updates") or {}).get("status")) == "enabled"
        check("secret_scanning", secret, str((security.get("secret_scanning") or {})), True, rows)
        check("push_protection", push, str((security.get("secret_scanning_push_protection") or {})), True, rows)
        check("dependabot_security_updates", dependabot, str((security.get("dependabot_security_updates") or {})), False, rows)

    try:
        pvr = gh_json(["api", f"repos/{slug}/private-vulnerability-reporting"]) or {}
        check(
            "private_vulnerability_reporting",
            bool(pvr.get("enabled")),
            json.dumps(pvr),
            True,
            rows,
        )
    except RuntimeError as exc:
        check("private_vulnerability_reporting", False, str(exc), True, rows)

    try:
        gql = gh_json(
            [
                "api",
                "graphql",
                "-f",
                (
                    "query={repository(owner:\"%s\", name:\"%s\") "
                    "{hasVulnerabilityAlertsEnabled isSecurityPolicyEnabled securityPolicyUrl}}"
                    % (args.owner, args.repo)
                ),
            ]
        )
        node = ((gql or {}).get("data") or {}).get("repository") or {}
        check(
            "vulnerability_alerts",
            bool(node.get("hasVulnerabilityAlertsEnabled")),
            json.dumps(node),
            True,
            rows,
        )
        check(
            "security_policy",
            bool(node.get("isSecurityPolicyEnabled")),
            str(node.get("securityPolicyUrl")),
            True,
            rows,
        )
    except RuntimeError as exc:
        check("vulnerability_alerts", False, str(exc), True, rows)
        check("security_policy", False, str(exc), True, rows)

    default_branch = (repo or {}).get("default_branch") or "main"
    try:
        branch = gh_json(["api", f"repos/{slug}/branches/{default_branch}"]) or {}
        check(
            "default_branch_protected_flag",
            bool(branch.get("protected")),
            f"branch={default_branch} protected={branch.get('protected')}",
            True,
            rows,
        )
    except RuntimeError as exc:
        check("default_branch_protected_flag", False, str(exc), True, rows)

    required_found: set[str] = set()
    extra_checks: set[str] = set()
    has_pr_rule = False
    has_force_push_block = False
    has_deletion_block = False
    try:
        rules = gh_json(["api", f"repos/{slug}/rules/branches/{default_branch}"]) or []
        for rule in rules:
            rtype = rule.get("type")
            params = rule.get("parameters") or {}
            if rtype == "pull_request":
                has_pr_rule = True
            elif rtype == "non_fast_forward":
                has_force_push_block = True
            elif rtype == "deletion":
                has_deletion_block = True
            elif rtype == "required_status_checks":
                for item in params.get("required_status_checks") or []:
                    ctx = item.get("context")
                    if not ctx:
                        continue
                    if ctx in args.required_checks:
                        required_found.add(ctx)
                    else:
                        extra_checks.add(ctx)
            elif rtype == "code_scanning":
                extra_checks.add("code_scanning:" + json.dumps(params.get("code_scanning_tools")))
    except RuntimeError as exc:
        check("ruleset_readable", False, str(exc), True, rows)

    check("ruleset_require_pull_request", has_pr_rule, f"present={has_pr_rule}", True, rows)
    check("ruleset_block_force_push", has_force_push_block, f"present={has_force_push_block}", True, rows)
    check("ruleset_block_deletion", has_deletion_block, f"present={has_deletion_block}", True, rows)

    missing = [name for name in args.required_checks if name not in required_found]
    check(
        "ruleset_required_status_checks",
        not missing,
        f"found={sorted(required_found)} missing={missing} extras={sorted(extra_checks)}",
        True,
        rows,
    )
    if extra_checks and not args.allow_extra_checks:
        check(
            "ruleset_no_unexpected_required_checks",
            False,
            f"unexpected={sorted(extra_checks)}",
            True,
            rows,
        )
    elif extra_checks:
        check(
            "ruleset_no_unexpected_required_checks",
            True,
            f"allowed extras={sorted(extra_checks)}",
            False,
            rows,
        )

    width = max(len(r["name"]) for r in rows)
    failed = 0
    print(f"Repository: {slug}")
    for row in rows:
        mark = "PASS" if row["ok"] else ("FAIL" if row["required"] else "WARN")
        if row["required"] and not row["ok"]:
            failed += 1
        print(f"[{mark}] {row['name']:<{width}}  {row['detail']}")

    if failed:
        print(f"\n{failed} required check(s) failed")
        return 1
    print("\nAll required checks passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except json.JSONDecodeError as exc:
        print(f"error: invalid JSON from gh: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
