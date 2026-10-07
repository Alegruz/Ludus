"""Real GameHost connection: partial-tick responsiveness and native VM retirement."""
import json
import socket
import struct
import subprocess
import sys
import tempfile
import time

host, a, b = sys.argv[1:]
for disconnect in (False, True):
    parent, child = socket.socketpair()
    parent.settimeout(10)
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([host, "--module", a, "--control-fd", str(child.fileno()),
            "--await-load", "--project-id", "b", "--game-id", "16", "--headless"], pass_fds=(child.fileno(),), stdout=log, stderr=log)
        child.close()
        try:
            def exact(n):
                data = b""
                while len(data) < n:
                    chunk = parent.recv(n-len(data))
                    assert chunk, "host disconnected"
                    data += chunk
                return data
            def receive():
                size = struct.unpack("<I", exact(4))[0]
                assert 0 < size <= 1048576
                return json.loads(exact(size))
            ready = receive()
            assert ready["event"] == "SessionReady", ready
            session, generation, sequence = ready["session"], 1, 0
            def command(kind, **fields):
                # IDs are fixed-width hex even above IEEE integer precision.
                command.seq += 1
                request = f"{command.seq:016x}"
                data = {"protocol": 1, "command": kind, "request": request, "session": session, "epoch": "0000000000000001"}
                if kind not in ("Hello", "Status", "Stop", "Load"): data["expected_generation"] = f"{generation:016x}"
                data.update(fields)
                payload = json.dumps(data, separators=(",", ":")).encode()
                parent.sendall(struct.pack("<I", len(payload)) + payload)
                while True:
                    response = receive()
                    if response.get("event") == "CommandResult" and response.get("request") == request: return response
            command.seq = 0
            assert command("Hello")["status"] == "Ok"
            loaded = command("Load", generation="0000000000000001")
            assert loaded["status"] == "Ok", loaded
            status = command("Status")
            assert status["capabilities"] & 8 and not status["script_paused"], status
            def debug(action, *, execution="a123456789abcdef", revision=1, stop="0000000000000000", **fields):
                request = {"version": 1, "action": action, "session": "f123456789abcdef", "execution": execution,
                           "revision": revision, "stop": stop, **fields}
                return command("ScriptDebug", extension=1, payload=json.dumps(request, separators=(",", ":")))
            assert debug("breakpoint", asset="0000000000000100", line=8, enabled=True)["status"] == "Ok"
            time.sleep(.05)
            paused = command("Status")
            assert paused["script_paused"], paused
            time.sleep(.05)
            assert command("Status")["sim_ticks"] == paused["sim_ticks"], "partial tick advanced"
            assert command("Reload", module_path=b, generation="0000000000000002")["status"] == "Busy"
            assert debug("inspect")["status"] == "InvalidRequest", "stale stop accepted"
            snapshot = debug("inspect", stop="0000000000000001")
            assert snapshot["status"] == "Ok", snapshot
            body = json.loads(snapshot["message"])
            assert debug("watch", stop=body["stop"], expression="state.Interactions")["status"] == "InvalidRequest"
            assert debug("inspect", stop=body["stop"], unexpected=1)["status"] == "InvalidRequest"
            assert body["partial"] and body["states"][0]["interactions"] == 0 and body["frames"], body
            if disconnect:
                parent.close()
            else:
                done = debug("continue", stop=body["stop"])
                assert done["status"] == "Ok" and json.loads(done["message"])["states"][0]["interactions"] == 1, done
                reload = command("Reload", module_path=b, generation="0000000000000002")
                assert reload["status"] == "Ok", reload
                generation = 2
                migrated = debug("inspect", execution="a123456789abcdf0", revision=2)
                assert migrated["status"] == "Ok" and json.loads(migrated["message"])["states"][0]["interactions"] == 1, migrated
                assert command("Stop")["status"] == "Ok"
            assert process.wait(timeout=10) == 0
            log.seek(0)
            text = log.read().decode(errors="replace")
            assert text.count("S2 VM retired before native module release") >= (1 if disconnect else 2), text
            assert "AddressSanitizer" not in text and "runtime error:" not in text, text
        except BaseException:
            log.seek(0)
            print(log.read().decode(errors="replace"))
            raise
        finally:
            parent.close()
            if process.poll() is None: process.kill(); process.wait()
print("S2 HOST PASS: same connection, pause/resume, native replacement, disconnect retirement")
