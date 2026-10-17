"""Own every disposable fixture and test the read-only external debug session.

Only this supervisor may forcibly clean up its own lab pair after a HELD result.
No PID supplied by a user, game, real file or backup is modified.
"""
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time


def capture(process):
    lines = queue.Queue()
    def read():
        for line in process.stdout:
            lines.put(line)
        lines.put(None)
    threading.Thread(target=read, daemon=True).start()
    return lines


def until(lines, marker, timeout=12):
    deadline = time.monotonic() + timeout
    text = ""
    while time.monotonic() < deadline:
        line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
        if line is None:
            raise AssertionError(f"EOF before {marker}: {text}")
        print(line, end="")
        text += line
        if marker in line:
            return text
    raise AssertionError(f"timeout before {marker}: {text}")


def run(debugger, module, case):
    host = str(Path(debugger).with_name("grandmp_native_counter_host.exe"))
    mode = {"invalid_ready": "attach_invalid", "missing_module": "attach_no_module"}.get(case, "attach_target")
    target = subprocess.Popen([host, module, mode], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
    session = None
    try:
        target_lines = capture(target)
        until(target_lines, "attach_target_ready=1")
        pid = str(os.getpid()) if case == "wrong_identity" else str(target.pid)
        if case == "invalid_pid":
            pid = "4294967296"
        operation = {"deny_install": "success", "deny_restore": "restore_success"}.get(case, "inspect")
        env = os.environ.copy()
        env.pop("GRANDMP_LAB_ATTACH_FAULT", None)
        faults = ("lost_stop", "detach_failure", "inventory_failure", "inspection_failure", "inspection_exception",
                  "context_failure", "disconnect", "before_attach_exception", "before_policy_exception",
                  "policy_failure", "after_policy_exception", "timeout", "lost_stop_disconnect")
        if case in faults:
            env["GRANDMP_LAB_ATTACH_FAULT"] = "lost_stop" if case == "lost_stop_disconnect" else case
        session = subprocess.Popen([debugger, "--attach-lab", pid, module, operation],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
        if case in ("lost_stop", "detach_failure", "inventory_failure", "inspection_exception", "context_failure", "disconnect",
                    "before_policy_exception", "policy_failure", "after_policy_exception", "timeout", "lost_stop_disconnect"):
            output = until(capture(session), "external_HELD")
            if case == "inspection_exception":
                assert "external_attached" in output and "external_lab_exception=inspection" in output
                assert "reason=active session exception" in output
                assert "continue_after_failure=0 detach_after_failure=0 terminate=0" in output
                assert "external_detached" not in output and "external_inspect=" not in output
            if case in ("before_policy_exception", "policy_failure", "after_policy_exception", "timeout"):
                assert "stop=no_event_owned" in output
                policy = "unconfirmed" if case in ("before_policy_exception", "policy_failure") else "disabled"
                assert f"policy={policy}" in output
            if case == "lost_stop_disconnect":
                assert "stop=event_token_revoked" in output and "policy=disabled" in output
            if case == "detach_failure":
                assert "stop=event_released_queue_unknown" in output
            time.sleep(0.1)
            while not target_lines.empty():
                target_lines.get_nowait()
            if case != "detach_failure":
                try:
                    line = target_lines.get(timeout=0.25)
                except queue.Empty:
                    print("pending_stop_execution_frozen=1")
                else:
                    raise AssertionError(f"target executed after HELD: {line}")
            assert session.poll() is None and target.poll() is None, "HELD must not exit/kill external target"
            # Resolve solely this supervisor's disposable pair; never request
            # ContinueDebugEvent or detach from a revoked/indeterminate token.
            if case in ("before_policy_exception", "policy_failure"):
                session.kill()  # Observe default OS policy, only on our disposable fixture.
                session.wait(timeout=3)
                target.wait(timeout=5)
                assert target.returncode != 0, "expected OS termination before kill-on-exit was disabled"
                print(f"debugger_death_before_policy=1 os_target_exit={target.returncode} residual_risk=1")
                return
            if case in ("disconnect", "after_policy_exception", "lost_stop_disconnect"):
                if case == "disconnect":
                    assert "external_inspect=INSPECTED" in output, "clean-stop disconnect scenario"
                session.kill()  # Simulate abrupt loss; Windows detaches when kill-on-exit is false.
                session.wait(timeout=3)
                assert target.poll() is None, "debugger loss after disabled policy must not kill fixture"
                until(target_lines, "attach_tick=1")
                print("os_resumed_without_debugger_continue=1 residual_risk=1")
                target.stdin.write("\n")
                target.stdin.flush()
                target.wait(timeout=5)
                assert target.returncode == 0, "clean disconnect must preserve target"
                print("forced_debugger_exit=1 external_survived=1 host_self_check=0")
                return
            target.kill()
            session.kill()
            session.wait(timeout=3)
            target.wait(timeout=3)
            print("supervisor_lab_cleanup=1 automatic_terminate=0")
            return
        output, _ = session.communicate(timeout=15)
        print(output, end="")
        expected = 0 if case == "success" else 3 if case in ("invalid_ready", "inspection_failure") else 2
        assert session.returncode == expected, (session.returncode, expected)
        if expected in (0, 3):
            assert "external_attached" in output and "external_detached=1 terminated=0" in output
            assert ("external_inspect=INSPECTED" if expected == 0 else "external_inspect=REJECTED") in output
            assert "writeEnabled=0" in output
            if expected == 0:
                assert "writes=0" in output
        else:
            assert "external_attached" not in output, "reject before attach"
        assert target.poll() is None, "external process must survive session end/rejection"
        target.stdin.write("\n")
        target.stdin.flush()
        target.wait(timeout=5)
        assert target.returncode == 0, "host self-check: descriptor/CALL/counter unchanged, no debugger"
        if case != "missing_module":
            until(target_lines, "attach_target_intact=1")
        print("external_survived=1 host_self_check=0")
    finally:
        # Cleanup authority belongs only to this creator, never the debugger.
        if target.poll() is None:
            target.kill()
        if session is not None and session.poll() is None:
            session.kill()
        if session is not None:
            session.wait(timeout=5)
        target.wait(timeout=5)


if __name__ == "__main__":
    run(*sys.argv[1:])
