#!/usr/bin/env python3
"""Summarize GCC line coverage for application sources after running CTest."""

import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    build = Path(sys.argv[1] if len(sys.argv) > 1 else root / "build").resolve()
    objects = sorted(p for p in (build / "CMakeFiles").rglob("*.gcno")
                     if "/src/" in str(p))
    if not objects:
        raise SystemExit("Configure with -DBLOKKILY_COVERAGE=ON, build, then run CTest first.")
    output = build / "artifacts" / "coverage"
    output.mkdir(parents=True, exist_ok=True)
    files = {}
    with tempfile.TemporaryDirectory(dir=output) as work:
        for obj in objects:
            subprocess.run(["gcov", "-j", "-b", str(obj)], cwd=work,
                           stdout=subprocess.DEVNULL, check=True)
        for report in Path(work).glob("*.gcov.json.gz"):
            for file in json.loads(gzip.decompress(report.read_bytes()))["files"]:
                path = Path(file["file"])
                # gui_main embeds the GUI verification driver; including its
                # assertions would inflate the production coverage percentage.
                if not path.is_relative_to(root / "src") or path.name == "gui_main.cpp":
                    continue
                lines = files.setdefault(str(path.relative_to(root)), {})
                for line in file["lines"]:
                    number = line["line_number"]
                    lines[number] = max(lines.get(number, 0), line["count"])
    rows = {
        path: {"covered": sum(count > 0 for count in lines.values()), "lines": len(lines),
               "uncovered": [number for number, count in sorted(lines.items()) if not count]}
        for path, lines in sorted(files.items())
    }
    (output / "summary.json").write_text(json.dumps(rows, indent=2) + "\n")
    report = ["# Application line coverage", "",
              "GCC/gcov counts for application C++ sources. Excludes vendored libraries,",
              "generated code, tests, QML, the CLI demo, and gui_main.cpp (which embeds",
              "the GUI verification driver). This measures executed lines, not assertions",
              "or branch coverage.", "", "| Area | Covered / executable lines | Coverage |",
              "| --- | ---: | ---: |"]
    for prefix in ("src/audio/", "src/plugins/", "src/instruments/", "src/model/",
                   "src/sequencer/", "src/app/", "src/project/", "src/"):
        group = [row for path, row in rows.items() if path.startswith(prefix)]
        covered = sum(row["covered"] for row in group)
        lines = sum(row["lines"] for row in group)
        percent = f"{covered / lines:.1%}" if lines else "n/a"
        report.append(f"| {prefix} | {covered} / {lines} | {percent} |")
    text = "\n".join(report) + "\n"
    (output / "report.md").write_text(text)
    print(text)


if __name__ == "__main__":
    main()
