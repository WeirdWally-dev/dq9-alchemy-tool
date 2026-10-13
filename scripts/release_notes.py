"""Print the CHANGELOG.md section for a version, after checking it matches version.py.

Used by the release workflow:  python scripts/release_notes.py v1.2.0 > notes.md
Fails if the tag, version.py and CHANGELOG.md disagree, so a release can't ship mislabelled.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    tag = sys.argv[1] if len(sys.argv) > 1 else ""
    want = tag.lstrip("vV")
    code = re.search(r'__version__\s*=\s*"([^"]+)"', (ROOT / "version.py").read_text(encoding="utf-8")).group(1)
    if want != code:
        print(f"Tag {tag!r} does not match version.py ({code}). Bump version.py or fix the tag.", file=sys.stderr)
        return 1

    lines = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8").splitlines()
    out, inside = [], False
    for line in lines:
        if line.startswith("## "):
            if inside:
                break
            inside = re.match(rf"## \[?{re.escape(want)}\]?(\s|$)", line) is not None
            continue
        if inside:
            out.append(line)
    if not inside and not out:
        print(f"CHANGELOG.md has no '## {want}' section. Add one before releasing.", file=sys.stderr)
        return 1
    print("\n".join(out).strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
