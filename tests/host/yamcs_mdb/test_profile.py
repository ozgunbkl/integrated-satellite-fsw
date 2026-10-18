"""Portable static guards for the maintained profile; native Yamcs check is separate."""
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
NS = {"x": "http://www.omg.org/spec/XTCE/20180204"}


class MdbProfileChecks(unittest.TestCase):
    def setUp(self):
        self.root = ET.parse(ROOT / "config/yamcs/xtce.xml").getroot()
        self.commands = {n.attrib["name"]: n for n in self.root.findall("x:CommandMetaData/x:MetaCommandSet/x:MetaCommand", NS)}
        self.containers = {n.attrib["name"]: n for n in self.root.findall("x:TelemetryMetaData/x:ContainerSet/x:SequenceContainer", NS)}
        self.parameters = {n.attrib["name"]: n for n in self.root.findall("x:TelemetryMetaData/x:ParameterSet/x:Parameter", NS)}

    def test_active_commands_only(self):
        self.assertEqual(self.root.attrib["name"], "myproject")
        active = {name for name, n in self.commands.items() if n.get("abstract") != "true"}
        self.assertEqual(active, {"ARCHIVE_GET_BY_INDEX", "HK_SetInterval", "PAYLOAD_SetRate", "FDIR_SetMode", "FDIR_RearmBoot",
                                 "ARCHIVE_BEGIN_TRANSFER", "ARCHIVE_PENDING_TRANSFER", "ARCHIVE_READ_TRANSFER",
                                 "ARCHIVE_CONFIRM_TRANSFER", "ARCHIVE_CANCEL_TRANSFER",
                                 "ADCS_SetMode", "PAYLOAD_Init", "PAYLOAD_Start", "PAYLOAD_Stop", "PAYLOAD_Reset"})
        self.assertEqual(set(self.commands), active | {"CCSDSPacket"})

    def test_no_simulator_tm_or_algorithm(self):
        self.assertEqual(set(self.containers), {"CCSDSPacket", "TelemetryPacket", "MasterHKContainer", "EventPacket", "ArchiveRecordContainer", "ArchiveTransferContainer"})
        self.assertIsNone(self.root.find(".//x:AlgorithmSet", NS))
        self.assertNotIn("Sunsensor", self.parameters)

    def test_tc_apids_and_flags(self):
        expected = {"ARCHIVE_GET_BY_INDEX": "96", "HK_SetInterval": "80", "PAYLOAD_SetRate": "112", "FDIR_SetMode": "48", "FDIR_RearmBoot": "48"}
        expected.update({name: "96" for name in self.commands if name.startswith("ARCHIVE_")})
        expected.update({"ADCS_SetMode": "16", "PAYLOAD_Init": "112", "PAYLOAD_Start": "112", "PAYLOAD_Stop": "112", "PAYLOAD_Reset": "112"})
        for name, apid in expected.items():
            assignments = {n.get("argumentName"): n.get("argumentValue") for n in self.commands[name].findall("x:BaseMetaCommand/x:ArgumentAssignmentList/x:ArgumentAssignment", NS)}
            self.assertEqual(assignments, {"CCSDS_Version": "0", "CCSDS_Type": "TC", "CCSDS_Sec_Hdr_Flag": "NotPresent", "CCSDS_APID": apid, "CCSDS_Group_Flags": "Standalone"})

    def test_fixed_command_ids_and_public_argument_names(self):
        for name, cmd_id, argument in (("ARCHIVE_GET_BY_INDEX", "30", "RecordIndex"), ("HK_SetInterval", "01", "CmdParam"), ("PAYLOAD_SetRate", "03", "CmdParam"), ("FDIR_SetMode", "52", "CmdParam"), ("ADCS_SetMode", "01", "CmdParam"), ("PAYLOAD_Init", "00", None), ("PAYLOAD_Start", "01", None), ("PAYLOAD_Stop", "02", None), ("PAYLOAD_Reset", "04", None)):
            command = self.commands[name]
            fixed = command.find("x:CommandContainer/x:EntryList/x:FixedValueEntry", NS)
            self.assertEqual((fixed.get("binaryValue"), fixed.get("sizeInBits")), (cmd_id, "8"))
            self.assertEqual([n.get("name") for n in command.findall("x:ArgumentList/x:Argument", NS)], [argument] if argument else [])

    def test_command_ranges(self):
        for name, minimum, maximum in (("ArchiveIndex_Type", "0", "65535"), ("HKIntervalSeconds_Type", "1", "60"), ("PayloadRate_Type", "1", "10"), ("SystemMode_Type", "0", "5"), ("ADCSMode_Type", "0", "2")):
            value = self.root.find(f".//x:IntegerArgumentType[@name='{name}']/x:ValidRangeSet/x:ValidRange", NS)
            self.assertEqual((value.get("minInclusive"), value.get("maxInclusive")), (minimum, maximum))

    def test_rearm_is_separate_explicit_boot_counter_and_key(self):
        command = self.commands["FDIR_RearmBoot"]
        self.assertIn("Queued is not COMPLETE", command.get("shortDescription"))
        fixed = command.find("x:CommandContainer/x:EntryList/x:FixedValueEntry", NS)
        self.assertEqual((fixed.get("binaryValue"), fixed.get("sizeInBits")), ("53", "8"))
        arguments = command.findall("x:ArgumentList/x:Argument", NS)
        self.assertEqual([(row.get("name"), row.get("argumentTypeRef")) for row in arguments],
                         [("BootCounter", "TransferId_Type"), ("ConfirmKey", "TransferConfirmKey_Type")])
        self.assertTrue(all(row.get("initialValue") is None for row in arguments))

    def test_archive_fields_offsets(self):
        entries = self.containers["ArchiveRecordContainer"].findall("x:EntryList/x:ParameterRefEntry", NS)
        offsets = [(n.get("parameterRef"), int(n.find("x:LocationInContainerInBits/x:FixedValue", NS).text)) for n in entries]
        self.assertEqual(offsets, [("ArchiveVersion", 48), ("ArchiveIndex", 56), ("ArchiveRecordId", 72), ("ArchivePayloadLength", 88), ("ArchivePayloadCRC", 104), ("ArchivePayload", 120)])

    def test_archive_integer_byte_order(self):
        for name, bits in (("ArchiveU8Type", "8"), ("ArchiveU16Type", "16")):
            value = self.root.find(f".//x:IntegerParameterType[@name='{name}']/x:IntegerDataEncoding", NS)
            self.assertEqual(value.get("sizeInBits"), bits)
            self.assertEqual(value.get("byteOrder", "mostSignificantByteFirst"), "mostSignificantByteFirst")

    def test_dynamic_payload_bits_from_byte_length(self):
        dynamic = self.root.find(".//x:BinaryParameterType[@name='ArchivePayloadType']/x:BinaryDataEncoding/x:SizeInBits/x:DynamicValue", NS)
        self.assertEqual(dynamic.find("x:ParameterInstanceRef", NS).get("parameterRef"), "ArchivePayloadLength")
        adjustment = dynamic.find("x:LinearAdjustment", NS)
        self.assertEqual((adjustment.get("slope"), adjustment.get("intercept")), ("8", "0"))

    def test_tm_apids_and_profile_restrictions(self):
        for name, apid in (("MasterHKContainer", "5"), ("EventPacket", "2000"), ("ArchiveRecordContainer", "96"), ("ArchiveTransferContainer", "97")):
            comparison = self.containers[name].find("x:BaseContainer/x:RestrictionCriteria/x:Comparison", NS)
            self.assertEqual((comparison.get("parameterRef"), comparison.get("value")), ("CCSDS_Packet_ID.APID", apid))
        comparisons = self.containers["TelemetryPacket"].findall("x:BaseContainer/x:RestrictionCriteria/x:ComparisonList/x:Comparison", NS)
        self.assertEqual({n.get("parameterRef"): n.get("value") for n in comparisons}, {"CCSDS_Packet_ID.Version": "0", "CCSDS_Packet_ID.Type": "0", "CCSDS_Packet_ID.SecHdrFlag": "0", "CCSDS_Packet_Sequence.GroupFlags": "3"})
        self.assertTrue(all(n.get("useCalibratedValue") == "false" for n in comparisons))

    def test_hk_offsets_and_float_order(self):
        entries = self.containers["MasterHKContainer"].findall("x:EntryList/x:ParameterRefEntry", NS)
        self.assertEqual([int(n.find("x:LocationInContainerInBits/x:FixedValue", NS).text) for n in entries], [48, 80, 112, 144, 176, 208, 240])
        encoding = self.root.find(".//x:FloatParameterType[@name='float_t']/x:FloatDataEncoding", NS)
        self.assertEqual((encoding.get("sizeInBits"), encoding.get("byteOrder"), encoding.get("encoding")), ("32", "mostSignificantByteFirst", "IEEE754_1985"))

    def test_transfer_command_layouts(self):
        manifest = ["BootId", "TransferId", "RequestId", "Digest", "RecordCount"]
        for name, opcode, args in (("BEGIN", "31", ["RequestId"]), ("PENDING", "32", []),
                                   ("READ", "33", manifest + ["RecordIndex"]),
                                   ("CONFIRM", "34", manifest + ["ConfirmKey"]), ("CANCEL", "35", manifest)):
            command = self.commands[f"ARCHIVE_{name}_TRANSFER"]
            fixed = command.find("x:CommandContainer/x:EntryList/x:FixedValueEntry", NS)
            self.assertEqual((fixed.get("binaryValue"), fixed.get("sizeInBits")), (opcode, "8"))
            self.assertEqual([n.get("name") for n in command.findall("x:ArgumentList/x:Argument", NS)], args)
            self.assertEqual([n.get("argumentRef") for n in command.findall("x:CommandContainer/x:EntryList/x:ArgumentRefEntry", NS)], args)
            self.assertTrue(all("initialValue" not in n.attrib for n in command.findall("x:ArgumentList/x:Argument", NS)))

    def test_hk_units_and_type_bindings(self):
        for field, type_name, unit in (("UptimeMs", "Uptime", "ms"), ("BatteryVoltage", "Voltage", "V"),
                ("BatterySOC", "SOC", "%"), ("BatteryTempC", "Temperature", "degC"),
                ("GyroX", "AngularRate", "rad/s"), ("GyroY", "AngularRate", "rad/s"),
                ("GyroZ", "AngularRate", "rad/s")):
            ref = self.parameters["HK_" + field].get("parameterTypeRef")
            self.assertEqual(ref, "HK_" + type_name + "Type")
            item = self.root.find(f".//x:ParameterTypeSet/*[@name='{ref}']", NS)
            self.assertEqual([n.text for n in item.findall("x:UnitSet/x:Unit", NS)], [unit])
            encoding = item.find("x:IntegerDataEncoding" if field == "UptimeMs" else "x:FloatDataEncoding", NS)
            self.assertEqual(encoding.get("sizeInBits"), "32")
            self.assertEqual(encoding.get("byteOrder"), "mostSignificantByteFirst")
            self.assertEqual(encoding.get("encoding"), "unsigned" if field == "UptimeMs" else "IEEE754_1985")

    def test_transfer_ids_remain_unitless(self):
        for name in ("TransferId", "TransferRequestId", "TransferDigest"):
            self.assertEqual(self.parameters[name].get("parameterTypeRef"), "uint32_t")
        self.assertIsNone(self.root.find(".//x:IntegerParameterType[@name='uint32_t']/x:UnitSet", NS))

    def test_temperature_placeholder_is_documented(self):
        self.assertIn("placeholder when unset", self.parameters["HK_BatteryTempC"].get("shortDescription"))
        self.assertIn("no validity bit", self.parameters["HK_BatteryTempC"].get("shortDescription"))

    def test_transfer_ranges_and_unsigned_64(self):
        for name, bits, minimum, maximum in (("TransferBoot_Type", "64", "1", "-1"),
                ("TransferId_Type", "32", "1", "4294967295"), ("TransferU32_Type", "32", "0", "4294967295"),
                ("TransferCount_Type", "16", "1", "30"), ("TransferIndex_Type", "16", "0", "29"),
                ("TransferConfirmKey_Type", "8", "165", "165")):
            item = self.root.find(f".//x:IntegerArgumentType[@name='{name}']", NS)
            self.assertEqual((item.get("signed"), item.get("sizeInBits")), ("false", bits))
            self.assertEqual(item.find("x:IntegerDataEncoding", NS).get("sizeInBits"), bits)
            value = item.find("x:ValidRangeSet/x:ValidRange", NS)
            self.assertEqual((value.get("minInclusive"), value.get("maxInclusive")), (minimum, maximum))

    def test_transfer_tm_offsets(self):
        entries = self.containers["ArchiveTransferContainer"].findall("x:EntryList/x:ParameterRefEntry", NS)
        self.assertEqual([n.get("parameterRef") for n in entries], ["TransferVersion", "TransferOperation", "TransferStatus",
            "TransferBootId", "TransferId", "TransferRequestId", "TransferDigest", "TransferRecordCount", "TransferIndex",
            "TransferRecordId", "TransferLength", "TransferCRC", "TransferPayload"])
        self.assertEqual([int(n.find("x:LocationInContainerInBits/x:FixedValue", NS).text) for n in entries],
                         [48, 56, 64, 72, 136, 168, 200, 232, 248, 264, 280, 296, 312])

    def test_transfer_dynamic_payload(self):
        dynamic = self.root.find(".//x:BinaryParameterType[@name='TransferPayloadType']/x:BinaryDataEncoding/x:SizeInBits/x:DynamicValue", NS)
        self.assertEqual(dynamic.find("x:ParameterInstanceRef", NS).get("parameterRef"), "TransferLength")
        self.assertEqual(dynamic.find("x:LinearAdjustment", NS).get("slope"), "8")
        encoding = self.root.find(".//x:IntegerParameterType[@name='TransferU64Type']/x:IntegerDataEncoding", NS)
        self.assertEqual((encoding.get("sizeInBits"), encoding.get("encoding"), encoding.get("byteOrder")), ("64", "unsigned", "mostSignificantByteFirst"))


if __name__ == "__main__":
    unittest.main()
