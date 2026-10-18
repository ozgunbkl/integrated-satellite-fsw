"""Opt-in bounded ground drain: verified dual save BEFORE exact-prefix CONFIRM.

No reset, WIPE, mode/fault commands, cancel, retries, or automatic resume after
an ambiguous result. Default only previews MDB generation; never sends TC.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time

from archive_transfer import MAX_EXPORT_BYTES, load_verified_export
import archive_transfer_wire as wire
from resource_monitor_check import write_snapshot
from yamcs_archive_transfer import JsonHttp, YamcsTransport, TransferError, download


def drain_once(transport, directory, index, *, confirm_clear=False, before_confirm=None):
    if confirm_clear is not True:
        raise ValueError("Explicit confirmation permission required before any exchange")
    pending = transport.exchange(wire.pending_command())
    if pending["status"] not in (0, 4):
        raise TransferError("Pending state unavailable; no clear attempted")
    path = Path(directory) / f"batch-{index:06d}.json"
    backup = Path(directory) / f"batch-{index:06d}.backup.json"
    if path.exists() or backup.exists() or path.is_symlink() or backup.is_symlink():
        raise ValueError("Batch paths must be new regular files")
    result = download(transport, path, resume_pending=pending["status"] == 0,
                      confirm_clear=False, progress=lambda _: None)
    if result["outcome"] == "empty":
        return None
    if result["outcome"] != "saved-retained":
        raise TransferError("Not a retained verified export; no clear")
    saved = result["saved"]
    with path.open("rb") as stream:
        raw = stream.read(MAX_EXPORT_BYTES + 1)
    if len(raw) > MAX_EXPORT_BYTES or load_verified_export(path) != saved.transfer:
        raise ValueError("Saved export changed before backup; no clear")
    with backup.open("xb") as stream:
        if stream.write(raw) != len(raw):
            raise OSError("Short backup write; no clear")
        stream.flush()
        os.fsync(stream.fileno())
    if backup.read_bytes() != raw or load_verified_export(backup) != saved.transfer:
        raise ValueError("Backup read-back failed; no clear")
    proof = dict(manifest=saved.transfer.as_dict(), primary=path.name, backup=backup.name,
                 sha256=hashlib.sha256(raw).hexdigest().upper(), confirmed=False)
    if before_confirm:
        before_confirm(proof)  # Durable pending checkpoint BEFORE destructive TC.
    transport.exchange(wire.confirm_command(saved, confirm_clear=True), saved=saved, confirm_clear=True)
    proof["confirmed"] = True
    return proof


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--board-ip", default="192.168.1.5")
    parser.add_argument("--send", action="store_true")
    parser.add_argument("--confirm-clear", action="store_true")
    parser.add_argument("--directory", type=Path, help="New directory under an existing parent")
    parser.add_argument("--stop-file", type=Path, help="Stop normally between batches when this new marker appears")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--once", action="store_true")
    selection.add_argument("--duration", type=int, help="300..7200 s, stops by deadline without mode changes")
    args = parser.parse_args(argv)
    if args.duration is not None and not 300 <= args.duration <= 7200:
        parser.error("Duration must be 300..7200 seconds")
    if not args.send:
        if args.confirm_clear or args.directory or args.once or args.duration is not None or args.stop_file:
            parser.error("Active options require --send and --confirm-clear; no I/O attempted")
        transport = YamcsTransport(JsonHttp(args.url), board_ip=args.board_ip)
        transport.preview(wire.begin_command(1))
        transport.preview(wire.pending_command())
        print("PASS: generation preview only; no board commands or clear")
        return 0
    if not args.confirm_clear or not args.directory or not (args.once or args.duration is not None):
        parser.error("Requires --send --confirm-clear --directory NEW and --once or --duration")
    if (args.directory.exists() or args.directory.is_symlink() or
            not args.directory.parent.is_dir()):
        parser.error("Directory must be new under an existing parent; no overwrite")
    if args.stop_file and (args.stop_file.exists() or args.stop_file.is_symlink() or not args.stop_file.parent.is_dir()):
        parser.error("Stop marker must be new under an existing parent; no I/O attempted")
    transport = YamcsTransport(JsonHttp(args.url), board_ip=args.board_ip,
                               allow_send=True, retries=0, timeout=8)
    args.directory.mkdir()
    started = time.monotonic()
    report = dict(result="RUNNING", batches=[], confirmed_records=0, pending=None,
                  limits="Verified dual local save then exact-prefix removal; no independent backup media, no mode/reset/WIPE or all-NOMINAL proof; ambiguous result stops without retry")
    with (args.directory / "drain.json").open("x", encoding="utf-8") as stream:
        def checkpoint(proof):
            report["pending"] = dict(proof)
            report["elapsed_seconds"] = round(time.monotonic() - started, 3)
            write_snapshot(stream, report)
        try:
            write_snapshot(stream, report)
            index = 0
            while True:
                proof = drain_once(transport, args.directory, index,
                                   confirm_clear=True, before_confirm=checkpoint)
                index += 1
                if proof is not None:
                    report["batches"].append(proof)
                    report["confirmed_records"] += proof["manifest"]["record_count"]
                    report["pending"] = None
                    print(f"CONFIRMED: batch={index - 1} records={proof['manifest']['record_count']} dual-save verified", flush=True)
                report["elapsed_seconds"] = round(time.monotonic() - started, 3)
                write_snapshot(stream, report)
                if args.once or time.monotonic() - started >= args.duration or (args.stop_file and args.stop_file.exists()):
                    break
                time.sleep(min(2, max(0, args.duration - (time.monotonic() - started))))
            report["result"] = "PASS"
            write_snapshot(stream, report)
            print("PASS: selected bounded drain completed; each removed prefix has two verified local exports")
            return 0
        except (Exception, KeyboardInterrupt) as error:
            report["result"] = "INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAIL"
            report["reason"] = "Drain stopped; retain files/pending evidence, no automatic retry or fault/mode override"
            report["elapsed_seconds"] = round(time.monotonic() - started, 3)
            write_snapshot(stream, report)
            print(report["result"] + ": " + report["reason"], flush=True)
            return 130 if isinstance(error, KeyboardInterrupt) else 1


if __name__ == "__main__":
    raise SystemExit(main())
