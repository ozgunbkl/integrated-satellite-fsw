import base64
from datetime import datetime, timedelta, timezone
from pathlib import Path
import struct
import sys
import unittest
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from check_yamcs_telemetry import (FIELDS, UNITS, ReadOnlyCheck, check_hk,
                                  correlate_events, event_text, iso, packet_body)


def hk(uptime=10000):
    return struct.pack("!3HI6f", 5, 0xC001, 27, uptime, 8.5, 75, 0, 1, -2, .5)


def extracted(uptime=10000):
    return {"parameterValues": [dict(parameter={"name": name}, location=48 + i * 32, size=32,
        rawValue={"type": "UINT32" if i == 0 else "FLOAT", "uint32Value" if i == 0 else "floatValue": value},
        engValue={"type": "UINT32" if i == 0 else "FLOAT", "uint32Value" if i == 0 else "floatValue": value})
        for i, (name, value) in enumerate(zip(FIELDS, (uptime, 8.5, 75, 0, 1, -2, .5)))]}


def event_packet(message="INFO: test", sequence=1):
    return struct.pack("!3H", 2000, 0xC000 | sequence, 63) + message.encode().ljust(64, b"\0")


class FakeApi:
    def __init__(self):
        self.now = datetime(2026, 10, 16, tzinfo=timezone.utc)
        self.unit_override = None
        self.links_calls = 0
        self.counter_change = False
        self.calls = []
        self.samples = [self.packet(hk(10000 - 5000 * i), self.now - timedelta(seconds=1 + 5 * i), i,
                                    "MasterHKContainer") for i in range(3)]
        when = self.now - timedelta(seconds=10)
        self.event_packet = self.packet(event_packet(), when, 5, "EventPacket")
        self.event = dict(generationTime=iso(when), message="INFO: test", severity="INFO",
                          source="MyPacketPreprocessor", type="FSW_EVENT", seqNumber=1)

    @staticmethod
    def packet(raw, when, sequence, name):
        return dict(packet=base64.b64encode(raw).decode(), generationTime=iso(when), sequenceNumber=sequence,
                    link="udp-in", id={"name": "/myproject/" + name})

    def __call__(self, path, *, timeout):
        # Deliberately no body parameter: any accidental POST attempt fails.
        self.calls.append(path)
        if path == "/api/links/myproject":
            self.links_calls += 1
            return {"links": [{"name": "udp-out", "dataOutCount": str(int(self.counter_change and self.links_calls > 1))}]}
        if "/api/mdb/" in path:
            name = path.rsplit("/", 1)[1]
            return {"type": {"unitSet": [{"unit": self.unit_override or UNITS[FIELDS.index(name)]}]}}
        if "/packets?" in path:
            query = parse_qs(urlsplit(path).query)
            return {"packets": self.samples if "MasterHKContainer" in query["name"][0] else [self.event_packet]}
        if "/events?" in path:
            return {"events": [self.event]}
        if path.endswith(":extract"):
            sequence = int(path.rsplit("/", 1)[1].split(":")[0])
            if sequence == 5:
                return {"parameterValues": [dict(parameter={"name": "EventMessage"}, location=48, size=512,
                    engValue={"type": "STRING", "stringValue": "INFO: test\0"})]}
            return extracted(10000 - 5000 * sequence)
        raise AssertionError("Unexpected endpoint")


class TelemetryChecks(unittest.TestCase):
    def test_hk_all_fields(self):
        self.assertEqual(check_hk(hk(), extracted())["HK_GyroY"], -2)

    def test_hk_uint32_max(self):
        self.assertEqual(check_hk(hk(0xFFFFFFFF), extracted(0xFFFFFFFF))["HK_UptimeMs"], 0xFFFFFFFF)

    def test_wrong_offset_size_value_type_and_duplicate(self):
        for field, value in (("location", 49), ("size", 16), ("engValue", {"type": "FLOAT", "floatValue": 0})):
            bad = extracted(); bad["parameterValues"][1][field] = value
            with self.assertRaises(ValueError): check_hk(hk(), bad)
        bad = extracted(); bad["parameterValues"].append(bad["parameterValues"][1])
        with self.assertRaises(ValueError): check_hk(hk(), bad)

    def test_missing_and_boolean_values_rejected(self):
        bad = extracted(); bad["parameterValues"].pop()
        with self.assertRaises(ValueError): check_hk(hk(), bad)
        bad = extracted(); bad["parameterValues"][1]["rawValue"]["floatValue"] = True
        with self.assertRaises(ValueError): check_hk(hk(), bad)

    def test_wrong_primary_profiles(self):
        for offset, mask in ((0, 0x20), (0, 0x10), (0, 8), (1, 1), (2, 0x40), (5, 1)):
            bad = bytearray(hk()); bad[offset] ^= mask
            with self.assertRaises(ValueError): packet_body(bad, 5, 34)
        with self.assertRaises(ValueError): check_hk(hk()[:-1], extracted())

    def test_nonfinite_rejected(self):
        bad = bytearray(hk()); bad[10:14] = struct.pack("!f", float("nan"))
        with self.assertRaises(ValueError): check_hk(bad, extracted())

    def test_event_prefix_severities_and_nul(self):
        for message, severity in (("INFO: test", "INFO"), ("WARN: test", "WARNING"),
                ("WARNING: test", "WARNING"), ("ERROR: test", "ERROR"), ("CRITICAL: test", "ERROR"),
                ("\0  INFO: te\0st  ", "INFO")):
            self.assertEqual(event_text(event_packet(message))[1], severity)
        self.assertEqual(event_text(event_packet("\0  INFO: te\0st  "))[0], "INFO: test")

    def test_event_invalid_utf8(self):
        bad = bytearray(event_packet()); bad[6] = 255
        with self.assertRaises(UnicodeDecodeError): event_text(bad)

    def fixture(self):
        start = datetime(2026, 10, 16, tzinfo=timezone.utc)
        when = start + timedelta(seconds=1)
        packet = {"generationTime": iso(when), "packet": base64.b64encode(event_packet()).decode()}
        event = dict(generationTime=iso(when - timedelta(milliseconds=1)), message="INFO: test",
                     severity="INFO", source="MyPacketPreprocessor", type="FSW_EVENT", seqNumber=1)
        return [packet], [event], start, start + timedelta(seconds=20)

    def test_event_matching(self):
        self.assertEqual(correlate_events(*self.fixture())["matched"], 1)

    def test_event_missing_extra_and_duplicate_identity(self):
        packets, events, start, stop = self.fixture()
        with self.assertRaises(ValueError): correlate_events(packets, [], start, stop)
        with self.assertRaises(ValueError): correlate_events(packets, events * 2, start, stop)
        extra = dict(events[0], message="INFO: extra", seqNumber=2)
        with self.assertRaises(ValueError): correlate_events(packets, events + [extra], start, stop)

    def test_wrong_severity_source_and_time(self):
        packets, events, start, stop = self.fixture()
        for change in (dict(severity="WARNING"), dict(source="other"), dict(generationTime=iso(stop))):
            with self.assertRaises(ValueError): correlate_events(packets, [dict(events[0], **change)], start, stop)

    def test_long_live_reduction_is_not_claimed_one_to_one(self):
        packets, events, start, stop = self.fixture()
        with self.assertRaisesRegex(ValueError, "Long repeat"):
            correlate_events(packets * 3, events, start, stop)

    def test_one_additional_repeat_still_has_two_events(self):
        packets, events, start, stop = self.fixture()
        result = correlate_events(packets * 2, events + [dict(events[0], seqNumber=2)], start, stop)
        self.assertEqual(result["matched"], 2)
        self.assertEqual(result["consecutive_pairs"], 1)

    def test_api_aliases_not_double_counted(self):
        check = ReadOnlyCheck(lambda path, **kwargs: {"packets": [{"id": 1}], "packet": [{"id": 1}]})
        self.assertEqual(len(check.pages("packets", {})), 1)

    def test_conflicting_aliases_and_malformed_pages(self):
        for page in ({"packets": [], "packet": [1]}, {"events": {}}, {"events": list(range(101))}):
            check = ReadOnlyCheck(lambda path, **kwargs: page)
            with self.assertRaises(ValueError): check.pages("events" if "events" in page else "packets", {})

    def test_pagination_preserves_filters_and_get_only(self):
        calls = []
        def api(path, **kwargs):
            self.assertNotIn("body", kwargs)
            calls.append(parse_qs(urlsplit(path).query))
            return {"events": [1], **({"continuationToken": "next"} if len(calls) == 1 else {})}
        self.assertEqual(ReadOnlyCheck(api).pages("events", {"source": "test", "start": "now"}), [1, 1])
        self.assertEqual(calls[1], dict(source=["test"], start=["now"], next=["next"]))

    def test_pagination_token_loop_and_page_bound(self):
        with self.assertRaises(ValueError):
            ReadOnlyCheck(lambda path, **kwargs: {"events": [], "continuationToken": "same"}).pages("events", {})
        calls = []
        def api(path, **kwargs):
            calls.append(path); return {"events": [], "continuationToken": str(len(calls))}
        with self.assertRaises(ValueError): ReadOnlyCheck(api).pages("events", {})
        self.assertEqual(len(calls), 10)

    def test_latest_limit_does_not_walk_entire_archive(self):
        calls = []
        def api(path, **kwargs):
            calls.append(path); return {"packets": [1, 2, 3], "continuationToken": "unneeded"}
        self.assertEqual(ReadOnlyCheck(api).pages("packets", {"limit": 3}, max_items=3), [1, 2, 3])
        self.assertEqual(len(calls), 1)

    def test_deadline(self):
        ticks = iter((0, 41))
        with self.assertRaises(TimeoutError):
            ReadOnlyCheck(lambda *_: self.fail("HTTP called past deadline"), clock=lambda: next(ticks)).get("/api/test")

    def test_complete_get_only_run(self):
        api = FakeApi()
        report = ReadOnlyCheck(api).run(api.now)
        self.assertEqual(report["result"], "PASS")
        self.assertEqual(len(report["hk_samples"]), 3)
        self.assertEqual(report["event_window"]["matched"], 1)
        self.assertEqual(report["tc_data_out_before"], report["tc_data_out_after"])
        self.assertFalse(any("commands" in path or ":restart" in path for path in api.calls))

    def test_live_unit_mismatch(self):
        api = FakeApi(); api.unit_override = "wrong"
        with self.assertRaisesRegex(ValueError, "HK unit"): ReadOnlyCheck(api).run(api.now)

    def test_live_counter_change(self):
        api = FakeApi(); api.counter_change = True
        with self.assertRaisesRegex(ValueError, "Concurrent TC"): ReadOnlyCheck(api).run(api.now)

    def test_stale_future_duplicate_and_out_of_order_hk(self):
        for mutation in ("stale", "future", "duplicate", "order"):
            api = FakeApi()
            if mutation == "stale": api.samples[0]["generationTime"] = iso(api.now - timedelta(seconds=60))
            if mutation == "future": api.samples[0]["generationTime"] = iso(api.now + timedelta(seconds=1))
            if mutation == "duplicate": api.samples[1] = api.samples[0]
            if mutation == "order": api.samples.reverse()
            with self.assertRaises(ValueError): ReadOnlyCheck(api).run(api.now)


if __name__ == "__main__":
    unittest.main()
