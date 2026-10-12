import subprocess
import sys

for mode, expected in (
    ("fault", "Native crash 0xc0000005"),
    ("uncaught", "Uncaught native error: native test error"),
    ("panic", "PANIC native check:123: progress: 50%"),
):
    result = subprocess.run([sys.argv[1], mode], capture_output=True, text=True, timeout=5)
    assert result.returncode not in (0, 2), (mode, result.returncode, result.stderr)
    assert expected in result.stderr, (mode, result.stderr)
print("Windows faults, uncaught errors, and original JUT panics report and exit.")
