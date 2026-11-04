"""Test-owned loopback relay + existing synthetic sender -> Controller -> Binding. No GTA."""
import queue
import socket
import subprocess
import sys
import threading

server_exe, synthetic_exe, receiver_exe = sys.argv[1:]
# No GrandMP server on this reserved endpoint; a handshake cannot be fabricated.
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserved:
    reserved.bind(("127.0.0.1", 0))
    check = subprocess.run([receiver_exe, "udp_absent", str(reserved.getsockname()[1])], capture_output=True, text=True, timeout=4)
    assert check.returncode == 0, check.stdout + check.stderr
print("absent_server=PASS")
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserved:
    reserved.bind(("127.0.0.1", 0))
    port = reserved.getsockname()[1]
children = []
records = {}
def start(name, args):
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    children.append(process)
    lines, pending = [], queue.Queue()
    def drain():
        for line in process.stdout:
            lines.append(line)
            pending.put(line)
        pending.put(None)
    thread = threading.Thread(target=drain, daemon=True)
    thread.start()
    records[name] = (lines, pending, thread)
    return process

def wait_line(name, marker, timeout):
    pending = records[name][1]
    import time
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = pending.get(timeout=max(.01, deadline - time.monotonic()))
        if line is None:
            raise RuntimeError(name + " exited before " + marker + ": " + "".join(records[name][0]))
        if marker in line:
            return
    raise RuntimeError("deadline: " + marker)

try:
    server = start("server", [server_exe, "--port", str(port)])
    wait_line("server", f"escutando em 127.0.0.1:{port};", 4)
    receiver = start("receiver", [receiver_exe, "udp_relay", str(port)])
    wait_line("receiver", "udp_binding_ready", 5)
    sender = start("synthetic", [synthetic_exe, "--port", str(port), "--on-foot-demo"])
    wait_line("receiver", "udp_binding_controls_pass", 8)
    sender.terminate()  # This test-owned synthetic process only: deliberately lose packets, no Disconnect.
    sender.wait(timeout=3)
    wait_line("receiver", "udp_binding_timeout_neutral PASS", 3)
    wait_line("receiver", "udp_binding_leave_retained_neutral PASS", 12)
    assert receiver.wait(timeout=3) == 0, "receiver failed"
    print("".join(records["receiver"][0]))
    print("synthetic -> UDP server -> ObserverNetwork -> Controller -> Binding=PASS")
finally:
    for process in reversed(children):
        if process.poll() is None:
            process.terminate()  # Only handles owned by this disposable test; never GTA.
        process.wait(timeout=3)
    for _, _, thread in records.values():
        thread.join(timeout=1)
