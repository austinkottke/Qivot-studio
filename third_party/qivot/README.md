# Qivot, as its single header

[Qivot](https://github.com/austinkottke/Qivot), the Qt ORM Studio is built on,
vendored as the amalgamated header Qivot publishes in `dist/qivot.hpp`.
`qivot.cpp` compiles its implementation once; everything else includes
`qivot.hpp`. Exported projects get a copy of the same three files.
`drivers/duckdb` is Qivot's Qt driver for DuckDB, compiled into Studio when
DuckDB is (see `cmake/duckdb.cmake`).

From Qivot commit `7a20573` (2026-10-03), with its DuckDB driver from
`drivers/duckdb` (read-only opening and rows converted as they're read). To update:

```bash
tools/update-qivot.sh              # the latest on GitHub (main)
tools/update-qivot.sh v1.2.0       # a tag or commit
tools/update-qivot.sh ~/src/Qivot  # a local checkout
```

MIT License; see `LICENSE.txt` and `NOTICE.txt`.
