#!/usr/bin/env python3
"""Preview/apply one category of approved renames in Simutrans source trees.

This helper is text-based, not a C++ parser. Run each rename category as a
separate reviewed change set; inspect the diff and build both repositories.
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
RULE_KEYS = {
    "identifiers": "identifier_rules",
    "filenames": "filename_rules",
    "directories": "directory_rules",
}


def read_rules(path, mode):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit("Cannot read rules file {}: {}".format(path, exc))
    rules = data.get(RULE_KEYS[mode], [])
    seen, targets, compiled = set(), set(), []
    for number, rule in enumerate(rules, 1):
        old, new = rule.get("old"), rule.get("new")
        if not isinstance(old, str) or not isinstance(new, str) or not old or not new:
            raise SystemExit("Invalid {} rule #{}".format(mode, number))
        if old == new:
            raise SystemExit("Rule #{} has identical old/new values".format(number))
        if old in seen:
            raise SystemExit("Duplicate source mapping: {}".format(old))
        if new in seen or new in targets or new in {item[0] for item in compiled}:
            raise SystemExit("Overlapping source/target mapping involving: {}".format(new))
        seen.add(old)
        targets.add(new)
        compiled.append((old, new))
    if not compiled:
        return None
    if mode == "identifiers":
        alternatives = sorted((re.escape(old) for old, _ in compiled), key=len, reverse=True)
        pattern = re.compile(
            r"(?<![A-Za-z0-9_])(?:" + "|".join(alternatives) + r")(?![A-Za-z0-9_])"
        )
    else:
        # Path components are matched exactly, never as substrings.
        pattern = None
    return {"mapping": dict(compiled), "pattern": pattern, "pairs": compiled}


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


def replace_content(content, rules, mode):
    if mode != "identifiers" or not rules:
        return content, []
    counts = {}

    def replace(match):
        old = match.group(0)
        new = rules["mapping"][old]
        counts[(old, new)] = counts.get((old, new), 0) + 1
        return new

    result = rules["pattern"].sub(replace, content)
    return result, [(old, new, count) for (old, new), count in sorted(counts.items())]


def renamed_path(relative, rules, mode):
    if not rules or mode == "identifiers":
        return relative, []
    parts = list(relative.parts)
    changes = []
    if mode == "filenames":
        index = len(parts) - 1
        old = parts[index]
        if old in rules["mapping"]:
            parts[index] = rules["mapping"][old]
            changes.append((old, parts[index]))
    elif mode == "directories":
        for index in range(len(parts) - 1):
            old = parts[index]
            if old in rules["mapping"]:
                parts[index] = rules["mapping"][old]
                changes.append((old, parts[index]))
    return Path(*parts), changes


def main():
    parser = argparse.ArgumentParser(description="Preview/apply one category of approved Simutrans renames.")
    parser.add_argument("--root", type=Path, default=Path.cwd(), help="repository root")
    parser.add_argument("--rules", type=Path, default=DEFAULT_RULES, help="rename rules JSON")
    parser.add_argument("--mode", choices=tuple(RULE_KEYS), default="identifiers",
                        help="one isolated stage: identifiers, filenames, or directories")
    parser.add_argument("--apply", action="store_true", help="write changes; default only previews")
    parser.add_argument("--include", action="append", default=[], help="extra extension, e.g. .xml")
    parser.add_argument("--exclude", action="append", default=[], help="extra directory name to skip")
    args = parser.parse_args()

    root, rules_path = args.root.resolve(), args.rules.resolve()
    if not root.is_dir():
        parser.error("repository root is not a directory: {}".format(root))
    if not rules_path.is_file():
        parser.error("rules file not found: {}".format(rules_path))
    rules = read_rules(rules_path, args.mode)
    if not rules:
        print("No {} rules configured. No files were changed.".format(args.mode))
        return 0

    extensions = set(SOURCE_EXTENSIONS)
    extensions.update(ext.lower() if ext.startswith(".") else "." + ext.lower() for ext in args.include)
    excludes = EXCLUDED_DIRS | set(args.exclude)
    changes, totals = [], {}

    for path in walk_files(root, extensions, excludes):
        try:
            original = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        except OSError as exc:
            print("SKIP {}: {}".format(path, exc), file=sys.stderr)
            continue
        updated, counts = replace_content(original, rules, args.mode)
        relative = path.relative_to(root)
        new_relative, path_changes = renamed_path(relative, rules, args.mode)
        if updated != original or new_relative != relative:
            changes.append((path, root / new_relative, updated, counts, path_changes))
            for old, new, count in counts:
                totals[(old, new)] = totals.get((old, new), 0) + count

    print("{} [{}]: {} file(s) would be affected".format(
        "APPLY" if args.apply else "DRY-RUN", args.mode, len(changes)))
    for (old, new), count in sorted(totals.items()):
        print("  content {} -> {}: {} occurrence(s)".format(old, new, count))
    for old_path, new_path, _, counts, path_changes in changes:
        notes = ["{} -> {}".format(old, new) for old, new in path_changes]
        notes.extend("{} -> {} x{}".format(old, new, count) for old, new, count in counts)
        print("  {}: {}".format(old_path.relative_to(root), "; ".join(notes) or str(new_path.relative_to(root))))

    if not args.apply:
        print("Preview only. Review the output; use --apply only for this category after approval.")
        return 0

    destinations = [new for _, new, *_ in changes]
    if len(destinations) != len(set(destinations)):
        raise SystemExit("Refusing to apply: multiple source files map to the same target path.")
    source_paths = {old for old, *_ in changes}
    for old_path, new_path, *_ in changes:
        if old_path != new_path and new_path.exists() and new_path not in source_paths:
            raise SystemExit("Refusing to overwrite existing path: {}".format(new_path.relative_to(root)))

    staged = []
    try:
        for old_path, new_path, updated, _, _ in changes:
            temp_path = old_path.with_name(old_path.name + ".rename-tmp")
            if temp_path.exists():
                raise SystemExit("Temporary path already exists: {}".format(temp_path))
            temp_path.write_text(updated, encoding="utf-8", newline="")
            staged.append((old_path, new_path, temp_path))
    except (OSError, SystemExit) as exc:
        for _, _, temp_path in staged:
            try:
                temp_path.unlink()
            except OSError:
                pass
        if isinstance(exc, SystemExit):
            raise
        print("ERROR while preparing files: {}".format(exc), file=sys.stderr)
        return 2

    # Move renamed files first only after all content has been staged.
    moved = []
    try:
        for old_path, new_path, temp_path in staged:
            if old_path == new_path:
                os.replace(str(temp_path), str(old_path))
            else:
                new_path.parent.mkdir(parents=True, exist_ok=True)
                if new_path.exists():
                    # Permit target paths occupied by another source in a rename cycle
                    # only when that source has already been moved out of the way.
                    raise OSError("Target path already exists: {}".format(new_path))
                os.replace(str(temp_path), str(new_path))
                old_path.unlink()
                moved.append((old_path, new_path))
            print("WROTE {}".format(new_path.relative_to(root)))
    except OSError as exc:
        print("ERROR while applying changes: {}".format(exc), file=sys.stderr)
        print("Some files may already have changed; inspect git status and restore from version control if needed.", file=sys.stderr)
        return 2

    print("Done. Inspect git diff, build, run tests, and validate both repositories.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
