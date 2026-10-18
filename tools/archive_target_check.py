"""Physical archive transaction checks. Dry-run unless BOTH --send and --confirm-clear.

Uses a previously verified pending export, preserves/saves its appended tail, then
confirms only saved snapshots. No serial/reset/WIPE/mode/link/queue changes.
Response-loss injection discards one accepted HTTP response locally, NOT UDP.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import secrets
import struct
from urllib import parse

from archive_downlink_check import decode_archive_packet
from archive_transfer import load_verified_export, unique_keys
import archive_transfer_wire as wire
from yamcs_archive_command_check import validate_command_binary
from yamcs_archive_transfer import (
    BoardRejected, JsonHttp, YamcsTransport, control_export, download,
    timestamp, unbase64, validate_binary,
)


class AcceptedResponseLoss:
    """Keep real server/board traffic intact; discard one successful POST response."""
    def __init__(self, api, command_name):
        self.api, self.command_name = api, command_name
        self.accepted = []

    def __call__(self, path, body=None, timeout=4):
        target = (body is not None and body.get("dryRun") is False and
                  parse.unquote(path.rsplit("/commands/", 1)[-1]) == self.command_name)
        if target and self.accepted and body.get("args") != self.accepted[0][0]:
            raise ValueError("Retry changed the command arguments before POST")
        result = self.api(path, body, timeout=timeout)
        if target and result.get("commandName") == self.command_name:
            if not isinstance(result.get("id"), str) or not result["id"]:
                raise ValueError("Loss injection requires an accepted command identity")
            args = copy.deepcopy(body["args"])
            if self.accepted and args != self.accepted[0][0]:
                raise ValueError("Retry changed the command arguments")
            self.accepted.append((args, result))
            if len(self.accepted) == 1:
                raise TimeoutError("CONTROLLED: accepted HTTP response discarded locally")
        return result


def with_response_loss(transport, op, operation):
    original_api, original_retries = transport.api, transport.retries
    _, name = transport._path(op)
    loss = AcceptedResponseLoss(original_api, name)
    try:
        transport.api, transport.retries = loss, 1
        result = operation()
    finally:
        transport.api, transport.retries = original_api, original_retries
    if len(loss.accepted) != 2 or loss.accepted[0][1]["id"] == loss.accepted[1][1]["id"]:
        raise ValueError("Expected exactly two distinct accepted commands with identical arguments")
    body = unbase64(loss.accepted[1][1].get("unprocessedBinary"))[6:]
    for _, issued in loss.accepted:
        validate_binary(unbase64(issued.get("unprocessedBinary")), body, False)
        deadline = transport.clock() + transport.timeout
        while not transport._history_sent(issued, body, name, deadline):
            if transport.clock() >= deadline:
                raise TimeoutError("Accepted loss-test command lacks verified Sent history")
            transport.sleep(min(0.1, deadline - transport.clock()))
    return result


def packet_baseline(transport, deadline):
    latest, previous, existing = None, None, set()
    for items in transport._packet_pages({"link": transport.incoming, "order": "desc", "limit": 100}, deadline):
        for item in items:
            if item.get("link") != transport.incoming:
                continue
            generated = timestamp(item.get("generationTime"))
            if previous is not None and generated > previous:
                raise ValueError("Packet baseline is not in descending time order")
            previous = generated
            if latest is None:
                latest = generated
            if generated < latest:
                return latest, existing
            existing.add((generated, unbase64(item.get("packet"))))
    return latest, existing


def legacy_read(transport, index):
    """Read one bounded existing global index for byte-exact tail comparison."""
    if not transport.allow_send or type(index) is not int or not 0 <= index < 30:
        raise ValueError("Legacy proof reads require --send and an index in 0..29")
    deadline = transport.clock() + transport.timeout
    name = f"/{transport.instance}/ARCHIVE_GET_BY_INDEX"
    path = (f"/api/processors/{transport.instance}/{transport.processor}/commands/" +
            parse.quote(name, safe=""))
    args = {"RecordIndex": index}
    transport._links(deadline)
    dry = transport._call(path, {"args": args, "dryRun": True}, deadline)
    if dry.get("commandName") != name:
        raise ValueError("Unexpected legacy dry-run command")
    validate_command_binary(unbase64(dry.get("unprocessedBinary")), index, postprocessed=False)
    latest, existing = packet_baseline(transport, deadline)
    transport.sequence += 1
    issued = transport._call(path, {"args": args, "dryRun": False, "origin": transport.origin,
                                    "sequenceNumber": transport.sequence}, deadline)
    if issued.get("commandName") != name or not isinstance(issued.get("id"), str) or not issued["id"]:
        raise ValueError("Unexpected legacy command identity")
    validate_command_binary(unbase64(issued.get("unprocessedBinary")), index, postprocessed=False)
    lower = timestamp(issued.get("generationTime"))
    if latest is not None and lower < latest:
        raise ValueError("Server clock precedes baseline")
    sent, candidate = False, None
    body = struct.pack("!BH", 0x30, index)
    while transport.clock() < deadline:
        if not sent:
            sent = transport._history_sent(issued, body, name, deadline)
        params = {"start": issued["generationTime"], "link": transport.incoming, "order": "asc", "limit": 100}
        for items in transport._packet_pages(params, deadline):
            for item in items:
                if item.get("link") != transport.incoming:
                    continue
                generated, raw = timestamp(item.get("generationTime")), unbase64(item.get("packet"))
                if generated < lower or (generated, raw) in existing or len(raw) < 2:
                    continue
                if struct.unpack_from("!H", raw)[0] & 0x07FF != 96:
                    continue
                reply = decode_archive_packet(raw)
                if reply["index"] != index:
                    continue
                record = {key: reply[key] for key in ("index", "record_id", "length", "crc")}
                record["payload"] = reply["payload"].hex()
                if candidate is not None and record != candidate:
                    raise ValueError("Conflicting legacy record replies")
                candidate = record
        if sent and candidate is not None:
            return candidate
        transport.sleep(min(0.1, max(0, deadline - transport.clock())))
    raise TimeoutError("Legacy tail proof lacks matching Sent history and CRC-valid reply")


def content(record):
    return tuple(record[key] for key in ("record_id", "length", "crc", "payload"))


def new_json(path, document):
    raw = (json.dumps(document, sort_keys=True, indent=2) + "\n").encode("utf-8")
    if len(raw) > 1048576:
        raise ValueError("Proof exceeds 1 MiB")
    with Path(path).open("xb") as stream:
        if stream.write(raw) != len(raw):
            raise OSError("Short proof write")
        stream.flush()
        os.fsync(stream.fileno())
    with Path(path).open("rb") as stream:
        if stream.read(len(raw) + 1) != raw:
            raise ValueError("Proof read-back mismatch")


def check(transport, export_path, output_dir, total_records=30, *, confirm_clear=False, progress=print):
    if confirm_clear is not True or not transport.allow_send:
        raise ValueError("Physical checks require --send AND --confirm-clear")
    original = load_verified_export(export_path)
    if type(total_records) is not int or not original.record_count < total_records <= 30:
        raise ValueError("Need an existing prefix plus appended tail within 30 slots")
    root = Path(output_dir)
    paths = {key: root / (Path(export_path).stem + suffix) for key, suffix in
             (("before", ".tail-before.json"), ("tail", ".tail.json"),
              ("resumed", ".resumed.json"), ("report", ".target-report.json"))}
    if not root.is_dir() or any(path.exists() or path.is_symlink() for path in paths.values()):
        raise ValueError("Proof/export paths must all be new files in an existing directory")

    def pending(expected=None):
        reply = transport.exchange(wire.pending_command())
        if expected is None:
            if reply["status"] != 4:
                raise ValueError("Expected no pending session")
        elif reply["status"] != 0 or reply["manifest"] != expected.as_dict():
            raise ValueError("Pending session differs from the intended saved snapshot")

    excluded = {original.request_id}

    def begin_tail(*, lose=False):
        req = next_request(excluded)
        operation = lambda: transport.exchange(wire.begin_command(req))
        reply = with_response_loss(transport, wire.BEGIN, operation) if lose else operation()
        transfer = wire.Transfer.from_dict(reply["manifest"])
        if (reply["status"] != 0 or transfer.boot_id != original.boot_id or
                transfer.record_count != len(tail)):
            raise ValueError("Tail BEGIN changed boot/count")
        return transfer

    def compare_saved(path):
        load_verified_export(path)
        with Path(path).open("r", encoding="utf-8") as stream:
            records = json.load(stream, object_pairs_hook=unique_keys)["records"]
        if [content(record) for record in records] != [content(record) for record in tail]:
            raise ValueError("Saved tail differs byte-for-byte from the pre-confirmation tail")

    pending(original)
    tail = [legacy_read(transport, index) for index in range(original.record_count, total_records)]
    new_json(paths["before"], {"format": "fsw-tail-proof-v1", "parent": original.as_dict(), "records": tail})
    progress(f"PRE-CLEAR: {len(tail)} appended records CRC-verified and saved separately")
    with_response_loss(transport, wire.READ,
                       lambda: transport.exchange(wire.read_command(original, 0)))
    progress("PASS: READ retries the same body after locally discarded accepted HTTP response")
    pending(original)
    with_response_loss(transport, wire.CONFIRM,
                       lambda: control_export(transport, export_path, confirm_clear=True))
    pending()
    remaining = [legacy_read(transport, index) for index in range(len(tail))]
    if [content(record) for record in remaining] != [content(record) for record in tail]:
        raise ValueError("CONFIRM did not preserve the appended tail exactly")
    progress(f"PASS: saved prefix confirmed despite HTTP reply loss; all {len(tail)} tail records preserved")

    current = begin_tail()
    download(transport, paths["tail"], request_id=current.request_id, progress=progress)
    compare_saved(paths["tail"])
    control_export(transport, export_path, confirm_clear=True)  # Old receipt, while NEW session is active.
    pending(current)
    for index, expected in enumerate(tail):
        if content(transport.exchange(wire.read_command(current, index))["record"]) != content(expected):
            raise ValueError("Repeated old CONFIRM changed the newer pending tail")
    progress("PASS: duplicate old CONFIRM is a no-op during a newer active transfer")

    control_export(transport, paths["tail"], cancel=True)
    control_export(transport, paths["tail"], cancel=True)
    pending()
    current = begin_tail(lose=True)
    progress("PASS: CANCEL/duplicate CANCEL retain tail; new BEGIN survives accepted HTTP reply loss")
    transport.exchange(wire.read_command(current, 0))  # Deliberately stop before saving this transfer.
    result = download(transport, paths["resumed"], resume_pending=True, progress=progress)
    if result["saved"].transfer != current:
        raise ValueError("Resume replaced the pending snapshot")
    compare_saved(paths["resumed"])
    progress("PASS: interrupted transfer resumes through PENDING without replacement/clear")

    control_export(transport, paths["resumed"], confirm_clear=True)
    control_export(transport, paths["resumed"], confirm_clear=True)
    pending()
    try:
        control_export(transport, export_path, confirm_clear=True)
    except BoardRejected as exception:
        if not str(exception).endswith(": STALE"):
            raise
    else:
        raise ValueError("Old receipt should be STALE after the newer confirmation")
    reply = transport.exchange(wire.begin_command(next_request(excluded)))
    if reply["status"] != 5:
        raise ValueError("Archive is not empty after confirming the saved tail")
    pending()
    report = {"format": "fsw-archive-target-check-v1", "result": "PASS",
              "prefix": original.as_dict(), "last_tail": current.as_dict(),
              "tail_count": len(tail), "confirmed_records": total_records,
              "loss_model": "one accepted HTTP response discarded locally per READ/CONFIRM/BEGIN; not UDP loss",
              "paths": {key: str(path.resolve()) for key, path in paths.items()}}
    new_json(paths["report"], report)
    progress(f"PASS: saved tail confirmed; latest receipt repeats; older receipt STALE; archive EMPTY ({total_records} records safely saved)")
    return report


def next_request(excluded):
    for _ in range(8):
        value = secrets.randbits(32)
        if value not in excluded:
            excluded.add(value)
            return value
    raise RuntimeError("Could not allocate a fresh request ID")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--board-ip", default="192.168.1.5")
    parser.add_argument("--total-records", type=int, default=30)
    parser.add_argument("--send", action="store_true")
    parser.add_argument("--confirm-clear", action="store_true")
    args = parser.parse_args(argv)
    if args.send != args.confirm_clear:
        parser.error("Use both --send and --confirm-clear, or neither for dry-run")
    load_verified_export(args.export)
    transport = YamcsTransport(JsonHttp(args.url), board_ip=args.board_ip,
                               allow_send=args.send, timeout=8, retries=0)
    if not args.send:
        transport.preview(wire.begin_command(0))
        transport.preview(wire.pending_command())
        print("DRY-RUN ONLY: no board commands, files, confirmations or resets")
        return
    check(transport, args.export, args.output_dir, args.total_records, confirm_clear=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, TypeError) as exception:
        raise SystemExit("FAIL (no automatic cleanup/reset): " + str(exception)) from exception
