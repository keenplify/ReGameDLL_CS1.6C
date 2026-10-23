#!/usr/bin/env python3
"""Create a checksummed Linux x86 fork bundle from a completed local build."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def package(build, destination):
    files = {
        "cstrike/dlls/cs.so": build / "regamedll/cs.so",
        "cstrike/addons/regamedll/libregamedll_query.so": build / "libregamedll_query.so",
        "cstrike/addons/regamedll/competitive_bots.cfg": ROOT / "regamedll/extra/competitive_bots.cfg",
    }
    for path in (ROOT / "regamedll/extra/regamedll").rglob("*"):
        if path.is_file():
            files["cstrike/addons/regamedll/" + path.relative_to(ROOT / "regamedll/extra/regamedll").as_posix()] = path
    for name in ("backend_ai_bridge.py", "standalone_ai_provider.py", "reply_templates.py", "run-ai.sh", "run-dedicated.sh", "export_cz_callouts.py"):
        files["cstrike/addons/regamedll/tools/" + name] = ROOT / "tools" / name
    for name in ("COMPETITIVE_BOTS.md", "YAPB_PORT_AUDIT.md", "LICENSE"):
        if (ROOT / name).is_file():
            files[name] = ROOT / name
    payload = {name: source.read_bytes() for name, source in sorted(files.items())}
    for name in ("cstrike/dlls/cs.so", "cstrike/addons/regamedll/libregamedll_query.so"):
        if payload[name][:5] != b"\x7fELF\x01":
            raise ValueError("release requires Linux x86 ELF binaries")
    payload["SHA256SUMS"] = "".join(hashlib.sha256(data).hexdigest() + "  " + name + "\n" for name, data in payload.items()).encode()
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Uncompressed tar, fixed metadata and sorted entries give deterministic packaging.
    with tarfile.open(destination, "w", format=tarfile.PAX_FORMAT) as archive:
        for name, data in sorted(payload.items()):
            entry = tarfile.TarInfo(name)
            entry.size = len(data)
            entry.mode = 0o755 if name.endswith(".sh") else 0o644
            entry.mtime = 0
            archive.addfile(entry, io.BytesIO(data))
    print(json.dumps({"artifact": str(destination), "files": len(payload), "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build-competitive")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/regamedll-competitive-linux-x86.tar")
    args = parser.parse_args()
    package(args.build, args.output)
