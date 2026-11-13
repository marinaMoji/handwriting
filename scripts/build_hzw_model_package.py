#!/usr/bin/env python3
"""Build, stage and verify the optional Hanzi Writer-enhanced Japanese Zinnia model.

EXPERIMENTAL. Nothing this script produces may be published until the legal
review in docs/licensing/handwriting/README.md (release gate) is resolved.
It never touches models/ and never uploads anything.

  build   regenerate the three training corpora, train, stage the package, verify it
  verify  check an already staged package (hashes, notices, licence texts, gate)
  eval    load the staged model with handwriting_eval on local samples

Typical use (6 to 20 minutes depending on machine load, nearly all of it zinnia_learn):

  ./scripts/fetch_models.sh
  git clone https://github.com/chanind/hanzi-writer-data.git experiments/hanzi-writer-data
  git -C experiments/hanzi-writer-data checkout 68d10a4b21150cae5e1ebbd223eed289cf32d90c
  cmake --build build --target zinnia_learn handwriting_eval
  python3 scripts/build_hzw_model_package.py build
  python3 scripts/build_hzw_model_package.py eval

Pinned inputs and expected hashes live in docs/licensing/handwriting/sources.json.
The build needs numpy (through ml/tegaki.py); verify needs only the standard library.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIC_DIR = ROOT / "docs" / "licensing" / "handwriting"
SOURCES_JSON = LIC_DIR / "sources.json"
DEFAULT_OUT = ROOT / "experiments" / "hzw-package"
PACKAGE_NAME = "marinamoji-handwriting-ja-hzw-experimental"
MODEL_BASENAME = "handwriting-ja-hzw-experimental"

HZW_CORPUS = "training-data/hzw-derived.ja.s"
TEGAKI_CORPUS = "training-data/tegaki-derived.ja.s"
MODEL_FILE = f"model/{MODEL_BASENAME}.model"
MODEL_NOTICE = f"model/{MODEL_BASENAME}.model.NOTICE.txt"
MODEL_META = f"model/{MODEL_BASENAME}.meta"

# path in package -> (licence, role)
PACKAGE_FILES = {
    "README.md": ("n/a", "package description"),
    "NOTICE.txt": ("n/a", "copyright, licence and modification notices"),
    "NOT-FOR-RELEASE.txt": ("n/a", "release gate marker"),
    MODEL_FILE: ("Arphic-1999 AND LGPL-2.1 (conservative policy; unresolved)", "trained Zinnia model"),
    MODEL_NOTICE: ("n/a", "modification notice for the binary model"),
    MODEL_META: ("n/a", "Tegaki-style model metadata"),
    HZW_CORPUS: ("Arphic-1999", "Hanzi Writer-derived training corpus"),
    TEGAKI_CORPUS: ("LGPL-2.1", "Tegaki-derived training corpus"),
    "training-data/README.md": ("n/a", "how the combined corpus is formed"),
    "licenses/ARPHICPL.TXT": ("verbatim licence text", "Arphic Public License"),
    "licenses/LGPL-2.1.txt": ("verbatim licence text", "GNU LGPL 2.1 as shipped by Tegaki"),
    "docs/PROVENANCE.md": ("n/a", "provenance"),
    "docs/MODIFICATIONS.md": ("n/a", "transformation description"),
    "docs/REPRODUCING.md": ("n/a", "reproduction instructions"),
    "docs/sources.json": ("n/a", "pinned sources and expected hashes"),
    "source/hanzi_writer_to_zinnia.py": ("same as marinaMoji handwriting repository", "converter"),
    "source/build_hzw_model_package.py": ("same as marinaMoji handwriting repository", "builder"),
    "source/tegaki.py": ("same as marinaMoji handwriting repository", "Tegaki XML loader"),
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def body_lines(path: Path) -> list[bytes]:
    """Data lines of a Zinnia training file, without ';' notice header lines."""
    with path.open("rb") as f:
        return [ln for ln in f if not ln.startswith(b";")]


def sha256_body(path: Path) -> str:
    h = hashlib.sha256()
    for ln in body_lines(path):
        h.update(ln)
    return h.hexdigest()


def load_sources() -> dict:
    return json.loads(SOURCES_JSON.read_text(encoding="utf-8"))


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, text=True, **kw)


def git(args: list[str], cwd: Path) -> str:
    return subprocess.run(["git", "-C", str(cwd)] + args, check=True, text=True, capture_output=True).stdout.strip()


class Mismatch(Exception):
    pass


def expect_hash(label: str, actual: str, expected: str, problems: list[str]) -> None:
    if actual != expected:
        problems.append(f"{label}: sha256 {actual}, expected {expected}")


# --------------------------------------------------------------------------- inputs


def check_inputs(src: dict, hanzi_repo: Path, tegaki_dir: Path) -> tuple[dict, list[str]]:
    problems: list[str] = []
    hzw = src["sources"]["hanzi_writer_data"]
    teg = src["sources"]["tegaki_zinnia_japanese"]
    seen = {}

    commit = git(["rev-parse", "HEAD"], hanzi_repo)
    seen["hanzi_writer_commit"] = commit
    if commit != hzw["commit"]:
        problems.append(f"hanzi-writer-data at {commit}, pinned {hzw['commit']}")
    dirty = git(["status", "--porcelain", "--", "data", "ARPHICPL.TXT"], hanzi_repo)
    if dirty:
        problems.append("hanzi-writer-data checkout has local changes under data/ or ARPHICPL.TXT")
    expect_hash(
        "hanzi-writer-data ARPHICPL.TXT",
        sha256(hanzi_repo / "ARPHICPL.TXT"),
        src["licence_texts"]["ARPHICPL.TXT"]["sha256"],
        problems,
    )
    expect_hash(
        "repository copy of ARPHICPL.TXT",
        sha256(LIC_DIR / "licenses" / "ARPHICPL.TXT"),
        src["licence_texts"]["ARPHICPL.TXT"]["sha256"],
        problems,
    )

    xml = tegaki_dir / "handwriting-ja.xml"
    seen["tegaki_xml_sha256"] = sha256(xml)
    expect_hash("Tegaki handwriting-ja.xml", seen["tegaki_xml_sha256"], teg["xml_sha256"], problems)
    expect_hash(
        "Tegaki COPYING", sha256(tegaki_dir / "COPYING"), src["licence_texts"]["LGPL-2.1.txt"]["sha256"], problems
    )
    expect_hash(
        "repository copy of LGPL-2.1.txt",
        sha256(LIC_DIR / "licenses" / "LGPL-2.1.txt"),
        src["licence_texts"]["LGPL-2.1.txt"]["sha256"],
        problems,
    )
    tarball = tegaki_dir.parent / "tegaki-zinnia-japanese_0.3.orig.tar.gz"
    if tarball.exists():
        seen["tegaki_tarball_sha256"] = sha256(tarball)
        expect_hash("Tegaki tarball", seen["tegaki_tarball_sha256"], teg["tarball_sha256"], problems)
    return seen, problems


# --------------------------------------------------------------------------- notices


def comment_block(lines: list[str]) -> str:
    return "".join(f"; {ln}".rstrip() + "\n" for ln in lines)


def hzw_header(src: dict) -> str:
    hzw = src["sources"]["hanzi_writer_data"]
    return comment_block(
        [
            "NOTICE: MODIFIED FILE (Arphic Public License, section 2(a))",
            "",
            "This file is a modification of stroke data from Hanzi Writer data",
            f"({hzw['url']}, commit {hzw['commit']}),",
            "which comes from Make Me a Hanzi (graphics.txt) and derives from the Arphic PL",
            "KaitiM GB and Arphic PL UKai fonts. Copyright (C) 1999 Arphic Technology Co., Ltd.",
            "and the respective contributors to those derivative works.",
            "",
            f"Changed by the marinaMoji project on {src['modification_date']} as follows:",
            " - only the 'medians' polylines were kept; the outline paths ('strokes') were dropped;",
            " - coordinates were mapped from the Hanzi Writer box to the 0..1000 Tomoe/Zinnia box:",
            "   x' = round(x / 1024 * 1000), y' = round((900 - y) / 1024 * 1000), clamped to 0..1000;",
            " - consecutive identical points were removed; a stroke reduced to one point",
            "   was extended by one unit so that it remains a stroke;",
            " - only the characters also present in tegaki-zinnia-japanese 0.3 were kept,",
            "   in Unicode code point order, one example per character;",
            " - each character was written as one Zinnia S-expression line.",
            "Stroke order and stroke count are unchanged. No synthetic distortion was applied.",
            "",
            "You may redistribute and/or modify this file under the terms of the Arphic Public",
            "License as published by Arphic Technology Co., Ltd. See licenses/ARPHICPL.TXT.",
            "THERE IS NO WARRANTY for this file, to the extent permitted by applicable law.",
            "Lines starting with ';' are comments and are ignored by zinnia_learn.",
        ]
    )


def tegaki_header(src: dict) -> str:
    teg = src["sources"]["tegaki_zinnia_japanese"]
    return comment_block(
        [
            "NOTICE: MODIFIED FILE (GNU LGPL 2.1, section 2(a))",
            "",
            "This file is a format conversion of handwriting-ja.xml from",
            f"tegaki-zinnia-japanese {teg['version']} (sha256 {teg['xml_sha256']}),",
            "(C) the Tegaki project contributors; the templates derive from the Tomoe project.",
            "",
            f"Changed by the marinaMoji project on {src['modification_date']} as follows:",
            " - every character template was rewritten from Tomoe XML to one Zinnia",
            "   S-expression line (width 1000, height 1000); points and order are unchanged.",
            "",
            "This file is free software; you can redistribute it and/or modify it under the",
            "terms of the GNU Lesser General Public License version 2.1. See licenses/LGPL-2.1.txt.",
            "It is distributed WITHOUT ANY WARRANTY.",
            "Lines starting with ';' are comments and are ignored by zinnia_learn.",
        ]
    )


def model_notice(src: dict, model_sha: str, corpus_hashes: dict) -> str:
    hzw = src["sources"]["hanzi_writer_data"]
    teg = src["sources"]["tegaki_zinnia_japanese"]
    return f"""MODIFICATION NOTICE for {MODEL_BASENAME}.model
sha256 {model_sha}

The Zinnia binary model format has no field for comments and its loader checks
the exact file length, so this notice accompanies the file instead of being
inserted into it. Keep this notice, licenses/ARPHICPL.TXT and
licenses/LGPL-2.1.txt with every copy of the model.

What this file is
  A Zinnia handwriting-recognition model (per-character linear SVM weights)
  trained by the marinaMoji project on {src['modification_date']} with zinnia_learn
  (default parameters) on the concatenation of:

  1. {TEGAKI_CORPUS}   (sha256 of data lines {corpus_hashes['tegaki']})
     converted from tegaki-zinnia-japanese {teg['version']}, handwriting-ja.xml.
     Licence: GNU LGPL 2.1.

  2. {HZW_CORPUS}      (sha256 of data lines {corpus_hashes['hzw']})
     converted from Hanzi Writer data, {hzw['url']}
     commit {hzw['commit']}, which comes from Make Me a Hanzi and derives
     from the Arphic PL KaitiM GB and Arphic PL UKai fonts.
     Copyright (C) 1999 Arphic Technology Co., Ltd. Licence: Arphic Public License.

How the sources were changed
  See the notice at the top of each training file and docs/MODIFICATIONS.md.
  In short: stroke coordinates were converted to Zinnia training examples and
  reduced by training to feature weights per character. The model does not
  contain glyph outlines or stroke coordinates.

Terms
  As a conservative project policy, and without claiming that this is legally
  required, marinaMoji treats this model as a modification of the Font within the
  meaning of the Arphic Public License and makes it Freely Available under that
  licence, together with the training files and scripts needed to regenerate it.
  The Tegaki-derived contribution remains subject to the GNU LGPL 2.1.
  Whether one file can satisfy both licences is an open legal question
  ({src['release_gate']}).

  THERE IS NO WARRANTY for this model, to the extent permitted by applicable law.
"""


def package_notice(src: dict, model_sha: str, corpus_hashes: dict) -> str:
    return f"""{PACKAGE_NAME}
Optional Japanese handwriting-recognition model for marinaMoji (EXPERIMENTAL)

This package contains material under two licences. The complete texts are in
licenses/ and must be kept, unaltered, with every copy.

1. Arphic Public License (licenses/ARPHICPL.TXT)
   Copyright (C) 1999 Arphic Technology Co., Ltd.
   Applies to {HZW_CORPUS}, a modified form of Hanzi Writer / Make Me a Hanzi
   stroke data derived from Arphic fonts. By conservative project policy it is
   also applied to {MODEL_FILE}.
   You may redistribute and modify these files under the Arphic Public License.

2. GNU Lesser General Public License 2.1 (licenses/LGPL-2.1.txt)
   Applies to {TEGAKI_CORPUS}, converted from tegaki-zinnia-japanese 0.3
   (Tegaki project; templates from the Tomoe project), and to the contribution
   of that data to {MODEL_FILE}.

Modification notices
   Each training file starts with a notice stating how and when it was changed.
   The binary model carries its notice in {MODEL_NOTICE}.
   Date of modification: {src['modification_date']}. Modified by: the marinaMoji project.
   Details: docs/MODIFICATIONS.md. Origins: docs/PROVENANCE.md.

Availability of the modifications
   The modified training files, the model and the scripts that regenerate them
   are included in this package (training-data/, model/, source/) and described
   in docs/REPRODUCING.md.

Hashes
   model                         {model_sha}
   Hanzi Writer-derived corpus   {corpus_hashes['hzw']}  (data lines)
   Tegaki-derived corpus         {corpus_hashes['tegaki']}  (data lines)
   combined corpus               {corpus_hashes['combined']}  (data lines)

NO WARRANTY
   This package is provided "as is", without warranty of any kind, to the
   extent permitted by applicable law. See sections 7 and 8 of the Arphic
   Public License and sections 15 and 16 of the GNU LGPL 2.1.

The marinaMoji application and its other models are separate works and are
not covered by the Arphic Public License by reason of loading this model.
This statement is the project's position, not a legal opinion.
"""


def package_readme(src: dict) -> str:
    return f"""# {PACKAGE_NAME}

**STAGED, NOT RELEASED.** This package must not be published, uploaded or bundled
with an installer until the legal review is resolved: {src['release_gate']}

An optional Japanese Zinnia model for marinaMoji handwriting input, trained on the
Tegaki Japanese templates plus one clean Hanzi Writer stroke median for each of the
5,029 characters present in both. It is an alternative to, not a replacement for,
the Tegaki-only `handwriting-ja.model`.

| Path | Content | Licence |
|---|---|---|
| `model/{MODEL_BASENAME}.model` | trained model | Arphic PL and LGPL-2.1 (conservative policy) |
| `model/{MODEL_BASENAME}.model.NOTICE.txt` | modification notice for the binary | |
| `training-data/hzw-derived.ja.s` | Hanzi Writer-derived training examples | Arphic PL |
| `training-data/tegaki-derived.ja.s` | Tegaki-derived training examples | LGPL-2.1 |
| `licenses/` | `ARPHICPL.TXT` and `LGPL-2.1.txt`, unmodified | |
| `docs/` | provenance, modifications, reproduction, pinned sources | |
| `source/` | the scripts that produced this package | |
| `NOTICE.txt` | copyright, licence and modification notices | |
| `MANIFEST.json` | sources, parameters and the sha256 of every file | |

To use it for testing, copy or link the model as `handwriting-ja.model` into a
separate models directory next to the Tegaki `handwriting-zh_TW.model` and pass
that directory with `--models`. Do not put it in the application's default
models directory.

Verify this package from a checkout of the marinaMoji handwriting repository:
`python3 scripts/build_hzw_model_package.py verify <this directory>`.
"""


TRAINING_README = f"""# Training data

Two files, kept separate so that each stays under its own licence:

- `hzw-derived.ja.s`: Arphic Public License. Derived from Hanzi Writer data.
- `tegaki-derived.ja.s`: GNU LGPL 2.1. Derived from tegaki-zinnia-japanese 0.3.

The combined corpus the model was trained on is the Tegaki file followed by the
Hanzi Writer file:

    cat tegaki-derived.ja.s hzw-derived.ja.s > combined.ja.s
    zinnia_learn combined.ja.s {MODEL_BASENAME}.model

Lines starting with `;` are notices; `zinnia_learn` ignores them (it prints an
empty line on stderr for each) and they do not change the model.
"""


# --------------------------------------------------------------------------- build


def build_corpora(src: dict, hanzi_repo: Path, tegaki_dir: Path, work: Path, problems: list[str]) -> dict:
    raw = work / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    conv = [
        sys.executable,
        str(ROOT / "scripts" / "hanzi_writer_to_zinnia.py"),
        "--hanzi-dir",
        str(hanzi_repo / "data"),
        "--tegaki-xml",
        str(tegaki_dir / "handwriting-ja.xml"),
        "--max-chars",
        "0",
        "--hzw-mode",
        "clean",
    ]
    run(conv + ["--out", str(raw / "hzw.s"), "--no-tegaki"], stdout=subprocess.DEVNULL)
    run(conv + ["--out", str(raw / "tegaki.s"), "--tegaki-only"], stdout=subprocess.DEVNULL)

    out = work / "training-data"
    out.mkdir(parents=True, exist_ok=True)
    hzw_s, teg_s, comb_s = out / "hzw-derived.ja.s", out / "tegaki-derived.ja.s", work / "combined.ja.s"
    hzw_s.write_bytes(hzw_header(src).encode() + (raw / "hzw.s").read_bytes())
    teg_s.write_bytes(tegaki_header(src).encode() + (raw / "tegaki.s").read_bytes())
    comb_s.write_bytes(teg_s.read_bytes() + hzw_s.read_bytes())

    exp, lines = src["expected_sha256"], src["training"]["expected_lines"]
    info = {}
    for key, path, pin in (
        ("tegaki", teg_s, "tegaki_corpus_body"),
        ("hzw", hzw_s, "hzw_corpus_body"),
        ("combined", comb_s, "combined_corpus_body"),
    ):
        n = len(body_lines(path))
        info[key] = {"path": path, "body_sha256": sha256_body(path), "lines": n}
        expect_hash(f"{key} corpus (data lines)", info[key]["body_sha256"], exp[pin], problems)
        want = lines["hanzi_writer" if key == "hzw" else key]
        if n != want:
            problems.append(f"{key} corpus has {n} data lines, expected {want}")
    return info


def train(combined: Path, work: Path, zinnia_learn: Path) -> tuple[Path, float]:
    model = work / f"{MODEL_BASENAME}.model"
    t0 = time.time()
    with (work / "train.log").open("w") as log:
        run([str(zinnia_learn), str(combined), str(model)], stdout=log, stderr=subprocess.DEVNULL)
    return model, time.time() - t0


def repo_state() -> dict:
    try:
        return {
            "commit": git(["rev-parse", "HEAD"], ROOT),
            "uncommitted_changes": bool(git(["status", "--porcelain"], ROOT)),
        }
    except (subprocess.CalledProcessError, FileNotFoundError):
        return {"commit": None, "uncommitted_changes": None}


def stage(src: dict, seen: dict, corpora: dict, model: Path, train_seconds: float | None, out: Path, mismatches: list[str]) -> Path:
    pkg = out / "stage" / PACKAGE_NAME
    if pkg.exists():
        shutil.rmtree(pkg)
    for sub in ("model", "training-data", "licenses", "docs", "source"):
        (pkg / sub).mkdir(parents=True)

    model_sha = sha256(model)
    hashes = {k: v["body_sha256"] for k, v in corpora.items()}

    shutil.copyfile(model, pkg / MODEL_FILE)
    (pkg / MODEL_NOTICE).write_text(model_notice(src, model_sha, hashes), encoding="utf-8")
    (pkg / MODEL_META).write_text(
        "name = Japanese + Hanzi Writer (experimental)\nshortname = 日+\nlanguage = ja\n", encoding="utf-8"
    )
    shutil.copyfile(corpora["hzw"]["path"], pkg / HZW_CORPUS)
    shutil.copyfile(corpora["tegaki"]["path"], pkg / TEGAKI_CORPUS)
    (pkg / "training-data" / "README.md").write_text(TRAINING_README, encoding="utf-8")
    for name in ("ARPHICPL.TXT", "LGPL-2.1.txt"):
        shutil.copyfile(LIC_DIR / "licenses" / name, pkg / "licenses" / name)
    for name in ("PROVENANCE.md", "MODIFICATIONS.md", "REPRODUCING.md", "sources.json"):
        shutil.copyfile(LIC_DIR / name, pkg / "docs" / name)
    shutil.copyfile(ROOT / "scripts" / "hanzi_writer_to_zinnia.py", pkg / "source" / "hanzi_writer_to_zinnia.py")
    shutil.copyfile(Path(__file__).resolve(), pkg / "source" / "build_hzw_model_package.py")
    shutil.copyfile(ROOT / "ml" / "tegaki.py", pkg / "source" / "tegaki.py")
    (pkg / "NOTICE.txt").write_text(package_notice(src, model_sha, hashes), encoding="utf-8")
    (pkg / "README.md").write_text(package_readme(src), encoding="utf-8")
    (pkg / "NOT-FOR-RELEASE.txt").write_text(
        "This package is staged for audit only. Do not publish, upload or bundle it\n"
        f"until the release gate is resolved: {src['release_gate']}\n",
        encoding="utf-8",
    )

    files = []
    for rel, (licence, role) in PACKAGE_FILES.items():
        p = pkg / rel
        files.append({"path": rel, "sha256": sha256(p), "bytes": p.stat().st_size, "licence": licence, "role": role})

    manifest = {
        "schema_version": 1,
        "package": PACKAGE_NAME,
        "status": "staged-experimental",
        "release_authorized": False,
        "release_gate": src["release_gate"],
        "generated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "modification_date": src["modification_date"],
        "builder": {
            "repository": "https://github.com/marinaMoji/handwriting",
            **repo_state(),
            "platform": platform.platform(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "train_seconds": None if train_seconds is None else round(train_seconds, 1),
        },
        "sources": {
            "hanzi_writer_data": {**src["sources"]["hanzi_writer_data"], "verified_commit": seen["hanzi_writer_commit"]},
            "tegaki_zinnia_japanese": {
                **src["sources"]["tegaki_zinnia_japanese"],
                "verified_xml_sha256": seen["tegaki_xml_sha256"],
                "verified_tarball_sha256": seen.get("tegaki_tarball_sha256"),
            },
            "zinnia": src["sources"]["zinnia"],
        },
        "training": {
            **src["training"],
            "lines": {k: v["lines"] for k, v in corpora.items()},
            "corpus_body_sha256": hashes,
            "combined_corpus": f"{TEGAKI_CORPUS} followed by {HZW_CORPUS}",
            "command": f"zinnia_learn combined.ja.s {MODEL_BASENAME}.model",
        },
        "model": {"path": MODEL_FILE, "sha256": model_sha, "expected_sha256": src["expected_sha256"]["model"]},
        "hash_mismatches": mismatches,
        "licensing": {
            "policy": "conservative: converted training data and trained weights are treated as APL-covered; "
            "Tegaki-derived material keeps its LGPL-2.1 obligations; not a legal conclusion",
            "unresolved": "whether one fused model can satisfy Arphic-1999 and LGPL-2.1 simultaneously",
            "not_included": ["InkNet weights (unchanged, Tegaki-only)", "handwriting-zh_TW.model (unchanged Tegaki 0.3)"],
        },
        "files": files,
    }
    (pkg / "MANIFEST.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return pkg


# --------------------------------------------------------------------------- verify


def verify_package(pkg: Path, src: dict | None = None) -> list[str]:
    """Return a list of problems; empty means the staged package is consistent."""
    src = src or load_sources()
    errs: list[str] = []
    mpath = pkg / "MANIFEST.json"
    if not mpath.is_file():
        return [f"{mpath} missing"]
    man = json.loads(mpath.read_text(encoding="utf-8"))

    listed = {f["path"]: f for f in man.get("files", [])}
    for rel in PACKAGE_FILES:
        if rel not in listed:
            errs.append(f"manifest does not list required file {rel}")
    on_disk = set()
    for p in pkg.rglob("*"):
        if p.is_symlink():
            errs.append(f"symlink in package: {p.relative_to(pkg)}")
        elif p.is_file():
            on_disk.add(p.relative_to(pkg).as_posix())
    on_disk.discard("MANIFEST.json")
    for rel in sorted(on_disk - set(listed)):
        errs.append(f"file not in manifest: {rel}")
    for rel, f in listed.items():
        p = pkg / rel
        if not p.is_file():
            errs.append(f"listed file missing: {rel}")
        elif sha256(p) != f["sha256"]:
            errs.append(f"hash differs from manifest: {rel}")
        if not f.get("licence"):
            errs.append(f"no licence recorded for {rel}")
    if errs:
        return errs

    # Licence texts unmodified.
    for name, meta in src["licence_texts"].items():
        expect_hash(f"licenses/{name}", sha256(pkg / "licenses" / name), meta["sha256"], errs)

    # Release gate.
    if man.get("release_authorized") is not False:
        errs.append("release_authorized must be false while the legal review is open")
    if man.get("release_gate") != src["release_gate"]:
        errs.append("manifest release_gate does not match sources.json")

    # Every training source identified with a licence and a pinned version.
    for key, pin in (("hanzi_writer_data", "verified_commit"), ("tegaki_zinnia_japanese", "verified_xml_sha256")):
        s = man.get("sources", {}).get(key, {})
        if not s.get("licence") or not s.get("url") or not s.get(pin):
            errs.append(f"manifest source {key} lacks licence, url or {pin}")
    if man["sources"]["hanzi_writer_data"].get("verified_commit") != src["sources"]["hanzi_writer_data"]["commit"]:
        errs.append("Hanzi Writer data commit differs from the pinned commit")

    # Modification notices: in each training file, and beside the binary model.
    date = man.get("modification_date", "")
    for rel, word in ((HZW_CORPUS, "Arphic Public License"), (TEGAKI_CORPUS, "LGPL")):
        with (pkg / rel).open("rb") as f:
            head = b"".join(ln for ln in (f.readline() for _ in range(40)) if ln.startswith(b";")).decode("utf-8", "replace")
        if "MODIFIED FILE" not in head or date not in head or word not in head:
            errs.append(f"{rel}: modification notice header missing, undated or without licence name")
    model_sha = sha256(pkg / MODEL_FILE)
    notice = (pkg / MODEL_NOTICE).read_text(encoding="utf-8")
    for needle in (model_sha, date, "Arphic Public License", "LGPL", "NO WARRANTY"):
        if needle not in notice:
            errs.append(f"{MODEL_NOTICE} does not mention {needle!r}")
    top = (pkg / "NOTICE.txt").read_text(encoding="utf-8")
    for needle in (model_sha, "Arphic Technology", "Arphic Public License", "Lesser General Public License", date):
        if needle not in top:
            errs.append(f"NOTICE.txt does not mention {needle!r}")

    # Corpora: body hashes match the manifest and the pins; APL part separable.
    exp = src["expected_sha256"]
    recorded = man["training"]["corpus_body_sha256"]
    hzw_body, teg_body = sha256_body(pkg / HZW_CORPUS), sha256_body(pkg / TEGAKI_CORPUS)
    comb = hashlib.sha256()
    for rel in (TEGAKI_CORPUS, HZW_CORPUS):
        for ln in body_lines(pkg / rel):
            comb.update(ln)
    for label, actual, key in (("hzw", hzw_body, "hzw_corpus_body"), ("tegaki", teg_body, "tegaki_corpus_body"), ("combined", comb.hexdigest(), "combined_corpus_body")):
        if actual != recorded.get(label):
            errs.append(f"{label} corpus differs from manifest record")
        if actual != exp[key] and not man.get("hash_mismatches"):
            errs.append(f"{label} corpus differs from pinned hash and manifest records no mismatch")
    if model_sha != man["model"]["sha256"]:
        errs.append("model hash differs from manifest record")
    if model_sha != exp["model"] and not man.get("hash_mismatches"):
        errs.append("model differs from pinned hash and manifest records no mismatch")
    if not (pkg / "NOT-FOR-RELEASE.txt").is_file():
        errs.append("NOT-FOR-RELEASE.txt missing")
    return errs


# --------------------------------------------------------------------------- commands


def cmd_build(args) -> int:
    src = load_sources()
    out, work = args.out, args.out / "work"
    work.mkdir(parents=True, exist_ok=True)
    seen, problems = check_inputs(src, args.hanzi_repo, args.tegaki_dir)
    corpora = build_corpora(src, args.hanzi_repo, args.tegaki_dir, work, problems)
    for key, v in corpora.items():
        print(f"corpus {key:8s} {v['lines']:6d} lines  {v['body_sha256']}")

    if args.reuse_model:
        model, seconds = args.reuse_model, None
        print(f"reusing model {model} (not retrained)")
    else:
        print("training with zinnia_learn (6 to 20 minutes)...", flush=True)
        model, seconds = train(corpora["combined"]["path"], work, args.zinnia_learn)
        print(f"trained in {seconds:.0f} s")
    expect_hash("model", sha256(model), src["expected_sha256"]["model"], problems)

    if problems:
        print("\nDifferences from docs/licensing/handwriting/sources.json:", file=sys.stderr)
        for p in problems:
            print("  - " + p, file=sys.stderr)
        if not args.allow_hash_mismatch:
            print("Not staging. Re-run with --allow-hash-mismatch to stage and record them.", file=sys.stderr)
            return 1
    pkg = stage(src, seen, corpora, model, seconds, out, problems)
    errs = verify_package(pkg, src)
    for e in errs:
        print("VERIFY FAIL " + e, file=sys.stderr)
    print(f"staged {pkg}  model sha256 {sha256(pkg / MODEL_FILE)}")
    print("NOT RELEASED: " + src["release_gate"])
    return 1 if errs else 0


def cmd_verify(args) -> int:
    errs = verify_package(args.package)
    for e in errs:
        print("FAIL " + e)
    if not errs:
        print(f"OK   {args.package} is consistent ({len(PACKAGE_FILES)} files); release not authorized")
    return 1 if errs else 0


def cmd_eval(args) -> int:
    """Load the staged model through the normal recognizer and evaluate on local samples."""
    pkg = args.package
    prod = ROOT / "models"
    mdir = args.out / "eval-models"
    if mdir.exists():
        shutil.rmtree(mdir)
    mdir.mkdir(parents=True)
    (mdir / "handwriting-ja.model").symlink_to((pkg / MODEL_FILE).resolve())
    (mdir / "handwriting-zh_TW.model").symlink_to((prod / "handwriting-zh_TW.model").resolve())
    cmd = [
        str(args.handwriting_eval),
        "--samples",
        str(args.samples),
        "--models",
        str(mdir),
        "--modes",
        "ja,both",
        "--variants",
        "resample:4+smooth:3@merge+split",
    ]
    res = subprocess.run(cmd, text=True, capture_output=True)
    (args.out / "eval.log").write_text(res.stdout + res.stderr, encoding="utf-8")
    print(res.stdout + res.stderr)
    return res.returncode


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    default_pkg = DEFAULT_OUT / "stage" / PACKAGE_NAME

    b = sub.add_parser("build", help="regenerate corpora, train, stage and verify")
    b.add_argument("--out", type=Path, default=DEFAULT_OUT)
    b.add_argument("--hanzi-repo", type=Path, default=ROOT / "experiments" / "hanzi-writer-data")
    b.add_argument("--tegaki-dir", type=Path, default=ROOT / "models_dl" / "tegaki-zinnia-japanese-0.3")
    b.add_argument("--zinnia-learn", type=Path, default=ROOT / "build" / "zinnia_learn")
    b.add_argument("--reuse-model", type=Path, help="stage this already trained model instead of retraining")
    b.add_argument("--allow-hash-mismatch", action="store_true", help="stage even if inputs or outputs differ from the pins")
    b.set_defaults(func=cmd_build)

    v = sub.add_parser("verify", help="check a staged package")
    v.add_argument("package", type=Path, nargs="?", default=default_pkg)
    v.set_defaults(func=cmd_verify)

    e = sub.add_parser("eval", help="evaluate the staged model with handwriting_eval")
    e.add_argument("package", type=Path, nargs="?", default=default_pkg)
    e.add_argument("--out", type=Path, default=DEFAULT_OUT)
    e.add_argument("--samples", type=Path, default=ROOT / "samples")
    e.add_argument("--handwriting-eval", type=Path, default=ROOT / "build" / "handwriting_eval")
    e.set_defaults(func=cmd_eval)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
