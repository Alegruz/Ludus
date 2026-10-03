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
from pathlib import Path
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
    if len(sys.argv) != 5:
        return 2
    with tempfile.TemporaryDirectory() as td:
        script = Path(td) / "journey.gdb"
        script.write_text(SCRIPT)
        result = subprocess.run(
            [sys.argv[1], "-q", "--batch", "-x", str(script), "--args", *sys.argv[2:]],
            capture_output=True, text=True, timeout=150)
    output = result.stdout + result.stderr
    print(output)
    required = ("PASS A source", "PASS actual A next", "PASS B source",
                "PASS actual B next", "PASS inferior exited zero")
    return 0 if result.returncode == 0 and all(marker in output for marker in required) else 1

if __name__ == "__main__":
    raise SystemExit(main())
