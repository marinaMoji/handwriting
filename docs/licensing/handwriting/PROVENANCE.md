# Provenance of handwriting training data and models

Pins and hashes below are duplicated in machine-readable form in [sources.json](sources.json); the build script refuses to stage a package if an input differs from them.

## Chain of derivation

```text
Arphic PL KaitiM GB, Arphic PL UKai (TrueType fonts, Arphic Public License)
        |  outline extraction, stroke segmentation, medians (Make Me a Hanzi)
        v
Make Me a Hanzi  graphics.txt                     (Arphic Public License)
        |  one JSON file per character (Hanzi Writer data)
        v
hanzi-writer-data  data/<char>.json  "medians"    (Arphic Public License)
        |  scripts/hanzi_writer_to_zinnia.py       see MODIFICATIONS.md
        v
hzw-derived.ja.s  (5,029 training examples)       (Arphic Public License)
        |
        |      tegaki-zinnia-japanese 0.3  handwriting-ja.xml   (LGPL-2.1)
        |              |  format conversion
        |              v
        |      tegaki-derived.ja.s  (6,646 training examples)   (LGPL-2.1)
        v              v
        concatenation, then zinnia_learn
        v
handwriting-ja-hzw-experimental.model   (APL and LGPL-2.1; see README.md)
```

## Hanzi Writer data

| | |
|---|---|
| Repository | https://github.com/chanind/hanzi-writer-data |
| Pinned commit | `68d10a4b21150cae5e1ebbd223eed289cf32d90c` (2019-06-22, "updating data from makemeahanzi"); this was the head of the default branch on 2026-11-13 |
| Files used | `data/<character>.json`, key `medians` only; 9,574 character files in the checkout, 5,029 used |
| Licence | Arphic Public License (`package.json`: "SEE LICENSE IN ARPHICPL.TXT"; README, section License) |
| Licence file | `/ARPHICPL.TXT`, sha256 `5590533436c70f10f2f524ee61456238c290175c6662fbe1c700b5f038a6d328` |

The upstream README states that the data comes from Make Me a Hanzi, which extracted it from fonts by Arphic Technology, and that it may be redistributed and modified under the Arphic Public License.

## Make Me a Hanzi

| | |
|---|---|
| Repository | https://github.com/skishore/makemeahanzi (head `bddc96d41bef78427ed0e034e9f7e31d71fd1b92` on 2026-11-13; not used directly) |
| Relevant file | `graphics.txt` (stroke outlines and medians) |
| Licence | Arphic Public License for `graphics.txt`; the dictionary file has a different licence and is not used |
| Stated origin | "derived from two free fonts: Arphic PL KaitiM GB and Arphic PL UKai" (upstream README) |

Arphic PL UKai is itself a modified Arphic font maintained by third parties under the same licence. The copyright holders of the stroke data are therefore Arphic Technology and the contributors to UKai, Make Me a Hanzi and Hanzi Writer data. marinaMoji has not traced which Hanzi Writer commit corresponds to which Make Me a Hanzi commit.

## Arphic Public License text

The copy in [licenses/ARPHICPL.TXT](licenses/ARPHICPL.TXT) is byte-for-byte the file at the root of hanzi-writer-data at the pinned commit. It was compared on 2026-11-13 with three other copies:

| Copy | sha256 | Difference from ours |
|---|---|---|
| hanzi-writer-data `APL/english/ARPHICPL.TXT`, makemeahanzi `APL/english/ARPHICPL.TXT`, SPDX `text/Arphic-1999.txt` | `3a5e90c0…2b386d` | one final newline |
| ftp.gnu.org `non-gnu/chinese-fonts-truetype/LICENSE` | `ba74a961…d4d11e` | CRLF line endings |

The wording is identical in all of them. We ship the copy we received with the data, unaltered, as §1 asks.

## Tegaki Japanese model and templates

| | |
|---|---|
| Package | tegaki-zinnia-japanese 0.3 (2009) |
| Download | http://archive.ubuntu.com/ubuntu/pool/universe/t/tegaki-zinnia-japanese/tegaki-zinnia-japanese_0.3.orig.tar.gz |
| Tarball sha256 | `d177614a5229cffb0b231a48201be67f9e1968884c0ad18895686d803f79ea23` |
| Training templates | `handwriting-ja.xml`, sha256 `6f30a4f42f24dc611f1a3e4d7a220c5009bb0323f237f01f50e3be3dfefb11f8`; 6,421 characters, 6,646 templates |
| Production model | `handwriting-ja.model`, sha256 `d58618576d12c1ea38992606f30d19158a0a3beda3ac23bacccf1e48e651f2b2` |
| Licence | GNU LGPL 2.1 (`COPYING` in the tarball, sha256 `a190dc9c8043755d90f8b0a75fa66b9e42d4af4c980bf5ddc633f0124db3cee7`) |

The tarball is the Ubuntu "orig" archive of the historical Tegaki release; the original project site (tegaki.org) is no longer the download source. The templates descend from the Tomoe project. [licenses/LGPL-2.1.txt](licenses/LGPL-2.1.txt) is the tarball's `COPYING`, byte-for-byte.

The traditional Chinese model (`handwriting-zh_TW.model`, sha256 `cd47f16b…330b11`) comes from tegaki-zinnia-traditional-chinese 0.3 under the same licence. It is not retrained and not part of the staged package.

## Zinnia

The trainer (`zinnia_learn`) and the recognition engine are built from the vendored sources in `third_party/zinnia-src` (https://github.com/taku910/zinnia, BSD-style licence, see `COPYING` there). Zinnia is a tool here, not training data. One local patch affects training: `svm.cpp` uses `std::shuffle` with `std::mt19937` seeded with 0 in place of the removed `std::random_shuffle`, which makes training deterministic.

## Resulting artifacts

| Artifact | Lines | sha256 (data lines, notice header excluded) | Licence |
|---|---|---|---|
| `tegaki-derived.ja.s` | 6,646 | `1665f400790bca1901f19b038548d2dc124a02dad12f695265170eb3cb38d6a9` | LGPL-2.1 |
| `hzw-derived.ja.s` | 5,029 | `7b02c6eb91163622435d573e7824ab9ee181d4aacbcf21135a854a444bf09525` | Arphic PL |
| combined corpus | 11,675 | `f817409e6d8404494ea49dd7cc5715a400e29a284d90684dea21d3ab18749574` | both, by section |
| `handwriting-ja-hzw-experimental.model` | | `6844964c3792670d8e59639be32b9796bd35380ba2fd59ba73694673dc0f0bb6` (whole file) | see README.md |

The combined corpus is the Tegaki file followed by the Hanzi Writer file, so the APL-derived portion stays identifiable and is distributed as its own file.

## Not used

InkNet is trained on Tegaki templates only and is unchanged. KanjiVG (CC BY-SA 3.0) is used for evaluation only. No handwriting collected from people enters any of these artifacts.
