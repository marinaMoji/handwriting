# Distribution checklist for the Hanzi Writer-enhanced model

Nothing is published until every box is ticked. The first section is the gate; the rest can be prepared in advance and is checked by `scripts/build_hzw_model_package.py verify` where marked (auto).

## Gate

- [ ] [Issue #21](https://github.com/marinaMoji/handwriting/issues/21) is closed with a written opinion.
- [ ] The opinion confirms that a fused Tegaki and Hanzi Writer model can satisfy LGPL-2.1 and APL together, or the package has been rebuilt as an APL-only model (see README.md, Open questions).
- [ ] Any change the opinion requires has been made to the notices, the package contents and these documents.
- [ ] `release_authorized` is set to `true` by a deliberate change to the build script and to `tests/licensing_check_test.py`, reviewed in a pull request that links the opinion.

## Package contents

- [ ] (auto) `licenses/ARPHICPL.TXT` present, sha256 `5590533436c70f10f2f524ee61456238c290175c6662fbe1c700b5f038a6d328`.
- [ ] (auto) `licenses/LGPL-2.1.txt` present, sha256 `a190dc9c8043755d90f8b0a75fa66b9e42d4af4c980bf5ddc633f0124db3cee7`.
- [ ] (auto) `NOTICE.txt` names both licences, Arphic Technology, the modification date and the model hash.
- [ ] (auto) `training-data/hzw-derived.ja.s` starts with a dated modification notice and matches the pinned hash.
- [ ] (auto) `training-data/tegaki-derived.ja.s` starts with a dated modification notice and matches the pinned hash.
- [ ] (auto) `model/…model.NOTICE.txt` names the model by sha256 and states how and when the sources were changed.
- [ ] (auto) `MANIFEST.json` lists every file with its sha256 and licence, and identifies both training sources with pinned versions.
- [ ] (auto) `docs/PROVENANCE.md`, `docs/MODIFICATIONS.md`, `docs/REPRODUCING.md`, `docs/sources.json` and `source/` are present.
- [ ] (auto) No file in the package is absent from the manifest; no symlinks.
- [ ] The package was built from a clean, committed checkout (`builder.uncommitted_changes` is `false` in the manifest).

## Availability of the modifications (APL §2(b))

- [ ] The Hanzi Writer-derived corpus is offered from the same place as the model, as its own file, not only as a script that regenerates it.
- [ ] The place is stable (for example a GitHub Release asset) and stated in the download description.
- [ ] No terms of use, EULA or download gate adds restrictions to the APL-covered files (§5).

## Separation from the application

- [ ] (auto) `models/handwriting-ja.model` and `models/handwriting-zh_TW.model` are the Tegaki 0.3 files.
- [ ] (auto) `scripts/fetch_models.sh`, CI workflows, CMake install rules and `src/` do not reference Hanzi Writer data or the staged package.
- [ ] The model is offered as an optional, separately downloaded file with its own licence notice shown before download; it is not inside an installer unless the opinion allows that channel.
- [ ] The application still works with the Tegaki-only models when the optional model is absent.
- [ ] The application's own licence terms are unchanged.

## Technical readiness (not licensing, but required before release)

- [ ] The model loads and evaluates with `build_hzw_model_package.py eval`.
- [ ] The gain over the Tegaki-only model is confirmed on the held-out set (issue #18), not only on the development samples.
- [ ] The application has a way to select the optional model without replacing the default (not implemented yet).
