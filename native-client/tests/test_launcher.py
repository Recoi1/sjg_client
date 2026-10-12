from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

launcher, helper = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="galaxy launcher ") as directory:
    root = Path(directory)
    script = root / launcher.name
    shutil.copyfile(launcher, script)
    def run():
        return subprocess.run([os.environ["COMSPEC"], "/d", "/c", str(script)],
                              cwd=root.parent, input="\n", text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    missing = run()
    assert missing.returncode == 1 and "executable is missing" in missing.stdout, missing.stdout
    (root / "build-game").mkdir()
    shutil.copyfile(helper, root / "build-game" / "galaxy.exe")
    started = run()
    assert started.returncode == 0, started.stdout
    assert (root / "game-last-run.log").is_file()
