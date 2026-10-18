"""Read-only HK/event comparison against archived packets and the current MDB.

Only HTTP GET: no commands, link controls, serial, reset or firmware upload.
Consecutive runs of one/two events remain 1:1 in pinned Yamcs 5.12. Longer
runs produce summaries and are checked separately by the native Java fixture.
"""
import argparse
from collections import Counter
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path
import struct
import time
from urllib.parse import quote, urlencode

from yamcs_archive_transfer import JsonHttp, timestamp, unbase64

FIELDS = ("HK_UptimeMs", "HK_BatteryVoltage", "HK_BatterySOC", "HK_BatteryTempC",
          "HK_GyroX", "HK_GyroY", "HK_GyroZ")
UNITS = ("ms", "V", "%", "degC", "rad/s", "rad/s", "rad/s")


def iso(value):
    return value.isoformat(timespec="milliseconds").replace("+00:00", "Z")


def packet_body(raw, apid, size):
    if len(raw) != size:
        raise ValueError("Unexpected telemetry length")
    packet_id, sequence, length = struct.unpack_from("!3H", raw)
    if packet_id != apid or sequence & 0xC000 != 0xC000 or length != size - 7:
        raise ValueError("Wrong telemetry primary profile/APID/length")
    return raw[6:]


def check_hk(raw, extracted):
    expected = struct.unpack("!I6f", packet_body(raw, 5, 34))
    rows = {}
    for row in extracted.get("parameterValues", []):
        name = row.get("parameter", {}).get("name")
        if name in FIELDS:
            if name in rows:
                raise ValueError("Duplicate HK extraction")
            rows[name] = row
    for i, name in enumerate(FIELDS):
        row = rows.get(name, {})
        if row.get("location") != 48 + 32 * i or row.get("size") != 32:
            raise ValueError("Wrong HK bit position/size: " + name)
        for key in ("rawValue", "engValue"):
            value = row.get(key, {})
            kind, slot = ("UINT32", "uint32Value") if i == 0 else ("FLOAT", "floatValue")
            number = value.get(slot)
            if value.get("type") != kind or type(number) not in (int, float):
                raise ValueError("Wrong HK numeric type: " + name)
            if i == 0:
                if type(number) is not int or number != expected[i]:
                    raise ValueError("Wrong HK uptime")
            elif (not math.isfinite(number) or not math.isfinite(expected[i]) or
                  struct.pack("!f", number) != struct.pack("!f", expected[i])):
                raise ValueError("HK float32 mismatch: " + name)
    return dict(zip(FIELDS, expected))


def event_text(raw):
    # Match Java String.replace(NUL, empty).trim() for valid UTF-8 firmware text.
    text = packet_body(raw, 2000, 70).decode("utf-8").replace("\0", "").strip("".join(map(chr, range(33))))
    severity = "ERROR" if text.startswith(("ERROR", "CRITICAL")) else "WARNING" if text.startswith("WARN") else "INFO"
    return text, severity


def correlate_events(packets, events, start, stop):
    expected = []
    previous, run_length, repeated_pairs = None, 0, 0
    seen_wire, duplicate_wire = set(), 0
    for packet in packets:
        when = timestamp(packet["generationTime"])
        if not start <= when < stop:
            raise ValueError("Packet outside requested event window")
        raw = unbase64(packet["packet"])
        if raw in seen_wire:
            duplicate_wire += 1
        seen_wire.add(raw)
        key = event_text(raw)
        if not key[0]:
            continue
        run_length = run_length + 1 if key == previous else 1
        if run_length > 2:
            raise ValueError("Long repeat run: use native reduction check, not a 1:1 claim")
        if run_length == 2:
            repeated_pairs += 1
        previous = key
        expected.append((when, key))
    if not expected:
        raise ValueError("No live firmware events in window")
    available, identities = [], set()
    for event in events:
        if event.get("source") != "MyPacketPreprocessor" or event.get("type") != "FSW_EVENT":
            raise ValueError("Unexpected event source/type")
        when = timestamp(event["generationTime"])
        identity = (event["source"], when, event["seqNumber"])
        if identity in identities:
            raise ValueError("Duplicate event archive identity")
        identities.add(identity)
        available.append((when, (event["message"], event["severity"])))
    used = set()
    for when, key in expected:
        matches = [i for i, (actual_time, actual_key) in enumerate(available)
                   if i not in used and key == actual_key and abs((when - actual_time).total_seconds()) <= .25]
        if not matches:
            raise ValueError("Missing matching firmware event")
        # Two identical adjacent messages may share a millisecond. Match the
        # nearest unused event; archive identity and extra-event checks still
        # reject double counting. Native test confirms one repeat is unreduced.
        used.add(min(matches, key=lambda i: (abs((when - available[i][0]).total_seconds()), i)))
    for i, (when, _) in enumerate(available):
        if start + timedelta(seconds=.25) <= when < stop - timedelta(seconds=.25) and i not in used:
            raise ValueError("Unmatched extra event inside live window")
    return {"matched": len(expected), "consecutive_pairs": repeated_pairs,
            "duplicate_wire_packets": duplicate_wire,
            "severity_counts": dict(Counter(key[1] for _, key in expected)),
            "message_sha256": hashlib.sha256(json.dumps([key for _, key in expected], ensure_ascii=True).encode()).hexdigest()}


class ReadOnlyCheck:
    def __init__(self, api, clock=time.monotonic):
        self.api, self.clock, self.deadline = api, clock, clock() + 40

    def get(self, path):
        remaining = self.deadline - self.clock()
        if remaining <= 0:
            raise TimeoutError("Read-only telemetry check deadline exceeded")
        return self.api(path, timeout=min(4, remaining))  # No body means GET.

    def pages(self, kind, params, max_items=None):
        params, seen, result = dict(params), set(), []
        singular = {"packets": "packet", "events": "event"}[kind]
        for _ in range(10):
            page = self.get("/api/archive/myproject/" + kind + "?" + urlencode(params))
            for name in (kind, singular):
                if name in page and (not isinstance(page[name], list) or len(page[name]) > 100):
                    raise ValueError("Invalid archive page")
            if kind in page and singular in page and page[kind] != page[singular]:
                raise ValueError("Conflicting archive aliases")
            result.extend(page.get(kind, page.get(singular, [])))  # Never concatenate aliases.
            if max_items is not None and len(result) >= max_items:
                return result[:max_items]
            token = page.get("continuationToken")
            if token is None or token == "":
                return result
            if not isinstance(token, str) or len(token) > 4096 or token in seen:
                raise ValueError("Invalid/repeated continuation token")
            seen.add(token)
            params["next"] = token
        raise ValueError("Archive page bound exceeded")

    def extract(self, packet):
        if packet.get("link") != "udp-in":
            raise ValueError("Unexpected telemetry link")
        return self.get("/api/archive/myproject/packets/" + quote(packet["id"]["name"], safe="") + "/" +
                        quote(packet["generationTime"], safe="") + "/" + str(packet["sequenceNumber"]) + ":extract")

    def run(self, now=None):
        links_before = self.get("/api/links/myproject")
        for name, unit in zip(FIELDS, UNITS):
            meta = self.get("/api/mdb/myproject/parameters/myproject/" + name)
            if [item.get("unit") for item in meta.get("type", {}).get("unitSet", [])] != [unit]:
                raise ValueError("Missing/wrong live HK unit: " + name)
        samples = self.pages("packets", {"name": "/myproject/MasterHKContainer", "link": "udp-in", "limit": 3, "order": "desc"}, max_items=3)
        # Anchor after fetching: a packet can arrive during the metadata GETs.
        now = now or datetime.now(timezone.utc)
        if len(samples) != 3:
            raise ValueError("Need three archived HK samples")
        checked = []
        identities = set()
        for packet in samples:
            identity = (packet["generationTime"], packet["sequenceNumber"])
            if identity in identities:
                raise ValueError("Duplicate HK sample")
            identities.add(identity)
            age = (now - timestamp(packet["generationTime"])).total_seconds()
            if not 0 <= age <= 25:
                raise ValueError("HK sample is stale/future")
            checked.append({"generationTime": packet["generationTime"], "sequenceNumber": packet["sequenceNumber"],
                            "values": check_hk(unbase64(packet["packet"]), self.extract(packet))})
        for newer, older in zip(checked, checked[1:]):
            delta = (newer["values"]["HK_UptimeMs"] - older["values"]["HK_UptimeMs"]) & 0xFFFFFFFF
            if timestamp(newer["generationTime"]) <= timestamp(older["generationTime"]) or not 0 < delta <= 25000:
                raise ValueError("HK samples not time-ordered/continuous")
        stop = now - timedelta(seconds=3)
        start = stop - timedelta(seconds=20)
        packets = self.pages("packets", {"name": "/myproject/EventPacket", "link": "udp-in", "start": iso(start),
                                         "stop": iso(stop), "limit": 100, "order": "asc"})
        times = [timestamp(p["generationTime"]) for p in packets]
        if times != sorted(times):
            raise ValueError("Event packets not sorted")
        events = self.pages("events", {"source": "MyPacketPreprocessor", "start": iso(start - timedelta(seconds=1)),
                                       "stop": iso(stop + timedelta(seconds=1)), "limit": 100, "order": "asc"})
        event_result = correlate_events(packets, events, start, stop)
        # Compare XTCE text with the raw body on a bounded subset as well.
        for packet in packets[:3]:
            rows = [r for r in self.extract(packet).get("parameterValues", []) if r.get("parameter", {}).get("name") == "EventMessage"]
            if len(rows) != 1 or rows[0].get("location") != 48 or rows[0].get("size") != 512:
                raise ValueError("Wrong event XTCE offset/size")
            actual = rows[0].get("engValue", {}).get("stringValue", "").replace("\0", "").strip()
            if actual != event_text(unbase64(packet["packet"]))[0]:
                raise ValueError("Wrong event XTCE text")
        links_after = self.get("/api/links/myproject")
        def sent(links):
            matches = [r.get("dataOutCount") for r in links.get("links", []) if r.get("name") == "udp-out"]
            if len(matches) != 1:
                raise ValueError("Missing TC link counter")
            counter = matches[0]
            if type(counter) is int and counter >= 0:
                return counter
            if isinstance(counter, str) and counter.isascii() and counter.isdecimal() and len(counter) <= 20:
                return int(counter)
            raise ValueError("Invalid TC link counter")
        before, after = sent(links_before), sent(links_after)
        if before != after:
            raise ValueError("Concurrent TC activity during read-only check")
        return {"result": "PASS", "checked_at": iso(now), "hk_samples": checked, "units": dict(zip(FIELDS, UNITS)),
                "event_window": {"start": iso(start), "stop": iso(stop), **event_result},
                "tc_data_out_before": before, "tc_data_out_after": after,
                "limits": "Valid live runs of at most two equal events; native fixture covers longer repeat reduction/severity; no exactly-once or physical sensor claim"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--output", type=Path, help="Optional new report file (exclusive creation)")
    args = parser.parse_args()
    report = ReadOnlyCheck(JsonHttp(args.url)).run()
    if args.output:
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2, allow_nan=False)
            stream.write("\n")
    print(json.dumps(report, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
