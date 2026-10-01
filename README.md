<h1 align="center">Qivot Studio</h1>

<p align="center"><strong>Explore, design and code against any database: SQLite, PostgreSQL, MySQL and SQL Server.</strong></p>

<p align="center">
  <a href="https://github.com/austinkottke/Qivot-studio/actions/workflows/build.yml"><img src="https://github.com/austinkottke/Qivot-studio/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <img src="https://img.shields.io/badge/Qt-6.8-41CD52?logo=qt&logoColor=white" alt="Qt 6.8">
  <img src="https://img.shields.io/badge/Qt-5.15-41CD52?logo=qt&logoColor=white" alt="Qt 5.15">
  <img src="https://img.shields.io/badge/macOS-000000?logo=apple&logoColor=white" alt="macOS">
  <img src="https://img.shields.io/badge/Windows-0078D6" alt="Windows">
  <img src="https://img.shields.io/badge/Linux-FCC624?logo=linux&logoColor=black" alt="Linux">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="MIT License"></a>
</p>

<p align="center">
  <a href="docs/studio-designer.png">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="docs/studio-designer-dark.png">
      <img src="docs/studio-designer.png" width="900"
           alt="The class designer: the bookshop schema on a canvas with new and edited tables marked, and the Qivot C++ class for the book table beside it">
    </picture>
  </a>
</p>

<table>
  <tr>
    <td width="33%"><a href="docs/studio-diagram.png"><img src="docs/studio-diagram.png" alt="The ER diagram of the Chinook database, with the Track table found and its relationships highlighted"></a></td>
    <td width="33%"><a href="docs/studio-data.png"><img src="docs/studio-data.png" alt="Browsing the Track table's rows, with row 3 open in the inspector"></a></td>
    <td width="33%"><a href="docs/studio-profile.png"><img src="docs/studio-profile.png" alt="The book table's column profiles: how full each column is, distinct values, ranges, histograms and the commonest values"></a></td>
  </tr>
  <tr>
    <td align="center"><sub><b>Diagram</b>: the whole schema, laid out along its keys</sub></td>
    <td align="center"><sub><b>Data</b>: any table, sorted and filtered in SQL, millions of rows</sub></td>
    <td align="center"><sub><b>Profile</b>: how full, how varied, and the shape of every column</sub></td>
  </tr>
  <tr>
    <td width="33%"><a href="docs/studio-builder.png"><img src="docs/studio-builder.png" alt="The query builder: books joined to authors, counted and averaged per country, filtered and sorted, with the SQL it writes and the result"></a></td>
    <td width="33%"><a href="docs/studio-query.png"><img src="docs/studio-query.png" alt="The SQL console running a top-artists-by-revenue query, with syntax highlighting"></a></td>
    <td width="33%"><a href="docs/studio-structure.png"><img src="docs/studio-structure.png" alt="The structure of the Track table: columns with key badges, foreign keys, the tables that reference it, and indexes"></a></td>
  </tr>
  <tr>
    <td align="center"><sub><b>Query builder</b>: joins along the keys, totals, filters; SQL and Qivot C++</sub></td>
    <td align="center"><sub><b>SQL console</b>: read-only on every database</sub></td>
    <td align="center"><sub><b>Structure</b>: columns, keys, relationships both ways, indexes</sub></td>
  </tr>
  <tr>
    <td width="33%"><a href="docs/studio-migration.png"><img src="docs/studio-migration.png" alt="The designer's SQL tab: the changes in words and the SQLite migration that makes them"></a></td>
    <td width="33%"><a href="docs/studio-cpp.png"><img src="docs/studio-cpp.png" alt="The book table's C++ tab: its Qivot model class, ready to copy"></a></td>
    <td width="33%"><a href="docs/studio-ide.png"><img src="docs/studio-ide.png" alt="The exported project open in the IDE, with all eight model tests passing"></a></td>
  </tr>
  <tr>
    <td align="center"><sub><b>Migration</b>: the SQL a design needs, checked against the data</sub></td>
    <td align="center"><sub><b>C++</b>: a Qivot model class for every table</sub></td>
    <td align="center"><sub><b>IDE</b>: the exported project, built and tested in Studio</sub></td>
  </tr>
</table>

<p align="center"><sub>Click any screenshot to see it full size.</sub></p>

Qivot Studio is a database designer and analyzer built on
[Qivot](https://github.com/austinkottke/Qivot), the Qt ORM. Open a database
and see its whole structure at a glance; design changes to it; and turn it
into Qivot C++ that you can build, test and run without leaving Studio.

### Analyze

- **SQLite files, or a server**: PostgreSQL, MySQL / MariaDB and SQL Server.
  Tables in other schemas are listed as `schema.table`.
- **A bird's-eye ER diagram** of the whole database: every table as a card,
  every foreign key as a line from its column to the column it references, with
  crow's-foot notation. Laid out automatically along the relationships, with
  lines routed around the tables in between. Zoom, pan, drag tables, find a
  table by name, and hover one to light up everything it connects to; a
  minimap shows where you are.
- **Every table and view**, with live row counts, in a filterable sidebar.
- **Columns** with their declared types, `NOT NULL`, defaults, and badges for
  primary keys, foreign keys and auto-increment.
- **Relationships both ways**: the foreign keys a table holds, *and* the tables
  that reference it. Click any reference to jump there.
- **Indexes**, including the implicit ones behind `PRIMARY KEY` and `UNIQUE`.
- **The data itself**: browse any table's rows on the Data tab. Sorting and
  filtering run in SQL and only the rows on screen are fetched, so a table of
  millions scrolls as smoothly as one of hundreds. Click a row to see every
  value in full.
- **Column profiles**: for every column, how much is filled, how many distinct
  values, the range, and its shape: a histogram for numbers, the commonest
  values otherwise. Profiled in the background, so big tables don't hold up
  the window.
- **A query builder**: pick a table, join related ones along their foreign
  keys, pick columns (counted, summed, averaged...), filter and sort. The SQL,
  in the database's own dialect, and the same query as Qivot C++ follow every
  change, and the result refreshes as you go.
- **A SQL console**: syntax highlighting, ⌘↩ to run, history, results in the
  same grid.
- **Read-only by design.** Files open read-only, and PostgreSQL and MySQL
  connections are made read-only on the server, so nothing can be changed
  through Studio. (SQL Server has no such session setting; Studio never writes,
  but use a read-only login to be certain.)

### Design

- **A class designer** on the same canvas: add and rename tables, add, change
  and drop columns (type, key, required, unique, default), and draw references
  between tables. Undo and redo everything; new and edited tables are marked.
- **The Qivot class** for every table updates as you edit, and so does the
  **migration**: the SQL that turns the database into the design, in its own
  dialect (PostgreSQL, MySQL, SQL Server, or SQLite's copy-and-swap rebuild).
  Renames stay renames, so no rows are lost.
- **Checked against the data** before anything runs: a column made required
  that has NULLs, made unique with repeated values, a new reference whose
  values point nowhere, a required column added to a table with rows. Each
  problem says what and how many.
- **Kept between runs**: edits are saved as you go and picked up when the
  database opens again; **Save…** / **Open…** a `.qivotdesign` file to keep or
  share one.
- **Apply to a copy** (SQLite): run the migration on a copy of the file and open
  it; the original is never touched.

### Code

- **C++ for every table**: a C++ tab with the Qivot model class, following
  Qivot's real rules (field per column, `QiForeignKey` where Qivot can follow
  it, notes for anything it can't map).
- **Export a project**: CMake, the models, an example program, and a test per
  model that loads every row and follows its foreign keys. Builds with Qt 6 or
  Qt 5.15.
- **An IDE** for the project, or any CMake folder: a file tree, tabbed editors
  with syntax colouring, and Build ⌘B / Test ⌘U / Run ⌘R with the output, the
  compiler's problems (click to jump to the line) and a pass/fail per model.

### Also

- **Four sample databases** to try it immediately, one click each from the welcome screen: a bookshop, a university (a circular reference, a table related to itself, a three-column composite key), a company org chart and a music store, 10,000–45,000 rows apiece. The same samples run on PostgreSQL, MySQL and SQL Server with one `docker compose` command ([tools/sample-servers](tools/sample-servers)).
- Light and dark mode, following the system.

## Download

A macOS build (Apple Silicon and Intel) is on the
[Releases](https://github.com/austinkottke/Qivot-studio/releases) page: open the
DMG and drag Qivot Studio to Applications. It isn't notarized yet, so the first
time, right-click the app and choose **Open**. Releases are made by running the
[Release workflow](https://github.com/austinkottke/Qivot-studio/actions/workflows/release.yml)
by hand (Run workflow: a version, and whether to publish it).

To make the DMG yourself: `tools/package-macos.sh ~/Qt/6.8.3/macos`.

## Build

Needs Qt 6.5 or newer (6.8 recommended), or Qt 5.15, and CMake 3.21+. Point
`CMAKE_PREFIX_PATH` at whichever Qt you have; the build picks Qt 6 when both are found. Qivot is included, as
its single header in `third_party/qivot`:

```bash
git clone https://github.com/austinkottke/Qivot-studio.git qivot-studio
```

```bash
cmake -S qivot-studio -B qivot-studio/build -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/macos
```

```bash
cmake --build qivot-studio/build
```

```bash
ctest --test-dir qivot-studio/build --output-on-failure
```

## Run

```bash
"qivot-studio/build/app/Qivot Studio.app/Contents/MacOS/Qivot Studio" --sample
```

(On Windows and Linux the binary is `build/app/Qivot Studio`.)

| Option | |
|---|---|
| `file.db` | open a database |
| `--sample` | open the bookshop sample |
| `--open-sample <id>` | open sample `bookshop`, `university`, `company` or `music` |
| `--sample-sql <folder>` | write every sample as PostgreSQL, MySQL and SQL Server scripts |
| `--connect <url>` | connect to a server: `postgres://user:pass@host:5432/db`, `mysql://…`, `sqlserver://…` |
| `--connect-dialog` | start with the connect dialog open |
| `--table <name>` | select a table once the file is open |
| `--view data` | start on the Data tab (or `profile`, `cpp`, `diagram`, `design`, `export`, `structure`) |
| `--query-builder` | open the query builder |
| `--view query --query "…"` | open the SQL console and run a query |
| `--project <folder>` | open a CMake project in the IDE |
| `--models <file>` | write Qivot models for every table to a header and quit |
| `--export <folder>` | write a buildable project (models, example, tests) and quit |
| `--dark`, `--light` | force the colour scheme |
| `--size 1440x900`, `--find <table>`, `--select-row <n>`, `--design-demo`, `--design-tab sql`, `--builder-demo`, `--build` | for screenshots and demos |
| `--shot <png>` | save a screenshot and quit |
| `--smoke` | load and quit; exit 1 if any QML warning was logged (used by CI) |

## Connecting to servers

Studio uses Qt's own database drivers, which in turn need each database's client
library on the computer:

| Database | Qt driver | What the computer needs |
|---|---|---|
| PostgreSQL | `QPSQL` (ships with Qt) | `libpq`. On macOS, Qt's driver looks for it in [Postgres.app](https://postgresapp.com). On Linux, `libpq5`. |
| MySQL / MariaDB | `QMYSQL` | Not shipped in Qt's macOS/Windows packages; build it from Qt's sources against `libmysqlclient`. On Linux, `libqt6sql6-mysql`. |
| SQL Server | `QODBC` (ships with Qt) | Microsoft's *ODBC Driver 18 for SQL Server*. |

The connect dialog says when a driver is missing rather than failing obscurely.

`tests/tst_servers.cpp` runs Studio against real servers holding the public
[Pagila](https://github.com/devrimgunduz/pagila), [Sakila and Employees](https://dev.mysql.com/doc/index-other.html)
and [Chinook](https://github.com/lerocha/chinook-database) sample databases; each
test is skipped unless its `STUDIO_TEST_PG` / `STUDIO_TEST_MYSQL` /
`STUDIO_TEST_MSSQL` variable points at a server.

## Layout

| Folder | What's there |
|---|---|
| `core/` | `QivotStudio.Core`: opening databases and describing them (`DatabaseSession`), paging any table's rows (`RowsModel`), the diagram layout (`ErLayout`), the SQL console (`QueryModel`), Qivot code generation (`CodeGen`), the designer and its migrations (`SchemaDesign`), the query builder (`QueryBuilder`), column profiles (`TableProfile`), project export (`ProjectExport`), building and testing (`ProjectBuild`), the IDE's files (`Workspace`), the samples (`SampleDatabase`, described once for every database by `SampleSchema`). Plain C++ with tests. |
| `ui/` | `QivotUI`, the first cut of **qivot-ui**: theme tokens (light/dark) and components (`ActionButton`, `Badge`, `Card`, `FilterField`, `NavItem`, `SegmentedControl`, `TextBox`). Kept free of Studio specifics so it can become its own library. |
| `app/` | The app: `main.cpp` and the screens in `qml/`. |
| `tests/` | Tests for each of the above, including a migration round trip (apply to a copy, read it back, nothing left to change) and an exported project that really builds and passes its own tests; live-server tests; whole-app smoke tests. |
| `third_party/qivot/` | Qivot, as its single header (`qivot.hpp`; `qivot.cpp` compiles it once). Schema reading comes from its `QiSchema`. Exported projects get the same files. Update with `tools/update-qivot.sh`. |

## Roadmap

1. **Analyzer**: DuckDB files; keyboard navigation and a history in the query
   builder.
2. **Designer**: send a design's models to an exported project, reorder
   columns by dragging.
3. **IDE**: go to definition, find in files, build kits.

## License

Qivot Studio is released under the [MIT License](LICENSE).
Copyright © 2026 Austin Kottke.

It is built with Qt (LGPL v3), Qivot (MIT) and, for the icon, the Inter
typeface (SIL Open Font License); see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
