#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Jolla Mobile Ltd
"""Manage only our token in ngfd's effective configuration, never a full backup."""

import argparse
import base64
import fcntl
import json
import os
from pathlib import Path
import pwd
import re
import stat
import subprocess
import sys
import tempfile

CONFIGS = ("usr/share/ngfd/ngfd.ini", "etc/ngfd/ngfd.ini")
STATE = "var/lib/ngfd-plugin-inari-blue"
TOKEN = b"inari-blue"


def read_regular(path):
    if path.is_symlink():
        raise ValueError("Refusing symlink: " + str(path))
    if not path.exists():
        return None
    if not path.is_file():
        raise ValueError("Not a regular file: " + str(path))
    return path.read_bytes()


def atomic_write(path, data, expected):
    """Replace atomically, preserving permissions, owner and extended attributes."""
    old_stat = path.stat() if expected is not None else None
    fd, temporary = tempfile.mkstemp(prefix=".inari-", dir=str(path.parent))
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            if old_stat:
                current = os.fstat(stream.fileno())
                if (current.st_uid, current.st_gid) != (old_stat.st_uid, old_stat.st_gid):
                    os.fchown(stream.fileno(), old_stat.st_uid, old_stat.st_gid)
                os.fchmod(stream.fileno(), stat.S_IMODE(old_stat.st_mode))
                for key in os.listxattr(str(path)):
                    os.setxattr(temporary, key, os.getxattr(str(path), key))
            os.fsync(stream.fileno())
        if read_regular(path) != expected:
            raise ValueError("File changed concurrently: " + str(path))
        os.replace(temporary, str(path))
        directory_fd = os.open(str(path.parent), os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def locate(data):
    """Return lines and the single general/plugins-optional key or insertion point."""
    lines = data.splitlines(keepends=True)
    group = False
    groups = 0
    key = None
    insertion = None
    for index, line in enumerate(lines):
        section = re.fullmatch(rb"[ \t]*\[([^\]\r\n]+)\][ \t]*(?:\r?\n)?", line)
        if section:
            group = section[1] == b"general"
            if group:
                groups += 1
                insertion = index + 1
        elif group and re.match(rb"[ \t]*plugins-optional[ \t]*=", line):
            if key is not None:
                raise ValueError("Duplicate plugins-optional key")
            key = index
    if groups != 1:
        raise ValueError("Expected exactly one [general] section")
    return lines, key, insertion


def parts(line):
    match = re.fullmatch(rb"(.*?=[ \t]*)(.*?)([ \t]*(?:\r?\n)?)", line)
    if not match:
        raise ValueError("Malformed plugins-optional line")
    return match.groups()


def token_span(value):
    # Plugin names are literal list entries. Never match a substring or comment.
    return re.search(rb"(?:^|;)(inari-blue)(?=;|$)", value)


def add(data):
    lines, key, insertion = locate(data)
    if key is None:
        if not lines[insertion - 1].endswith(b"\n"):
            raise ValueError("Unterminated [general] header")
        newline = b"\r\n" if lines[insertion - 1].endswith(b"\r\n") else b"\n"
        before = b""
        after = b"plugins-optional = inari-blue" + newline
        lines.insert(insertion, after)
    else:
        before = lines[key]
        prefix, value, suffix = parts(before)
        if token_span(value):
            return data, None
        separator = b";" if value and not value.endswith(b";") else b""
        after = prefix + value + separator + TOKEN + suffix
        lines[key] = after
    record = {"before": base64.b64encode(before).decode("ascii"),
              "after": base64.b64encode(after).decode("ascii")}
    return b"".join(lines), record


def remove(data, record):
    lines, key, _ = locate(data)
    if key is None:
        return data
    before = base64.b64decode(record["before"], validate=True)
    after = base64.b64decode(record["after"], validate=True)
    if lines[key] == after:
        lines[key] = before
    else:
        prefix, value, suffix = parts(lines[key])
        match = token_span(value)
        if not match:
            return data
        start, end = match.span(1)
        if start:
            start -= 1  # Prefer removing the separator we appended.
        elif end < len(value):
            end += 1
        value = value[:start] + value[end:]
        lines[key] = prefix + value + suffix if before or value else b""
    return b"".join(lines)


def manage(root, action):
    directory = root / STATE
    if action == "remove" and not directory.exists():
        return
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    # Lock the package-owned directory; no leftover lock file on uninstall.
    fd = os.open(str(directory), os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        state_path = directory / "managed.json"
        raw_state = read_regular(state_path)
        records = json.loads(raw_state.decode("ascii")) if raw_state else {}
        if set(records) - set(CONFIGS):
            raise ValueError("Unknown configuration path in ownership record")
        if action in ("install", "refresh"):
            if action == "refresh" and CONFIGS[0] in records:
                # ngfd-settings-sailfish has replaced its own payload. Forget our
                # old vendor-file edit; a now-bundled token belongs to that package.
                del records[CONFIGS[0]]
                updated_state = json.dumps(records, sort_keys=True).encode("ascii")
                atomic_write(state_path, updated_state, raw_state)
                raw_state = updated_state
            relative = CONFIGS[1] if (root / CONFIGS[1]).exists() else CONFIGS[0]
            path = root / relative
            original = read_regular(path)
            if original is None:
                raise ValueError("Missing ngfd configuration: " + str(path))
            updated, record = add(original)
            if record:
                records[relative] = record
                # Persist ownership first so an interrupted install is reversible.
                atomic_write(state_path, json.dumps(records, sort_keys=True).encode("ascii"), raw_state)
                atomic_write(path, updated, original)
        else:
            for relative in list(records):
                path = root / relative
                original = read_regular(path)
                if original is not None:
                    updated = remove(original, records[relative])
                    if updated != original:
                        atomic_write(path, updated, original)
                del records[relative]
                updated_state = json.dumps(records, sort_keys=True).encode("ascii")
                atomic_write(state_path, updated_state, raw_state)
                raw_state = updated_state
            if state_path.exists():
                state_path.unlink()
    finally:
        os.close(fd)


def restart_sessions():
    """Reload only already-running ngfd services in existing user sessions."""
    for runtime in Path("/run/user").glob("[0-9]*"):
        if not runtime.name.isdigit():
            continue
        sockets = (runtime / "bus", runtime / "dbus/user_bus_socket")
        bus = next((p for p in sockets if p.exists() and stat.S_ISSOCK(p.stat().st_mode)), None)
        if bus is None:
            continue
        try:
            user = pwd.getpwuid(int(runtime.name))
        except KeyError:
            continue
        command = ["/usr/sbin/runuser", "-u", user.pw_name, "--", "/usr/bin/env",
                   "XDG_RUNTIME_DIR=" + str(runtime), "DBUS_SESSION_BUS_ADDRESS=unix:path=" + str(bus),
                   "/usr/bin/systemctl", "--user", "try-restart", "ngfd.service"]
        try:
            subprocess.run(command, check=True, timeout=15, cwd="/")
        except (OSError, subprocess.SubprocessError) as error:
            print("Inari Blue: restart ngfd or log in again: " + str(error), file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("install", "refresh", "remove", "restart"))
    parser.add_argument("--root", type=Path, default=Path("/"), help="Test filesystem root; disables service restarts")
    args = parser.parse_args()
    if args.action == "restart":
        if args.root != Path("/"):
            parser.error("restart is only allowed with the real root")
        restart_sessions()
    else:
        manage(args.root, args.action)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        sys.exit("Inari Blue configuration: " + str(error))
