#!/usr/bin/env python3
"""Make the Windows zip: the app, Qt (windeployqt), and the DLLs the database
drivers load (libpq, libmysql and theirs), found by reading each DLL's imports.

    tools/package/package-windows.py --qt <prefix> --exe <built qivot-studio exe>
        --version 0.1.0 --out dist
        [--search DIR]...                 where to find the drivers' DLLs (PostgreSQL's bin, MySQL's lib/bin)
        [--require-drivers QPSQL,QMYSQL]  fail unless these load in the finished folder
        [--try-connect URL]...            and fail unless these connect

Needs `pefile` (pip install pefile).
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

import pefile


def imports(dll: pathlib.Path):
    try:
        pe = pefile.PE(str(dll), fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
        return [e.dll.decode() for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])]
    except pefile.PEFormatError:
        return []


def system_dll(name: str) -> bool:
    system = pathlib.Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
    lower = name.lower()
    return (system / name).exists() or lower.startswith(("api-ms-win-", "ext-ms-")) \
        or lower in ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "ucrtbase.dll")


def bundle_dependencies(folder: pathlib.Path, search: list[pathlib.Path]):
    """Copy into `folder` every DLL something in it imports that isn't Windows'
    own or already there, from the search folders; until nothing's missing."""
    missing = set()
    while True:
        present = {p.name.lower() for p in folder.rglob("*.dll")} | {p.name.lower() for p in folder.rglob("*.exe")}
        wanted = set()
        for f in list(folder.rglob("*.dll")) + list(folder.rglob("*.exe")):
            for d in imports(f):
                if d.lower() not in present and not system_dll(d):
                    wanted.add(d)
        copied = False
        for d in sorted(wanted):
            src = next((s / d for s in search if (s / d).exists()), None)
            if src:
                print(f"   + {d}  (from {src.parent})")
                shutil.copy2(src, folder / d)
                copied = True
            else:
                missing.add(d)
        if not copied:
            return sorted(missing - {p.name for p in folder.rglob("*.dll")})


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--qt", required=True, type=pathlib.Path)
    p.add_argument("--exe", required=True, type=pathlib.Path)
    p.add_argument("--version", required=True)
    p.add_argument("--out", default="dist", type=pathlib.Path)
    p.add_argument("--search", action="append", default=[], type=pathlib.Path)
    p.add_argument("--require-drivers", default="")
    p.add_argument("--try-connect", action="append", default=[])
    a = p.parse_args()

    root = pathlib.Path(__file__).resolve().parents[2]
    name = f"Qivot-Studio-{a.version}-Windows-x64"
    folder = a.out.resolve() / name
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    exe = folder / "Qivot Studio.exe"
    shutil.copy2(a.exe, exe)

    print("== Bundling Qt", flush=True)
    subprocess.run([str(a.qt / "bin" / "windeployqt.exe"), "--release", "--no-translations",
                    "--qmldir", str(root / "app" / "qml"), "--qmldir", str(root / "ui"), str(exe)], check=True)
    # Mimer SQL isn't something Studio connects to.
    for f in (folder / "sqldrivers").glob("qsqlmimer*.dll"):
        f.unlink()

    print("== Bundling the database drivers' libraries", flush=True)
    # The built program's own folder first: DuckDB's DLL is copied there by the build.
    search = [a.exe.resolve().parent] + [s for s in a.search if s.exists()]
    missing = bundle_dependencies(folder, search)
    if missing:
        # Only a driver's library may be missing: that driver goes (the app offers
        # only drivers that load); anything else is an error.
        for drv in list((folder / "sqldrivers").glob("*.dll")):
            needs = [d for d in imports(drv) if d in missing]
            if needs:
                print(f"   dropping {drv.name}: needs {', '.join(needs)}")
                drv.unlink()
        still = bundle_dependencies(folder, search)
        if still:
            sys.exit(f"Not found: {', '.join(still)}")

    for f in ("LICENSE", "THIRD-PARTY-NOTICES.md"):
        shutil.copy2(root / f, folder / (f + ".txt" if f == "LICENSE" else f))

    print("== Database drivers", flush=True)
    cmd = [str(exe), "--list-drivers"]
    if a.require_drivers:
        cmd += ["--require-drivers", a.require_drivers]
    for url in a.try_connect:
        cmd += ["--try-connect", url]
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout + r.stderr)
    if r.returncode != 0:
        sys.exit("The drivers check failed")

    print("== Zipping", flush=True)
    archive = shutil.make_archive(str(a.out.resolve() / name), "zip", a.out.resolve(), name)
    print("==", archive, f"({os.path.getsize(archive) // (1024 * 1024)} MB)")


if __name__ == "__main__":
    main()
