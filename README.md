<h1 align="center">Qivot Studio</h1>

<p align="center"><strong>Explore, design and code against any database: SQLite, DuckDB, PostgreSQL, MySQL, SQL Server and Redis.</strong></p>

<p align="center">
  <a href="https://github.com/austinkottke/Qivot-studio/actions/workflows/build.yml"><img src="https://github.com/austinkottke/Qivot-studio/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <img src="https://img.shields.io/badge/Qt-6.8-41CD52?logo=qt&logoColor=white" alt="Qt 6.8">
  <img src="https://img.shields.io/badge/Qt-5.15-41CD52?logo=qt&logoColor=white" alt="Qt 5.15">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="MIT License"></a>
</p>

<p align="center">
  <a href="https://github.com/austinkottke/Qivot-studio/releases"><img src="https://img.shields.io/github/v/release/austinkottke/Qivot-studio?label=Download&include_prereleases&color=2ea44f&style=for-the-badge" alt="Download the latest release"></a>
  <br>
  <a href="https://github.com/austinkottke/Qivot-studio/releases"><img src="https://img.shields.io/badge/macOS-Apple%20Silicon-000000?logo=apple&logoColor=white&style=for-the-badge" alt="macOS, Apple Silicon"></a>
  <a href="https://github.com/austinkottke/Qivot-studio/releases"><img src="https://img.shields.io/badge/macOS-Intel-555555?logo=apple&logoColor=white&style=for-the-badge" alt="macOS, Intel"></a>
  <a href="https://github.com/austinkottke/Qivot-studio/releases"><img src="https://img.shields.io/badge/Windows-x64-0078D6?style=for-the-badge" alt="Windows x64"></a>
  <a href="https://github.com/austinkottke/Qivot-studio/releases"><img src="https://img.shields.io/badge/Linux-AppImage-FCC624?logo=linux&logoColor=black&style=for-the-badge" alt="Linux AppImage"></a>
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
    <td width="560"><a href="docs/studio-diagram.png"><img src="docs/feature-diagram.png" width="560" alt="The ER diagram of the Chinook database, with the Track table found and its relationships highlighted"></a></td>
    <td width="280">
      <sub><b>DIAGRAM</b></sub>
      <h3>The whole schema at a<br>glance</h3>
      <p>Every table as a card, every<br>foreign key as a line in<br>crow's-foot notation, laid out<br>along the relationships. Zoom<br>with the wheel, pan, find a<br>table and hover it to light up<br>everything it connects to.<br>Exports as PNG, SVG or PDF.</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td width="280">
      <sub><b>DATA</b></sub>
      <h3>Any table, millions of<br>rows</h3>
      <p>Sorting and filtering run in SQL<br>and only the rows on screen are<br>fetched, so huge tables scroll<br>smoothly. Click a row to see<br>every value in full; export to<br>CSV or JSON.</p>
    </td>
    <td width="560"><a href="docs/studio-data.png"><img src="docs/feature-data.png" width="560" alt="Browsing the Track table's rows, with row 3 open in the inspector"></a></td>
  </tr>
</table>

<table>
  <tr>
    <td width="560"><a href="docs/studio-selection.png"><img src="docs/feature-selection.png" width="560" alt="Sixteen cells of the price and pages columns selected, with their count, sum, average, min and max in the bar below"></a></td>
    <td width="280">
      <sub><b>SELECTION</b></sub>
      <h3>Select it, add it up,<br>copy it</h3>
      <p>Drag across cells, Shift-click,<br>or take whole rows: the bar adds<br>them up (count, sum, average,<br>min, max), and ⌘C copies them as<br>tab-separated text that pastes<br>straight into a spreadsheet.</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td width="280">
      <sub><b>EDITING</b></sub>
      <h3>Change rows, then undo</h3>
      <p>Edit cells, add and delete rows,<br>set NULLs. Every change is<br>marked in the grid and shown as<br>SQL before it runs; save in one<br>transaction, and undo the save<br>if you need to.</p>
    </td>
    <td width="560"><a href="docs/studio-editing.png"><img src="docs/feature-editing.png" width="560" alt="Editing the publisher table: an edited cell, a value set to NULL, a deleted row and a new one, with the unsaved changes ready to review as SQL and save"></a></td>
  </tr>
</table>

<table>
  <tr>
    <td width="560"><a href="docs/studio-profile.png"><img src="docs/feature-profile.png" width="560" alt="The book table's column profiles: how full each column is, distinct values, ranges, histograms and the commonest values"></a></td>
    <td width="280">
      <sub><b>PROFILE</b></sub>
      <h3>The shape of every<br>column</h3>
      <p>How full each column is, how<br>many distinct values, the range,<br>and a histogram or the commonest<br>values, profiled in the<br>background.</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td width="280">
      <sub><b>SQL CONSOLE</b></sub>
      <h3>Read-only on every<br>database</h3>
      <p>Syntax highlighting and<br>autocomplete that knows the<br>query: tables after FROM,<br>columns after<br><code>alias.</code>. History and<br>saved queries per database.</p>
    </td>
    <td width="560"><a href="docs/studio-query.png"><img src="docs/feature-query.png" width="560" alt="The SQL console running a top-artists-by-revenue query, with syntax highlighting"></a></td>
  </tr>
</table>

<table>
  <tr>
    <td width="560"><a href="docs/studio-running.png"><img src="docs/feature-running.png" width="560" alt="A long query running in a tab, with its time so far and the Stop button"></a></td>
    <td width="280">
      <sub><b>RUNNING</b></sub>
      <h3>Slow queries don't<br>freeze anything</h3>
      <p>Queries run in the background,<br>each in its own tab. Stop (⌘.)<br>asks the server to cancel:<br><code>pg_cancel_backend</code>,<br><code>KILL QUERY</code> or<br><code>KILL</code>.</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td width="280">
      <sub><b>PLAN</b></sub>
      <h3>How the database would<br>run it</h3>
      <p>Explain (⇧⌘↩) shows the plan<br>without running the query: a<br>tree of steps with estimated<br>rows and cost, and every full<br>table scan flagged. SQLite,<br>PostgreSQL, MySQL and SQL<br>Server.</p>
    </td>
    <td width="560"><a href="docs/studio-plan.png"><img src="docs/feature-plan.png" width="560" alt="A query's plan: four steps, one reading a whole table without an index, highlighted; the history of queries beside it"></a></td>
  </tr>
</table>

<table>
  <tr>
    <td width="560"><a href="docs/studio-builder.png"><img src="docs/feature-builder.png" width="560" alt="The query builder: books joined to authors, counted and averaged per country, filtered and sorted, with the SQL it writes and the result"></a></td>
    <td width="280">
      <sub><b>QUERY BUILDER</b></sub>
      <h3>Joins along the keys</h3>
      <p>Pick a table, join related ones<br>along their foreign keys, count,<br>sum and average, filter and<br>sort. The SQL and the same query<br>as Qivot C++ follow every<br>change.</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td width="280">
      <sub><b>STRUCTURE</b></sub>
      <h3>Columns, keys and<br>indexes</h3>
      <p>Declared types, NOT NULL,<br>defaults and key badges; the<br>foreign keys a table holds and<br>the tables that reference it,<br>one click to jump there; every<br>index.</p>
    </td>
    <td width="560"><a href="docs/studio-structure.png"><img src="docs/feature-structure.png" width="560" alt="The structure of the Track table: columns with key badges, foreign keys, the tables that reference it, and indexes"></a></td>
  </tr>
</table>

<table>
  <tr>
    <td width="560"><a href="docs/studio-recent.png"><img src="docs/feature-recent.png" width="560" alt="The welcome screen listing saved connections, starred, and recent files and servers"></a></td>
    <td width="280">
      <sub><b>CONNECTIONS</b></sub>
      <h3>Saved and recent</h3>
      <p>Files open with a click; servers<br>fill in the Connect dialog,<br>ready for the password. Star the<br>ones to keep. Passwords are<br>never stored.</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td width="280">
      <sub><b>SSL AND SSH</b></sub>
      <h3>Through a tunnel,<br>encrypted</h3>
      <p>An SSH tunnel through your own<br>ssh and key, and each database's<br>TLS settings: PostgreSQL's<br>sslmode and certificates,<br>MySQL's CA, SQL Server's<br>Encrypt.</p>
    </td>
    <td width="560"><a href="docs/studio-connect.png"><img src="docs/feature-connect.png" width="560" alt="The Connect dialog with TLS set to Verify full and a CA file, and an SSH tunnel through a bastion server"></a></td>
  </tr>
</table>

<table>
  <tr>
    <td width="560"><a href="docs/studio-redis.png"><img src="docs/feature-redis.png" width="560" alt="A Redis database's keys with their types and TTLs, and the user:1002 hash's fields in the grid"></a></td>
    <td width="280">
      <sub><b>REDIS</b></sub>
      <h3>Every key, by its type</h3>
      <p>Keys found by pattern, a page at<br>a time, each with its type and<br>TTL. Hashes, lists, sets, sorted<br>sets and streams in the grid;<br>strings as text, JSON formatted,<br>binary as hex.</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td width="280">
      <sub><b>REDIS CONSOLE</b></sub>
      <h3>redis-cli, read-only</h3>
      <p>Commands as redis-cli runs them.<br>Studio asks the server about<br>each one and refuses any that<br>writes until you allow changes;<br>ones that would block never run.</p>
    </td>
    <td width="560"><a href="docs/studio-redis-console.png"><img src="docs/feature-redis-console.png" width="560" alt="The Redis console: HGETALL, ZREVRANGE and TTL answered, and a SET refused because the session is read-only"></a></td>
  </tr>
</table>

<table>
  <tr>
    <td width="560"><a href="docs/studio-redis-server.png"><img src="docs/feature-redis-server.png" width="560" alt="Redis server information: version, role, memory, clients, operations per second, hit rate and the full INFO"></a></td>
    <td width="280">
      <sub><b>REDIS SERVER</b></sub>
      <h3>The server at a glance</h3>
      <p>INFO's highlights (version,<br>role, memory, clients,<br>operations per second, hit rate,<br>eviction) and the rest of it<br>below.</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td width="280">
      <sub><b>COMPARE</b></sub>
      <h3>Two databases, and the<br>SQL between them</h3>
      <p>Compare a file, a server or a<br>sample with another: the<br>differences, and the migration<br>that makes either one match, to<br>copy, save or apply.</p>
    </td>
    <td width="560"><a href="docs/studio-compare.png"><img src="docs/feature-compare.png" width="560" alt="Comparing the bookshop with a changed copy: four differences and the migration that makes the copy match"></a></td>
  </tr>
</table>

<table>
  <tr>
    <td width="560"><a href="docs/studio-migration.png"><img src="docs/feature-migration.png" width="560" alt="The designer's SQL tab: the changes in words and the SQLite migration that makes them"></a></td>
    <td width="280">
      <sub><b>MIGRATION</b></sub>
      <h3>Designs checked<br>against the data</h3>
      <p>Every design change becomes SQL<br>in the database's own dialect.<br>Required columns with NULLs,<br>duplicate values in a unique<br>column, references that point<br>nowhere: each problem is caught<br>before anything runs.</p>
    </td>
  </tr>
</table>

<table>
  <tr>
    <td width="280">
      <sub><b>C++</b></sub>
      <h3>A Qivot model class<br>for every table</h3>
      <p>Following Qivot's real rules: a<br>field per column,<br><code>QiForeignKey</code> where<br>Qivot can follow it, and a note<br>for anything it can't map.</p>
    </td>
    <td width="560"><a href="docs/studio-cpp.png"><img src="docs/feature-cpp.png" width="560" alt="The book table's C++ tab: its Qivot model class, ready to copy"></a></td>
  </tr>
</table>

<table>
  <tr>
    <td width="560"><a href="docs/studio-ide.png"><img src="docs/feature-ide.png" width="560" alt="The exported project open in the IDE, with all eight model tests passing"></a></td>
    <td width="280">
      <sub><b>IDE</b></sub>
      <h3>Build, test and run<br>without leaving Studio</h3>
      <p>Export a CMake project with the<br>models, an example and a test<br>per model, then build ⌘B, test<br>⌘U and run ⌘R, with the<br>compiler's problems a click from<br>the line.</p>
    </td>
  </tr>
</table>

<p align="center"><sub>Click any screenshot to see it full size.</sub></p>

Qivot Studio is a database designer and analyzer built on
[Qivot](https://github.com/austinkottke/Qivot), the Qt ORM. Open a database
and see its whole structure at a glance; design changes to it; and turn it
into Qivot C++ that you can build, test and run without leaving Studio.

### Analyze

- **SQLite and DuckDB files, or a server**: PostgreSQL, MySQL / MariaDB and SQL Server,
  directly or **through an SSH tunnel**, with each one's **TLS** settings
  (PostgreSQL's `sslmode` and certificates, MySQL's CA and client
  certificates, SQL Server's Encrypt / Strict). Tables in other schemas are
  listed as `schema.table`.
- **Several databases at once**, a tab each (⌘N for another): each keeps its own
  place, screens, query tabs and results while you switch between them.
- **DuckDB files** open read-only like SQLite ones, recognised by their header:
  structure, keys and views, data, profiles, queries and DuckDB's own plans.
- **Redis**: browse the keys (a pattern, a page at a time, each with its type
  and TTL), see any value by type (strings as text, JSON formatted, binary as
  hex; hashes, lists, sets, sorted sets and streams in the grid, paged), pick a
  database, and read INFO at a glance. A console runs commands as redis-cli
  does, read-only until you allow changes: Studio asks the server about each
  command and refuses any that writes, and never runs ones that would block
  (MONITOR, SUBSCRIBE, BLPOP…). TLS, ACL users and SSH tunnels work as for the
  other servers.
- **Saved and recent connections** on the welcome screen: files open with a
  click, servers fill in the Connect dialog. Star one to keep it. Passwords
  are never stored.
- **A bird's-eye ER diagram** of the whole database: every table as a card,
  every foreign key as a line from its column to the column it references, with
  crow's-foot notation. Laid out automatically along the relationships, with
  lines routed around the tables in between. Zoom, pan, drag tables, find a
  table by name, and hover one to light up everything it connects to; a
  minimap shows where you are. The mouse wheel zooms around the pointer (or
  pans, if you'd rather: the Scroll menu chooses), a trackpad pans and
  pinches, and choosing a table brings it into view. Cards stay where you drag
  them, per database, and the diagram exports as **PNG, SVG or PDF**.
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
- **Select a block of cells** (drag, Shift-click, a row number for the row,
  ⌘A for everything) in any grid: the bar underneath adds it up (count, sum,
  average, min, max), and ⌘C copies it as tab-separated text that pastes
  straight into a spreadsheet; or with the column names, or as CSV.
- **Column profiles**: for every column, how much is filled, how many distinct
  values, the range, and its shape: a histogram for numbers, the commonest
  values otherwise. Profiled in the background, so big tables don't hold up
  the window.
- **A query builder**: pick a table, join related ones along their foreign
  keys, pick columns (counted, summed, averaged...), filter and sort. The SQL,
  in the database's own dialect, and the same query as Qivot C++ follow every
  change, and the result refreshes as you go.
- **A SQL console** with **tabs** (⌘T, ⌘W; kept per database): syntax
  highlighting and **autocomplete** that knows the query (tables after `FROM`,
  a table's columns after `alias.`), ⌘↩ to run, and **Explain** (⇧⌘↩) to see
  how the database would run it, without running it: a tree of steps,
  estimated rows and cost, with every full table scan flagged. On SQLite,
  PostgreSQL, MySQL and SQL Server.
- **Queries run in the background**, on a connection of their own, so a slow
  one never freezes the window; **Stop** (⌘.) asks the server to cancel it
  (`pg_cancel_backend`, `KILL QUERY`, `KILL`).
- **History and saved queries**, per database: every run with its rows and
  time, failures included; save one by name (⌘S), find it again, double-click
  to run.
- **Export** any table (as filtered and sorted) or any query result, in full,
  to **CSV or JSON**.
- **Read-only until you say so.** Files open read-only, and PostgreSQL and
  MySQL connections are made read-only on the server. Changing anything takes
  an explicit **Allow changes**, which opens a separate connection for writing;
  lock it again with one click. (SQL Server has no read-only session setting;
  Studio doesn't write until you allow it, but a read-only login makes sure.)

### Change

Once changes are allowed, and always shown as SQL before they run:

- **Edit rows**: double-click a cell, use the row inspector, add and delete
  rows, set NULLs. Unsaved changes are marked in the grid; **Review SQL**,
  **Discard** or **Save**, all in one transaction.
- **Undo a save**: old values back, deleted rows back, new rows gone. Undo
  checks the rows are still as they were saved and refuses, changing nothing,
  if they aren't (or if a delete cascaded to other tables).
- **Import CSV** into a table: the file's columns matched to the table's by
  name (or by hand), commas, semicolons or tabs, in one transaction: a bad
  row stops it with its line number, and nothing is imported.
- **Apply a design to the database**: the migration runs in one transaction
  (an SQLite file is backed up first), and the design starts again from the
  result. MySQL commits each change to the structure as it goes, and says so.
- **Compare** two databases' structures (a file, a server, or a sample): the
  differences, and the SQL that makes either one match the other, to copy,
  save, or apply.

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
- **Apply** the migration to the database itself (see *Change*), or, for
  SQLite, **to a copy** of the file, leaving the original untouched.

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
- **A command line**, [`qivot-cli`](#command-line-qivot-cli): inspect, models, projects, diffs and migrations from scripts and CI.
- Light and dark mode, following the system.

## Download

Builds for each platform are on the
[Releases](https://github.com/austinkottke/Qivot-studio/releases) page, with
Qt and the database drivers' client libraries inside, so SQLite, DuckDB, PostgreSQL and
MySQL / MariaDB work with nothing else installed:

| Platform | File | To run it |
|---|---|---|
| macOS, Apple Silicon | `Qivot-Studio-<version>-macOS-AppleSilicon.dmg` | Open it and drag Qivot Studio to Applications. It isn't notarized yet: the first time, right-click the app and choose **Open**. |
| macOS, Intel | `Qivot-Studio-<version>-macOS-Intel.dmg` | The same. |
| Windows 10 / 11 (x64) | `Qivot-Studio-<version>-Windows-x64.zip` | Unzip it anywhere and run `Qivot Studio.exe`. |
| Linux (x86-64) | `Qivot-Studio-<version>-Linux-x86_64.AppImage` | `chmod +x` it and run it. Built on Ubuntu 22.04, so it runs there and on anything newer; it uses the computer's own OpenGL, as every desktop has. |

SQL Server needs Microsoft's *ODBC Driver 18 for SQL Server* on the computer
(on macOS, Qt's ODBC driver needs iODBC, so SQL Server works best from Windows
and Linux).

Releases are made by running the
[Release workflow](https://github.com/austinkottke/Qivot-studio/actions/workflows/release.yml)
by hand (Run workflow: a version, and whether to publish it). Each package is
checked before it's kept: its drivers must load from inside it, and on Windows
and Linux it must connect to a real PostgreSQL (and MySQL) server.

To make them yourself, first build Qt's PostgreSQL and MySQL drivers against
the client libraries you'll bundle (`tools/package/build-sql-drivers.py`), then:
`tools/package-macos.sh <qt>` (a DMG), `tools/package/package-linux.sh <qt>`
(an AppImage) or `tools/package/package-windows.py` (a zip). The steps for each
are in `.github/workflows/release.yml`.

## Build

Needs Qt 6.5 or newer (6.8 recommended), or Qt 5.15, and CMake 3.21+. Point
`CMAKE_PREFIX_PATH` at whichever Qt you have; the build picks Qt 6 when both are found. Qivot is included, as
its single header in `third_party/qivot`, with its DuckDB driver. DuckDB's C
library is downloaded for your platform the first time you configure (into the
build folder); `-DDUCKDB_ROOT=<unzipped libduckdb release>` uses one you have, and
`-DSTUDIO_DUCKDB=OFF` builds without DuckDB:

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
| `--connect <url>` | connect to a server: `postgres://user:pass@host:5432/db`, `mysql://…`, `sqlserver://…`, `redis://:pass@host:6379/0` |
| `--connect-dialog` | start with the connect dialog open |
| `--table <name>` | select a table once the file is open |
| `--view data` | start on the Data tab (or `profile`, `cpp`, `diagram`, `design`, `export`, `structure`) |
| `--query-builder` | open the query builder |
| `--view query --query "…"` | open the SQL console and run a query |
| `--project <folder>` | open a CMake project in the IDE |
| `--models <file>` | write Qivot models for every table to a header and quit |
| `--export <folder>` | write a buildable project (models, example, tests) and quit |
| `--allow-changes` | allow changes to the database once it's open |
| `--compare-with <file>` | on the Compare screen (`--view compare`), compare with a file (or `sample:<id>`) |
| `--export-diagram <file>` | save the diagram as `.png`, `.svg` or `.pdf` once it's laid out |
| `--view query --query "…" --explain` | show a query's plan instead of running it |
| `--dark`, `--light` | force the colour scheme |
| `--size 1440x900`, `--find <table>`, `--select-row <n>`, `--select-cells <t,l,b,r>`, `--design-demo`, `--design-tab sql`, `--builder-demo`, `--edit-demo`, `--complete-demo`, `--build`, `--wheel <notches>`, `--wheel-pixels <dx,dy>` | for screenshots, demos and tests |
| `--shot <png>` | save a screenshot and quit |
| `--smoke` | load and quit; exit 1 if any QML warning was logged (used by CI) |
| `--list-drivers [--require-drivers QPSQL,QMYSQL] [--try-connect <url>]` | say which database drivers load (and whether each server given connects), without a window; exit 1 if a required one doesn't (used to check the packages) |

## Command line: `qivot-cli`

Studio's engine without the window, for scripts and CI. It is built as
`build/cli/qivot-cli`, and every package carries it inside the app:
`qivot-studio cli …` (on macOS, `"Qivot Studio.app/Contents/MacOS/Qivot Studio" cli …`).

A database is a file, `sample:<id>`, a server URL (the password can come from
`QIVOT_PASSWORD` rather than the URL), or `migrations:<dir>`: an SQLite
database built by running a folder of migrations.

```bash
qivot-cli inspect app.db                                  # tables, columns, keys, indexes (--json too)
qivot-cli models postgres://me@db/shop -o src/models.h    # Qivot model classes, as the C++ tab shows them
qivot-cli project app.db -o ~/Projects/App                # a buildable Qt project around them
qivot-cli query app.db "SELECT * FROM book" --format csv  # read-only; table, csv, tsv or json
qivot-cli diff design.db app.db                           # what differs, and the SQL that makes app.db match
```

**Migrations**, run by Qivot's `QiMigrator` (a `qivot_migrations` table, checksums,
a transaction each, a lock on servers):

```bash
qivot-cli migrate new "add tags" --dir migrations --from design.db   # writes 0003_add_tags.up.sql + .down.sql
qivot-cli migrate status app.db --dir migrations
qivot-cli migrate up app.db --dir migrations            # --dry-run prints the SQL instead
qivot-cli migrate down app.db --dir migrations --to 2
```

`migrate new` compares the database you want (`--from`, a file you designed in
Studio, say) with where the migrations have got to so far (`--to`, which defaults
to `migrations:<dir>`, or a development server), and writes the difference as the
next migration, with its down step.

**In CI**, `--exit-code` turns differences into exit code 1:

```bash
qivot-cli migrate status "$DATABASE_URL" --dir migrations --exit-code   # anything pending, or edited after it ran?
qivot-cli diff migrations:migrations app.db --exit-code                 # does the database match the migrations?
```

Exit codes: 0 done, 1 differences or pending (with `--exit-code`), 2 failed.
`qivot-cli --help` lists every option. `diff` and `migrate new` cover tables,
columns, keys, references and unique constraints; a new table gets its indexes,
but other index changes, views and triggers aren't compared yet.

## Connecting to servers

Studio uses Qt's own database drivers, which in turn need each database's client
library on the computer:

| Database | Qt driver | What the computer needs |
|---|---|---|
| PostgreSQL | `QPSQL` (ships with Qt) | `libpq`. On macOS, Qt's driver looks for it in [Postgres.app](https://postgresapp.com). On Linux, `libpq5`. |
| MySQL / MariaDB | `QMYSQL` | Not shipped in Qt's macOS/Windows packages; build it from Qt's sources against `libmysqlclient`. On Linux, `libqt6sql6-mysql`. |
| SQL Server | `QODBC` (ships with Qt) | Microsoft's *ODBC Driver 18 for SQL Server*. |

The connect dialog says when a driver is missing rather than failing obscurely.

**SSL and SSH** are under *SSL and SSH* in the Connect dialog. An SSH tunnel
uses the computer's own `ssh` (OpenSSH, built into macOS, Linux and Windows
10+), signing in with a key file or ssh-agent; a key with a passphrase needs to
be in the agent. A server seen for the first time is added to `known_hosts`;
one whose key has changed is refused. The database's host and port are as the
SSH server sees them.

`tests/tst_servers.cpp` runs Studio against real servers holding the public
[Pagila](https://github.com/devrimgunduz/pagila), [Sakila and Employees](https://dev.mysql.com/doc/index-other.html)
and [Chinook](https://github.com/lerocha/chinook-database) sample databases; each
test is skipped unless its `STUDIO_TEST_PG` / `STUDIO_TEST_MYSQL` /
`STUDIO_TEST_MSSQL` variable points at a server. The SSH test also needs
`STUDIO_TEST_SSH` (an SSH server that can reach the PostgreSQL one, as
`host:port`) and `STUDIO_TEST_SSH_KEY`. `tests/tst_redis.cpp` uses
`STUDIO_TEST_REDIS` (`host:port`) and `STUDIO_TEST_REDIS_PASS`, and writes to
database 7.

## Layout

| Folder | What's there |
|---|---|
| `core/` | `QivotStudio.Core`: opening databases and describing them (`DatabaseSession`), paging any table's rows (`RowsModel`), the diagram layout (`ErLayout`), the SQL console (`QueryModel`), Qivot code generation (`CodeGen`), the designer and its migrations (`SchemaDesign`), the query builder (`QueryBuilder`), column profiles (`TableProfile`), project export (`ProjectExport`), building and testing (`ProjectBuild`), the IDE's files (`Workspace`), the samples (`SampleDatabase`, described once for every database by `SampleSchema`), running scripts (`SqlScript`), CSV/JSON in and out (`DataTransfer`, `CsvImport`), comparing schemas (`SchemaCompare`), diagram pictures (`DiagramExport`), query history (`QueryLibrary`), autocomplete (`SqlCompleter`) and plans (`QueryPlan`). Plain C++ with tests. |
| `ui/` | `QivotUI`, the first cut of **qivot-ui**: theme tokens (light/dark) and components (`ActionButton`, `Badge`, `Card`, `FilterField`, `NavItem`, `SegmentedControl`, `TextBox`). Kept free of Studio specifics so it can become its own library. |
| `app/` | The app: `main.cpp` and the screens in `qml/`. |
| `cli/` | `qivot-cli`: the same engine from the command line (`qivotcli.cpp`; the app runs it as `qivot-studio cli`). |
| `tests/` | Tests for each of the above, including a migration round trip (apply to a copy, read it back, nothing left to change) and an exported project that really builds and passes its own tests; live-server tests (editing, undo, import, applying designs and plans on PostgreSQL, MySQL and SQL Server); whole-app smoke tests. |
| `third_party/qivot/` | Qivot, as its single header (`qivot.hpp`; `qivot.cpp` compiles it once). Schema reading comes from its `QiSchema`. Exported projects get the same files. Update with `tools/update-qivot.sh`. |

## Roadmap

1. **Analyzer**: changes to DuckDB files; editing Redis values in place; Redshift, ClickHouse,
   Snowflake, Oracle and DB2 (through Qivot); keyboard navigation in the
   query builder.
2. **Designer**: send a design's models to an exported project, reorder
   columns by dragging.
3. **IDE**: go to definition, find in files, build kits.

## License

Qivot Studio is released under the [MIT License](LICENSE).
Copyright © 2026 Austin Kottke.

It is built with Qt (LGPL v3), Qivot (MIT) and, for the icon, the Inter
typeface (SIL Open Font License); see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
