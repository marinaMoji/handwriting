#!/usr/bin/env python3
"""Licensing and packaging checks for handwriting models (standard library only, no network).

Always checked:
  - licence texts under docs/licensing/handwriting/licenses/ are present and unmodified;
  - the licensing documents and the pinned-source file are present and complete;
  - production paths (fetch script, CI, CMake install rules, application sources) do not
    reference Hanzi Writer data or the staged experimental package;
  - if production models are present locally, they are the Tegaki 0.3 files.

Checked when a staged package exists (experiments/hzw-package/stage/..., or the directory
given as the first argument or in HZW_PACKAGE_DIR): the full package verification of
scripts/build_hzw_model_package.py, and that release is not authorized.
"""
import hashlib
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIC = ROOT / "docs" / "licensing" / "handwriting"
sys.path.insert(0, str(ROOT / "scripts"))

import build_hzw_model_package as pkg  # noqa: E402

failures = 0


def expect(cond: bool, msg: str) -> None:
    global failures
    print(("OK   " if cond else "FAIL ") + msg)
    if not cond:
        failures += 1


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    src = json.loads((LIC / "sources.json").read_text(encoding="utf-8"))

    # 1. Licence texts present and unmodified.
    for name, meta in src["licence_texts"].items():
        p = LIC / "licenses" / name
        expect(p.is_file() and sha256(p) == meta["sha256"], f"licence text {name} present and unmodified")

    # 2. Documents and pins.
    for name in ("README.md", "PROVENANCE.md", "MODIFICATIONS.md", "REPRODUCING.md", "DISTRIBUTION-CHECKLIST.md"):
        expect((LIC / name).is_file(), f"docs/licensing/handwriting/{name} present")
    for key in ("hanzi_writer_data", "tegaki_zinnia_japanese"):
        s = src["sources"].get(key, {})
        expect(bool(s.get("licence") and s.get("url")), f"sources.json: {key} has a licence and a url")
    expect(re.fullmatch(r"[0-9a-f]{40}", src["sources"]["hanzi_writer_data"].get("commit", "")) is not None,
           "sources.json: Hanzi Writer data pinned to a full commit hash")
    expect(all(re.fullmatch(r"[0-9a-f]{64}", v) for k, v in src["expected_sha256"].items() if k != "comment"),
           "sources.json: expected corpus and model hashes are sha256 values")
    expect(src["release_gate"] in (LIC / "README.md").read_text(encoding="utf-8"),
           "licensing README names the release gate")
    provenance = (LIC / "PROVENANCE.md").read_text(encoding="utf-8")
    expect(src["sources"]["hanzi_writer_data"]["commit"] in provenance
           and src["expected_sha256"]["model"] in provenance,
           "PROVENANCE.md agrees with sources.json (commit, model hash)")

    # 3. Production paths do not pull in experimental material.
    needle = re.compile(r"hanzi|hzw|arphic|experiments/", re.I)
    production = [ROOT / "scripts" / "fetch_models.sh", ROOT / "models" / "README.md"]
    production += sorted((ROOT / ".github" / "workflows").glob("*.yml"))
    production += [p for p in sorted((ROOT / "src").glob("*")) if p.suffix in (".cpp", ".h", ".mm", ".inc")
                   and not p.name.startswith("profile_")]
    offenders = []
    for p in production:
        if p.is_file() and needle.search(p.read_text(encoding="utf-8", errors="replace")):
            offenders.append(str(p.relative_to(ROOT)))
    expect(not offenders, f"{len(production)} production files (fetch script, CI, src/) do not reference "
           f"Hanzi Writer or experiments/ {offenders or ''}".rstrip())
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    installs = re.findall(r"^\s*install\s*\((.*?)\)", cmake, re.S | re.M)
    expect(not any(re.search(r"\.model|experiments|hzw", i, re.I) for i in installs),
           "CMake install rules do not install model files or experiments/")
    expect(pkg.MODEL_BASENAME not in ("handwriting-ja", "handwriting-zh_TW") and "experimental" in pkg.MODEL_BASENAME,
           "experimental model has a distinct, clearly marked file name")

    # 4. Local production models, when present, are the Tegaki files.
    for name, want in src["production_models"].items():
        p = ROOT / "models" / name
        if p.is_file():
            expect(sha256(p) == want, f"models/{name} is the Tegaki 0.3 production model")
        else:
            print(f"SKIP models/{name} not downloaded")

    # 5. Staged package, when present.
    arg = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("HZW_PACKAGE_DIR")
    staged = Path(arg) if arg else pkg.DEFAULT_OUT / "stage" / pkg.PACKAGE_NAME
    if (staged / "MANIFEST.json").is_file():
        errs = pkg.verify_package(staged, src)
        for e in errs:
            print("     " + e)
        expect(not errs, f"staged package {staged.name} passes verification")
        man = json.loads((staged / "MANIFEST.json").read_text(encoding="utf-8"))
        expect(man.get("release_authorized") is False, "staged package is not authorized for release")
    elif arg:
        expect(False, f"package directory {staged} has no MANIFEST.json")
    else:
        print("SKIP no staged package (build with scripts/build_hzw_model_package.py build)")

    print("All licensing checks passed." if not failures else f"{failures} licensing check(s) failed.")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
