"""Create offline BitTorrent v1 metainfo for one verified ZIP; never start seeding."""
import argparse
import hashlib
import json
from pathlib import Path

PIECE_BYTES = 2 * 1024 * 1024


def bencode(value):
    if isinstance(value, bytes):
        return str(len(value)).encode() + b":" + value
    if type(value) is int:
        return b"i" + str(value).encode() + b"e"
    if isinstance(value, list):
        return b"l" + b"".join(bencode(v) for v in value) + b"e"
    if isinstance(value, dict) and all(isinstance(k, bytes) for k in value):
        return b"d" + b"".join(bencode(k) + bencode(value[k]) for k in sorted(value)) + b"e"
    raise TypeError("Only canonical bencode integers, bytes, lists and byte-key dictionaries are accepted")


def create_torrent(archive, output, expected_sha256):
    archive, output = Path(archive), Path(output)
    if archive.is_symlink() or not archive.is_file() or archive.suffix.lower() != ".zip":
        raise ValueError("Expected the verified ZIP file")
    if output.exists():
        raise FileExistsError("Torrent output already exists")
    checksum, pieces, size = hashlib.sha256(), [], 0
    with archive.open("rb") as stream:
        while block := stream.read(PIECE_BYTES):
            checksum.update(block)
            pieces.append(hashlib.sha1(block).digest())
            size += len(block)
    if not size or checksum.hexdigest() != expected_sha256:
        raise ValueError("ZIP SHA256 differs; no torrent written")
    info = {b"length":size, b"name":archive.name.encode("utf-8"), b"piece length":PIECE_BYTES, b"pieces":b"".join(pieces)}
    # BEP5 trackerless metainfo; no guessed trackers, private passkeys or network activity.
    encoded = bencode({b"created by":b"EvilDeadCommunity release tools", b"info":info, b"nodes":[]})
    with output.open("xb") as stream:
        stream.write(encoded)
    return {"file":output.name, "sha256":hashlib.sha256(encoded).hexdigest(),
            "info_hash_v1":hashlib.sha1(bencode(info)).hexdigest(), "payload":archive.name,
            "payload_sha256":checksum.hexdigest(), "payload_bytes":size, "piece_bytes":PIECE_BYTES,
            "pieces":len(pieces), "trackerless":True, "private":False, "network_used":False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sha256", required=True)
    args = parser.parse_args()
    print(json.dumps(create_torrent(args.archive,args.output,args.sha256)), flush=True)
