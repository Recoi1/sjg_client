from pathlib import Path
import hashlib
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/yaz0-decode-encode"))
import yaz0

ASSETS = (
    "LayoutData/TitleLogo.arc",
    "LayoutData/PressStart.arc",
    "ObjectData/Mario.arc",
    "StageData/HeavensDoorGalaxy.arc",
    "StageData/HeavensDoorGalaxy/HeavensDoorGalaxyScenario.arc",
    "StageData/EggStarGalaxy.arc",
)


def main():
    for relative in ASSETS:
        source = ROOT / "Petari/orig/RMGE01/files" / relative
        # shortcut: 64 MiB per archive, raise only for a verified larger game asset.
        if source.stat().st_size > 64 * 1024 * 1024:
            raise RuntimeError(f"Input archive exceeds size limit: {source}")
        data = source.read_bytes()
        if data[:4] == b"Yaz0":
            data = yaz0.decompress(data)
        if data[:4] != b"RARC":
            raise RuntimeError(f"Expected RARC archive: {source}")
        destination = ROOT / "build-assets" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix(".arc.tmp")
        temporary.write_bytes(data)
        temporary.replace(destination)
        print(f"Prepared {relative}: {len(data):,} bytes")
    dol = (ROOT / "Petari/orig/RMGE01/sys/main.dol").read_bytes()
    if hashlib.sha1(dol).hexdigest() != "9a71008ae1ee9010e267fa67d1f0b0d4f0e895dd":
        raise RuntimeError("Embedded boot archive requires the verified RMGE01 executable")
    # RMGE01's embedded ErrorMessageArchive, bounded by its verified executable extent.
    error = dol[0x53a0e0:0x53a0e0 + 0x9ac0]
    if len(error) != 0x9ac0 or error[:8] != b"RARC\x00\x00\x9a\xc0":
        raise RuntimeError("Unexpected embedded boot archive")
    destination = ROOT / "build-assets/Memory/ErrorMessageArchive.arc"
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".arc.tmp")
    temporary.write_bytes(error)
    temporary.replace(destination)
    print(f"Prepared embedded ErrorMessageArchive: {len(error):,} bytes")
    # These extents are tied to the verified RMGE01 DOL above.
    for name, offset, size, header in (
        ("GalaxyIDBCSV", 0x538900, 0xd20, (55, 7, 100, 24)),
        ("StoryEventBCSV", 0x538720, 0x1e0, (14, 2, 40, 8)),
    ):
        data = dol[offset:offset + size]
        actual = tuple(int.from_bytes(data[i:i + 4], "big") for i in range(0, 16, 4))
        if len(data) != size or actual != header:
            raise RuntimeError(f"Unexpected embedded table: {name}")
        destination = ROOT / "build-assets/Memory" / (name + ".bcsv")
        temporary = destination.with_suffix(".bcsv.tmp")
        temporary.write_bytes(data)
        temporary.replace(destination)
        print(f"Prepared embedded {name}: {size:,} bytes")



if __name__ == "__main__":
    main()
