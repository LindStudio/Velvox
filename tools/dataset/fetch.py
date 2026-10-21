#!/usr/bin/env python3
"""Headliner benchmark dataset manager.

Downloads the approved test-vocal sources listed in sources.json into a local data
directory (never into the repository), verifies them, extracts them and records the
provenance of every file.

    python tools/dataset/fetch.py                 # all sources with status "approved"
    python tools/dataset/fetch.py --source vocalset11
    python tools/dataset/fetch.py --list

Data directory: --data, else %HEADLINER_DATA%, else %USERPROFILE%/dev/velvox-data
Layout:
    downloads/<file>            original archives, never modified
    sources/<id>/...            extracted originals (read-only use)
    manifest/<id>.json          provenance: url, license, retrieval date, md5/sha256 per file
    logs/fetch.log              failures and retries

Nothing here bypasses access restrictions: sources whose license is not verified for
our use ("not-approved", "unverified") are never downloaded; "evaluation-only" needs
--allow-evaluation and is marked as such in the manifest.
"""

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import ssl
import sys
import time
import urllib.request
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
CHUNK = 1 << 20


def data_dir(arg):
    if arg:
        return pathlib.Path(arg)
    env = os.environ.get("HEADLINER_DATA")
    if env:
        return pathlib.Path(env)
    return pathlib.Path(os.path.expanduser("~")) / "dev" / "velvox-data"


def log(root, msg):
    (root / "logs").mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().isoformat(timespec="seconds")
    with open(root / "logs" / "fetch.log", "a", encoding="utf-8") as f:
        f.write(f"{stamp} {msg}\n")
    print(msg)


def file_hash(path, algo):
    h = hashlib.new(algo)
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(CHUNK), b""):
            h.update(block)
    return h.hexdigest()


def download(url, target, expected_bytes, root, retries=5):
    """Resumable download (HTTP Range). Returns True when the file is complete."""
    ctx = ssl.create_default_context()
    for attempt in range(1, retries + 1):
        have = target.stat().st_size if target.exists() else 0
        if expected_bytes and have == expected_bytes:
            return True
        req = urllib.request.Request(url, headers={"User-Agent": "Headliner-dataset-manager/1.0"})
        if have:
            req.add_header("Range", f"bytes={have}-")
        try:
            with urllib.request.urlopen(req, context=ctx, timeout=60) as r:
                mode = "ab" if have and r.status == 206 else "wb"
                if mode == "wb":
                    have = 0
                with open(target, mode) as f:
                    last = time.time()
                    while True:
                        block = r.read(CHUNK)
                        if not block:
                            break
                        f.write(block)
                        have += len(block)
                        if time.time() - last > 15:
                            pct = f" ({100.0 * have / expected_bytes:.1f} %)" if expected_bytes else ""
                            print(f"  {target.name}: {have / 1e6:.0f} MB{pct}", flush=True)
                            last = time.time()
            if not expected_bytes or have == expected_bytes:
                return True
            log(root, f"incomplete download of {target.name}: {have} of {expected_bytes} bytes (attempt {attempt})")
        except Exception as e:  # network errors: retry with resume
            log(root, f"download error {target.name} (attempt {attempt}): {e}")
            time.sleep(min(60, 5 * attempt))
    return False


def extract(archive, dest, root):
    dest.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            name = pathlib.PurePosixPath(info.filename)
            # Archive members must stay inside the destination (no absolute paths, no "..")
            if name.is_absolute() or ".." in name.parts:
                log(root, f"skipped unsafe archive member {info.filename}")
                continue
            if name.name.startswith("._") or "__MACOSX" in name.parts:
                continue
            z.extract(info, dest)


def build_manifest(source, root, retrieved):
    src_dir = root / "sources" / source["id"]
    files, seen = [], {}
    for p in sorted(src_dir.rglob("*")):
        if p.is_file() and p.suffix.lower() in (".wav", ".flac", ".aif", ".aiff", ".mp3"):
            sha = file_hash(p, "sha256")
            entry = {"path": str(p.relative_to(root)).replace("\\", "/"), "bytes": p.stat().st_size, "sha256": sha}
            if sha in seen:
                entry["duplicate_of"] = seen[sha]
            else:
                seen[sha] = entry["path"]
            files.append(entry)
    manifest = {
        "source": source["id"], "name": source["name"], "url": source["url"],
        "license": source.get("license"), "license_url": source.get("license_url"),
        "attribution": source.get("attribution"), "approved": source.get("approved", []),
        "restrictions": source.get("restrictions", []), "status": source.get("status"),
        "retrieved": retrieved, "archive_md5": source.get("md5"), "files": files,
    }
    (root / "manifest").mkdir(parents=True, exist_ok=True)
    with open(root / "manifest" / f"{source['id']}.json", "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    dups = sum(1 for e in files if "duplicate_of" in e)
    log(root, f"{source['id']}: manifest with {len(files)} audio files ({dups} duplicates)")


def fetch(source, root, allow_eval):
    status = source.get("status")
    if status != "approved" and not (allow_eval and status == "evaluation-only"):
        log(root, f"{source['id']}: not downloaded (status {status})")
        return False
    if not source.get("download"):
        log(root, f"{source['id']}: no direct download URL")
        return False

    downloads = root / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    target = downloads / source.get("file", source["id"] + ".zip")
    retrieved = dt.date.today().isoformat()

    if not download(source["download"], target, source.get("bytes"), root):
        log(root, f"{source['id']}: download FAILED")
        return False
    if source.get("md5"):
        md5 = file_hash(target, "md5")
        if md5 != source["md5"]:
            log(root, f"{source['id']}: md5 mismatch ({md5}) - file kept for inspection, not extracted")
            return False
        log(root, f"{source['id']}: md5 verified")

    dest = root / "sources" / source["id"]
    if not any(dest.rglob("*.wav")):
        extract(target, dest, root)
    build_manifest(source, root, retrieved)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data")
    ap.add_argument("--source")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--allow-evaluation", action="store_true")
    args = ap.parse_args()

    with open(HERE / "sources.json", encoding="utf-8") as f:
        sources = json.load(f)["sources"]
    if args.list:
        for s in sources:
            print(f"{s['id']:20s} {s.get('status', '?'):16s} {s.get('license', '?')}")
        return 0

    root = data_dir(args.data)
    ok = True
    for s in sources:
        if args.source and s["id"] != args.source:
            continue
        ok = fetch(s, root, args.allow_evaluation) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
