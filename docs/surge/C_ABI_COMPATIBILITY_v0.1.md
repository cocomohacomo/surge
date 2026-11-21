# SURGE C ABI Compatibility Policy v0.1

## Version query

The public header `surge/c_api.h` defines:

- `SURGE_ABI_VERSION_MAJOR`
- `SURGE_ABI_VERSION_MINOR`
- `SURGE_ABI_VERSION_PATCH`
- `SURGE_ABI_VERSION_ENCODE(major, minor, patch)`

The shared library exports `surge_abi_version()`, which returns the encoded
runtime ABI version. Consumers should compare this value with the ABI level
against which they were built before using optional or version-specific APIs.

The ABI version is independent of the package version and CMake project
version. The current ABI is **0.1.0**. Major zero explicitly means the ABI is
provisional and does not yet carry a long-term stability guarantee.

## Compatibility rules

- **Major:** increment for an incompatible exported symbol, calling convention,
  ownership, layout, or behavioral contract change. The shared-library
  SOVERSION must track the ABI major.
- **Minor:** increment for additive ABI-compatible functions and types.
- **Patch:** increment for ABI-preserving corrections and clarifications.
- Existing exported function signatures and existing public structure layouts
  must not be changed within a compatible ABI line.
- New fields must not be appended to public structs passed by pointer unless
  the API first introduces an explicit structure-size/version negotiation
  mechanism. Prefer new versioned structs or new functions.
- A consumer must not assume that a newer minor ABI is available merely
  because the package version is newer.

## Current limitations

This policy establishes a queryable version identifier and compatibility
rules. It does not yet provide symbol visibility control, a complete exported
symbol manifest, ABI-diff automation, or a stable ABI promise. Those remain
release gates before declaring ABI 1.0 stable.
