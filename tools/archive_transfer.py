"""Offline transfer verification/export. No sockets, command issuing, reset or wipe.

Only a verified, exclusively created, fsync'ed and re-read export yields a receipt.
The receipt is input for a future ground transport, NOT proof of a board ACK.
"""
import binascii
from dataclasses import dataclass
import json
import os
from pathlib import Path
import struct

FORMAT = "fsw-archive-transfer-v1"
CAPACITY = 30
MAX_EXPORT_BYTES = 32768


def unsigned(value, bits, name):
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError(f"Invalid {name}: expected uint{bits}")
    return value


@dataclass(frozen=True)
class Transfer:
    boot_id: int
    transfer_id: int
    request_id: int
    digest: int
    record_count: int

    @classmethod
    def from_dict(cls, value):
        if not isinstance(value, dict) or set(value) != set(cls.__dataclass_fields__):
            raise ValueError("Invalid transfer manifest fields")
        fields = {name: unsigned(value[name], bits, name) for name, bits in
                  (("boot_id", 64), ("transfer_id", 32), ("request_id", 32), ("digest", 32), ("record_count", 16))}
        if not fields["boot_id"] or not fields["transfer_id"] or not 1 <= fields["record_count"] <= CAPACITY:
            raise ValueError("Invalid boot/transfer ID or record count")
        return cls(**fields)

    def as_dict(self):
        return {name: getattr(self, name) for name in self.__dataclass_fields__}


@dataclass(frozen=True)
class SavedTransfer:
    transfer: Transfer
    path: str


def normalized_records(records):
    if not isinstance(records, list) or not 1 <= len(records) <= CAPACITY:
        raise ValueError("Invalid record collection")
    normalized = []
    seen = set()
    for value in records:
        if not isinstance(value, dict) or set(value) != {"index", "record_id", "length", "crc", "payload"}:
            raise ValueError("Invalid record fields")
        record = {name: unsigned(value[name], 16, name) for name in ("index", "record_id", "length", "crc")}
        if record["index"] in seen or record["index"] >= len(records):
            raise ValueError("Missing, duplicated or out-of-range record index")
        seen.add(record["index"])
        encoded = value["payload"]
        if not isinstance(encoded, str) or len(encoded) != 2 * record["length"] or record["length"] > 128:
            raise ValueError("Invalid payload length/hex")
        try:
            payload = bytes.fromhex(encoded)
        except ValueError as exception:
            raise ValueError("Invalid payload hex") from exception
        if len(payload) != record["length"] or binascii.crc_hqx(payload, 0xFFFF) != record["crc"]:
            raise ValueError("Record payload CRC16 mismatch")
        record["payload"] = payload.hex()
        normalized.append(record)
    return sorted(normalized, key=lambda record: record["index"])


def digest_records(records):
    """CRC-32/ISO-HDLC over canonical BE metadata and verified payloads."""
    records = normalized_records(records)
    digest = binascii.crc32(struct.pack("!H", len(records)))
    for record in records:
        digest = binascii.crc32(struct.pack("!4H", record["index"], record["record_id"],
                                           record["length"], record["crc"]), digest)
        digest = binascii.crc32(bytes.fromhex(record["payload"]), digest)
    return digest


def verified_document(manifest, records):
    transfer = Transfer.from_dict(manifest)
    records = normalized_records(records)
    if len(records) != transfer.record_count or digest_records(records) != transfer.digest:
        raise ValueError("Incomplete or mismatched transfer digest")
    return {"format": FORMAT, "manifest": transfer.as_dict(), "records": records}


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON field")
        result[key] = value
    return result


def verified_export_bytes(raw):
    if len(raw) > MAX_EXPORT_BYTES:
        raise ValueError("Export exceeds bounded format size")
    document = json.loads(raw, object_pairs_hook=unique_keys)
    if not isinstance(document, dict) or set(document) != {"format", "manifest", "records"} or document["format"] != FORMAT:
        raise ValueError("Invalid export format")
    canonical = verified_document(document["manifest"], document["records"])
    return Transfer.from_dict(canonical["manifest"])


def load_verified_export(path):
    """Offline revalidation, not by itself permission to send a destructive command."""
    with Path(path).open("rb") as stream:
        return verified_export_bytes(stream.read(MAX_EXPORT_BYTES + 1))


def resync_verified_export(path):
    """Explicitly revalidate/sync an existing export for a later identical confirmation.

    Does not rewrite bytes or infer board state. A lost board receipt may mean the
    prefix was already removed; only the same manifest may be retried.
    """
    with Path(path).open("r+b") as stream:
        raw = stream.read(MAX_EXPORT_BYTES + 1)
        transfer = verified_export_bytes(raw)
        stream.flush()
        os.fsync(stream.fileno())
        stream.seek(0)
        if stream.read(MAX_EXPORT_BYTES + 1) != raw:
            raise ValueError("Export changed during re-sync")
    if load_verified_export(path) != transfer:
        raise ValueError("Export identity changed after re-sync")
    return SavedTransfer(transfer, str(Path(path).resolve(strict=True)))


def save_verified_export(path, manifest, records):
    """No overwrite. Failure at any step returns no confirmation receipt.

    A partial file can remain after I/O failure and must never be used as an ACK.
    fsync is an OS request, not a guarantee against storage-device/power failure.
    """
    document = verified_document(manifest, records)
    encoded = (json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
    if len(encoded) > MAX_EXPORT_BYTES:
        raise ValueError("Export exceeds bounded format size")
    with Path(path).open("xb") as stream:
        if stream.write(encoded) != len(encoded):
            raise OSError("Short export write")
        stream.flush()
        os.fsync(stream.fileno())
    receipt = load_verified_export(path)
    if receipt != Transfer.from_dict(document["manifest"]):
        raise ValueError("Export read-back identity mismatch")
    return SavedTransfer(receipt, str(Path(path).resolve(strict=True)))
