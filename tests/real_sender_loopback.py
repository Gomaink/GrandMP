"""Actual capture body with simulated game reads, existing server/transport/Binding. No GTA."""
import socket
import subprocess
import sys
import queue
import threading
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserved:
    reserved.bind(("127.0.0.1", 0))
    port = reserved.getsockname()[1]
server = subprocess.Popen([sys.argv[1], "--port", str(port)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
ready = queue.Queue()
def drain():
    for line in server.stdout:
        ready.put(line)
thread = threading.Thread(target=drain, daemon=True)
thread.start()
try:
    line = ready.get(timeout=4)
    assert f"escutando em 127.0.0.1:{port};" in line, line
    result = subprocess.run([sys.argv[2], sys.argv[3] if len(sys.argv)>3 else "sender_pipeline", str(port)], capture_output=True, text=True, timeout=18)
    print(result.stdout)
    print(result.stderr)
    assert result.returncode == 0
finally:
    if server.poll() is None:
        server.terminate()  # This disposable test-owned server handle only, never GTA.
    server.wait(timeout=3)
    thread.join(timeout=1)
