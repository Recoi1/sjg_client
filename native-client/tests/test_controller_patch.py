import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def main():
    executable, fixture, layout, swap_left, swap_right = sys.argv[1:]
    root = ET.parse(fixture).getroot()
    patch = root.find(f"patch[@id='cc{layout}']")
    entries = {}
    for memory in patch.findall("memory"):
        address = int(memory.attrib["offset"], 16)
        if 0x80000A4C <= address <= 0x80000B2C:
            words = struct.unpack(">4I", bytes.fromhex(memory.attrib["value"]))
            assert words[0] >> 16 == 0x700C and words[3] >> 16 == 0x616B
            entries[address] = [words[0] & 0xFFFF, words[3] & 0xFFFF]
    assert len(entries) == 15
    for enabled, name in [(swap_left, "swaplzl"), (swap_right, "swaprzr")]:
        if enabled == "1":
            for memory in root.find(f"patch[@id='{name}']").findall("memory"):
                address = int(memory.attrib["offset"], 16)
                entries[address][0] = int(memory.attrib["value"], 16) & 0xFFFF
    environment = dict(os.environ, GALAXY_CONTROLLER_LAYOUT=layout,
                       GALAXY_SWAP_L_ZL=swap_left, GALAXY_SWAP_R_ZR=swap_right)
    with tempfile.TemporaryDirectory(prefix="galaxy-controller-") as directory:
        expected = Path(directory) / "expected.bin"
        expected.write_bytes(b"".join(struct.pack("<II", *entry) for entry in entries.values()))
        subprocess.run([str(Path(executable).resolve()), str(expected), directory],
                       env=environment, check=True, timeout=15)
        if (layout, swap_left, swap_right) == ("nvidia", "0", "0"):
            preferences = Path(directory) / "galaxy_controller.dat"
            assert preferences.read_bytes() == b"GCC\x01\x01\x01\x00\x00"
            clean_environment = {key: value for key, value in environment.items()
                                 if key not in {"GALAXY_CONTROLLER_LAYOUT", "GALAXY_SWAP_L_ZL", "GALAXY_SWAP_R_ZR"}}
            subprocess.run([str(Path(executable).resolve()), str(expected), directory, "saved"],
                           env=clean_environment, check=True, timeout=15)
            for invalid in [b"GCC\x01\x01", b"GCC\x01\x02\x01\x00\x00"]:
                preferences.write_bytes(invalid)
                subprocess.run([str(Path(executable).resolve()), str(expected), directory, "default"],
                               env=clean_environment, check=True, timeout=15)


if __name__ == "__main__":
    main()
