# Qivot, as its single header

[Qivot](https://github.com/austinkottke/Qivot), the Qt ORM Studio is built on,
vendored as the amalgamated header Qivot publishes in `dist/qivot.hpp`.
`qivot.cpp` compiles its implementation once; everything else includes
`qivot.hpp`. Exported projects get a copy of the same three files.

From Qivot commit `77f4ecd` (2026-10-01). To update:

```bash
tools/update-qivot.sh              # the latest on GitHub (main)
tools/update-qivot.sh v1.2.0       # a tag or commit
tools/update-qivot.sh ~/src/Qivot  # a local checkout
```

MIT License; see `LICENSE.txt` and `NOTICE.txt`.
