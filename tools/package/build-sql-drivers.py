#!/usr/bin/env python3
"""Build Qt's PostgreSQL and MySQL drivers for a Qt installation, from Qt's
own source, against the client libraries this computer has, and install them
into that Qt's plugins/sqldrivers. The packaging steps then bundle them with
the libraries they load (libpq, libmysqlclient, and theirs).

Qt's binary packages ship the PostgreSQL driver built against a libpq at a
path that's rarely there, and no MySQL driver at all; built here, both load
from the app's own folder.

    tools/package/build-sql-drivers.py --qt <prefix>
        [--pg-root DIR]                       PostgreSQL's prefix (include/, lib/)
        [--mysql-include DIR --mysql-lib FILE] MySQL's C client
        [--arch arm64|x86_64]                 macOS: the one architecture to build
        [--work DIR]                          where to download and build (default: build-sql-drivers)

Run where Qt's compiler is set up (on Windows, an MSVC developer prompt).
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import urllib.request


def run(cmd, **kw):
    print("$", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def qt_version(qt: pathlib.Path) -> str:
    for name in ("qtpaths6", "qtpaths", "qmake6", "qmake"):
        exe = qt / "bin" / (name + (".exe" if os.name == "nt" else ""))
        if exe.exists():
            args = ["--query", "QT_VERSION"] if name.startswith("qtpaths") else ["-query", "QT_VERSION"]
            return subprocess.run([str(exe)] + args, check=True, capture_output=True, text=True).stdout.strip()
    sys.exit(f"No qtpaths or qmake in {qt}/bin")


def fetch_sources(version: str, work: pathlib.Path) -> pathlib.Path:
    """qtbase's sqldrivers folder (and SQLite's, which its CMake refers to)."""
    top = f"qtbase-everywhere-src-{version}"
    dest = work / top
    if (dest / "src/plugins/sqldrivers/CMakeLists.txt").exists():
        return dest
    archive = work / f"{top}.tar.xz"
    if not archive.exists():
        minor = ".".join(version.split(".")[:2])
        for base in ("https://download.qt.io/official_releases/qt", "https://download.qt.io/archive/qt"):
            url = f"{base}/{minor}/{version}/submodules/{top}.tar.xz"
            try:
                print("Downloading", url, flush=True)
                with urllib.request.urlopen(url, timeout=120) as r, open(archive, "wb") as f:
                    shutil.copyfileobj(r, f)
                break
            except Exception as e:  # try the next mirror
                print("  ", e, flush=True)
                archive.unlink(missing_ok=True)
        else:
            sys.exit("Couldn't download qtbase's sources")
    wanted = (f"{top}/src/plugins/sqldrivers/", f"{top}/src/3rdparty/sqlite/", f"{top}/.cmake.conf")
    with tarfile.open(archive) as t:
        members = [m for m in t.getmembers() if m.name.startswith(wanted)]
        t.extractall(work, members=members)
    return dest


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--qt", required=True, type=pathlib.Path)
    p.add_argument("--pg-root")
    p.add_argument("--mysql-include")
    p.add_argument("--mysql-lib")
    p.add_argument("--arch")
    p.add_argument("--work", default="build-sql-drivers", type=pathlib.Path)
    a = p.parse_args()

    qt = a.qt.resolve()
    work = a.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    version = qt_version(qt)
    src = fetch_sources(version, work) / "src/plugins/sqldrivers"
    build = work / "build"
    shutil.rmtree(build, ignore_errors=True)

    qt_cmake = qt / "bin" / ("qt-cmake.bat" if os.name == "nt" else "qt-cmake")
    cmake = [qt_cmake, "-S", src, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
             f"-DCMAKE_INSTALL_PREFIX={qt}",
             # Only the two being added; Qt's own SQLite and ODBC drivers stay as shipped.
             "-DFEATURE_sql_psql=ON", "-DFEATURE_sql_mysql=ON",
             "-DFEATURE_sql_sqlite=OFF", "-DFEATURE_sql_odbc=OFF", "-DFEATURE_sql_mimer=OFF",
             "-DFEATURE_sql_oci=OFF", "-DFEATURE_sql_db2=OFF", "-DFEATURE_sql_ibase=OFF"]
    if a.pg_root:
        cmake.append(f"-DPostgreSQL_ROOT={a.pg_root}")
    if a.mysql_include:
        cmake.append(f"-DMySQL_INCLUDE_DIR={a.mysql_include}")
    if a.mysql_lib:
        cmake.append(f"-DMySQL_LIBRARY={a.mysql_lib}")
    if a.arch:
        # Just this one: Qt's toolchain builds all of a universal Qt's
        # architectures for its own modules, and forces the MySQL driver to
        # Intel only, unless told otherwise.
        cmake += [f"-DCMAKE_OSX_ARCHITECTURES={a.arch}", "-DQT_FORCE_SINGLE_QT_OSX_ARCHITECTURE=ON",
                  "-DQT_FORCE_MACOS_ALL_ARCHES=ON"]
    run(cmake)
    run(["cmake", "--build", build, "--parallel"])
    run(["cmake", "--install", build])

    plugins = qt / "plugins" / "sqldrivers"
    if a.arch and sys.platform == "darwin":
        for f in plugins.glob("libqsql*sql.dylib"):
            archs = subprocess.run(["lipo", "-archs", str(f)], capture_output=True, text=True).stdout.split()
            print(f.name, "is", " ".join(archs))
            if archs != [a.arch]:
                sys.exit(f"{f.name} was built for {' '.join(archs)}, not {a.arch}")
    found = sorted(f.name for f in plugins.iterdir() if "psql" in f.name or "mysql" in f.name)
    print("Installed in", plugins, ":", ", ".join(found))
    if not any("psql" in f for f in found) or not any("mysql" in f for f in found):
        sys.exit("The PostgreSQL or MySQL driver didn't build")


if __name__ == "__main__":
    main()
