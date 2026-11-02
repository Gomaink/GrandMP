"""Loads only the physically CLOSED own ASI in a disposable own host, never GTA."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import sys
import uuid

host, asi, loader = map(Path, sys.argv[1:4])
mode = sys.argv[4]
assert mode in ("normal", "preloaded")
assert hashlib.sha256(loader.read_bytes()).hexdigest().upper() == "29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B"
root = Path(sys.argv[5]) / (mode + "-" + uuid.uuid4().hex)
root.mkdir(parents=True)
shutil.copy2(host, root / host.name)
shutil.copy2(asi, root / asi.name)
shutil.copy2(loader, root / "dinput8.dll")
(root / "dinput8.ini").write_text("[GlobalSets]\nDontLoadFromDllMain=1\nLoadPlugins=1\n", encoding="ascii")
run = subprocess.run([str(root / host.name), mode], cwd=root, capture_output=True, timeout=12)
(root / "stdout.txt").write_bytes(run.stdout)
(root / "stderr.txt").write_bytes(run.stderr)
print(run.stdout.decode(errors="replace"))
print("evidence=", root)
assert run.returncode == 0, (run.returncode, run.stderr)
