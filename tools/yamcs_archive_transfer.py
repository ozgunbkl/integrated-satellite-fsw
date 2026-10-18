"""Bounded Yamcs archive downloader. Default: MDB dry-run only, no board command.

--send --output FILE downloads and retains records/session. Deletion additionally
requires --confirm-clear after verified exclusive save/fsync/read-back. No raw UDP,
serial, reset, legacy WIPE, link/queue enable or server configuration changes.
"""
import argparse
import base64
from datetime import datetime
import ipaddress
import json
import math
from pathlib import Path
import re
import secrets
import struct
import time
from urllib import error, parse, request

from archive_transfer import (Transfer, load_verified_export, resync_verified_export,
                              save_verified_export, unique_keys)
import archive_transfer_wire as wire

NAMES = {wire.BEGIN: "BEGIN", wire.PENDING: "PENDING", wire.READ: "READ", wire.CONFIRM: "CONFIRM", wire.CANCEL: "CANCEL"}
STATUS = ("OK", "FULL", "CRC", "PARAM", "NOT_FOUND", "EMPTY", "UNAVAILABLE", "BUSY", "STALE", "NOT_READY", "EXHAUSTED")
MAX_JSON = 1048576


class TransferError(RuntimeError):
    pass


class BoardRejected(TransferError):
    pass


class OutcomeUnknown(TransferError):
    pass


class NoRedirect(request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class JsonHttp:
    def __init__(self, url, opener=None, clock=time.monotonic):
        parsed = parse.urlsplit(url)
        if (parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username is not None or
                parsed.password is not None or parsed.path not in ("", "/") or parsed.query or parsed.fragment):
            raise ValueError("Use an http(s) server origin without credentials/path/query/fragment")
        if parsed.port is not None and not 1 <= parsed.port <= 65535:
            raise ValueError("Invalid server port")
        self.url = url.rstrip("/")
        self.clock = clock
        self.opener = opener or request.build_opener(request.ProxyHandler({}), NoRedirect())

    def __call__(self, path, body=None, timeout=4):
        if not path.startswith("/api/") or not 0 < timeout <= 30:
            raise ValueError("Invalid API path/timeout")
        data = json.dumps(body, allow_nan=False).encode("utf-8") if body is not None else None
        req = request.Request(self.url + path, data=data, headers={"Content-Type": "application/json"})
        deadline = self.clock() + timeout
        raw = bytearray()
        with self.opener.open(req, timeout=timeout) as response:
            while len(raw) <= MAX_JSON:
                if self.clock() >= deadline: raise TimeoutError("HTTP body deadline exceeded")
                # read1 performs at most one underlying read; a continuously trickling
                # response must not keep read(n) filling its buffer indefinitely.
                chunk = response.read1(min(65536, MAX_JSON + 1 - len(raw)))
                if not chunk: break
                raw.extend(chunk)
        if len(raw) > MAX_JSON:
            raise ValueError("Yamcs response exceeds bounded JSON size")
        value = json.loads(raw, object_pairs_hook=unique_keys)
        if not isinstance(value, dict):
            raise ValueError("Expected Yamcs JSON object")
        return value


def timestamp(value):
    if not isinstance(value, str) or len(value) > 64:
        raise ValueError("Invalid Yamcs timestamp")
    result = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if result.tzinfo is None:
        raise ValueError("Yamcs timestamp requires an offset")
    return result


def unbase64(value):
    if not isinstance(value, str):
        raise ValueError("Missing base64 binary")
    return base64.b64decode(value, validate=True)


def command_details(body):
    """Allowlist, exact byte layout and typed args; uint64 goes to JSON as a string."""
    if not isinstance(body, bytes) or not body or body[0] not in NAMES:
        raise ValueError("Only transfer opcodes 31..35 are permitted")
    op = body[0]
    size = {wire.BEGIN: 5, wire.PENDING: 1, wire.READ: 25, wire.CONFIRM: 24, wire.CANCEL: 23}[op]
    if len(body) != size:
        raise ValueError("Unexpected transfer command length")
    args, transfer, index = {}, None, None
    if op == wire.BEGIN:
        args["RequestId"] = struct.unpack_from("!I", body, 1)[0]
    elif op != wire.PENDING:
        boot, tid, request_id, digest, count = wire.MANIFEST.unpack_from(body, 1)
        transfer = Transfer.from_dict(dict(boot_id=boot, transfer_id=tid, request_id=request_id, digest=digest, record_count=count))
        args = {"BootId": str(boot), "TransferId": tid, "RequestId": request_id, "Digest": digest, "RecordCount": count}
        if op == wire.READ:
            index = struct.unpack_from("!H", body, 23)[0]
            if index >= count:
                raise ValueError("READ index outside snapshot")
            args["RecordIndex"] = index
        if op == wire.CONFIRM:
            if body[23] != 165:
                raise ValueError("Missing explicit CONFIRM key")
            args["ConfirmKey"] = 165
    return op, args, transfer, index


def validate_binary(binary, body, postprocessed):
    if len(binary) != 6 + len(body):
        raise ValueError("Generated command size mismatch")
    packet_id, sequence, length = struct.unpack_from("!3H", binary)
    if packet_id != 0x1060 or sequence & 0xC000 != 0xC000 or binary[6:] != body:
        raise ValueError("Generated command target/flags/body differs from allowlisted command")
    if length != (len(body) - 1 if postprocessed else 0):
        raise ValueError("Unexpected pre/postprocessed CCSDS length")


def retryable(exception):
    if isinstance(exception, error.HTTPError):
        return exception.code in (408, 429, 500, 502, 503, 504)
    return isinstance(exception, (OSError, TimeoutError, error.URLError))


class YamcsTransport:
    def __init__(self, api, *, instance="myproject", processor="realtime", board_ip="192.168.1.5",
                 incoming="udp-in", outgoing="udp-out", allow_send=False, timeout=8, retries=2,
                 clock=time.monotonic, sleep=time.sleep):
        if type(timeout) not in (int, float) or not math.isfinite(timeout) or not 0 < timeout <= 30 or type(retries) is not int or not 0 <= retries <= 3:
            raise ValueError("Timeout must be (0,30] seconds and retries 0..3")
        if type(allow_send) is not bool:
            raise ValueError("allow_send must be explicit boolean")
        for name in (instance, processor, incoming, outgoing):
            if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", name):
                raise ValueError("Invalid instance/processor/link name")
        self.api, self.instance, self.processor = api, instance, processor
        self.board_ip = str(ipaddress.IPv4Address(board_ip))
        self.incoming, self.outgoing, self.allow_send = incoming, outgoing, allow_send
        self.timeout, self.retries, self.clock, self.sleep = timeout, retries, clock, sleep
        self.origin = "fsw-archive-" + secrets.token_hex(8)
        self.sequence = 0

    def _call(self, path, body=None, deadline=None):
        remaining = 4 if deadline is None else deadline - self.clock()
        if remaining <= 0:
            raise TimeoutError("Attempt deadline exceeded")
        return self.api(path, body, timeout=min(4, remaining))

    def _path(self, op):
        name = f"/{self.instance}/ARCHIVE_{NAMES[op]}_TRANSFER"
        return f"/api/processors/{self.instance}/{self.processor}/commands/" + parse.quote(name, safe=""), name

    def _preview(self, body, deadline=None):
        op, args, _, _ = command_details(body)
        path, name = self._path(op)
        result = self._call(path, {"args": args, "dryRun": True}, deadline)
        if result.get("commandName") != name:
            raise ValueError("Yamcs generated another command")
        validate_binary(unbase64(result.get("unprocessedBinary")), body, False)
        return result

    def preview(self, body):
        # CONFIRM preview must not become a way to bypass the saved-file guard.
        if command_details(body)[0] == wire.CONFIRM:
            raise ValueError("CONFIRM generation is available only in the explicit saved-file flow")
        return self._preview(body)

    def _links(self, deadline):
        for name, type_name in ((self.outgoing, "UdpTcDataLink"), (self.incoming, "UdpTmDataLink")):
            link = self._call(f"/api/links/{self.instance}/{name}", deadline=deadline)
            if link.get("disabled") or link.get("status") != "OK" or link.get("type") != "org.yamcs.tctm." + type_name:
                raise TransferError("Yamcs link not ready: " + name)
            if name == self.outgoing:
                target = re.escape("sending to " + self.board_ip + ":10025") + r"(?:$|[\s,;)])"
                if not re.search(target, link.get("detailedStatus", "")):
                    raise TransferError("Yamcs TC link targets a different board/port")

    def _history_sent(self, issued, body, name, deadline):
        try:
            history = self._call(f"/api/archive/{self.instance}/commands/" + parse.quote(issued["id"], safe=""), deadline=deadline)
        except error.HTTPError as exception:
            if exception.code == 404: return False  # Eventual command archive publication.
            raise
        if history.get("id") != issued["id"] or history.get("commandName") != name:
            raise ValueError("Command history identity mismatch")
        attributes = {}
        for item in history.get("attr", []):
            key = item["name"]
            if key in attributes: raise ValueError("Duplicate command history attribute")
            attributes[key] = item["value"]
        status = attributes.get("Acknowledge_Sent_Status", {}).get("stringValue")
        if status in ("NOK", "ERROR", "CANCELLED", "TIMEOUT"):
            raise TransferError("Yamcs rejected transmission: " + status)
        if status != "OK" or "binary" not in attributes:
            return False
        validate_binary(unbase64(attributes["binary"].get("binaryValue")), body, True)
        return True

    def _packet_pages(self, params, deadline):
        params = dict(params)
        seen = set()
        for _ in range(10):
            page = self._call(f"/api/archive/{self.instance}/packets?" + parse.urlencode(params), deadline=deadline)
            for key in ("packets", "packet"):
                if key in page and (not isinstance(page[key], list) or len(page[key]) > 100):
                    raise ValueError("Invalid packet page")
            if page.get("packets") and page.get("packet") and page["packets"] != page["packet"]:
                raise ValueError("Conflicting packet aliases")
            items = page.get("packets") or page.get("packet", [])
            if not isinstance(items, list) or len(items) > 100:
                raise ValueError("Invalid packet page")
            yield items
            token = page.get("continuationToken")
            if token is None or token == "": return
            if not isinstance(token, str) or len(token) > 4096 or token in seen:
                raise ValueError("Invalid/repeated packet continuation token")
            seen.add(token)
            params["next"] = token  # Retain every original filter, not just the token.
        raise TransferError("Packet page bound exceeded; no confirmation will be inferred")

    def _baseline(self, deadline):
        # Generation times have millisecond collisions. Snapshot all already-archived
        # replies at the latest timestamp BEFORE issuing, not just `start=issued time`.
        latest, previous, existing = None, None, set()
        # Query the indexed transfer container, not the entire mission/event
        # archive. Identity/collision checks below still apply to every reply.
        for items in self._packet_pages({"name": f"/{self.instance}/ArchiveTransferContainer",
                                        "link": self.incoming, "order": "desc", "limit": 100}, deadline):
            for item in items:
                if not isinstance(item, dict): raise ValueError("Invalid packet entry")
                if item.get("link") != self.incoming: continue
                generated = timestamp(item.get("generationTime"))
                if previous is not None and generated > previous:
                    raise ValueError("Packet archive is not sorted as requested")
                previous = generated
                if latest is None: latest = generated
                if generated < latest: return latest, existing
                raw = unbase64(item.get("packet"))
                if len(raw) >= 2 and struct.unpack_from("!H", raw)[0] & 0x07FF == 97:
                    existing.add((generated, raw))
        return latest, existing

    def _replies(self, start, existing, deadline):
        lower = timestamp(start)
        for items in self._packet_pages({"name": f"/{self.instance}/ArchiveTransferContainer",
                                        "start": start, "link": self.incoming, "order": "asc", "limit": 100}, deadline):
            for item in items:
                if not isinstance(item, dict): raise ValueError("Invalid packet entry")
                if item.get("link") != self.incoming: continue
                generated = timestamp(item.get("generationTime"))
                if generated < lower: continue
                raw = unbase64(item.get("packet"))
                if (generated, raw) in existing: continue
                if len(raw) >= 2 and struct.unpack_from("!H", raw)[0] & 0x07FF == 97:
                    yield wire.decode_reply(raw)

    @staticmethod
    def _matches(reply, op, args, transfer, index):
        if reply["operation"] != op: return False
        if op == wire.BEGIN: return reply["manifest"]["request_id"] == args["RequestId"]
        if op == wire.PENDING: return True
        return reply["manifest"] == transfer.as_dict() and (op != wire.READ or reply["index"] == index)

    def exchange(self, body, *, saved=None, confirm_clear=False):
        if not self.allow_send:
            raise TransferError("Actual transmission requires --send")
        op, args, transfer, index = command_details(body)
        if op == wire.CONFIRM and wire.confirm_command(saved, confirm_clear=confirm_clear) != body:
            raise ValueError("CONFIRM differs from verified saved receipt")
        last_error = None
        confirmation_submitted = False
        for attempt in range(self.retries + 1):
            deadline = self.clock() + self.timeout
            try:
                self._links(deadline)
                self._preview(body, deadline)
                path, name = self._path(op)
                latest, existing = self._baseline(deadline)
                if op == wire.CONFIRM and wire.confirm_command(saved, confirm_clear=confirm_clear) != body:
                    raise ValueError("Export changed before actual CONFIRM")
                self.sequence += 1
                if op == wire.CONFIRM: confirmation_submitted = True
                issued = self._call(path, {"args": args, "dryRun": False, "origin": self.origin,
                                          "sequenceNumber": self.sequence}, deadline)
                if issued.get("commandName") != name or not isinstance(issued.get("id"), str) or not issued["id"]:
                    raise ValueError("Invalid issued-command identity")
                generated = timestamp(issued.get("generationTime"))
                if latest is not None and generated < latest:
                    raise ValueError("Command generation time precedes the packet baseline; check server clock")
                validate_binary(unbase64(issued.get("unprocessedBinary")), body, False)
                candidate, sent = None, False
                while self.clock() < deadline:
                    if not sent: sent = self._history_sent(issued, body, name, deadline)
                    for reply in self._replies(issued["generationTime"], existing, deadline):
                        if not self._matches(reply, op, args, transfer, index): continue
                        if candidate is not None and any(candidate[key] != reply[key] for key in ("status", "manifest", "index", "record")):
                            raise ValueError("Inconsistent duplicate transfer replies")
                        candidate = reply
                    if sent and candidate is not None:
                        if candidate["status"] not in (0, 4 if op == wire.PENDING else 5 if op == wire.BEGIN else 0):
                            raise BoardRejected(f"Board rejected {NAMES[op]}: {STATUS[candidate['status']]}")
                        return candidate
                    self.sleep(min(0.15, max(0, deadline - self.clock())))
                raise TimeoutError("Missing matching board reply and/or verified Sent history")
            except Exception as exception:
                if not retryable(exception):
                    if confirmation_submitted and not isinstance(exception, BoardRejected):
                        raise OutcomeUnknown("CONFIRM may already have committed; verification failed. Retry only the SAME saved-file confirmation.") from exception
                    raise
                last_error = exception
                # All retries use the exact same request/manifest/index, never a new BEGIN or wipe.
                if attempt == self.retries: break
        hint = "CONFIRM may already have committed; retry only the SAME saved-file confirmation." if op == wire.CONFIRM else "Session/data were not automatically cleared; inspect/resume pending transfer."
        raise OutcomeUnknown(hint) from last_error


def download(transport, output, *, request_id=None, resume_pending=False, confirm_clear=False, progress=print):
    if type(confirm_clear) is not bool: raise ValueError("confirm_clear must be explicit boolean")
    path = Path(output)
    if path.exists() or path.is_symlink() or not path.parent.is_dir():
        raise ValueError("Output must be a new file in an existing directory; nothing will be overwritten")
    if resume_pending and request_id is not None: raise ValueError("Resume cannot replace the active request ID")
    request_id = secrets.randbits(32) if request_id is None else request_id
    command = wire.pending_command() if resume_pending else wire.begin_command(request_id)
    progress("Reading pending session" if resume_pending else f"BEGIN request_id={request_id}")
    reply = transport.exchange(command)
    if reply["status"] == 5: return {"outcome": "empty", "saved": None}
    if reply["status"] == 4: raise TransferError("No pending transfer to resume")
    transfer = Transfer.from_dict(reply["manifest"])
    records = []
    for index in range(transfer.record_count):
        reply = transport.exchange(wire.read_command(transfer, index))
        records.append(reply["record"])
        progress(f"READ {index + 1}/{transfer.record_count} verified")
    saved = save_verified_export(path, transfer.as_dict(), records)
    progress("SAVED: verified/fsync/read-back " + saved.path)
    if confirm_clear:
        reply = transport.exchange(wire.confirm_command(saved, confirm_clear=True), saved=saved, confirm_clear=True)
        return {"outcome": "confirmed", "saved": saved, "reply": reply}
    return {"outcome": "saved-retained", "saved": saved}


def control_export(transport, path, *, confirm_clear=False, cancel=False):
    if type(cancel) is not bool or type(confirm_clear) is not bool:
        raise ValueError("Control intent must be explicit boolean")
    if not cancel and not confirm_clear:
        raise ValueError("Saved-file confirmation requires explicit confirm_clear=True")
    if cancel:
        if confirm_clear: raise ValueError("Cancel never confirms/deletes records")
        transfer = load_verified_export(path)
        reply = transport.exchange(wire.cancel_command(transfer))
        return {"outcome": "cancelled-retained", "reply": reply}
    saved = resync_verified_export(path)
    reply = transport.exchange(wire.confirm_command(saved, confirm_clear=confirm_clear), saved=saved, confirm_clear=confirm_clear)
    return {"outcome": "confirmed", "saved": saved, "reply": reply}


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--instance", default="myproject")
    parser.add_argument("--processor", default="realtime")
    parser.add_argument("--board-ip", type=ipaddress.IPv4Address, default=ipaddress.IPv4Address("192.168.1.5"))
    parser.add_argument("--send", action="store_true", help="Allow actual transfer commands (default: dry-run only)")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--request-id", type=lambda text: int(text, 0))
    parser.add_argument("--resume-pending", action="store_true")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--confirm-export", type=Path, help="Retry/confirm this exact verified saved manifest")
    mode.add_argument("--cancel-export", type=Path, help="Cancel this saved manifest without deleting records")
    parser.add_argument("--confirm-clear", action="store_true", help="Explicitly confirm removal AFTER verified save")
    parser.add_argument("--timeout", type=float, default=8)
    parser.add_argument("--retries", type=int, default=2)
    args = parser.parse_args(argv)
    control = args.confirm_export or args.cancel_export
    if not args.send and (args.output or control or args.confirm_clear or args.resume_pending):
        parser.error("Output/resume/clear/control modes require --send; default only checks MDB generation")
    if args.send and not control and args.output is None: parser.error("Download requires --output NEW_FILE")
    if control and (args.output or args.resume_pending or args.request_id is not None): parser.error("Saved-file control cannot start another transfer")
    if args.confirm_export and not args.confirm_clear: parser.error("--confirm-export requires explicit --confirm-clear")
    if args.cancel_export and args.confirm_clear: parser.error("Cancel does not permit --confirm-clear")
    if args.resume_pending and args.request_id is not None: parser.error("Resume uses the current pending request ID")
    if args.request_id is not None: wire.begin_command(args.request_id)
    return args


def main(argv=None):
    args = arguments(argv)
    transport = YamcsTransport(JsonHttp(args.url), instance=args.instance, processor=args.processor,
                               board_ip=args.board_ip, allow_send=args.send, timeout=args.timeout, retries=args.retries)
    if not args.send:
        transport.preview(wire.begin_command(0 if args.request_id is None else args.request_id))
        transport.preview(wire.pending_command())
        print("PASS: Yamcs dry-run generation only. No board command or file write.")
        return
    if args.confirm_export or args.cancel_export:
        result = control_export(transport, args.confirm_export or args.cancel_export,
                                confirm_clear=args.confirm_clear, cancel=bool(args.cancel_export))
    else:
        result = download(transport, args.output, request_id=args.request_id,
                          resume_pending=args.resume_pending, confirm_clear=args.confirm_clear)
    print("RESULT: " + result["outcome"])
    if result["outcome"] == "saved-retained":
        print("Records/session retained. Use --confirm-export FILE --send --confirm-clear later, or --cancel-export FILE --send.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, TypeError) as exception:
        raise SystemExit("FAIL: " + str(exception)) from exception
