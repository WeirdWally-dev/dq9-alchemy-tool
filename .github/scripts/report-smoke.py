"""Turns the emulator smoke test's logs and screenshots into GitHub annotations, which (unlike job
logs and artifacts) anyone can read without signing in. Screenshots are shrunk to small JPEGs,
base64-encoded and split over a few annotations."""
import base64, io, os, re, sys

from PIL import Image

SMOKE = "smoke"


def annotate(title, text, level="warning"):
    text = text.replace("%", "%25").replace("\r", "").replace("\n", "%0A")
    print(f"::{level} title={title}::{text}")


def read(name):
    try:
        with open(os.path.join(SMOKE, name), encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


log = read("logcat.txt").splitlines()
keep = re.compile(r"dq9|DQ9|Qt|qt\.|libc|DEBUG|FATAL|AndroidRuntime|crash|Abort|signal|tombstone|"
                  r"weirdwallydev|ActivityManager.*dq9", re.I)
lines = [l for l in log if keep.search(l)]
annotate("pids", read("pid.txt") + "\n" + read("install.txt")[-500:])
annotate("crash buffer", read("crash.txt")[-30000:] or "(empty)", "error" if read("crash.txt").strip() else "notice")
app = read("applog.txt").splitlines()
annotate("app log", "\n".join(app[-300:])[-20000:] or "(app not running at the end)")
annotate("system log (filtered)", "\n".join(l for l in lines if "dq9" in l.lower() or "FATAL" in l or "AndroidRuntime" in l)[-8000:] or "(nothing)")

shots = sorted(f for f in os.listdir(SMOKE) if f.endswith(".png"))
for name in shots:
    try:
        im = Image.open(os.path.join(SMOKE, name)).convert("L")
    except Exception as e:  # noqa: BLE001
        annotate(f"screenshot {name}", f"unreadable: {e}")
        continue
    im.thumbnail((180, 400))
    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=40)
    b64 = base64.b64encode(buf.getvalue()).decode()
    parts = [b64[i:i + 4000] for i in range(0, len(b64), 4000)]
    for i, p in enumerate(parts):
        annotate(f"screenshot {name} {im.size[0]}x{im.size[1]} part {i + 1}/{len(parts)}", p, "notice")
sys.exit(0)
