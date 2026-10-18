"""Check the Yamcs read-only archive command. Actual transmission requires --send."""
import argparse
import base64
import ipaddress
import json
import struct
import time
from urllib import error, parse, request

import serial

from archive_downlink_check import archive_index, decode_archive_packet


def validate_command_binary(binary, index, *, postprocessed):
    """Only APID 0x060 + fixed GET 0x30 + the requested BE index is permitted."""
    if len(binary) != 9:
        raise ValueError("Archive GET command must be exactly 9 bytes")
    packet_id, sequence, length = struct.unpack("!HHH", binary[:6])
    if packet_id != 0x1060 or sequence & 0xC000 != 0xC000:
        raise ValueError("Unexpected command target/type/secondary-header/sequence flags")
    if binary[6:] != struct.pack("!BH", 0x30, index):
        raise ValueError("Command is not GET_BY_INDEX for the requested index")
    # XTCE has a zero placeholder; the link's postprocessor fills the real length.
    if length != (2 if postprocessed else 0):
        raise ValueError("Unexpected CCSDS length before/after link processing")
    return sequence & 0x3FFF


def output_count(link):
    # Protobuf JSON encodes uint64 as a decimal string, not always a JSON number.
    try:
        count = int(link.get("dataOutCount", 0))
    except (ValueError, TypeError) as exception:
        raise ValueError("Invalid Yamcs command link counter") from exception
    if count < 0:
        raise ValueError("Negative Yamcs command link counter")
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--instance", default="myproject")
    parser.add_argument("--processor", default="realtime")
    parser.add_argument("--board-ip", type=ipaddress.IPv4Address, default="192.168.1.5")
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--index", type=archive_index, default=0)
    parser.add_argument("--send", action="store_true",
                        help="Actually issue two read commands; no reset, wipe or mode change")
    args = parser.parse_args()
    instance = parse.quote(args.instance, safe="")
    processor = parse.quote(args.processor, safe="")
    command_name = f"/{args.instance}/ARCHIVE_GET_BY_INDEX"
    command_path = (f"/api/processors/{instance}/{processor}/commands/" +
                    parse.quote(command_name, safe=""))

    def api(path, body=None):
        data = json.dumps(body).encode("utf-8") if body is not None else None
        req = request.Request(args.url.rstrip("/") + path, data=data,
                              headers={"Content-Type": "application/json"})
        with request.urlopen(req, timeout=4) as response:
            return json.load(response)

    def outgoing_link():
        link = api(f"/api/links/{instance}/udp-out")
        expected_target = f"sending to {args.board_ip}:10025"
        if (link.get("disabled") or link.get("status") != "OK" or
                link.get("type") != "org.yamcs.tctm.UdpTcDataLink" or
                expected_target not in link.get("detailedStatus", "")):
            raise RuntimeError("Yamcs UDP command link is not ready for the expected board")
        return link

    before_count = output_count(outgoing_link())
    # All these calls generate bytes only; absent indices are never sent to the board.
    for index in sorted({args.index, 0, 0x1234, 65535}):
        dry = api(command_path, {"args": {"RecordIndex": index}, "dryRun": True})
        if dry.get("commandName") != command_name:
            raise RuntimeError("Yamcs generated a different command")
        raw = base64.b64decode(dry["unprocessedBinary"], validate=True)
        validate_command_binary(raw, index, postprocessed=False)
        print(f"DRY RUN: index={index}, pre-link bytes={raw.hex()} OK", flush=True)
    if output_count(outgoing_link()) != before_count:
        raise RuntimeError("The command link counter changed during the dry-run checks")
    if not args.send:
        print("PASS: Yamcs command generation only; nothing sent to the board.", flush=True)
        return

    with serial.Serial() as port:
        port.port = args.port
        port.baudrate = 115200
        port.timeout = 0.03
        port.dtr = False
        port.rts = False
        port.open()
        records = []

        for command_sequence in (1, 2):
            port.reset_input_buffer()
            result = api(command_path, {"args": {"RecordIndex": args.index},
                                       "origin": "fsw-archive-read-check",
                                       "sequenceNumber": command_sequence})
            print("ISSUED: " + result["id"], flush=True)
            history_path = (f"/api/archive/{instance}/commands/" +
                            parse.quote(result["id"], safe=""))
            deadline = time.monotonic() + 12
            sent_verified = False
            read_seen = False
            crc_seen = False
            reply = None
            while time.monotonic() < deadline:
                for _ in range(100):
                    if not port.in_waiting:
                        break
                    line = port.readline().decode("utf-8", errors="replace").strip()
                    if any(marker in line for marker in
                           ("Guru Meditation", "Backtrace:", "abort()", "Initialized in INITIAL_BOOT")):
                        raise RuntimeError("Unexpected board failure/reset: " + line)
                    if line.startswith(("TC PROC:", "CDHS: Routing", "ARCHIVE:", "ARCHIVE ERROR:")):
                        print(line, flush=True)
                    if "TC PROC: [CRC OK] Packet Length: 19 bytes." in line:
                        crc_seen = True
                    if f"ARCHIVE: Read success at index {args.index}." in line:
                        read_seen = True
                    if line.startswith("ARCHIVE ERROR:"):
                        raise RuntimeError("Board archive request failed: " + line)

                if not sent_verified:
                    history = api(history_path)
                    attributes = {item["name"]: item["value"] for item in history.get("attr", [])}
                    status = attributes.get("Acknowledge_Sent_Status", {}).get("stringValue")
                    if status in ("NOK", "ERROR", "CANCELLED"):
                        raise RuntimeError("Yamcs did not transmit the command: " + status)
                    if status == "OK" and "binary" in attributes:
                        binary = base64.b64decode(attributes["binary"]["binaryValue"], validate=True)
                        sequence = validate_command_binary(binary, args.index, postprocessed=True)
                        print(f"SENT VERIFIED: bytes={binary.hex()}, TC sequence={sequence}", flush=True)
                        sent_verified = True

                if reply is None:
                    query = parse.urlencode({"start": result["generationTime"], "link": "udp-in",
                                             "order": "asc", "limit": 100})
                    page = api(f"/api/archive/{instance}/packets?{query}")
                    for packet in page.get("packets", page.get("packet", [])):
                        raw = base64.b64decode(packet["packet"], validate=True)
                        if len(raw) < 2 or struct.unpack("!H", raw[:2])[0] & 0x07FF != 0x060:
                            continue
                        reply = decode_archive_packet(raw)
                        if reply["index"] != args.index:
                            raise RuntimeError("A different archive index was received")
                        print(f"RECEIVED VIA YAMCS: index={reply['index']}, id=0x{reply['record_id']:04X}, "
                              f"length={reply['length']}, CRC16=0x{reply['crc']:04X} OK, "
                              f"TM sequence={reply['sequence']}", flush=True)
                        break
                if sent_verified and crc_seen and read_seen and reply is not None:
                    records.append(reply)
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError(f"Missing proof: sent={sent_verified}, board CRC={crc_seen}, "
                                   f"archive read={read_seen}, Yamcs reply={reply is not None}")

    first, second = records
    for field in ("index", "record_id", "length", "crc", "payload"):
        if first[field] != second[field]:
            raise RuntimeError("Repeated read changed the record: " + field)
    if second["sequence"] != ((first["sequence"] + 1) & 0x3FFF):
        raise RuntimeError("Archive TM sequence did not advance by one")
    after_count = output_count(outgoing_link())
    if after_count - before_count < 2:
        raise RuntimeError("Yamcs link did not count both outgoing commands")
    print(f"PASS: Yamcs -> ESP32 -> Yamcs, two valid read commands and identical CRC-checked "
          f"records; udp-out counter {before_count} -> {after_count}. No reset/WIPE/ACK sent.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError) as exception:
        raise SystemExit("FAIL: " + str(exception)) from exception
