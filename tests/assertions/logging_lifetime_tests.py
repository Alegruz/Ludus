import resource
import signal
from transport import run_child
import sys

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
for mode, expected, message in (
    ("sink", 0, "inside real console sink with logger lock held"),
    ("check", 0, "post-shutdown Check"),
    ("pre-fatal", -signal.SIGABRT, "before logger initialization"),
    ("post-fatal", -signal.SIGABRT, "after logger shutdown"),
):
    result = run_child([sys.argv[1], mode])
    if result.returncode != expected or message not in result.stderr:
        raise RuntimeError(f"Logger lifetime {mode}: {result.returncode}\n{result.stderr}")
    if any(marker in result.stderr for marker in ("AddressSanitizer", "LeakSanitizer", "ThreadSanitizer", "runtime error:")):
        raise RuntimeError(result.stderr)
print("Assertions operate before logger initialization and after real logger shutdown")
