"""Opt-in full-RAM preservation before the persistent-recovery test package.

Two live retained exports must cover all 30 archive slots and be byte-identical.
Never CONFIRM/CANCEL/WIPE, reset, flash or edit an existing file. A smaller pending
prefix is NOT proof that the rest of RAM is preserved; stop for operator review.
"""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path

from archive_transfer import CAPACITY, MAX_EXPORT_BYTES, load_verified_export
import archive_transfer_wire as wire
from yamcs_archive_transfer import JsonHttp, YamcsTransport, download


class RetainOnly:
    def __init__(self, transport): self.transport = transport
    def exchange(self, body, **kwargs):
        if not isinstance(body, bytes) or not body or body[0] not in (wire.BEGIN, wire.PENDING, wire.READ) or kwargs:
            raise ValueError("Preservation permits only BEGIN/PENDING/READ without deletion")
        return self.transport.exchange(body)


def verify_pair(first, second):
    paths = (Path(first), Path(second))
    if paths[0].resolve() == paths[1].resolve() or any(path.is_symlink() for path in paths):
        raise ValueError("Two different regular retained export files are required")
    manifests = [load_verified_export(path) for path in paths]
    if manifests[0] != manifests[1] or manifests[0].record_count != CAPACITY:
        raise ValueError("Preservation requires two matching full-capacity live manifests")
    raw = []
    for path in paths:
        with path.open("rb") as stream:
            raw.append(stream.read(MAX_EXPORT_BYTES + 1))
    if raw[0] != raw[1] or len(raw[0]) > MAX_EXPORT_BYTES:
        raise ValueError("Retained export bytes differ or exceed the bounded format")
    return dict(manifest=manifests[0].as_dict(), sha256=hashlib.sha256(raw[0]).hexdigest().upper(),
                files=[str(path.resolve()) for path in paths], whole_capacity_verified=True)


def preserve(transport, first, second, receipt):
    paths = tuple(Path(path) for path in (first, second, receipt))
    if len({str(path.resolve()) for path in paths}) != 3 or any(
            path.exists() or path.is_symlink() or not path.parent.is_dir() for path in paths):
        raise ValueError("Use three distinct new files in existing directories; no overwrite")
    retained = RetainOnly(transport)
    pending = retained.exchange(wire.pending_command())
    if pending["status"] not in (0, 4):
        raise ValueError("Could not establish the current retained session")
    if pending["status"] == 0 and pending["manifest"]["record_count"] != CAPACITY:
        raise ValueError("Existing pending prefix does not cover all RAM; operator review required")
    first_result = download(retained, paths[0], resume_pending=pending["status"] == 0, progress=lambda _: None)
    if first_result["outcome"] != "saved-retained":
        raise ValueError("Full-capacity retained archive is required before any reset")
    download(retained, paths[1], resume_pending=True, progress=lambda _: None)
    proof = verify_pair(paths[0], paths[1])
    proof["result"] = "PASS"
    proof["limits"] = "Live bounded full-capacity snapshot before first flash only; no guarantee for later test-generated RAM or independent backup media"
    with paths[2].open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(proof, indent=2) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    with paths[2].open(encoding="utf-8") as stream:
        if json.load(stream) != proof:
            raise ValueError("Preservation receipt read-back failed")
    verify_pair(paths[0], paths[1])
    return proof


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--board-ip", type=ipaddress.IPv4Address, default=ipaddress.IPv4Address("192.168.1.5"))
    parser.add_argument("--send-retained-reads", action="store_true", required=True)
    parser.add_argument("--first", type=Path, required=True)
    parser.add_argument("--second", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        transport = YamcsTransport(JsonHttp(args.url), board_ip=str(args.board_ip), allow_send=True, timeout=8, retries=1)
        proof = preserve(transport, args.first, args.second, args.receipt)
    except (Exception, KeyboardInterrupt):
        print("FAIL: complete retained RAM preservation was not verified; do not reset/upload. No automatic cancel/clear.")
        return 1
    print(f"PASS: two live retained exports; records={proof['manifest']['record_count']}; sha256={proof['sha256']}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
