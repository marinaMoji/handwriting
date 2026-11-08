# Recognition models

Model binaries are **not** committed. Download them with:

```bash
./scripts/fetch_models.sh
```

Expected files after download:

| File | Source package |
|------|----------------|
| `handwriting-ja.model` | tegaki-zinnia-japanese 0.3 |
| `handwriting-ja.meta` | tegaki-zinnia-japanese 0.3 |
| `handwriting-zh_TW.model` | tegaki-zinnia-traditional-chinese 0.3 |
| `handwriting-zh_TW.meta` | tegaki-zinnia-traditional-chinese 0.3 |

## Default download mirrors

The fetch script uses Ubuntu package orig tarballs (same content as the historical Tegaki 0.3 releases):

- http://archive.ubuntu.com/ubuntu/pool/universe/t/tegaki-zinnia-japanese/tegaki-zinnia-japanese_0.3.orig.tar.gz
- http://archive.ubuntu.com/ubuntu/pool/universe/t/tegaki-zinnia-traditional-chinese/tegaki-zinnia-traditional-chinese_0.3.orig.tar.gz

Override with `JA_URL` / `TW_URL` if needed.

## Licenses

- **Zinnia engine** (`third_party/zinnia-src`): BSD-style (see `COPYING` in that tree).
- **Tegaki Japanese / Traditional Chinese models**: LGPL-2.1 (see `COPYING.japanese-model` and `COPYING.traditional-chinese-model` after running the fetch script).

Do not redistribute the `.model` files without complying with LGPL-2.1.
