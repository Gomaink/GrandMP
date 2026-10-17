"""Read-only diagnostic against the existing disposable counter host, never GTA."""
import ctypes
from ctypes import wintypes
import os
import subprocess
import sys
import time
import uuid


def run(host, module, diagnostic, wrong_module):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateEventW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
    kernel.CreateEventW.restype = wintypes.HANDLE
    kernel.SetEvent.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    event_name = "Local\\GrandMPObserverExit-" + uuid.uuid4().hex
    event = kernel.CreateEventW(None, True, False, event_name)
    if not event:
        raise OSError(ctypes.get_last_error(), "CreateEventW")
    environment = dict(os.environ, GRANDMP_NATIVE_COUNTER_ASYNC_READY="1",
                       GRANDMP_COUNTER_OBSERVER_EXIT=event_name)
    child = None
    try:
        child = subprocess.Popen([host, module, "async_observe"], env=environment,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        command = [diagnostic, "--pid", str(child.pid), "--expected-asi", module, "--lab-profile"]
        deadline = time.monotonic() + 7
        while True:
            observed = subprocess.run(command, capture_output=True, text=True, timeout=2)
            if observed.returncode == 0:
                break
            if child.poll() is not None or time.monotonic() >= deadline:
                raise AssertionError("READY diagnostic not confirmed: " + observed.stdout + observed.stderr)
            time.sleep(0.05)
        print(observed.stdout, end="")
        for marker in ("state=READY", "PIN=1", "count=0", "callbackCapable=0", "installed=0"):
            if marker not in observed.stdout:
                raise AssertionError("missing diagnostic field: " + marker)
        wrong = subprocess.run([diagnostic, "--pid", str(child.pid), "--expected-asi", wrong_module,
                                "--lab-profile"], capture_output=True, text=True, timeout=2)
        if wrong.returncode != 3:
            raise AssertionError("wrong module identity accepted")
        unknown_game = subprocess.run(command[:-1], capture_output=True, text=True, timeout=2)
        if unknown_game.returncode != 3:
            raise AssertionError("lab process accepted as Hoodlum")
        dry_run = subprocess.run(command[:-1] + ["--dry-run"], capture_output=True, text=True, timeout=2)
        if dry_run.returncode != 3 or "process identity rejected" not in dry_run.stdout:
            raise AssertionError("dry-run accepted lab process as Hoodlum")
        print("dry_run_identity_rejection PASS")
        if not kernel.SetEvent(event):
            raise OSError(ctypes.get_last_error(), "SetEvent")
        output, _ = child.communicate(timeout=3)
        print(output, end="")
        if child.returncode != 0 or "ASYNC_OBSERVE" not in output:
            raise AssertionError("noncooperative host did not exit normally")
        print("read_only_diagnostic PASS: identity rejections and normal host exit")
    finally:
        kernel.SetEvent(event)
        if child and child.poll() is None:
            child.kill()  # Only this disposable child, never a supplied PID.
            child.communicate(timeout=3)
        kernel.CloseHandle(event)


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit("Usage: host experimental diagnostic wrong-module")
    run(*sys.argv[1:])
