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

The ABI version is independent of the Python package version. The CMake
shared-library `SOVERSION` is derived directly from
`SURGE_ABI_VERSION_MAJOR`, preventing the SONAME major from drifting away
from the public header. The current ABI is **0.1.0**. Major zero explicitly
means the ABI is provisional and does not yet carry a long-term stability
guarantee.

## Compatibility rules

- **Major:** increment for an incompatible exported symbol, calling convention,
  ownership, layout, or behavioral contract change.
- **Minor:** increment for additive ABI-compatible functions and types.
- **Patch:** increment for ABI-preserving corrections and clarifications.
- Existing exported function signatures and existing public structure layouts
  must not be changed within a compatible ABI line.
- New fields must not be appended to public structs passed by pointer unless
  the API first introduces an explicit structure-size/version negotiation
  mechanism. Prefer new versioned structs or new functions.
- A consumer must not assume that a newer minor ABI is available merely
  because the package version is newer.

## Automated checks

The current CI performs four separate checks:

1. `tools/check_c_abi.py` extracts the public function prototypes and public
   struct field declarations from `include/surge/c_api.h` and compares them
   with the reviewed current manifest `abi/c-abi-v0.1.json`.
2. `tools/check_abi_compatibility.py` compares the current declarations with
   immutable versioned baselines under `abi/compatibility-baselines/`.
   Removing/changing existing functions or changing existing struct layouts
   within the same ABI major is rejected. Additive declarations require a
   minor-version increase.
3. `tools/check_binary_abi.py` inspects the built shared library and verifies
   that every manifest function is actually exported on Linux, macOS and
   Windows.
4. `surge_c_abi_layout_tests` emits pointer width, `sizeof`, `alignof`, and
   every public struct field's offset and size. Verbose CI logs retain the
   observed layout for each supported OS.

The current reviewed surface contains 65 C ABI functions and 17 public
structures. CI validates the declaration and binary export surfaces on all
three operating systems.

## Remaining limitations

- The per-OS layout observations are not yet retained as immutable baselines
  across compiler versions, architectures, and future releases.
- The native library still exports additional C++ symbols according to the
  current platform build configuration. A deliberately restricted symbol
  visibility map is not yet in place.
- ABI major zero remains provisional. Passing these automated checks is not a
  declaration of ABI 1.0 stability or a substitute for consumer compatibility
  testing against released binaries.
