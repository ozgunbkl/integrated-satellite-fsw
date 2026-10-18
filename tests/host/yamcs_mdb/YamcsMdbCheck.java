import java.lang.reflect.Proxy;
import java.nio.ByteBuffer;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.yamcs.ProcessorConfig;
import org.yamcs.YConfiguration;
import org.yamcs.TmPacket;
import org.yamcs.ErrorInCommand;
import org.yamcs.events.EventProducerFactory;
import org.yamcs.cmdhistory.CommandHistoryPublisher;
import org.yamcs.commanding.PreparedCommand;
import org.yamcs.mdb.Mdb;
import org.yamcs.mdb.MdbFactory;
import org.yamcs.mdb.MetaCommandProcessor;
import org.yamcs.mdb.ProcessorData;
import org.yamcs.mdb.XtceTmExtractor;
import org.yamcs.parameter.ParameterValue;
import org.yamcs.parameter.Value;
import org.yamcs.utils.TimeEncoding;
import org.yamcs.xtce.BaseDataType;
import com.example.myproject.MyCommandPostprocessor;
import com.example.myproject.MyPacketPreprocessor;

/** Real pinned Yamcs loader/extractor/command builder; no server, links or board. */
public class YamcsMdbCheck {
    private static void check(boolean condition, String reason) {
        if (!condition) throw new AssertionError(reason);
    }
    private static Map<String, Value> extract(XtceTmExtractor extractor, byte[] packet) {
        Map<String, Value> values = new HashMap<>();
        for (ParameterValue value : extractor.processPacket(packet, 0, 0, 0).getParameterResult()) {
            values.put(value.getParameter().getName(), value.getEngValue());
        }
        return values;
    }
    private static int crc(byte[] payload) {
        int crc = 0xFFFF;
        for (byte b : payload) {
            crc ^= (b & 0xFF) << 8;
            for (int i = 0; i < 8; i++) crc = ((crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021 : crc << 1) & 0xFFFF;
        }
        return crc;
    }
    private static byte[] archive(byte[] payload) {
        return ByteBuffer.allocate(15 + payload.length).putShort((short)96).putShort((short)0xC007)
            .putShort((short)(payload.length + 8)).put((byte)1).putShort((short)0x1234)
            .putShort((short)0xABCD).putShort((short)payload.length).putShort((short)crc(payload)).put(payload).array();
    }
    private static void tmChecks(Mdb mdb) {
        XtceTmExtractor extractor = new XtceTmExtractor(mdb);
        extractor.provideAll();
        check(crc("123456789".getBytes(java.nio.charset.StandardCharsets.US_ASCII)) == 0x29B1, "CRC reference");
        for (int length : new int[] {0, 1, 9, 128}) {
            byte[] payload = new byte[length];
            for (int i = 0; i < length; i++) payload[i] = (byte)i;
            Map<String, Value> values = extract(extractor, archive(payload));
            check(values.get("ArchiveVersion").getUint32Value() == 1, "archive version");
            check(values.get("ArchiveIndex").getUint32Value() == 0x1234, "archive index BE");
            check(values.get("ArchiveRecordId").getUint32Value() == 0xABCD, "record ID BE");
            check(values.get("ArchivePayloadLength").getUint32Value() == length, "payload length BE");
            check(values.get("ArchivePayloadCRC").getUint32Value() == crc(payload), "payload CRC field BE");
            check(Arrays.equals(values.get("ArchivePayload").getBinaryValue(), payload), "dynamic payload length/offset");
        }
        byte[] hk = ByteBuffer.allocate(34).putShort((short)5).putShort((short)0xC000).putShort((short)27)
            .putInt(0x01020304).putFloat(8.5f).putFloat(75).putFloat(25)
            .putFloat(1).putFloat(-2).putFloat(0.5f).array();
        Map<String, Value> values = extract(extractor, hk);
        check(values.get("HK_UptimeMs").getUint32Value() == 0x01020304, "HK uptime BE");
        String[] names = {"HK_BatteryVoltage","HK_BatterySOC","HK_BatteryTempC","HK_GyroX","HK_GyroY","HK_GyroZ"};
        float[] expected = {8.5f,75,25,1,-2,0.5f};
        for (int i = 0; i < names.length; i++) check(values.get(names[i]).getFloatValue() == expected[i], "HK float " + names[i]);
        String[] hkNames = {"HK_UptimeMs","HK_BatteryVoltage","HK_BatterySOC","HK_BatteryTempC","HK_GyroX","HK_GyroY","HK_GyroZ"};
        String[] units = {"ms","V","%","degC","rad/s","rad/s","rad/s"};
        for (int i = 0; i < hkNames.length; i++) {
            var unitSet = ((BaseDataType)mdb.getParameter("/myproject/" + hkNames[i]).getParameterType()).getUnitSet();
            check(unitSet.size() == 1 && unitSet.get(0).getUnit().equals(units[i]), "HK unit " + hkNames[i]);
        }
        check(((BaseDataType)mdb.getParameter("/myproject/TransferId").getParameterType()).getUnitSet().isEmpty(), "unitless transfer ID");
        byte[] event = ByteBuffer.allocate(70).putShort((short)2000).putShort((short)0xC000).putShort((short)63).array();
        System.arraycopy(new byte[] {'p','r','o','b','e'}, 0, event, 6, 5);
        check(extract(extractor, event).get("EventMessage").getStringValue().startsWith("probe"), "event container retained");
        for (int field : new int[] {0, 1, 2, 3, 4}) {
            byte[] invalid = archive(new byte[] {42});
            if (field < 3) invalid[0] |= new int[] {0x20, 0x10, 0x08}[field];
            else if (field == 3) invalid[2] = 0;
            else invalid[1] = 0x62;
            check(!extract(extractor, invalid).containsKey("ArchiveIndex"), "wrong profile must not extract archive");
        }
    }
    private static void commandChecks(Mdb mdb) throws Exception {
        MetaCommandProcessor builder = new MetaCommandProcessor(new ProcessorData("offline", mdb, new ProcessorConfig()));
        MyCommandPostprocessor post = new MyCommandPostprocessor("offline");
        post.setCommandHistoryPublisher((CommandHistoryPublisher)Proxy.newProxyInstance(
            CommandHistoryPublisher.class.getClassLoader(), new Class<?>[] {CommandHistoryPublisher.class}, (p,m,a) -> null));
        String[] names = {"ARCHIVE_GET_BY_INDEX","HK_SetInterval","PAYLOAD_SetRate","FDIR_SetMode","ADCS_SetMode"};
        String[] args = {"RecordIndex","CmdParam","CmdParam","CmdParam","CmdParam"};
        int[] apids = {96,80,112,48,16}, ids = {0x30,1,3,0x52,1};
        int[][] good = {{0,0x1234,65535},{1,10,60},{1,10},{0,1,2,3,4,5},{0,1,2}};
        int[][] bad = {{-1,65536},{0,61},{0,11},{-1,6},{-1,3}};
        Map<Integer,Integer> previous = new HashMap<>();
        int encoded = 0, rejected = 0;
        for (int n = 0; n < names.length; n++) {
            var command = mdb.getMetaCommand("/myproject/" + names[n]);
            check(command != null, "missing real command " + names[n]);
            for (int value : good[n]) {
                byte[] raw = builder.buildCommand(command, Map.of(args[n], value), 0).getCmdPacket();
                byte[] packet = post.process(new PreparedCommand(raw)); // Actual installed postprocessor, no link.
                ByteBuffer b = ByteBuffer.wrap(packet);
                check(Short.toUnsignedInt(b.getShort()) == (0x1000 | apids[n]), "TC header/APID");
                int sequence = Short.toUnsignedInt(b.getShort());
                check((sequence & 0xC000) == 0xC000, "unsegmented TC");
                int seq = sequence & 0x3FFF;
                if (previous.containsKey(apids[n])) check(seq == ((previous.get(apids[n]) + 1) & 0x3FFF), "per-APID sequence");
                previous.put(apids[n], seq);
                check(Short.toUnsignedInt(b.getShort()) == packet.length - 7, "postprocessed wire length");
                check(Byte.toUnsignedInt(b.get()) == ids[n], "command ID");
                check((n == 0 ? Short.toUnsignedInt(b.getShort()) : Byte.toUnsignedInt(b.get())) == value && !b.hasRemaining(), "command payload/BE");
                encoded++;
            }
            for (int value : bad[n]) {
                boolean failed = false;
                try { builder.buildCommand(command, Map.of(args[n], value), 0); }
                catch (ErrorInCommand expected) { failed = true; }
                check(failed, "out-of-range command was accepted"); rejected++;
            }
        }
        String[] payloadNames = {"PAYLOAD_Init", "PAYLOAD_Start", "PAYLOAD_Stop", "PAYLOAD_Reset"};
        int[] payloadIds = {0, 1, 2, 4};
        for (int id = 0; id < payloadNames.length; ++id) {
            var command = mdb.getMetaCommand("/myproject/" + payloadNames[id]);
            check(command != null, "missing explicit payload lifecycle command");
            byte[] packet = post.process(new PreparedCommand(builder.buildCommand(command, Map.of(), 0).getCmdPacket()));
            ByteBuffer b = ByteBuffer.wrap(packet);
            check(packet.length == 7 && Short.toUnsignedInt(b.getShort()) == 0x1070, "payload lifecycle APID/size");
            check((Short.toUnsignedInt(b.getShort()) & 0xC000) == 0xC000, "payload standalone flags");
            check(Short.toUnsignedInt(b.getShort()) == 0 && Byte.toUnsignedInt(b.get()) == payloadIds[id] && !b.hasRemaining(), "payload exact ID/body");
            encoded++;
        }
        for (String legacy : new String[] {"Reboot","SwitchVoltageOn","SwitchVoltageOff","MyProjectPacket","SatTcU8Pair"}) {
            check(mdb.getMetaCommand("/myproject/" + legacy) == null, "legacy command still active: " + legacy);
        }
        check(mdb.getSequenceContainer("/myproject/Spacecraft") == null, "simulator TM still active");
        System.out.println("PASS: real Yamcs extraction (archive 0/1/9/128, HK/event, 5 profile rejects); "
            + encoded + " postprocessed commands, " + rejected + " range rejects; no legacy commands.");
    }
    private static void recoveryChecks(Mdb mdb) throws Exception {
        MetaCommandProcessor builder = new MetaCommandProcessor(new ProcessorData("offline", mdb, new ProcessorConfig()));
        MyCommandPostprocessor post = new MyCommandPostprocessor("offline");
        post.setCommandHistoryPublisher((CommandHistoryPublisher)Proxy.newProxyInstance(
            CommandHistoryPublisher.class.getClassLoader(), new Class<?>[] {CommandHistoryPublisher.class}, (p,m,a) -> null));
        var command = mdb.getMetaCommand("/myproject/FDIR_RearmBoot");
        check(command != null, "missing separate per-boot approval");
        for (long boot : new long[] {1L, 0x89ABCDEFL, 0xFFFFFFFFL}) {
            byte[] raw = builder.buildCommand(command, Map.of("BootCounter", boot, "ConfirmKey", 165), 0).getCmdPacket();
            byte[] packet = post.process(new PreparedCommand(raw));
            ByteBuffer b = ByteBuffer.wrap(packet);
            check(packet.length == 12 && Short.toUnsignedInt(b.getShort()) == 0x1030, "rearm size/APID");
            check((Short.toUnsignedInt(b.getShort()) & 0xC000) == 0xC000 && Short.toUnsignedInt(b.getShort()) == 5, "rearm header");
            check(Byte.toUnsignedInt(b.get()) == 0x53 && Integer.toUnsignedLong(b.getInt()) == boot &&
                Byte.toUnsignedInt(b.get()) == 165 && !b.hasRemaining(), "rearm exact BE body");
        }
        for (Map<String, Object> args : List.<Map<String, Object>>of(
                Map.of("BootCounter", 0, "ConfirmKey", 165), Map.of("BootCounter", -1, "ConfirmKey", 165),
                Map.of("BootCounter", 0x100000000L, "ConfirmKey", 165), Map.of("BootCounter", 1, "ConfirmKey", 164),
                Map.of("BootCounter", 1, "ConfirmKey", 166), Map.of("BootCounter", 1), Map.of("ConfirmKey", 165))) {
            boolean rejected = false;
            try { builder.buildCommand(command, args, 0); } catch (ErrorInCommand expected) { rejected = true; }
            check(rejected, "invalid/missing rearm arguments accepted");
        }
        System.out.println("PASS: separate recovery approval: three postprocessed unsigned boot counters/BE/key, seven range/missing-argument rejects; no command sent.");
    }
    private static void transferChecks(Mdb mdb) throws Exception {
        XtceTmExtractor extractor = new XtceTmExtractor(mdb);
        extractor.provideAll();
        for (int length : new int[] {0,1,9,128}) {
            byte[] payload = new byte[length];
            for (int i = 0; i < length; i++) payload[i] = (byte)i;
            byte[] packet = ByteBuffer.allocate(39 + length).putShort((short)97).putShort((short)0xC123)
                .putShort((short)(32 + length)).put((byte)1).put((byte)0x33).put((byte)0)
                .putLong(0xFEDCBA9876543210L).putInt(0x89ABCDEF).putInt(0xFEDCBA98).putInt(0x417EE9BC)
                .putShort((short)30).putShort((short)29).putShort((short)0xABCD)
                .putShort((short)length).putShort((short)crc(payload)).put(payload).array();
            Map<String,Value> values = extract(extractor, packet);
            check(values.get("TransferBootId").getUint64Value() == 0xFEDCBA9876543210L, "transfer uint64 high bit");
            check(values.get("TransferId").getUint32Value() == 0x89ABCDEF, "transfer uint32 high bit");
            check(values.get("TransferRequestId").getUint32Value() == 0xFEDCBA98, "request uint32 high bit");
            check(values.get("TransferDigest").getUint32Value() == 0x417EE9BC, "digest BE");
            check(values.get("TransferRecordCount").getUint32Value() == 30 && values.get("TransferIndex").getUint32Value() == 29, "count/index");
            check(values.get("TransferLength").getUint32Value() == length && values.get("TransferCRC").getUint32Value() == crc(payload), "length/CRC");
            check(Arrays.equals(values.get("TransferPayload").getBinaryValue(), payload), "transfer dynamic payload");
        }
        MetaCommandProcessor builder = new MetaCommandProcessor(new ProcessorData("offline", mdb, new ProcessorConfig()));
        MyCommandPostprocessor post = new MyCommandPostprocessor("offline");
        post.setCommandHistoryPublisher((CommandHistoryPublisher)Proxy.newProxyInstance(
            CommandHistoryPublisher.class.getClassLoader(), new Class<?>[] {CommandHistoryPublisher.class}, (p,m,a) -> null));
        Map<String,Object> manifest = Map.of("BootId", "18446744073709551615", "TransferId", 4294967295L,
            "RequestId", 4294967295L, "Digest", 4294967295L, "RecordCount", 30);
        String[] names = {"BEGIN","PENDING","READ","CONFIRM","CANCEL"};
        for (int i = 0; i < names.length; i++) {
            Map<String,Object> args = new HashMap<>();
            if (i == 0) args.put("RequestId", 4294967295L);
            if (i >= 2) args.putAll(manifest);
            if (i == 2) args.put("RecordIndex", 29);
            if (i == 3) args.put("ConfirmKey", 165);
            byte[] packet = post.process(new PreparedCommand(builder.buildCommand(
                mdb.getMetaCommand("/myproject/ARCHIVE_" + names[i] + "_TRANSFER"), args, 0).getCmdPacket()));
            ByteBuffer b = ByteBuffer.wrap(packet);
            check(Short.toUnsignedInt(b.getShort()) == 0x1060, "transfer TC APID/type");
            check((Short.toUnsignedInt(b.getShort()) & 0xC000) == 0xC000, "transfer TC sequence flags");
            check(Short.toUnsignedInt(b.getShort()) == packet.length - 7 && Byte.toUnsignedInt(b.get()) == 0x31 + i, "transfer command ID/length");
            if (i == 0) check(b.getInt() == -1, "BEGIN u32 max");
            if (i >= 2) {
                check(b.getLong() == -1L && b.getInt() == -1 && b.getInt() == -1 && b.getInt() == -1 &&
                      Short.toUnsignedInt(b.getShort()) == 30, "manifest BE unsigned maxima");
            }
            if (i == 2) check(Short.toUnsignedInt(b.getShort()) == 29, "READ index");
            if (i == 3) check(Byte.toUnsignedInt(b.get()) == 165, "explicit CONFIRM key");
            check(!b.hasRemaining(), "transfer TC no extra bytes");
        }
        String[][] bad = {{"BootId","0"},{"BootId","18446744073709551616"},{"TransferId","0"},
            {"TransferId","4294967296"},{"RequestId","-1"},{"Digest","4294967296"},
            {"RecordCount","0"},{"RecordCount","31"},{"ConfirmKey","164"},{"ConfirmKey","166"}};
        var confirm = mdb.getMetaCommand("/myproject/ARCHIVE_CONFIRM_TRANSFER");
        for (String[] wrong : bad) {
            Map<String,Object> args = new HashMap<>(manifest); args.put("ConfirmKey", 165); args.put(wrong[0], wrong[1]);
            boolean failed = false;
            try { builder.buildCommand(confirm, args, 0); } catch (ErrorInCommand expected) { failed = true; }
            check(failed, "invalid transfer argument accepted: " + Arrays.toString(wrong));
        }
        boolean missingKey = false;
        try { builder.buildCommand(confirm, manifest, 0); } catch (ErrorInCommand expected) { missingKey = true; }
        check(missingKey, "CONFIRM key must not default silently");
        System.out.println("PASS: native transfer extraction 0/1/9/128 and unsigned64 high bit; 5 postprocessed transfer TCs and 11 range/missing-key rejects.");
    }
    private static class OfflinePreprocessor extends MyPacketPreprocessor {
        OfflinePreprocessor() { super("offline"); }
        void finish() { eventProducer.setRepeatedEventReduction(false, 0); eventProducer.close(); }
    }
    private static TmPacket eventPacket(int sequence, String message) {
        byte[] raw = ByteBuffer.allocate(70).putShort((short)2000)
            .putShort((short)(0xC000 | sequence)).putShort((short)63).array();
        byte[] text = message.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        System.arraycopy(text, 0, raw, 6, text.length);
        return new TmPacket(TimeEncoding.getWallclockTime(), raw);
    }
    private static void eventChecks() {
        EventProducerFactory.setMockup(true);
        OfflinePreprocessor pre = new OfflinePreprocessor();
        var queue = EventProducerFactory.getMockupQueue();
        try {
            String[] messages = {"INFO: probe", "WARN: probe", "WARNING: probe", "ERROR: probe", "CRITICAL: probe"};
            String[] severities = {"INFO", "WARNING", "WARNING", "ERROR", "ERROR"};
            for (int i = 0; i < messages.length; i++) {
                TmPacket packet = eventPacket(i, messages[i]);
                check(pre.process(packet) == packet, "event packet retained");
                check(packet.getGenerationTime() != TimeEncoding.INVALID_INSTANT, "event generation time assigned");
                check(queue.size() == 1, "one event for each distinct valid input");
                var event = queue.remove();
                check(event.getMessage().equals(messages[i]), "UTF8/NUL event body");
                check(event.getSeverity().name().equals(severities[i]), "event severity mapping");
                check(event.getSource().equals("OfflinePreprocessor") && event.getType().equals("FSW_EVENT"), "event source/type");
            }
            pre.process(eventPacket(5, "\0   "));
            check(queue.isEmpty(), "empty padded event ignored");
            pre.process(eventPacket(5, "INFO: pair"));
            pre.process(eventPacket(6, "INFO: pair"));
            pre.process(eventPacket(7, "INFO: after pair"));
            check(queue.size() == 3, "one additional repeat is emitted unchanged on flush");
            check(queue.remove().getMessage().equals("INFO: pair"), "pair first");
            check(queue.remove().getMessage().equals("INFO: pair"), "pair second is not a summary");
            check(queue.remove().getMessage().equals("INFO: after pair"), "pair flush ordering");
            pre.process(eventPacket(6, "INFO: repeated"));
            pre.process(eventPacket(7, "INFO: repeated"));
            pre.process(eventPacket(7, "INFO: repeated")); // Same wire packet is not deduplicated.
            check(queue.size() == 1, "consecutive repeats buffered by Yamcs reduction");
            check(queue.remove().getMessage().equals("INFO: repeated"), "first repeat emitted immediately");
            pre.process(eventPacket(8, "INFO: next")); // Flush repeats without a 10-second sleep.
            check(queue.size() == 2, "repeat summary plus next event");
            String summary = queue.remove().getMessage();
            check(summary.equals("Repeated 2 times: INFO: repeated"), "summary counts two additional occurrences");
            check(queue.remove().getMessage().equals("INFO: next"), "distinct event flush order");
        } finally { pre.finish(); EventProducerFactory.setMockup(false); }
        System.out.println("PASS: installed preprocessor severity/NUL/empty event checks; consecutive repeat reduction (including duplicate wire), no network/board.");
    }
    public static void main(String[] args) throws Exception {
        TimeEncoding.setUp();
        YConfiguration.setupTool();
        Mdb mdb = MdbFactory.createInstance(List.of(YConfiguration.wrap(Map.of("type", "xtce", "spec", Path.of(args[0]).toAbsolutePath().toString()))), false, false);
        tmChecks(mdb);
        commandChecks(mdb);
        recoveryChecks(mdb);
        transferChecks(mdb);
        eventChecks();
    }
}
