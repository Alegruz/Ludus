import resource
import signal
from transport import run_child
import sys

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
for arguments, expected in (([], 0), (["fatal"], -signal.SIGABRT)):
    result = run_child([sys.argv[1], *arguments])
    if result.returncode != expected:
        raise RuntimeError(f"Allocation probe: {result.returncode}, expected {expected}\n{result.stderr}")
    if any(marker in result.stderr for marker in ("AddressSanitizer", "LeakSanitizer", "ThreadSanitizer", "runtime error:")):
        raise RuntimeError(result.stderr)
print("No intercepted allocation on tested paths; TSan owns C++ new/delete, other jobs cover those replacements")
