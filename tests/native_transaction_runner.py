"""Verify fatal lab-child termination from outside the debugger process.

The transaction debugger retains its pending event on FATAL and exits with
kill-on-exit enabled. This supervisor observes the child's real process handle;
it never attaches, writes memory, or resumes any thread.
"""
import ctypes
from ctypes import wintypes
import re
import queue
import subprocess
import sys
import threading
import time


def run(debugger, module, scenario):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.GetExitCodeProcess.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    process = subprocess.Popen([debugger, module, scenario], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, errors="replace")
    child_handle = None
    child_gone = False
    captured = []
    lines = queue.Queue()

    def read_output():
        try:
            for line in process.stdout:
                lines.put(line)
        finally:
            lines.put(None)

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    deadline = time.monotonic() + 15
    try:
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError("debugger output deadline exceeded")
            try:
                line = lines.get(timeout=remaining)
            except queue.Empty:
                raise AssertionError("debugger output deadline exceeded") from None
            if line is None:
                break
            captured.append(line)
            print(line, end="")
            match = re.search(r"transaction=FATAL pid=(\d+) ", line)
            if match:
                if child_handle or child_gone:
                    raise AssertionError("duplicate fatal child identity")
                # SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, never write.
                child_handle = kernel.OpenProcess(0x00100000 | 0x1000, False, int(match.group(1)))
                if not child_handle:
                    error = ctypes.get_last_error()
                    if error != 87:  # ERROR_INVALID_PARAMETER: PID already absent.
                        raise AssertionError(f"cannot observe lab child: {error}")
                    child_gone = True
        process.wait(timeout=3)
        text = "".join(captured)
        if process.returncode != 0:
            raise AssertionError(f"debugger test failed: {process.returncode}")
        if "fatal_termination_requested=1" not in text or "continue_after_fatal=0" not in text:
            raise AssertionError("fatal termination policy not reached")
        if not child_handle and not child_gone:
            raise AssertionError("fatal child PID not captured")
        if child_handle:
            if kernel.WaitForSingleObject(child_handle, 5000) != 0:
                raise AssertionError("fatal child remained alive after debugger exit")
            exit_code = wintypes.DWORD()
            if not kernel.GetExitCodeProcess(child_handle, ctypes.byref(exit_code)):
                raise AssertionError("GetExitCodeProcess failed")
            if exit_code.value in (0, 259):
                raise AssertionError(f"unexpected fatal child exit: {exit_code.value}")
            print(f"fatal_child_exit_observed=0x{exit_code.value:08X} no_resume=1")
        else:
            print("fatal_child_pid_absent=1 no_resume=1")
    finally:
        if child_handle:
            kernel.CloseHandle(child_handle)
        if process.poll() is None:
            process.kill()  # kill-on-exit terminates only its disposable child.
            process.wait(timeout=3)
        reader.join(timeout=1)
        if process.stdout:
            process.stdout.close()


if __name__ == "__main__":
    if len(sys.argv) != 4 or not sys.argv[3].startswith("fatal_"):
        raise SystemExit("Usage: native_transaction_runner.py lab_debugger lab_dll fatal_case")
    run(*sys.argv[1:])
