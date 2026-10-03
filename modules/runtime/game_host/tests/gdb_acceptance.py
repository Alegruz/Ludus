"""Real GDB source stops, locals, stacks, next and generation replacement.

The inferior exit status is checked separately from GDB's status. No automatic
continue command list can count as single stepping. The old module stays loaded
while its source frame is stopped; reload only runs after that frame resumes.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

SCRIPT = r'''set pagination off
set debuginfod enabled off
set confirm off
set breakpoint pending on
set print pretty off
python
import gdb
import json
import os
from pathlib import Path
from types import SimpleNamespace
from play_probe import debugger_stopped
from play_tool import PlaySupervisor
def assert_actor_guards(pid):
    assert debugger_stopped(pid), "actual GDB inferior was not observed stopped"
    in_fd, input_write = os.pipe()
    output_read, out_fd = os.pipe()
    try:
        actor = PlaySupervisor(None, in_fd, out_fd)
        actor.child = SimpleNamespace(pid=pid)
        actor.state = "Paused"
        actor.epoch = "0000000000000001"
        actor.active = dict(generation="0000000000000001")
        def forbidden_query(path):
            raise AssertionError("reload began validation while inferior was stopped")
        actor.verify_generation = forbidden_query
        actor.command(dict(protocol=1, type="reload", epoch=actor.epoch,
                           request="0000000000000001", generation_path="must-not-be-read",
                           expected_generation=actor.active["generation"]))
        assert actor.pending["0000000000000001"]["result"]["status"] == "Busy"
        assert not actor.host_frames and actor.probe is None
        print("PASS actual stopped-inferior supervisor reload rejection")
    finally:
        for fd in (in_fd, input_write, output_read, out_fd):
            os.close(fd)
def stack_has(name):
    current = gdb.selected_frame()
    while current is not None:
        if name in (current.name() or ""):
            return True
        current = current.older()
    return False
class UpdateBreakpoint(gdb.Breakpoint):
    def stop(self):
        return int(gdb.parse_and_eval("input->FrameIndex")) in (0, 30)
UpdateBreakpoint("FixtureUpdate")
end
run
python
frame = gdb.selected_frame()
assert "FixtureUpdate" in frame.name(), frame.name()
assert frame.find_sal().symtab.fullname().endswith("fixture_module.cpp")
assert stack_has("JourneyBeforeReload")
line_before = frame.find_sal().line
assert int(gdb.parse_and_eval("input->FrameIndex")) == 0
pid = gdb.selected_inferior().pid
assert_actor_guards(pid)
before = Path(f"/proc/{pid}/maps").read_text()
assert "game_fixture_A.so" in before and "game_fixture_B.so" not in before
print("PASS A source breakpoint, host stack, arguments, old mapping retained while stopped")
end
bt
info args
next
python
assert gdb.selected_frame().name() == frame.name()
assert gdb.selected_frame().find_sal().line != line_before
assert float(gdb.parse_and_eval("state->PositionX")) == 0.0
print("PASS actual A next and simulation local")
end
info locals
continue
python
frame = gdb.selected_frame()
assert "FixtureUpdate" in frame.name(), frame.name()
assert frame.find_sal().symtab.fullname().endswith("fixture_module.cpp")
assert stack_has("JourneyAfterReload")
line_before = frame.find_sal().line
assert int(gdb.parse_and_eval("input->FrameIndex")) == 30
maps = Path(f"/proc/{pid}/maps").read_text()
assert_actor_guards(pid)
assert "game_fixture_B.so" in maps and "game_fixture_A.so" not in maps
print("PASS B source breakpoint and distinct generation symbols/mapping in same inferior")
end
bt
info args
next
python
assert gdb.selected_frame().name() == frame.name()
assert gdb.selected_frame().find_sal().line != line_before
assert float(gdb.parse_and_eval("state->SimTime")) > 0.0
print("PASS actual B next and preserved simulation local")
end
info locals
continue
python
assert int(gdb.parse_and_eval("$_exitcode")) == 0
print("PASS inferior exited zero after same-session reload and stepping")
end
quit
'''

def main() -> int:
    if len(sys.argv) not in (5, 6) or (len(sys.argv) == 6 and sys.argv[5] != "--attach"):
        return 2
    attach = len(sys.argv) == 6
    tooling = Path(__file__).resolve().parents[4] / "scripts/python"
    script_text = SCRIPT.replace("import gdb\n", "import gdb\nimport sys\nsys.path.insert(0, " + repr(str(tooling)) + ")\n", 1)
    if attach:
        # GDB is the parent of the already-running inferior, respecting Linux's
        # ptrace policy without changing sysctls or granting unrelated tracers.
        setup = '''python
import subprocess
import time
inferior = subprocess.Popen(gdb.string_to_argv(gdb.parameter("args")) + ["--wait-attach"], stdin=subprocess.PIPE)
deadline = time.monotonic() + 15
while "game_fixture_A.so" not in Path(f"/proc/{inferior.pid}/maps").read_text():
    assert inferior.poll() is None and time.monotonic() < deadline, "inferior did not load A before attach"
    time.sleep(0.01)
gdb.execute(f"attach {inferior.pid}")
print("PASS attach to running host with A already loaded")
inferior.stdin.write(b"g")
inferior.stdin.flush()
end
continue
'''
        # The executable must be supplied explicitly: GDB's args exclude it.
        setup = setup.replace('gdb.string_to_argv(gdb.parameter("args"))',
                              '[gdb.current_progspace().filename] + gdb.string_to_argv(gdb.parameter("args"))')
        script_text = script_text.replace("run\npython\nframe", setup + "python\nframe", 1)
    with tempfile.TemporaryDirectory() as td:
        script = Path(td) / "journey.gdb"
        script.write_text(script_text)
        result = subprocess.run(
            [sys.argv[1], "-q", "--batch", "-x", str(script), "--args", *sys.argv[2:5]],
            capture_output=True, text=True, timeout=150)
    output = result.stdout + result.stderr
    print(output)
    required = ("PASS A source", "PASS actual A next", "PASS B source",
                "PASS actual B next", "PASS inferior exited zero",
                "PASS actual stopped-inferior supervisor reload rejection")
    if attach:
        required += ("PASS attach to running host",)
    return 0 if result.returncode == 0 and all(marker in output for marker in required) else 1

if __name__ == "__main__":
    raise SystemExit(main())
