"""Real loopback server + synthetic sender + ObserverNetwork/controller host.
No GTA/backend/fake entity. Proves transport of walk/turn/stop INPUT only.
"""
import re
import socket
import struct
import subprocess
import sys
import time
import os
import signal
from concurrent.futures import ThreadPoolExecutor


def run(server, host, synthetic):
    reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    reserve.bind(('127.0.0.1', 0))
    port = reserve.getsockname()[1]
    reserve.close()
    processes = []
    legacy = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    legacy.connect(('127.0.0.1', port))
    legacy.settimeout(0.2)
    try:
        srv = subprocess.Popen([server, '--port', str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        processes.append(srv)
        time.sleep(0.2)
        legacy.send(struct.pack('!4sBBHQ', b'GMP1', 1, 1, 8, 987654321))
        observer = subprocess.Popen([host, '--port', str(port), '--duration-ms', '12500'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        processes.append(observer)
        drain = ThreadPoolExecutor(max_workers=1)
        observed = drain.submit(observer.communicate, timeout=18)
        time.sleep(0.25)
        sender = subprocess.Popen([synthetic, '--port', str(port), '--on-foot-demo'], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0)
        processes.append(sender)
        # Longer than the unchanged input (500ms), roster (8s), and session (10s)
        # deadlines. Real ongoing neutral packets must keep the same key alive.
        time.sleep(10.5)
        if sender.poll() is not None:
            early_output, early_error = sender.communicate()
            raise AssertionError(f'demo ended early code={sender.returncode}: {early_output} {early_error}; server={srv.poll()}')
        sender.send_signal(signal.CTRL_BREAK_EVENT if os.name == 'nt' else signal.SIGINT)
        client_output, client_error = sender.communicate(timeout=3)
        assert sender.returncode == 0
        output, error = observed.result(timeout=8)
        drain.shutdown()
        assert observer.returncode == 0, error
        rows = re.findall(r'controller_input remote=(\d+) generation=(\d+) seq=(\d+) right=([\d.-]+) forward=([\d.-]+)', output)
        assert rows, output
        assert len({(r[0], r[1]) for r in rows}) == 1, rows
        assert any(float(r[3]) == 0 and float(r[4]) == 1 for r in rows), output
        assert any(float(r[3]) == 1 and float(r[4]) == 0 for r in rows), output
        assert any(float(r[3]) == 0 and float(r[4]) == 0 for r in rows), output
        sequences = [int(r[2]) for r in rows]
        assert sequences == sorted(set(sequences)) and max(sequences) > 15, output
        assert 'reason=server_remote_leave_cause_unspecified' in output, output
        assert 'reason=operator_stop disconnect_sent=1' in client_output, (client_output, client_error)
        assert client_output.count('estabelecida.') == 1, client_output
        print(client_output)
        assert 'native_binding=blocked' in output
        kinds = []
        while True:
            try:
                packet = legacy.recv(256)
            except socket.timeout:
                break
            kinds.append(packet[5])
        assert 6 in kinds and 10 not in kinds, kinds
        assert srv.poll() is None
        print(output)
        print('PASS: persistent real UDP walk/turn/stop then neutral, stable key >10s, graceful Disconnect, legacy XYZ only; NO GTA movement executed.')
    finally:
        legacy.close()
        for p in reversed(processes):
            if p.poll() is None:
                p.terminate()  # Disposable test processes we created; never GTA.
            p.wait(timeout=3)


def welcome_timeout(synthetic):
    """One real UDP welcome, then silence. No reset/retry of an uncertain ID."""
    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer.bind(('127.0.0.1', 0))
    peer.settimeout(0.2)
    sender = subprocess.Popen([synthetic, '--port', str(peer.getsockname()[1]), '--on-foot-demo'],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    sequences, hellos, disconnect = [], 0, False
    session = 11223344
    try:
        deadline = time.monotonic() + 13
        while time.monotonic() < deadline and not disconnect:
            try:
                packet, address = peer.recvfrom(256)
            except socket.timeout:
                continue
            if packet[5] == 1:
                hellos += 1
                if hellos == 1:
                    nonce = struct.unpack_from('!Q', packet, 8)[0]
                    peer.sendto(struct.pack('!4sBBHQQ', b'GMP1', 1, 2, 16, nonce, session), address)
            elif packet[5] == 9:
                assert struct.unpack_from('!Q', packet, 8)[0] == session
                sequences.append(struct.unpack_from('!I', packet, 16)[0])
            elif packet[5] == 4:
                assert struct.unpack_from('!Q', packet, 8)[0] == session
                disconnect = True
        output, error = sender.communicate(timeout=2)
        assert disconnect and sender.returncode == 2, (output, error, sender.returncode)
        assert hellos >= 3 and max(sequences) > 15 and sequences == sorted(set(sequences))
        assert output.count('estabelecida.') == 1 and 'reason=welcome_timeout disconnect_sent=1' in output, output
        print(output)
        print('PASS: unchanged 10s Welcome deadline terminates demo without reusing/resetting the session sequence.')
    finally:
        peer.close()
        if sender.poll() is None:
            sender.terminate()  # Our disposable synthetic test process only.
        sender.wait(timeout=3)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--welcome-timeout':
        welcome_timeout(sys.argv[2])
    else:
        run(*sys.argv[1:])
