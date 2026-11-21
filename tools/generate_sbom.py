#!/usr/bin/env python3
"""Generate a CycloneDX 1.5 SBOM for the wheel and its active runtime dependencies."""
from __future__ import annotations
import argparse
import json
import re
import zipfile
from email.parser import Parser
from pathlib import Path
from importlib import metadata

from packaging.requirements import Requirement
from packaging.markers import default_environment


def normalized(name: str) -> str:
    return re.sub(r"[-_.]+", "-", name).lower()


def license_entries(meta) -> list[dict]:
    expression = meta.get("License-Expression")
    if expression and expression.strip():
        return [{"expression": expression.strip()}]
    value = meta.get("License")
    if value and value.strip() and value.strip().lower() not in {"unknown", "none"}:
        return [{"license": {"name": value.strip()}}]
    return [{"license": {"name": "UNKNOWN"}}]


def component(name: str, version: str, meta, purl_type: str = "pypi") -> dict:
    safe_name = normalized(name)
    return {
        "type": "library",
        "name": name,
        "version": version,
        "purl": f"pkg:{purl_type}/{safe_name}@{version}",
        "bom-ref": f"pkg:{purl_type}/{safe_name}@{version}",
        "licenses": license_entries(meta),
    }


def wheel_metadata(path: Path):
    with zipfile.ZipFile(path) as archive:
        candidates = [name for name in archive.namelist()
                      if name.endswith(".dist-info/METADATA")]
        if len(candidates) != 1:
            raise ValueError("WHEEL_METADATA_ENTRY_INVALID")
        return Parser().parsestr(archive.read(candidates[0]).decode("utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wheel", required=True, type=Path)
    parser.add_argument("--cmake", type=Path, default=Path("CMakeLists.txt"))
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not args.wheel.is_file():
        raise SystemExit("SBOM_WHEEL_NOT_FOUND")
    root_meta = wheel_metadata(args.wheel)
    root_name, root_version = root_meta.get("Name"), root_meta.get("Version")
    if not root_name or not root_version:
        raise SystemExit("SBOM_WHEEL_IDENTITY_MISSING")
    root = component(root_name, root_version, root_meta)

    environment = default_environment()
    queue: list[str] = []
    root_dependency_names: set[str] = set()
    for raw in root_meta.get_all("Requires-Dist", []):
        requirement = Requirement(raw)
        if requirement.marker is None or requirement.marker.evaluate(environment):
            queue.append(requirement.name)
            root_dependency_names.add(normalized(requirement.name))
    visited: dict[str, dict] = {}
    edges: dict[str, set[str]] = {root["bom-ref"]: set()}
    while queue:
        name = queue.pop(0)
        key = normalized(name)
        if key in visited:
            continue
        try:
            dist = metadata.distribution(name)
        except metadata.PackageNotFoundError as exc:
            raise SystemExit(f"SBOM_DEPENDENCY_NOT_INSTALLED:{name}") from exc
        item = component(dist.metadata.get("Name") or name, dist.version, dist.metadata)
        visited[key] = item
        edges[item["bom-ref"]] = set()
        for raw in dist.metadata.get_all("Requires-Dist", []):
            requirement = Requirement(raw)
            if requirement.marker is None or requirement.marker.evaluate(environment):
                dependency_key = normalized(requirement.name)
                dependency_ref = f"pkg:pypi/{dependency_key}@"
                edges[item["bom-ref"]].add(dependency_ref)
                queue.append(requirement.name)

    # Resolve dependency references to the concrete selected versions.
    by_name = {normalized(item["name"]): item for item in visited.values()}
    dependencies = []
    for ref, deps in edges.items():
        resolved = []
        for dependency_ref in deps:
            name = dependency_ref.split("/")[1].split("@")[0]
            if name not in by_name:
                raise SystemExit("SBOM_DEPENDENCY_GRAPH_INCOMPLETE:" + name)
            resolved.append(by_name[name]["bom-ref"])
        dependencies.append({"ref": ref, "dependsOn": sorted(resolved)})

    cmake_text = args.cmake.read_text(encoding="utf-8")
    version_match = re.search(r"project\(surge_core VERSION ([0-9.]+)", cmake_text)
    if not version_match:
        raise SystemExit("SBOM_NATIVE_CORE_VERSION_MISSING")
    core = {
        "type": "library",
        "name": "surge-core",
        "version": version_match.group(1),
        "purl": f"pkg:generic/surge-core@{version_match.group(1)}",
        "bom-ref": f"pkg:generic/surge-core@{version_match.group(1)}",
        "licenses": [{"expression": "MIT"}],
    }
    bom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "version": 1,
        "metadata": {"component": root},
        "components": sorted([*visited.values(), core], key=lambda item: item["bom-ref"]),
        "dependencies": sorted(
            [*dependencies, {"ref": root["bom-ref"], "dependsOn": sorted(
                by_name[name]["bom-ref"] for name in root_dependency_names)},
             {"ref": core["bom-ref"], "dependsOn": []}],
            key=lambda item: item["ref"],
        ),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(bom, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unknown = [item["name"] for item in [root, *visited.values()]
               if any(entry.get("license", {}).get("name") == "UNKNOWN"
                      for entry in item.get("licenses", []))]
    print(f"CycloneDX SBOM generated: {len(visited) + 2} components")
    print("LICENSE_METADATA_UNKNOWN:" + (",".join(sorted(unknown)) if unknown else "none"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
