// Exports one deterministic, path-free function report for the Airfix
// Knowledge Bridge. Generated reports are local evidence and belong under
// the ignored artifacts/ tree.

import java.io.BufferedWriter;
import java.io.File;
import java.io.IOException;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.framework.Application;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.block.BasicBlockModel;
import ghidra.program.model.block.CodeBlock;
import ghidra.program.model.block.CodeBlockIterator;
import ghidra.program.model.block.CodeBlockReference;
import ghidra.program.model.block.CodeBlockReferenceIterator;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.JumpTable;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class ExportAirfixFunction extends GhidraScript {
    private static final String SCHEMA = "airfix.re.ghidra-function.v1";
    private static final long MAX_RVA = 0xffffffffL;
    private static final int MAX_REPORT_BYTES = 32 * 1024 * 1024;
    private static final int MAX_INSTRUCTIONS = 100_000;
    private static final int MAX_PCODE_OPS = 100_000;
    private static final int MAX_REFERENCES = 100_000;
    private static final int MAX_SWITCH_TABLES = 4_096;
    private static final int MAX_SWITCH_CASES = 100_000;
    private static final int MAX_PSEUDOCODE_CODE_POINTS = 1_000_000;

    private static String canonicalSha256(String value) {
        if (value == null || !value.matches("(?i)[0-9a-f]{64}")) {
            throw new IllegalStateException(
                "current program has no canonical executable SHA-256");
        }
        return value.toLowerCase(Locale.ROOT);
    }

    private static long parseRva(String value) {
        String text = value.trim();
        if (text.startsWith("0x") || text.startsWith("0X")) {
            text = text.substring(2);
        }
        if (!text.matches("(?i)[0-9a-f]{1,8}")) {
            throw new IllegalArgumentException(
                "RVA must contain one to eight hexadecimal digits");
        }
        return Long.parseUnsignedLong(text, 16);
    }

    private static String formatRva(long value) {
        if (value < 0 || value > MAX_RVA) {
            throw new IllegalArgumentException("RVA is outside PE32 range");
        }
        return String.format(Locale.ROOT, "0x%08X", value);
    }

    private long rva(Address address) {
        long value = address.subtract(currentProgram.getImageBase());
        if (value < 0 || value > MAX_RVA) {
            throw new IllegalArgumentException(
                "address is outside the PE32 image: " + address);
        }
        return value;
    }

    private String rvaText(Address address) {
        return formatRva(rva(address));
    }

    private static String hexBytes(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) {
            result.append(String.format(Locale.ROOT, "%02X", value & 0xff));
        }
        return result.toString();
    }

    private static String unsignedHex(long value, int bytes) {
        int digits = Math.max(2, bytes * 2);
        if (bytes >= Long.BYTES) {
            return String.format(Locale.ROOT, "0x%016X", value);
        }
        long mask = (1L << (bytes * 8)) - 1L;
        return String.format(Locale.ROOT, "0x%0" + digits + "X", value & mask);
    }

    private static String redactHostPaths(String value) {
        String result = value.replaceAll(
            "(?i)(?<![A-Z0-9_])[A-Z]:[\\\\/][^\\s\\\"'<>|]*",
            "<redacted-path>");
        result = result.replaceAll(
            "(?i)\\\\\\\\(?:[^\\s\\\"'<>|]+[\\\\/])+[^\\s\\\"'<>|]*",
            "<redacted-path>");
        result = result.replaceAll(
            "(?i)(?<![A-Z0-9_])/(?:home|users|private|tmp|var/tmp|opt|mnt|" +
            "volumes|workspace|workspaces|root|srv|etc|data|var|usr)/" +
            "[^\\s\\\"'<>|]*",
            "<redacted-path>");
        return result.replaceAll(
            "(?i)(^|[\\s\\\"'])" +
            "(/(?:[A-Za-z0-9._+@=-]+/)*[A-Za-z0-9._+@=-]+" +
            "[^\\s\\\"'<>|]*)",
            "$1<redacted-path>");
    }

    private static JsonElement redactHostPaths(JsonElement value) {
        if (value == null || value.isJsonNull()) return value;
        if (value.isJsonPrimitive()) {
            JsonPrimitive primitive = value.getAsJsonPrimitive();
            if (primitive.isString()) {
                return new JsonPrimitive(redactHostPaths(primitive.getAsString()));
            }
            return primitive.deepCopy();
        }
        if (value.isJsonArray()) {
            JsonArray result = new JsonArray();
            for (JsonElement item : value.getAsJsonArray()) {
                result.add(redactHostPaths(item));
            }
            return result;
        }
        JsonObject result = new JsonObject();
        for (Map.Entry<String, JsonElement> entry :
                value.getAsJsonObject().entrySet()) {
            result.add(entry.getKey(), redactHostPaths(entry.getValue()));
        }
        return result;
    }

    private static String functionName(Function function) {
        return function == null ? null : function.getName(true);
    }

    private JsonObject referenceTarget(Address address, String kind) {
        JsonObject result = new JsonObject();
        result.addProperty("kind", kind);
        if (address != null && address.isMemoryAddress()) {
            try {
                result.addProperty("target_rva", rvaText(address));
            }
            catch (IllegalArgumentException ignored) {
                result.addProperty("target_space", address.getAddressSpace().getName());
            }
            Function target = currentProgram.getFunctionManager().getFunctionAt(address);
            if (target != null) {
                result.addProperty("name", functionName(target));
            }
            else {
                ghidra.program.model.symbol.Symbol symbol =
                    currentProgram.getSymbolTable().getPrimarySymbol(address);
                if (symbol != null) {
                    result.addProperty("name", symbol.getName(true));
                }
            }
        }
        return result;
    }

    private JsonObject signature(Function function) {
        JsonObject signature = new JsonObject();
        signature.addProperty(
            "calling_convention", function.getCallingConventionName());
        signature.addProperty("return_type", function.getReturnType().getDisplayName());
        signature.addProperty("no_return", function.hasNoReturn());
        signature.addProperty("varargs", function.hasVarArgs());
        JsonArray arguments = new JsonArray();
        for (Parameter parameter : function.getParameters()) {
            JsonObject item = new JsonObject();
            item.addProperty("ordinal", parameter.getOrdinal());
            item.addProperty("name", parameter.getName());
            item.addProperty("type", parameter.getDataType().getDisplayName());
            item.addProperty("storage", parameter.getVariableStorage().toString());
            item.addProperty("auto", parameter.isAutoParameter());
            arguments.add(item);
        }
        signature.add("arguments", arguments);
        return signature;
    }

    private JsonObject boundary(Function function) {
        JsonObject boundary = new JsonObject();
        boundary.addProperty("start_rva", rvaText(function.getEntryPoint()));
        Address max = function.getBody().getMaxAddress();
        long end = rva(max) + 1;
        boundary.addProperty("end_rva_exclusive", formatRva(end));
        boundary.addProperty("size", function.getBody().getNumAddresses());
        JsonArray ranges = new JsonArray();
        AddressRangeIterator iterator = function.getBody().getAddressRanges(true);
        while (iterator.hasNext()) {
            AddressRange range = iterator.next();
            JsonObject item = new JsonObject();
            item.addProperty("start_rva", rvaText(range.getMinAddress()));
            item.addProperty(
                "end_rva_exclusive", formatRva(rva(range.getMaxAddress()) + 1));
            ranges.add(item);
        }
        boundary.add("ranges", ranges);
        return boundary;
    }

    private JsonObject cfg(Function function) throws Exception {
        BasicBlockModel model = new BasicBlockModel(currentProgram);
        CodeBlockIterator iterator =
            model.getCodeBlocksContaining(function.getBody(), monitor);
        List<CodeBlock> blocks = new ArrayList<>();
        while (iterator.hasNext()) {
            monitor.checkCancelled();
            blocks.add(iterator.next());
        }
        blocks.sort(Comparator.comparing(CodeBlock::getMinAddress));

        JsonObject cfg = new JsonObject();
        cfg.addProperty("block_count", blocks.size());
        JsonArray items = new JsonArray();
        JsonArray edges = new JsonArray();
        Set<String> seenEdges = new TreeSet<>();
        for (CodeBlock block : blocks) {
            JsonObject item = new JsonObject();
            item.addProperty("start_rva", rvaText(block.getMinAddress()));
            item.addProperty(
                "end_rva_exclusive", formatRva(rva(block.getMaxAddress()) + 1));
            items.add(item);
            CodeBlockReferenceIterator destinations = block.getDestinations(monitor);
            while (destinations.hasNext()) {
                CodeBlockReference reference = destinations.next();
                Address destination = reference.getDestinationAddress();
                if (destination == null || !function.getBody().contains(destination)) {
                    continue;
                }
                String sourceRva = rvaText(reference.getSourceAddress());
                String destinationRva = rvaText(destination);
                String flow = reference.getFlowType().toString();
                String key = sourceRva + "|" + destinationRva + "|" + flow;
                if (seenEdges.add(key)) {
                    JsonObject edge = new JsonObject();
                    edge.addProperty("source_rva", sourceRva);
                    edge.addProperty("target_rva", destinationRva);
                    edge.addProperty("flow", flow);
                    edges.add(edge);
                }
            }
        }
        cfg.add("blocks", items);
        cfg.add("edges", edges);
        return cfg;
    }

    private static int inferredOperandWidth(String operandText) {
        String text = operandText.toLowerCase(Locale.ROOT);
        if (text.contains("long double ptr") ||
                text.contains("longdouble ptr")) return 10;
        if (text.contains("double ptr")) return 8;
        if (text.contains("float ptr")) return 4;
        if (text.contains("tword ptr")) return 10;
        if (text.contains("qword ptr")) return 8;
        if (text.contains("dword ptr")) return 4;
        if (text.contains("byte ptr")) return 1;
        if (text.contains("word ptr")) return 2;
        return 0;
    }

    private JsonArray constants(Instruction instruction) throws Exception {
        Map<String, JsonObject> values = new TreeMap<>();
        for (int operand = 0; operand < instruction.getNumOperands(); ++operand) {
            for (Object object : instruction.getOpObjects(operand)) {
                if (!(object instanceof Scalar)) {
                    continue;
                }
                Scalar scalar = (Scalar)object;
                int bytes = Math.max(1, (scalar.bitLength() + 7) / 8);
                bytes = Math.min(bytes, 8);
                long bits = scalar.getUnsignedValue();
                JsonObject integer = new JsonObject();
                integer.addProperty("kind", "integer");
                integer.addProperty("bits", unsignedHex(bits, bytes));
                integer.addProperty("width", bytes);
                integer.addProperty("signed_value", scalar.getSignedValue());
                values.put("integer|" + bytes + "|" + bits, integer);
            }
        }

        String mnemonic = instruction.getMnemonicString().toUpperCase(Locale.ROOT);
        if (mnemonic.startsWith("FLD") || mnemonic.startsWith("FILD")) {
            int width = inferredOperandWidth(instruction.toString());
            boolean supportedWidth = mnemonic.startsWith("FILD")
                ? width == 2 || width == 4 || width == 8
                : width == 4 || width == 8;
            if (supportedWidth) {
                for (Reference reference : instruction.getReferencesFrom()) {
                    Address target = reference.getToAddress();
                    if (!reference.getReferenceType().isRead() ||
                        target == null || !target.isMemoryAddress()) {
                        continue;
                    }
                    byte[] bytes = new byte[width];
                    if (currentProgram.getMemory().getBytes(target, bytes) != width) {
                        continue;
                    }
                    long bits = 0;
                    if (currentProgram.getLanguage().isBigEndian()) {
                        for (byte value : bytes) {
                            bits = (bits << 8) | (value & 0xffL);
                        }
                    }
                    else {
                        for (int index = bytes.length - 1; index >= 0; --index) {
                            bits = (bits << 8) | (bytes[index] & 0xffL);
                        }
                    }
                    JsonObject memoryConstant = new JsonObject();
                    if (mnemonic.startsWith("FILD")) {
                        memoryConstant.addProperty("kind", "integer");
                        memoryConstant.addProperty("bits", unsignedHex(bits, width));
                        memoryConstant.addProperty("width", width);
                    }
                    else {
                        memoryConstant.addProperty(
                            "kind", width == 4 ? "f32" : "f64");
                        memoryConstant.addProperty("bits", unsignedHex(bits, width));
                        memoryConstant.addProperty("value", width == 4
                            ? Float.toString(Float.intBitsToFloat((int)bits))
                            : Double.toString(Double.longBitsToDouble(bits)));
                    }
                    memoryConstant.addProperty("source_rva", rvaText(target));
                    values.put((mnemonic.startsWith("FILD") ? "integer|" :
                        (width == 4 ? "f32|" : "f64|")) +
                        Long.toUnsignedString(bits) + "|" + rvaText(target),
                        memoryConstant);
                }
            }
        }
        JsonArray result = new JsonArray();
        for (JsonObject value : values.values()) {
            result.add(value);
        }
        return result;
    }

    private String normalizeVarnode(
            Varnode varnode, Map<Long, Integer> uniqueIds) {
        if (varnode == null) return null;
        int size = varnode.getSize();
        if (varnode.isConstant()) {
            return "const:" + size + ":" + unsignedHex(varnode.getOffset(), size);
        }
        Register register = currentProgram.getRegister(varnode);
        if (register != null) {
            return "reg:" + register.getName().toLowerCase(Locale.ROOT) +
                ":" + size;
        }
        Address address = varnode.getAddress();
        String space = address.getAddressSpace().getName().toLowerCase(Locale.ROOT);
        if (varnode.isUnique()) {
            int identifier = uniqueIds.computeIfAbsent(
                varnode.getOffset(), ignored -> uniqueIds.size());
            return "unique:u" + identifier + ":" + size;
        }
        if (varnode.isAddress() && address.isMemoryAddress()) {
            try {
                return "ram:" + rvaText(address) + ":" + size;
            }
            catch (IllegalArgumentException ignored) {
                return "ram:outside-image:" + size;
            }
        }
        if ("stack".equals(space)) {
            return "stack:" + Long.toString(varnode.getOffset()) + ":" + size;
        }
        return space + ":" + Long.toUnsignedString(varnode.getOffset(), 16) +
            ":" + size;
    }

    private JsonArray normalizedPcode(List<Instruction> instructions) {
        JsonArray result = new JsonArray();
        Map<Long, Integer> uniqueIds = new LinkedHashMap<>();
        for (Instruction instruction : instructions) {
            int ordinal = 0;
            for (PcodeOp operation : instruction.getPcode()) {
                if (result.size() >= MAX_PCODE_OPS) {
                    throw new IllegalStateException(
                        "normalized P-code exceeds the operation limit");
                }
                JsonObject item = new JsonObject();
                item.addProperty("site_rva", rvaText(instruction.getAddress()));
                item.addProperty("ordinal", ordinal++);
                item.addProperty("opcode", operation.getMnemonic());
                String output = normalizeVarnode(operation.getOutput(), uniqueIds);
                if (output != null) item.addProperty("output", output);
                JsonArray inputs = new JsonArray();
                for (Varnode input : operation.getInputs()) {
                    inputs.add(normalizeVarnode(input, uniqueIds));
                }
                item.add("inputs", inputs);
                result.add(item);
            }
        }
        return result;
    }

    private JsonArray thisOffsets(List<Instruction> instructions) {
        Map<Long, JsonObject> values = new TreeMap<>();
        for (Instruction instruction : instructions) {
            for (int operand = 0; operand < instruction.getNumOperands(); ++operand) {
                boolean hasThisRegister = false;
                List<Scalar> scalars = new ArrayList<>();
                for (Object object : instruction.getOpObjects(operand)) {
                    if (object instanceof Register) {
                        String name = ((Register)object).getName();
                        hasThisRegister |= "ECX".equalsIgnoreCase(name) ||
                            "RCX".equalsIgnoreCase(name);
                    }
                    else if (object instanceof Scalar) {
                        scalars.add((Scalar)object);
                    }
                }
                if (!hasThisRegister) continue;
                for (Scalar scalar : scalars) {
                    long offset = scalar.getSignedValue();
                    if (offset < 0 || offset > MAX_RVA) continue;
                    JsonObject item = values.computeIfAbsent(offset, ignored -> {
                        JsonObject created = new JsonObject();
                        created.addProperty("offset", formatRva(offset));
                        created.addProperty("basis", "direct-this-register");
                        created.add("sites", new JsonArray());
                        return created;
                    });
                    item.getAsJsonArray("sites").add(rvaText(instruction.getAddress()));
                }
            }
        }
        JsonArray result = new JsonArray();
        for (JsonObject item : values.values()) result.add(item);
        return result;
    }

    private JsonObject x87Finding(Instruction instruction) {
        String mnemonic = instruction.getMnemonicString().toUpperCase(Locale.ROOT);
        if (!mnemonic.startsWith("F") || mnemonic.startsWith("FS:")) return null;
        String kind = "operation";
        if (mnemonic.contains("CW") || mnemonic.endsWith("SW") ||
                 mnemonic.contains("ENV") || mnemonic.contains("SAVE") ||
                 mnemonic.contains("RSTOR") || mnemonic.contains("INIT") ||
                 mnemonic.contains("CLEX") || mnemonic.equals("FWAIT")) {
            kind = "environment";
        }
        else if (mnemonic.startsWith("FLD") || mnemonic.startsWith("FILD")) kind = "load";
        else if (mnemonic.startsWith("FST") || mnemonic.startsWith("FIST") ||
                 mnemonic.startsWith("FBST")) kind = "spill";
        JsonObject item = new JsonObject();
        item.addProperty("site_rva", rvaText(instruction.getAddress()));
        item.addProperty("mnemonic", mnemonic);
        item.addProperty("kind", kind);
        int width = inferredOperandWidth(instruction.toString());
        if (width != 0) item.addProperty("memory_width", width);
        boolean pop = mnemonic.endsWith("P") || mnemonic.startsWith("FISTP") ||
            mnemonic.startsWith("FBSTP");
        if ("spill".equals(kind)) {
            item.addProperty("pops_stack", pop);
            item.addProperty("retains_x87_value", !pop);
            item.addProperty("precision_control_sensitive", true);
            item.addProperty("rounding_control_sensitive",
                (mnemonic.startsWith("FIST") &&
                    !mnemonic.startsWith("FISTTP")) ||
                mnemonic.startsWith("FBST"));
        }
        return item;
    }

    private JsonObject decompile(Function function) throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException(
                "failed to open program in decompiler: " + decompiler.getLastMessage());
        }
        try {
            DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
            if (!result.decompileCompleted() || result.getDecompiledFunction() == null ||
                result.getHighFunction() == null) {
                throw new IllegalStateException(
                    "DECOMPILATION_FAILED=" + function.getEntryPoint());
            }
            JsonObject output = new JsonObject();
            String pseudocode = result.getDecompiledFunction().getC()
                .replace("\r\n", "\n").replace('\r', '\n').stripTrailing();
            if (pseudocode.codePointCount(0, pseudocode.length()) >
                    MAX_PSEUDOCODE_CODE_POINTS) {
                throw new IllegalStateException(
                    "decompiler output exceeds the text limit");
            }
            output.addProperty("text", pseudocode);
            output.addProperty("engine", "ghidra");
            JsonArray switches = new JsonArray();
            JumpTable[] tables = result.getHighFunction().getJumpTables();
            if (tables.length > MAX_SWITCH_TABLES) {
                throw new IllegalStateException(
                    "decompiler output exceeds the switch-table limit");
            }
            Arrays.sort(tables, Comparator.comparing(JumpTable::getSwitchAddress));
            long totalCases = 0;
            for (JumpTable table : tables) {
                JsonObject item = new JsonObject();
                item.addProperty("site_rva", rvaText(table.getSwitchAddress()));
                Address[] cases = table.getCases();
                if (cases.length > MAX_SWITCH_CASES) {
                    throw new IllegalStateException(
                        "decompiler output exceeds the switch-case limit");
                }
                totalCases += cases.length;
                if (totalCases > MAX_SWITCH_CASES) {
                    throw new IllegalStateException(
                        "decompiler output exceeds the combined switch-case limit");
                }
                Integer[] labels = table.getLabelValues();
                JsonArray entries = new JsonArray();
                for (int index = 0; index < cases.length; ++index) {
                    JsonObject entry = new JsonObject();
                    if (labels != null && index < labels.length && labels[index] != null) {
                        entry.addProperty("value", labels[index]);
                    }
                    entry.addProperty("target_rva", rvaText(cases[index]));
                    entries.add(entry);
                }
                item.add("cases", entries);
                switches.add(item);
            }
            output.add("switch_tables", switches);
            return output;
        }
        finally {
            decompiler.dispose();
        }
    }

    private JsonObject functionReport(Function function, String functionId)
            throws Exception {
        List<Instruction> instructions = new ArrayList<>();
        InstructionIterator iterator = currentProgram.getListing().getInstructions(
            function.getBody(), true);
        while (iterator.hasNext()) {
            if (instructions.size() >= MAX_INSTRUCTIONS) {
                throw new IllegalStateException(
                    "function exceeds the instruction limit");
            }
            instructions.add(iterator.next());
        }

        JsonArray instructionItems = new JsonArray();
        JsonArray constants = new JsonArray();
        Map<String, JsonObject> constantMap = new TreeMap<>();
        JsonArray x87 = new JsonArray();
        JsonArray indirectCalls = new JsonArray();
        JsonArray xrefs = new JsonArray();
        Map<String, JsonObject> callees = new TreeMap<>();
        Map<String, JsonObject> reads = new TreeMap<>();
        Map<String, JsonObject> writes = new TreeMap<>();

        for (Instruction instruction : instructions) {
            monitor.checkCancelled();
            JsonObject item = new JsonObject();
            item.addProperty("rva", rvaText(instruction.getAddress()));
            item.addProperty("bytes", hexBytes(instruction.getBytes()));
            item.addProperty("mnemonic", instruction.getMnemonicString());
            item.addProperty("text", instruction.toString());
            instructionItems.add(item);

            for (com.google.gson.JsonElement element : constants(instruction)) {
                JsonObject value = element.getAsJsonObject();
                String key = value.get("kind").getAsString() + "|" +
                    value.get("bits").getAsString() + "|" +
                    (value.has("source_rva")
                        ? value.get("source_rva").getAsString() : "");
                constantMap.putIfAbsent(key, value);
                if (constantMap.size() > MAX_INSTRUCTIONS) {
                    throw new IllegalStateException(
                        "function exceeds the constant limit");
                }
            }

            JsonObject x87Item = x87Finding(instruction);
            if (x87Item != null) x87.add(x87Item);

            boolean directCall = false;
            for (Reference reference : instruction.getReferencesFrom()) {
                if (xrefs.size() >= MAX_REFERENCES) {
                    throw new IllegalStateException(
                        "function exceeds the reference limit");
                }
                RefType type = reference.getReferenceType();
                Address target = reference.getToAddress();
                JsonObject xref = referenceTarget(target, type.toString());
                xref.addProperty("site_rva", rvaText(instruction.getAddress()));
                xref.addProperty("direction", "outgoing");
                xrefs.add(xref);
                if (type.isCall()) {
                    JsonObject call = referenceTarget(target, "direct");
                    call.addProperty("site_rva", rvaText(instruction.getAddress()));
                    String key = rvaText(instruction.getAddress()) + "|" +
                        (call.has("target_rva")
                        ? call.get("target_rva").getAsString()
                        : (target == null ? "<unresolved>" : target.toString())) +
                        "|" + type.toString();
                    callees.putIfAbsent(key, call);
                    directCall = target != null && target.isMemoryAddress() &&
                        !instruction.getFlowType().isComputed();
                    if (call.has("name") &&
                        call.get("name").getAsString().toLowerCase(Locale.ROOT)
                            .contains("ftol")) {
                        JsonObject conversion = new JsonObject();
                        conversion.addProperty(
                            "site_rva", rvaText(instruction.getAddress()));
                        conversion.addProperty("mnemonic", "_FTOL");
                        conversion.addProperty("kind", "conversion-call");
                        conversion.addProperty(
                            "precision_control_sensitive", true);
                        conversion.addProperty(
                            "rounding_control_sensitive", true);
                        x87.add(conversion);
                    }
                }
                if ((type.isRead() || type.isWrite()) && target != null &&
                    target.isMemoryAddress() && !function.getBody().contains(target)) {
                    JsonObject global = referenceTarget(target, "data");
                    global.addProperty("site_rva", rvaText(instruction.getAddress()));
                    String key = rvaText(instruction.getAddress()) + "|";
                    try { key += rvaText(target); }
                    catch (IllegalArgumentException ignored) { key += target.toString(); }
                    key += "|" + type.toString();
                    if (type.isRead()) reads.putIfAbsent(key, global);
                    if (type.isWrite()) writes.putIfAbsent(key, global);
                }
            }
            if (instruction.getFlowType().isCall() && !directCall) {
                JsonObject call = new JsonObject();
                call.addProperty("site_rva", rvaText(instruction.getAddress()));
                call.addProperty("text", instruction.toString());
                JsonArray displacements = new JsonArray();
                Set<Long> seen = new TreeSet<>();
                for (int operand = 0; operand < instruction.getNumOperands(); ++operand) {
                    for (Object object : instruction.getOpObjects(operand)) {
                        if (object instanceof Scalar) {
                            long value = ((Scalar)object).getUnsignedValue();
                            if (value <= MAX_RVA && seen.add(value)) {
                                displacements.add(formatRva(value));
                            }
                        }
                    }
                }
                call.add("candidate_displacements", displacements);
                indirectCalls.add(call);
            }
        }
        for (JsonObject value : constantMap.values()) constants.add(value);

        JsonArray callers = new JsonArray();
        List<Reference> incoming = new ArrayList<>();
        ReferenceIterator incomingIterator = currentProgram.getReferenceManager()
            .getReferencesTo(function.getEntryPoint());
        while (incomingIterator.hasNext()) {
            if (incoming.size() >= MAX_REFERENCES) {
                throw new IllegalStateException(
                    "function exceeds the incoming-reference limit");
            }
            incoming.add(incomingIterator.next());
        }
        incoming.sort(Comparator.comparing(Reference::getFromAddress));
        for (Reference reference : incoming) {
            if (xrefs.size() >= MAX_REFERENCES) {
                throw new IllegalStateException(
                    "function exceeds the combined-reference limit");
            }
            JsonObject caller = new JsonObject();
            Address from = reference.getFromAddress();
            if (from.isMemoryAddress()) {
                try { caller.addProperty("site_rva", rvaText(from)); }
                catch (IllegalArgumentException ignored) {
                    caller.addProperty("site_space", from.getAddressSpace().getName());
                }
            }
            Function owner = currentProgram.getFunctionManager().getFunctionContaining(from);
            if (owner != null) {
                caller.addProperty("target_rva", rvaText(owner.getEntryPoint()));
                caller.addProperty("name", functionName(owner));
            }
            caller.addProperty("kind", reference.getReferenceType().toString());
            callers.add(caller);
            JsonObject xref = caller.deepCopy();
            xref.addProperty("direction", "incoming");
            xrefs.add(xref);
        }

        JsonArray calleeArray = new JsonArray();
        for (JsonObject value : callees.values()) calleeArray.add(value);
        JsonObject globals = new JsonObject();
        JsonArray readArray = new JsonArray();
        for (JsonObject value : reads.values()) readArray.add(value);
        JsonArray writeArray = new JsonArray();
        for (JsonObject value : writes.values()) writeArray.add(value);
        globals.add("reads", readArray);
        globals.add("writes", writeArray);

        JsonObject decompilation = decompile(function);
        JsonObject report = new JsonObject();
        report.addProperty("id", functionId);
        report.addProperty("rva", rvaText(function.getEntryPoint()));
        report.addProperty("name", functionName(function));
        report.add("boundary", boundary(function));
        report.add("signature", signature(function));
        report.add("cfg", cfg(function));
        report.add("instructions", instructionItems);
        JsonObject pseudocode = new JsonObject();
        pseudocode.addProperty("engine", "ghidra");
        pseudocode.add("text", decompilation.get("text"));
        report.add("pseudocode", pseudocode);
        report.add("callers", callers);
        report.add("callees", calleeArray);
        report.add("xrefs", xrefs);
        report.add("globals", globals);
        report.add("this_offsets", thisOffsets(instructions));
        report.add("constants", constants);
        report.add("switch_tables", decompilation.get("switch_tables"));
        report.add("indirect_calls", indirectCalls);
        report.add("pcode", normalizedPcode(instructions));
        report.add("x87", x87);
        return report;
    }

    private static void writeAtomic(File output, JsonObject report) throws IOException {
        File parentFile = output.getAbsoluteFile().getParentFile();
        if (parentFile == null) {
            throw new IllegalArgumentException("output must have a parent directory");
        }
        Files.createDirectories(parentFile.toPath());
        Path temporary = Files.createTempFile(
            parentFile.toPath(), ".airfix-ghidra-function-", ".partial");
        boolean published = false;
        try {
            Gson gson = new GsonBuilder().disableHtmlEscaping()
                .setPrettyPrinting().create();
            try (BufferedWriter writer = Files.newBufferedWriter(
                    temporary, StandardCharsets.UTF_8)) {
                gson.toJson(report, writer);
                writer.write('\n');
            }
            long size = Files.size(temporary);
            if (size <= 0 || size > MAX_REPORT_BYTES) {
                throw new IllegalStateException(
                    "function report size is outside the allowed range");
            }
            try {
                Files.move(temporary, output.toPath(),
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
            }
            catch (AtomicMoveNotSupportedException error) {
                throw new IllegalStateException(
                    "atomic report publication is not supported", error);
            }
            published = true;
        }
        finally {
            if (!published) Files.deleteIfExists(temporary);
        }
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 3) {
            throw new IllegalArgumentException(
                "usage: ExportAirfixFunction.java <output-path> " +
                "<FN-MODULE-RVA> <rva>");
        }
        if (!arguments[1].matches("FN-[A-Z0-9][A-Z0-9_-]*-[0-9A-F]{8}")) {
            throw new IllegalArgumentException(
                "function ID must use canonical FN-MODULE-RVA form");
        }
        long requestedRva = parseRva(arguments[2]);
        String expectedSuffix = arguments[1].substring(arguments[1].length() - 8);
        if (!expectedSuffix.equals(formatRva(requestedRva).substring(2))) {
            throw new IllegalArgumentException(
                "function ID suffix must match the requested RVA");
        }
        Address entry = currentProgram.getImageBase().add(requestedRva);
        Function function = currentProgram.getFunctionManager().getFunctionAt(entry);
        if (function == null || function.isExternal()) {
            throw new IllegalStateException(
                "unresolvedAddresses=1\nunresolved=" + formatRva(requestedRva));
        }

        JsonObject root = new JsonObject();
        root.addProperty("schema", SCHEMA);
        JsonObject source = new JsonObject();
        source.addProperty(
            "sha256", canonicalSha256(currentProgram.getExecutableSHA256()));
        root.add("source", source);
        JsonObject binary = new JsonObject();
        binary.addProperty("language", currentProgram.getLanguageID().toString());
        binary.addProperty("compiler", currentProgram.getCompilerSpec().getCompilerSpecID().toString());
        binary.addProperty("image_base", currentProgram.getImageBase().toString());
        binary.addProperty("format", currentProgram.getExecutableFormat());
        root.add("binary", binary);
        JsonObject tools = new JsonObject();
        JsonObject ghidra = new JsonObject();
        ghidra.addProperty("version", Application.getApplicationVersion());
        tools.add("ghidra", ghidra);
        root.add("tools", tools);
        JsonArray functions = new JsonArray();
        functions.add(functionReport(function, arguments[1]));
        root.add("functions", functions);

        root = redactHostPaths(root).getAsJsonObject();
        writeAtomic(new File(arguments[0]).getAbsoluteFile(), root);
        println("Exported Airfix function report for " + arguments[1]);
    }
}
