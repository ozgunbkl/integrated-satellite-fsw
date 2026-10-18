"""Bounded offline TC builder/TM decoder for archive transfer v1; never sends packets."""
import binascii
import struct
from archive_transfer import SavedTransfer, Transfer, load_verified_export, unsigned

BEGIN, PENDING, READ, CONFIRM, CANCEL = range(0x31, 0x36)
MANIFEST = struct.Struct("!QIIIH")
REPLY = struct.Struct("!BBBQIIIH4H")


def manifest_bytes(transfer):
    if not isinstance(transfer, Transfer):
        raise ValueError("Expected validated Transfer")
    transfer = Transfer.from_dict(transfer.as_dict())
    return MANIFEST.pack(transfer.boot_id, transfer.transfer_id, transfer.request_id,
                         transfer.digest, transfer.record_count)


def begin_command(request_id):
    return struct.pack("!BI", BEGIN, unsigned(request_id, 32, "request_id"))


def pending_command():
    return bytes([PENDING])


def read_command(transfer, index):
    data = manifest_bytes(transfer)
    if unsigned(index, 16, "index") >= transfer.record_count:
        raise ValueError("Index is outside the transfer snapshot")
    return bytes([READ]) + data + struct.pack("!H", index)


def cancel_command(transfer):
    return bytes([CANCEL]) + manifest_bytes(transfer)


def confirm_command(saved, *, confirm_clear=False):
    """Explicit destructive intent; a raw manifest/loaded file alone is insufficient.

    The transport must additionally match the board's returned CONFIRM receipt.
    This builder cannot prove packet delivery, board identity or power-loss durability.
    """
    if confirm_clear is not True or not isinstance(saved, SavedTransfer):
        raise ValueError("CONFIRM requires a synced-save receipt AND explicit confirm_clear=True")
    if load_verified_export(saved.path) != saved.transfer:
        raise ValueError("Saved export no longer matches the confirmation identity")
    return bytes([CONFIRM]) + manifest_bytes(saved.transfer) + b"\xA5"


def tc_packet(command, sequence=0):
    if not isinstance(command, bytes) or not 1 <= len(command) <= 48:
        raise ValueError("TC body must be 1..48 bytes")
    sequence = unsigned(sequence, 14, "sequence")
    return struct.pack("!3H", 0x1060, 0xC000 | sequence, len(command) - 1) + command


def decode_reply(packet):
    if not isinstance(packet, bytes) or not 39 <= len(packet) <= 167:
        raise ValueError("Transfer TM size outside 39..167")
    packet_id, sequence, length = struct.unpack_from("!3H", packet)
    if packet_id != 0x0061 or sequence & 0xC000 != 0xC000 or length != len(packet) - 7:
        raise ValueError("Invalid transfer TM primary header/length")
    version, op, status, boot, tid, request, digest, count, index, record_id, size, crc = REPLY.unpack_from(packet, 6)
    if version != 1 or op not in range(BEGIN, CANCEL + 1) or status not in range(11):
        raise ValueError("Unknown transfer version/operation/status")
    if size > 128 or len(packet) != 39 + size:
        raise ValueError("Invalid transfer payload size")
    manifest = {"boot_id": boot, "transfer_id": tid, "request_id": request, "digest": digest, "record_count": count}
    record = None
    if status == 0:
        transfer = Transfer.from_dict(manifest)
    if op == READ and status == 0:
        payload = packet[39:]
        if index >= transfer.record_count or binascii.crc_hqx(payload, 0xFFFF) != crc:
            raise ValueError("Invalid record index/CRC16")
        record = {"index": index, "record_id": record_id, "length": size, "crc": crc, "payload": payload.hex()}
    elif size != 0 or record_id != 0 or crc != 0 or (op != READ and index != 0xFFFF):
        raise ValueError("Control/error response contains record data")
    return {"operation": op, "status": status, "manifest": manifest, "index": index,
            "record": record, "sequence": sequence & 0x3FFF}
