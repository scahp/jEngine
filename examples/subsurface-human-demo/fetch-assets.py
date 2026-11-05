#!/usr/bin/env python3
"""Retrieve only missing, exact pinned CC BY head assets for the portable sample."""
import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import urllib.request

COMMIT = "51249d3d4d9ea1c3320741077aaa5335b28c3418"
ASSETS = {'Infinite-Level_02.OBJ': {'bytes': 1383204, 'sha256': '6a250caf0a10ec72ff3e175fe9d56e32dbbae00d525ea2cba38aa465583171d3'}, 'Infinite-Level_02.jpg': {'bytes': 8975275, 'sha256': '4fc7297835e2081cb914d1a8507fc837687102eed7bd3c6bed25b72960a09e08'}, 'Infinite-Level_02_World_NoSmoothUV.jpg': {'bytes': 18297370, 'sha256': 'd1c3d6610f5cfc879f8eb09d78f74544680fe59b4da7c0fbe2cf4e7ec69996ae'}, 'SpecularAOMap.png': {'bytes': 139507, 'sha256': 'fc7740e66a8aaeb951d003f146adb84701890d3a042a1b7aae4cb29423724561'}}

def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true", help="Check existing files without network access")
    args = parser.parse_args()
    target_dir = Path(__file__).resolve().parent / "Assets" / "InfiniteHead"
    target_dir.mkdir(parents=True, exist_ok=True)
    for name, expected in ASSETS.items():
        target = target_dir / name
        if target.exists():
            if not target.is_file() or target.stat().st_size != expected["bytes"] or digest(target) != expected["sha256"]:
                raise RuntimeError(name + " differs from the pinned source; keep your edit and restore it explicitly before retrying")
            print("Verified " + name)
            continue
        if args.verify_only:
            raise RuntimeError("Missing " + name)
        url = "https://raw.githubusercontent.com/scahp/Shadows/" + COMMIT + "/Shadows/SkinData/" + name
        temporary = None
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "jEngine-pinned-skin-sample"})
            with urllib.request.urlopen(request, timeout=60) as source, tempfile.NamedTemporaryFile(dir=target_dir, prefix=".skin-download-", delete=False) as output:
                temporary = Path(output.name)
                size, checksum = 0, hashlib.sha256()
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    size += len(block)
                    if size > expected["bytes"]:
                        raise RuntimeError("Source exceeds the pinned size for " + name)
                    checksum.update(block)
                    output.write(block)
            if size != expected["bytes"] or checksum.hexdigest() != expected["sha256"]:
                raise RuntimeError("Source size/hash mismatch for " + name)
            if target.exists():
                raise RuntimeError("A file appeared during retrieval; preserving " + name)
            os.replace(temporary, target)
            temporary = None
            print("Downloaded and verified " + name)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    print("All four original assets verified. Keep ATTRIBUTION.md and provenance.json with the sample.")

if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("Skin sample: " + str(error), file=sys.stderr)
        sys.exit(1)
