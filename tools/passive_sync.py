"""Synchronize native speech buckets through the authenticated reply-cache API."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
from urllib.request import Request, urlopen

from passive_banter import buckets, cache_key
from standalone_ai_provider import validate_model_choice


def bucket_keys():
    return [cache_key(mode, topic, frame) for mode, topic, frame, _ in buckets()]


def validate_groups(groups, keys):
    if not isinstance(groups, dict) or set(groups) != set(keys):
        raise ValueError("incomplete passive catalog")
    for lines in groups.values():
        if not isinstance(lines, list) or not 1 <= len(lines) <= 1000:
            raise ValueError("invalid passive bucket")
        for line in lines:
            validate_model_choice({"reply": line}, {})
            if line != line.strip() or not line.strip():
                raise ValueError("invalid passive line")


def sync(path, remote):
    if not remote.url or not remote.token:
        raise ValueError("missing cache connection")
    keys = bucket_keys()
    groups = {}
    for key in keys:
        req = Request(remote.url + "?key=" + key, headers={"Authorization": "Bearer " + remote.token})
        with urlopen(req, timeout=3) as response:
            raw = response.read(600001)
        if len(raw) > 600000:
            raise ValueError("oversized passive bucket")
        groups[key] = json.loads(raw)["variants"]
    validate_groups(groups, keys)
    body = json.dumps(groups, ensure_ascii=False, separators=(",", ":")).encode()
    if len(body) > 12_000_000:
        raise ValueError("oversized passive catalog")
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as f:
        temp = Path(f.name)
    try:
        temp.write_bytes(body)
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)
    return hashlib.sha256(body).hexdigest()


if __name__ == "__main__":
    from backend_ai_bridge import RemoteCache
    import sys
    try:
        version = sync(Path(sys.argv[1]), RemoteCache())
        print("[ReGameDLL] passive catalog synchronized", version[:16], flush=True)
    except (ValueError, OSError, KeyError, TypeError) as error:
        print("[ReGameDLL] passive catalog sync failed:", type(error).__name__, file=sys.stderr)
        sys.exit(1)
