#!/usr/bin/python3
"""Launch the Exec command from a freedesktop X session entry."""
import argparse
import configparser
import os
from pathlib import Path
import shlex
import sys


def command_for(entry: Path) -> tuple[list[str], configparser.SectionProxy]:
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str
    with entry.open(encoding="utf-8") as stream:
        parser.read_file(stream)
    desktop = parser["Desktop Entry"]
    if desktop.get("Type", "Application") != "Application":
        raise ValueError(f"unsupported desktop entry type in {entry}")
    if desktop.get("Hidden", "false").lower() == "true":
        raise ValueError(f"desktop entry is hidden: {entry}")
    raw = desktop.get("Exec")
    if not raw:
        raise ValueError(f"missing Exec in {entry}")
    name = desktop.get("Name", entry.stem)
    icon = desktop.get("Icon", "")
    uri = entry.resolve().as_uri()
    result: list[str] = []
    for token in shlex.split(raw, posix=True):
        if token in ("%f", "%F", "%u", "%U"):
            continue
        if token == "%i":
            if icon:
                result.extend(("--icon", icon))
            continue
        value = ""
        i = 0
        while i < len(token):
            if token[i] != "%":
                value += token[i]
                i += 1
                continue
            if i + 1 >= len(token):
                raise ValueError(f"trailing field-code marker in Exec for {entry}: {token}")
            code = token[i + 1]
            if code == "%":
                value += "%"
            elif code == "c":
                value += name
            elif code == "k":
                value += uri
            else:
                raise ValueError(f"unsupported embedded field code %{code} in Exec for {entry}: {token}")
            i += 2
        result.append(value)
    if not result:
        raise ValueError(f"empty Exec command in {entry}")
    return result, desktop


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--desktop-file", required=True, type=Path)
    parser.add_argument("--print-command", action="store_true")
    args = parser.parse_args()
    try:
        argv, desktop = command_for(args.desktop_file)
    except (OSError, KeyError, ValueError, configparser.Error) as exc:
        print(f"desktop session: {exc}", file=sys.stderr)
        return 2
    names = desktop.get("DesktopNames", "").split(":")
    if names and names[0]:
        os.environ["XDG_SESSION_DESKTOP"] = names[0]
        os.environ["XDG_CURRENT_DESKTOP"] = ":".join(names)
    elif desktop.get("X-Dtlogin-DesktopNames"):
        names = desktop["X-Dtlogin-DesktopNames"].split(":")
        os.environ["XDG_SESSION_DESKTOP"] = names[0]
        os.environ["XDG_CURRENT_DESKTOP"] = ":".join(names)
    os.environ["DESKTOP_SESSION"] = args.desktop_file.stem
    os.environ.setdefault("XDG_SESSION_TYPE", "x11")
    if args.print_command:
        print(shlex.join(argv))
        return 0
    os.execvp(argv[0], argv)
    return 127


if __name__ == "__main__":
    raise SystemExit(main())
