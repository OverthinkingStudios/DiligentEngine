#!/usr/bin/env python3
"""Convert EarthworksFX fprintf logging calls to spdlog / fmt::format+fputs."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "src" / "core"

LOG_TARGETS = {
    "terrafectorSystem::_logfile",
    "_logfile",
    "logFile",
}

EXPORT_TARGETS = {
    "file",
    "listfile",
    "summary",
    "bake.txt_file",
}

SPEC_MAP = [
    (r"%(\d+)\.(\d+)f", r"{:\1.\2f}"),
    (r"%(\d+)d", r"{}"),
    (r"%(\d+)u", r"{}"),
    (r"%(\d+)x", r"{:#x}"),
    (r"%s", r"{}"),
    (r"%d", r"{}"),
    (r"%u", r"{}"),
    (r"%f", r"{}"),
    (r"%c", r"{}"),
    (r"%x", r"{:#x}"),
    (r"%X", r"{:#X}"),
    (r"%p", r"{}"),
    (r"%ld", r"{}"),
    (r"%lu", r"{}"),
    (r"%zd", r"{}"),
    (r"%zu", r"{}"),
    (r"%7\.d", r"{}"),
    (r"%6d", r"{}"),
    (r"%3d", r"{}"),
    (r"%5\.4f", r"{:.4f}"),
    (r"%2\.3f", r"{:.3f}"),
    (r"%2\.2f", r"{:.2f}"),
    (r"%2\.1f", r"{:.1f}"),
    (r"%3\.1f", r"{:.1f}"),
    (r"%3\.3f", r"{:.3f}"),
    (r"%4\.3f", r"{:.3f}"),
    (r"%2,1f", r"{:.1f}"),
    (r"% \n", r" %"),
]


def printf_fmt_to_spdlog(fmt: str) -> str:
    fmt = fmt.replace("\\n", "")
    fmt = fmt.replace("\n", "")
    for pat, repl in SPEC_MAP:
        fmt = re.sub(pat, repl, fmt)
    fmt = fmt.replace("%%", "%")
    return fmt.strip()


def pick_level(text: str) -> str:
    lower = text.lower()
    if "error" in lower or "failed" in lower or "fix " in lower:
        return "error"
    if "warn" in lower:
        return "warn"
    return "info"


def split_fprintf_args(args: str) -> tuple[str, str]:
    args = args.strip()
    if not args.startswith('"'):
        raise ValueError(f"expected format string first: {args[:80]}")
    i = 1
    escaped = False
    while i < len(args):
        c = args[i]
        if escaped:
            escaped = False
        elif c == "\\":
            escaped = True
        elif c == '"':
            break
        i += 1
    fmt = args[1:i]
    rest = args[i + 1 :].strip()
    if rest.startswith(","):
        rest = rest[1:].strip()
    return fmt, rest


def convert_fprintf_call(target: str, args: str, indent: str) -> list[str]:
    fmt, rest = split_fprintf_args(args)
    spd_fmt = printf_fmt_to_spdlog(fmt)

    if target in LOG_TARGETS:
        level = pick_level(fmt)
        if rest:
            return [f'{indent}spdlog::{level}("{spd_fmt}", {rest});']
        return [f'{indent}spdlog::{level}("{spd_fmt}");']

    if target in EXPORT_TARGETS:
        if rest:
            return [
                f'{indent}std::fputs(fmt::format("{spd_fmt}\\n", {rest}).c_str(), {target});'
            ]
        return [f'{indent}std::fputs(fmt::format("{spd_fmt}\\n").c_str(), {target});']

    raise ValueError(f"unknown fprintf target: {target}")


def convert_indent_loop(line: str) -> str | None:
    m = re.match(
        r'^(\s*)for\s*\(\s*int\s+(\w+)\s*=\s*0;\s*\2\s*<\s*([^;]+);\s*\2\+\+\s*\)\s*fprintf\s*\(\s*(terrafectorSystem::_logfile|_logfile|logFile)\s*,\s*"(\s*)"\s*\)\s*;',
        line,
    )
    if not m:
        return None
    indent, _, depth_expr, _, spaces = m.groups()
    width = len(spaces) if spaces else 4
    return (
        f'{indent}spdlog::info("{spaces}{{}}", '
        f'std::string(static_cast<size_t>({depth_expr}) * {width}, \' \'));'
    )


def process_file(path: Path) -> bool:
    text = path.read_text(encoding="utf-8", errors="replace")
    original = text
    out_lines: list[str] = []

    for line in text.splitlines(keepends=True):
        stripped = line.lstrip()
        indent = line[: len(line) - len(stripped)]

        if stripped.startswith("//"):
            if "fprintf" in stripped or "fflush" in stripped:
                continue
            out_lines.append(line)
            continue

        if re.search(r"fflush\s*\(\s*(terrafectorSystem::_logfile|_logfile|logFile)\s*\)\s*;", line):
            continue

        loop_conv = convert_indent_loop(line.rstrip("\n"))
        if loop_conv is not None:
            out_lines.append(loop_conv + "\n")
            continue

        m = re.search(
            r"fprintf\s*\(\s*([A-Za-z_][\w.:]*)\s*,\s*(.+)\)\s*;",
            line.rstrip("\n"),
        )
        if m:
            target, args = m.group(1), m.group(2)
            try:
                converted = convert_fprintf_call(target, args, indent)
                out_lines.extend(c + "\n" for c in converted)
                continue
            except Exception as exc:
                print(f"{path}:{line.strip()} -> skip ({exc})", file=sys.stderr)

        out_lines.append(line)

    new_text = "".join(out_lines)
    if new_text != original:
        path.write_text(new_text, encoding="utf-8")
        return True
    return False


def main() -> int:
    changed = []
    for path in sorted(ROOT.rglob("*")):
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue
        if process_file(path):
            changed.append(path)
    print(f"Updated {len(changed)} files")
    for p in changed:
        print(f"  {p.relative_to(ROOT.parent.parent)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
