#!/usr/bin/env python3
"""Preview or apply the shared, explicitly approved Simutrans rename rules.

Works in simutrans-extended-plus and simutrans-exjp.
Default is dry-run. It performs exact token replacement, not C++ parsing.
Always use a clean worktree and review the complete diff before building/testing.
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

DEFAULT_RULES = Path(__file__).with_name("rename-rules.json")
SOURCE_EXTENSIONS = {
    ".h", ".hh", ".hpp", ".cc", ".cpp", ".c", ".cxx", ".inl",
    ".cmake", ".txt", ".vcxproj", ".vcproj", ".filters", ".sln",
    ".mk", ".mak", ".py", ".sh", ".bat", ".ps1", ".rc", ".def",
}
EXCLUDED_DIRS = {
    ".git", ".vs", "Debug", "Debug32", "Debug64", "Release", "Release32",
    "Release64", "x64", "Win32", "build", "cmake-build-debug",
    "cmake-build-release", "squirrel", "openttd",
}


def read_rules(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit("Cannot read rules file {}: {}".format(path, exc))
    rules = data.get("identifier_rules", [])
    seen = set()
    compiled = []
    for number, rule in enumerate(rules, 1):
        old, new = rule.get("old"), rule.get("new")
        if not isinstance(old, str) or not isinstance(new, str) or not old or not new:
            raise SystemExit("Invalid rule #{}".format(number))
        if old == new:
            raise SystemExit("Rule #{} has identical old/new values".format(number))
        if old in seen:
            raise SystemExit("Duplicate source identifier: {}".format(old))
        seen.add(old)
        compiled.append({
            "old": old,
            "new": new,
            "pattern": re.compile(r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])"),
        })
    return compiled


def walk_files(root, extensions, excludes):
    for current, dirs, names in os.walk(str(root)):
        current_path = Path(current)
        dirs[:] = sorted(
            name for name in dirs
            if name not in excludes and not (current_path / name).is_symlink()
        )
        for name in sorted(names):
            path = current_path / name
            if path.is_symlink():
                continue
            if path.suffix.lower() in extensions or name in {"Makefile", "CMakeLists.txt"}:
                yield path


def replace_identifiers(content, rules):
    counts = []
    for rule in rules:
        content, count = rule["pattern"].subn(rule["new"], content)
        if count:
            counts.append((rule["old"], rule["new"], count))
    return content, counts


def renamed_path(relative, rules):
    parts = []
    changes = []
    for original_part in relative.parts:
        part = original_part
        for rule in rules:
            replacement, count = rule["pattern"].subn(rule["new"], part)
            if count:
                changes.append((part, replacement))
                part = replacement
        parts.append(part)
    return Path(*parts), changes


def main():
    parser = argparse.ArgumentParser(description="Preview/apply approved Simutrans renames.")
    parser.add_argument("--root", type=Path, default=Path.cwd(), help="repository root")
    parser.add_argument("--rules", type=Path, default=DEFAULT_RULES, help="rename rules JSON")
    parser.add_argument("--apply", action="store_true", help="write changes; default only previews")
    parser.add_argument("--include", action="append", default=[], help="extra extension, e.g. .xml")
    parser.add_argument("--exclude", action="append", default=[], help="extra directory name to skip")
    parser.add_argument("--rename-files", action="store_true", help="also rename file paths (default: content-only)")
    args = parser.parse_args()

    root = args.root.resolve()
    rules_path = args.rules.resolve()
    if not root.is_dir():
        parser.error("repository root is not a directory: {}".format(root))
    if not rules_path.is_file():
        parser.error("rules file not found: {}".format(rules_path))

    rules = read_rules(rules_path)
    if not rules:
        print("No rename rules configured. No files were changed.")
        return 0

    extensions = set(SOURCE_EXTENSIONS)
    extensions.update(ext.lower() if ext.startswith(".") else "." + ext.lower() for ext in args.include)
    excludes = EXCLUDED_DIRS | set(args.exclude)
    changes = []
    totals = {}

    for path in walk_files(root, extensions, excludes):
        try:
            original = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        except OSError as exc:
            print("SKIP {}: {}".format(path, exc), file=sys.stderr)
            continue

        updated, counts = replace_identifiers(original, rules)
        relative = path.relative_to(root)
        new_relative, path_changes = renamed_path(relative, rules) if args.rename_files else (relative, [])
        if updated != original or new_relative != relative:
            changes.append((path, root / new_relative, updated, counts, path_changes))
            for old, new, count in counts:
                totals[(old, new)] = totals.get((old, new), 0) + count

    print("{}: {} file(s) would be affected".format("APPLY" if args.apply else "DRY-RUN", len(changes)))
    for (old, new), count in sorted(totals.items()):
        print("  identifier {} -> {}: {} occurrence(s)".format(old, new, count))
    for old_path, new_path, _, counts, path_changes in changes:
        note = ", ".join("{} -> {} x{}".format(old, new, count) for old, new, count in counts)
        if old_path != new_path:
            note = (note + "; " if note else "") + "path {} -> {}".format(
                old_path.relative_to(root), new_path.relative_to(root)
            )
        print("  {}".format(note or old_path.relative_to(root)))

    if not args.apply:
        print("Preview only. No files were modified; use --apply after reviewing this output.")
        return 0

    destinations = [new for _, new, *_ in changes]
    if len(destinations) != len(set(destinations)):
        raise SystemExit("Refusing to apply: multiple source files map to the same target path.")
    for old_path, new_path, *_ in changes:
        if old_path != new_path and new_path.exists():
            raise SystemExit("Refusing to overwrite existing path: {}".format(new_path.relative_to(root)))
        if old_path != new_path and old_path.with_name(old_path.name + ".rename-tmp").exists():
            raise SystemExit("Temporary path already exists: {}".format(old_path.with_name(old_path.name + ".rename-tmp")))

    staged = []
    try:
        for old_path, new_path, updated, _, _ in changes:
            temp_path = old_path.with_name(old_path.name + ".rename-tmp") if old_path != new_path else old_path
            temp_path.write_text(updated, encoding="utf-8", newline="")
            staged.append((old_path, new_path, temp_path))
        for old_path, new_path, temp_path in staged:
            if old_path != new_path:
                new_path.parent.mkdir(parents=True, exist_ok=True)
                temp_path.replace(new_path)
            print("WROTE {}".format(new_path.relative_to(root)))
        for old_path, new_path, _ in staged:
            if old_path != new_path and old_path.exists():
                old_path.unlink()
    except OSError as exc:
        print("ERROR while applying changes: {}".format(exc), file=sys.stderr)
        print("Inspect git status and any *.rename-tmp files before retrying.", file=sys.stderr)
        return 2

    print("Done. Inspect git diff, build, run tests, and validate both repositories.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
