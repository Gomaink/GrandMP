"""Real UDP relay with two ObserverNetwork clients; no GTA or synthetic client."""
import socket
import subprocess
import sys
import queue
import threading

lan = len(sys.argv) == 4 and sys.argv[3] == 'lan'
host = '127.0.0.2' if lan else '127.0.0.1'
if lan:
    for options in (['--bind', '0.0.0.0'], ['--bind', '8.8.8.8'], ['--bind', 'bad'],
                    ['--bind'], ['--port', '0'], ['--bind', host, '--bind', host]):
        result = subprocess.run([sys.argv[1], *options], capture_output=True, timeout=3)
        if result.returncode != 2:
            raise RuntimeError('server did not refuse invalid endpoint: ' + repr(options))
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
    reserve.bind((host, 0))
    port = reserve.getsockname()[1]
server = subprocess.Popen([sys.argv[1], '--port', str(port), *(['--bind', host] if lan else [])], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
try:
    ready = queue.Queue(maxsize=1)
    reader = threading.Thread(target=lambda: ready.put(server.stdout.readline()), daemon=True)
    reader.start()
    line = ready.get(timeout=3)
    if f'escutando em {host}:{port};' not in line or server.poll() is not None:
        raise RuntimeError('server did not confirm the configured bind: ' + repr(line))
    result = subprocess.run([sys.argv[2], 'duplex_lan' if lan else 'duplex', str(port)], capture_output=True, text=True, timeout=20)
    print(result.stdout)
    print(result.stderr)
    if result.returncode:
        raise SystemExit(result.returncode)
finally:
    server.terminate()  # Only this disposable test-owned process; never GTA.
    server.wait(timeout=3)
    reader.join(timeout=1)
