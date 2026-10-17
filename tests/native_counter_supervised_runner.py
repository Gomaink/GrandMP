"""Supervise only our own disposable pair; no user PID or game is accepted."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import queue
import re
import struct
import subprocess
import sys
import time

from native_counter_attach_runner import capture, until


def verify_remote(pid, scope, installed):
    # Read-only evidence independent of the debugger's transaction log.
    values = dict(re.findall(r"(callsite|original|wrapper|descriptor)=([0-9A-F]{8})", scope))
    assert len(values) == 4, scope
    values = {key: int(value, 16) for key, value in values.items()}
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(0x1010, False, pid)  # VM_READ | QUERY_LIMITED_INFORMATION
    assert handle
    def read(address, size):
        data = ctypes.create_string_buffer(size)
        count = ctypes.c_size_t()
        assert kernel.ReadProcessMemory(handle, address, data, size, ctypes.byref(count))
        assert count.value == size
        return data.raw
    try:
        destination = values["wrapper" if installed else "original"]
        expected = b"\xe8" + struct.pack("<i", destination - values["callsite"] - 5)
        assert read(values["callsite"], 5) == expected
        assert struct.unpack("<I", read(values["descriptor"] + 52, 4))[0] == installed
        print(f"supervisor_call_and_installed_verified={installed}")
    finally:
        kernel.CloseHandle(handle)


def run(debugger, module, case):
    baseline = case.startswith("baseline_")
    case = case.removeprefix("baseline_")
    host = str(Path(debugger).with_name("grandmp_native_counter_host.exe"))
    changes = ("threads_create", "threads_exit", "threads_churn", "modules_add", "modules_remove", "modules_reload")
    inventory_faults = ("restore_module_event_missing", "restore_module_changed", "restore_thread_event_missing", "restore_exit_event_missing",
                        "restore_context_failure", "restore_context_overlap", "restore_context_win32", "restore_stop_lost")
    env = os.environ.copy()
    env.pop("GRANDMP_LAB_ATTACH_FAULT", None)
    env.pop("GRANDMP_LAB_INVENTORY_MODE", None)
    if case in changes:
        env["GRANDMP_LAB_INVENTORY_MODE"] = case
    elif case in inventory_faults:
        env["GRANDMP_LAB_INVENTORY_MODE"] = "modules_add" if case == "restore_module_event_missing" else "threads_churn"
        env["GRANDMP_LAB_ATTACH_FAULT"] = case
    target = subprocess.Popen([host, module, "attach_counter"], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    session = None
    try:
        target_lines = capture(target)
        until(target_lines, "attach_target_ready=1")
        scenario = "success" if case in ("deny_install", "deny_restore") + changes + inventory_faults else case
        session = subprocess.Popen([debugger, "--supervised-lab", str(target.pid), module, scenario],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, env=env)
        lines = capture(session)
        output = ""
        scope = ""
        deadline = time.monotonic() + 18
        held = False
        while time.monotonic() < deadline:
            line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
            assert line is not None, f"unexpected EOF: {output}"
            print(line, end="")
            output += line
            if line.startswith("supervised_scope "):
                scope = line
            if line.startswith("supervised_confirm="):
                command = line.split("=", 1)[1].strip()
                deny = case == "deny_install" or (case == "deny_restore" and command.startswith("RESTORE "))
                session.stdin.write(("REFUSED" if deny else command) + "\n")
                session.stdin.flush()
            if "supervised_detached=1" in line:
                break
            if "supervised_HELD" in line:
                held = True
                if scope:
                    output += until(lines, "supervised_evidence")
                break
        else:
            raise AssertionError("bounded supervision expired")
        if baseline:
            assert case in changes and held, output
            assert "supervised_install=INSTALLED" in output and "external inventory changed" in output
            assert "supervised_restore=" not in output and "supervised_detached" not in output
            assert session.poll() is None and target.poll() is None
            until(target_lines, f"inventory_fixture mode={case} changed=1 installed=1")
            verify_remote(target.pid, scope, 1)
            print(f"legacy_inventory_refusal_reproduced=1 case={case} installed_call_retained=1")
            return
        if case == "success" or case in changes:
            assert not held and session.wait(timeout=3) == 0
            assert "supervised_install=INSTALLED" in output
            assert "increased=1" in output and "supervised_restore=RESTORED" in output
            assert "supervised_restore_break_requested=1" in output
            assert "original_call_restored=1 wrapper_remains_resident=1" in output
            assert target.poll() is None
            if case in changes:
                until(target_lines, f"inventory_fixture mode={case} changed=1 installed=1")
            verify_remote(target.pid, scope, 0)
            target.stdin.write("\n")
            target.stdin.flush()
            assert target.wait(timeout=5) == 0, "original forwarding, descriptor and CALL self-check"
            until(target_lines, "attach_target_intact=1")
            if case in changes:
                # Real target work must have completed before the restore stop.
                assert "inventory_reconciled phase=restoration" in output
                delta = re.search(r"inventory_delta phase=restoration modules_added=(\d+) modules_removed=(\d+) "
                                  r"modules_changed=(\d+) pre_threads_exited=(\d+)", output)
                assert delta, output
                added, removed, changed, exited = map(int, delta.groups())
                if case in ("threads_exit", "threads_churn"):
                    assert exited >= 1, "must retire a real pre-attach thread"
                if case in ("threads_create", "threads_churn"):
                    assert "inventory_thread_event=created" in output
                if case == "modules_add":
                    assert added >= 1 and "inventory_module_event=added" in output
                if case == "modules_remove":
                    assert removed >= 1 and "inventory_module_event=removed" in output
                if case == "modules_reload":
                    assert "inventory_module_event=added" in output and "inventory_module_event=removed" in output
            print("supervised_full_cycle=1 forwarding_exactly_once=1 external_survived=1")
            return
        assert held and "continue_after_failure=0 detach_after_failure=0 terminate=0" in output
        assert "supervised_detached" not in output
        if case in ("short_write", "flush_failure", "commit_failure"):
            assert "supervised_install=ROLLED_BACK" in output and "rollback=1" in output
        elif case == "restore_short_write":
            assert "supervised_restore=ROLLED_BACK" in output and "rollback=1" in output
        elif case == "fatal_write":
            assert "supervised_install=FATAL" in output and "rollback=1" in output
        elif case == "fatal_restore_write":
            assert "supervised_restore=FATAL" in output and "rollback=1" in output
        elif case == "restore_identity":
            assert "supervised_restore=REJECTED" in output
        elif case == "deny_install":
            assert "authorization=0" in output and "supervised_install=" not in output
        elif case == "deny_restore":
            assert "restore confirmation refused" in output and "supervised_restore=" not in output
        elif case in ("identity", "invalid_state", "context_failure"):
            assert "supervised_confirm=" not in output and "supervised_install=" not in output
        elif case == "unexpected_bytes":
            assert "supervised_install=REJECTED" in output and "writes=0" in output
        elif case in inventory_faults:
            assert "supervised_install=INSTALLED" in output and "supervised_restore=" not in output
            reasons = {
                "restore_module_event_missing": "module snapshot/events count",
                "restore_module_changed": "module changed without matching load event",
                "restore_thread_event_missing": "thread snapshot/events count",
                "restore_exit_event_missing": "pre-thread missing/identity without exit event",
                "restore_context_failure": "injected context failure",
                "restore_context_overlap": "context EIP overlaps CALL",
                "restore_context_win32": "GetThreadContext lab invalid handle",
                "restore_stop_lost": "unauthenticated stop",
            }
            assert reasons[case] in output, output
            if case == "restore_context_win32":
                assert "win32=6" in output, "ERROR_INVALID_HANDLE captured before diagnostics/cleanup"
        if scope and not case.startswith("fatal"):
            verify_remote(target.pid, scope, int(case in ("deny_restore", "restore_identity", "restore_short_write") + inventory_faults))
        time.sleep(0.1)
        while not target_lines.empty():
            target_lines.get_nowait()
        try:
            line = target_lines.get(timeout=0.25)
        except queue.Empty:
            print("pending_stop_execution_frozen=1")
        else:
            raise AssertionError(f"target executed after HELD: {line}")
        assert session.poll() is None and target.poll() is None
        print("supervised_held_alive=1 automatic_terminate=0")
    finally:
        # Only the fixture creator may end these laboratory processes. End the
        # target first, never rely on killing the debugger to preserve a stop.
        if target.poll() is None:
            target.kill()
        if session is not None and session.poll() is None:
            session.kill()
        if session is not None:
            session.wait(timeout=5)
        target.wait(timeout=5)


if __name__ == "__main__":
    run(*sys.argv[1:])
