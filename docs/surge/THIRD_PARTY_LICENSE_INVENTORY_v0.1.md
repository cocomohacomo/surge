# Third-party License Inventory v0.1

Date: 2026-11-21

This is the source-level dependency inventory for the current library shape.
The generated CycloneDX SBOM is the authoritative record of exact resolved
versions and package metadata for each CI wheel build.

## Direct Python runtime dependency

| Component | Constraint | Role | License evidence |
|---|---|---|---|
| jsonschema | `>=4.20,<5` | Validate SURGE input, proposal, and model schemas | Resolved license metadata is captured by the CI SBOM |

## Python build and audit tooling

| Component | Constraint | Role |
|---|---|---|
| setuptools | `>=68` | Build backend |
| wheel | build-system dependency | Wheel packaging |
| packaging | `>=24,<27` in development requirements | Parse environment markers and dependency metadata for SBOM generation |

## Native Core

- C++17 and the C++ standard library are used by the native Core.
- CMake is the build system.
- No third-party C++ runtime library is intentionally linked by the current
  Core target.
- The repository license is MIT.

## Audit limits and release rule

The runtime dependency graph can include transitive Python distributions.
Their exact versions, package URLs, and declared license metadata are emitted
by `tools/generate_sbom.py` into a CycloneDX 1.5 document retained as a CI
artifact named `surge-sbom`.

Package metadata is a declaration, not legal advice or proof of redistribution
rights. Before a binary or source release, review every resolved component's
license text, notices, source availability obligations, and any bundled
artifacts. Any component with missing or ambiguous license metadata must be
resolved manually before release. The SBOM does not replace that review.
