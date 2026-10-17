"""End-to-end UDP server -> ObserverNetwork -> scene -> adapter host test.

Uses the real GrandMP server and wire format with two small loopback peers.
It does not launch GTA or access any game process.
"""
import socket
import signal
import subprocess
import sys
import time
import re

def reserve_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        return reservation.getsockname()[1]


def stop_process(process):
    if process.poll() is not None:
        return
    if sys.platform == "win32":
        try:
            process.send_signal(signal.CTRL_BREAK_EVENT)
            process.wait(timeout=2)
            return
        except (OSError, subprocess.TimeoutExpired):
            pass
    process.terminate()
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def stop_client(process):
    if process.poll() is not None:
        return
    process.send_signal(signal.CTRL_BREAK_EVENT)
    process.wait(timeout=3)


def run(server_binary, host_binary, synthetic_client):
    port = reserve_port()
    creationflags = subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0
    server = subprocess.Popen([server_binary, "--port", str(port)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              creationflags=creationflags)
    host = None
    clients = []
    try:
        time.sleep(0.15)
        assert server.poll() is None, "GrandMP server did not start"
        host = subprocess.Popen([host_binary, "--port", str(port), "--duration-ms", "6500"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                creationflags=creationflags)
        time.sleep(0.2)
        first = subprocess.Popen([synthetic_client, "--port", str(port)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                 creationflags=creationflags)
        clients.append(first)
        time.sleep(0.3)
        second = subprocess.Popen([synthetic_client, "--port", str(port)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                  creationflags=creationflags)
        clients.append(second)
        time.sleep(1.8)
        stop_client(first)

        output, _ = host.communicate(timeout=8)
        text = output.decode("latin1", errors="replace")
        if host.returncode != 0:
            raise AssertionError(f"scene host failed ({host.returncode}):\n{text}")
        spawned = set(re.findall(r"\[backend\] spawn id=(\d+)", text))
        if len(spawned) < 2:
            raise AssertionError(f"expected two synthetic clients to spawn; saw {spawned}:\n{text}")
        if "[backend] despawn id=" not in text or "[backend] pose id=" not in text:
            raise AssertionError(f"missing pose or disconnect in scene host output:\n{text}")
        if "cleanup=ok affinity=ok" not in text:
            raise AssertionError(f"host cleanup/owner-thread check failed:\n{text}")
        print("server -> ObserverNetwork -> scene -> adapter -> backend PASS "
              f"(synthetic players {', '.join(sorted(spawned))})")
    finally:
        for client in clients:
            if client.poll() is None:
                try:
                    stop_client(client)
                except (OSError, subprocess.TimeoutExpired):
                    client.kill()
                    client.wait(timeout=2)
            if client.stderr:
                client.stderr.close()
        if host is not None and host.poll() is None:
            host.kill()
            host.wait(timeout=2)
        stop_process(server)
        if server.stderr:
            server.stderr.close()


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("Uso: remote_scene_loopback.py grandmp_server grandmp_remote_scene_host grandmp_synthetic_client")
    run(sys.argv[1], sys.argv[2], sys.argv[3])
