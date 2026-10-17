"""Teste real de replicacao UDP em 127.0.0.1, sem GTA ou dependencias externas.
Uso: python replication_loopback.py caminho/do/grandmp_server[.exe]
"""
import socket
import struct
import subprocess
import sys
import time

MAGIC = b"GMP1"


def msg(kind, payload):
    return struct.pack("!4sBBH", MAGIC, 1, kind, len(payload)) + payload


def hello(nonce):
    return msg(1, struct.pack("!Q", nonce))


def pos(player, seq, x, y, z):
    return msg(3, struct.pack("!QIfff", player, seq, x, y, z))


def leave(player):
    return msg(4, struct.pack("!Q", player))


def snapshot(player):
    return msg(8, struct.pack("!Q", player))


def parse(data):
    if len(data) < 8 or data[:4] != MAGIC or data[4] != 1:
        return None
    kind = data[5]
    length = struct.unpack("!H", data[6:8])[0]
    if len(data) != 8 + length:
        return None
    if kind == 2 and length == 16:
        return (kind, *struct.unpack("!QQ", data[8:]))
    if kind in (5, 7) and length == 8:
        return (kind, struct.unpack("!Q", data[8:])[0])
    if kind == 6 and length == 24:
        return (kind, *struct.unpack("!QIfff", data[8:]))
    return None


def wait_for(client, predicate, label, timeout=2.5):
    deadline = time.monotonic() + timeout
    seen = []
    while time.monotonic() < deadline:
        try:
            client.settimeout(min(0.3, max(0.01, deadline - time.monotonic())))
            message = parse(client.recv(256))
        except socket.timeout:
            continue
        if message is not None:
            seen.append(message)
            if predicate(message):
                return message
    raise AssertionError(f"{label}: mensagem nao recebida; vistos={seen[-12:]}")


def start_client(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.connect(("127.0.0.1", port))
    return sock


def connect(client, nonce):
    client.send(hello(nonce))
    welcome = wait_for(client, lambda m: m[0] == 2 and m[1] == nonce,
                       f"Welcome nonce={nonce}")
    return welcome[2]


def run(server_binary):
    reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]
    reservation.close()
    process = subprocess.Popen([server_binary, "--port", str(port)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    clients = []
    try:
        time.sleep(0.15)
        assert process.poll() is None, "servidor nao iniciou"
        a, b, c = (start_client(port) for _ in range(3))
        clients += [a, b, c]
        aid = connect(a, 101)
        a.send(pos(aid, 1, 10.5, 20, 30))
        bid = connect(b, 202)
        assert bid != aid
        wait_for(b, lambda m: m == (5, aid), "B recebe entrada de A")
        wait_for(b, lambda m: m[0] == 6 and m[1] == aid and m[2] == 1 and
                 m[3] == 10.5, "B recebe snapshot da posicao de A")
        wait_for(a, lambda m: m == (5, bid), "A recebe entrada de B")

        b.send(pos(bid, 1, 40.5, 50, 60))
        wait_for(a, lambda m: m[0] == 6 and m[1] == bid and m[2] == 1 and
                 m[3] == 40.5, "A recebe posicao de B")
        # B solicita recuperacao. O servidor reenvia apenas os outros jogadores.
        b.send(snapshot(bid))
        wait_for(b, lambda m: m == (5, aid), "B recupera A via snapshot")
        wait_for(b, lambda m: m[0] == 6 and m[1] == aid,
                 "B recupera posicao de A via snapshot")

        cid = connect(c, 303)
        assert cid not in (aid, bid)
        for rid in (aid, bid):
            wait_for(c, lambda m, rid=rid: m == (5, rid), f"C ve jogador {rid}")
            wait_for(c, lambda m, rid=rid: m[0] == 6 and m[1] == rid,
                     f"C recebe snapshot da posicao {rid}")
        wait_for(a, lambda m: m == (5, cid), "A ve C")

        # Posicao antiga/repetida nao deve chegar ao C (sequenciamento no servidor).
        a.send(pos(aid, 1, 999, 999, 999))
        time.sleep(0.16)
        a.send(pos(aid, 2, 11.5, 21, 31))
        wait_for(c, lambda m: m[0] == 6 and m[1] == aid and m[2] == 2 and
                 m[3] == 11.5, "C recebe nova posicao A")

        b.send(leave(bid))
        wait_for(a, lambda m: m == (7, bid), "A recebe saida de B")
        wait_for(c, lambda m: m == (7, bid), "C recebe saida de B")
        # Porta de origem diferente com ID valido nao pode controlar sessao A.
        c.send(pos(aid, 999, 500, 500, 500))
        time.sleep(0.16)
        a.send(pos(aid, 3, 12.5, 22, 32))
        wait_for(c, lambda m: m[0] == 6 and m[1] == aid and m[2] == 3 and
                 m[3] == 12.5, "spoof rejeitado, sequencia A ainda valida")
        print("Replicacao UDP real PASS: 3 clientes, snapshot, posicoes,"
              " duplicatas, endpoint e desconexao")
    finally:
        for client in clients:
            client.close()
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
        if process.stderr:
            process.stderr.close()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Uso: replication_loopback.py grandmp_server")
    run(sys.argv[1])
