# Third-party notices

Qivot Studio is MIT licensed (see `LICENSE`). It is built with, and its
downloadable builds include, the following:

## Qt

Qivot Studio uses the [Qt framework](https://www.qt.io), under the
**GNU Lesser General Public License v3** (LGPL-3.0). The macOS DMG ships Qt's
libraries unmodified and dynamically linked (in `Qivot Studio.app/Contents/Frameworks`),
so they can be replaced with other builds of the same Qt version.

- Qt's licences: https://doc.qt.io/qt-6/licensing.html
- The LGPL v3: https://www.gnu.org/licenses/lgpl-3.0.html
- Qt's source code: https://download.qt.io/official_releases/qt/

Qt is Copyright (C) The Qt Company Ltd. and other contributors.

## Qivot

The ORM Studio is built on, included as its single header (`third_party/qivot`,
with its `LICENSE.txt` and `NOTICE.txt`). Projects Studio exports carry the same files.
MIT License, Copyright (c) 2026 Austin Kottke. https://github.com/austinkottke/Qivot

## Inter

The app icon's lettering is set in Inter Display (`tools/fonts`), by
The Inter Project Authors, under the **SIL Open Font License 1.1**
(`tools/fonts/OFL.txt`). https://github.com/rsms/inter

## Database drivers

The DMG includes Qt's SQLite, PostgreSQL and ODBC drivers. The PostgreSQL and
SQL Server client libraries they load (libpq, Microsoft's ODBC driver) are not
included; they come with Postgres.app and Microsoft's installer, under their
own licences.
