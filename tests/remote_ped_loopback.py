"""Real existing UDP v1 server + synthetic peer -> production sink -> fake engine.
Never starts GTA or loads an enabled ASI. Processes expose loopback only.
"""
import subprocess
import sys
import time
from remote_scene_loopback import reserve_port, stop_process, stop_client

def run(server_binary, host_binary, synthetic_binary, movement=False):
    port = reserve_port()
    flags = subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0
    server = subprocess.Popen([server_binary, "--port", str(port)], stdout=subprocess.DEVNULL,
                              stderr=subprocess.PIPE, creationflags=flags)
    host = client = None
    try:
        time.sleep(0.15)
        assert server.poll() is None, "server start failed"
        host = subprocess.Popen([host_binary, "loopback", str(port)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, creationflags=flags)
        time.sleep(0.2)
        client = subprocess.Popen([synthetic_binary, "--port", str(port)], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.PIPE, creationflags=flags)
        time.sleep(8 if movement else 1.5)
        stop_client(client)
        output, _ = host.communicate(timeout=8)
        text = output.decode("latin1", errors="replace")
        assert host.returncode == 0, text
        if movement:
            assert "remote_move_loopback creates=1 tasks=1 arrived=1 removes=1 native_thread_only=1" in text, text
        else:
            assert "state=ped_created" in text and "state=removed" in text, text
            assert "creates=1 removals=1 ready=1 owner_only=1" in text, text
        print("server UDP v1 -> ObserverNetwork -> RemotePlayerScene -> IntentSink -> native-phase fake PASS")
    finally:
        if client is not None:
            if client.poll() is None: stop_process(client)
            client.stderr.close()
        if host is not None and host.poll() is None: stop_process(host)
        stop_process(server)
        server.stderr.close()

if __name__ == "__main__":
    if len(sys.argv) not in (4,5): raise SystemExit("expected server, host, synthetic_client [movement]")
    run(*sys.argv[1:4], movement=len(sys.argv)==5 and sys.argv[4]=="movement")
