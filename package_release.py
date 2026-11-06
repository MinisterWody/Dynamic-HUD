#!/usr/bin/env python3
"""Create the Dynamic HUD 1.0 release ZIP directly from the repository tree."""

from __future__ import annotations

import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent
VERSION = "1.0"
FIXED_TIME = (1980, 1, 1, 0, 0, 0)


def package_entries() -> list[tuple[Path, str]]:
    return [
        (ROOT / "Plugin" / "Built" / "DynamicHUD.dll", "Data/F4SE/Plugins/DynamicHUD.dll"),
        (ROOT / "MCM" / "Config" / "DynamicHUD" / "config.json", "Data/MCM/Config/DynamicHUD/config.json"),
        (ROOT / "MCM" / "Config" / "DynamicHUD" / "settings.ini", "Data/MCM/Config/DynamicHUD/settings.ini"),
        (ROOT / "MCM" / "Config" / "DynamicHUD" / "keybinds.json", "Data/MCM/Config/DynamicHUD/keybinds.json"),
        (ROOT / "MCM" / "Config" / "DynamicHUD" / "CustomElements" / "README.txt", "Data/MCM/Config/DynamicHUD/CustomElements/README.txt"),
    ]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Output ZIP path")
    args = parser.parse_args()
    output = (args.output or ROOT / f"Dynamic HUD {VERSION}.zip").resolve()

    entries = package_entries()
    missing = [str(path.relative_to(ROOT)) for path, _ in entries if not path.is_file()]
    if missing:
        raise SystemExit("Missing release input(s):\n  " + "\n  ".join(missing))

    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path, archive_name in sorted(entries, key=lambda item: item[1].lower()):
            info = zipfile.ZipInfo(archive_name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            zf.writestr(info, path.read_bytes())

    expected = {archive_name for _, archive_name in entries}
    with zipfile.ZipFile(output, "r") as zf:
        bad = zf.testzip()
        if bad:
            raise SystemExit(f"ZIP integrity check failed at: {bad}")
        names = set(zf.namelist())
        if names != expected:
            missing_names = sorted(expected - names)
            extra_names = sorted(names - expected)
            raise SystemExit(
                "Unexpected release contents."
                + ("\nMissing: " + ", ".join(missing_names) if missing_names else "")
                + ("\nExtra: " + ", ".join(extra_names) if extra_names else "")
            )

    print(output)


if __name__ == "__main__":
    main()
