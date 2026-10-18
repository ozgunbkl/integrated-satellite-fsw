"""Receive and validate one archive record twice over UDP; reset is opt-in."""
import argparse
import binascii
import ipaddress
import re
import select
import socket
import struct
import time

import serial


ARCHIVE_APID = 0x060
ARCHIVE_VERSION = 1
ARCHIVE_HEADER_SIZE = 9
MAX_PAYLOAD = 128


def decode_archive_packet(packet):
    """Validate the CCSDS envelope and v1 body; CRC covers payload only."""
    if len(packet) < 6 + ARCHIVE_HEADER_SIZE:
        raise ValueError("Archive TM is shorter than its headers")
    packet_id, sequence_control, packet_length = struct.unpack("!HHH", packet[:6])
    if packet_id != ARCHIVE_APID:
        raise ValueError("Expected version-0 TM, no secondary header, APID 0x060")
    if sequence_control & 0xC000 != 0xC000:
        raise ValueError("Expected an unsegmented CCSDS packet")
    if len(packet) != 6 + packet_length + 1:
        raise ValueError("CCSDS packet length does not match UDP datagram length")
    version, index, record_id, length, crc = struct.unpack("!BHHHH", packet[6:15])
    if version != ARCHIVE_VERSION:
        raise ValueError(f"Unsupported archive format version: {version}")
    if length > MAX_PAYLOAD or len(packet) != 15 + length:
        raise ValueError("Archive payload length is invalid")
    payload = packet[15:]
    computed_crc = binascii.crc_hqx(payload, 0xFFFF)
    if computed_crc != crc:
        raise ValueError(f"CRC mismatch: stored 0x{crc:04X}, computed 0x{computed_crc:04X}")
    return {"index": index, "record_id": record_id, "length": length,
            "crc": crc, "payload": payload, "sequence": sequence_control & 0x3FFF}


def make_get_command(index):
    payload = struct.pack("!BH", 0x30, index)
    return struct.pack("!HHH", 0x1060, 0xC000, len(payload) - 1) + payload


def archive_index(value):
    try:
        index = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("Index must be an integer from 0 to 65535") from error
    if not 0 <= index <= 65535:
        raise argparse.ArgumentTypeError("Index must be from 0 to 65535")
    return index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7", help="ESP32 serial port (default: COM7)")
    parser.add_argument("--baud", type=int, default=115200)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--ip", type=ipaddress.IPv4Address,
                        help="Running board IP; do not request a reset")
    target.add_argument("--reset", action="store_true",
                        help="Restart and discover IP; existing RAM archive is cleared")
    parser.add_argument("--index", type=archive_index, default=0)
    parser.add_argument("--listen-ip", type=ipaddress.IPv4Address, default="0.0.0.0")
    parser.add_argument("--tm-port", type=int, default=10015)
    parser.add_argument("--tc-port", type=int, default=10025)
    args = parser.parse_args()
    for port_number in (args.tm_port, args.tc_port):
        if not 1 <= port_number <= 65535:
            parser.error("UDP port must be from 1 to 65535")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp, serial.Serial() as port:
        # Exclusive on Windows: fail if YAMCS/another listener owns this port.
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            udp.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        try:
            udp.bind((str(args.listen_ip), args.tm_port))
        except OSError as error:
            raise RuntimeError("Cannot bind the TM port. Close the competing listener or "
                               "select a port matching the firmware; no process was stopped.") from error
        udp.setblocking(False)
        port.port = args.port
        port.baudrate = args.baud
        port.timeout = 0.1
        port.dtr = False
        port.rts = False
        port.open()
        board_ip = str(args.ip) if args.ip is not None else None

        def read_line():
            nonlocal board_ip
            line = port.readline().decode("utf-8", errors="replace").strip()
            match = re.search(r"ESP32 IP: (\d+\.\d+\.\d+\.\d+)", line)
            if match and args.reset:
                board_ip = match.group(1)
            if any(marker in line for marker in ("Guru Meditation", "Backtrace:", "abort()")):
                raise RuntimeError("Unexpected board failure: " + line)
            if line.startswith(("SYSTEM MODE:", "YAMCS:", "TC PROC:", "ARCHIVE:",
                                "ARCHIVE ERROR:", "TC_RX:")):
                print(line, flush=True)
            return line

        if args.reset:
            port.rts = True
            time.sleep(0.1)
            port.rts = False
            deadline = time.monotonic() + 35
            while time.monotonic() < deadline:
                if "TM GEN: -- Running NOMINAL Mission Cycle --" in read_line():
                    break
            else:
                raise RuntimeError("Timed out waiting for the first NOMINAL mission cycle")
            if board_ip is None:
                raise RuntimeError("Board IP was not confirmed from its boot log")
            # Give the mission cycle time to append the first record.
            time.sleep(0.5)
        else:
            port.reset_input_buffer()

        def get_record():
            # Discard stale datagrams so the next record must answer this request.
            while select.select([udp], [], [], 0)[0]:
                udp.recvfrom(65535)
            udp.sendto(make_get_command(args.index), (board_ip, args.tc_port))
            deadline = time.monotonic() + 8
            board_datagrams = 0
            while time.monotonic() < deadline:
                if port.in_waiting:
                    line = read_line()
                    if line.startswith("ARCHIVE ERROR:"):
                        raise RuntimeError("Board rejected the archive request: " + line)
                if not select.select([udp], [], [], 0.02)[0]:
                    continue
                packet, sender = udp.recvfrom(65535)
                if sender[0] != board_ip or len(packet) < 2:
                    continue
                board_datagrams += 1
                if struct.unpack("!H", packet[:2])[0] & 0x07FF != ARCHIVE_APID:
                    continue  # Other regular telemetry/events share the TM port.
                record = decode_archive_packet(packet)
                if record["index"] != args.index:
                    raise RuntimeError("Received a different archive index than requested")
                print(f"RECEIVED: index={record['index']}, id=0x{record['record_id']:04X}, "
                      f"length={record['length']}, CRC16=0x{record['crc']:04X} OK, "
                      f"CCSDS sequence={record['sequence']}", flush=True)
                print("PAYLOAD: " + record["payload"].hex(), flush=True)
                return record
            raise RuntimeError(f"No archive TM received ({board_datagrams} datagrams from the board). "
                               "Check board IP, firmware TM destination, "
                               "PC firewall and whether the requested record exists.")

        print(f"CHECK: receive archive index {args.index} from {board_ip} twice", flush=True)
        first = get_record()
        second = get_record()
        for field in ("index", "record_id", "length", "crc", "payload"):
            if first[field] != second[field]:
                raise RuntimeError("Repeated read changed the stored record: " + field)
        if second["sequence"] != ((first["sequence"] + 1) & 0x3FFF):
            raise RuntimeError("Archive APID sequence did not advance by one")
        print("PASS: actual UDP receipt, packet lengths and CRC16 verified; "
              "the same archived record was received again. No WIPE/ACK sent.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit("FAIL: " + str(error)) from error
