// Applies only explicitly confirmed, evidence-backed Airfix Knowledge Bridge
// records to the current Ghidra program. Module identity is SHA-256 plus RVA;
// local paths and unreviewed hypotheses are rejected or skipped.

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.IOException;
import java.io.StringReader;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.channels.SeekableByteChannel;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Pattern;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.EnumDataType;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.ReturnParameterImpl;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.util.data.DataTypeParser;

public class ApplyAirfixKnowledge extends GhidraScript {
    private static final String SCHEMA = "airfix.re.ghidra-knowledge.v1";
    private static final CategoryPath CATEGORY =
        new CategoryPath("/AirfixKnowledge");
    private static final long MAX_RVA = 0xffffffffL;
    private static final long MAX_INPUT_BYTES = 32L * 1024L * 1024L;
    private static final int MAX_RECORDS = 100_000;
    private static final int MAX_IDENTIFIER_CHARS = 160;
    private static final int MAX_SUMMARY_CHARS = 1_000;
    private static final int MAX_NOTE_CHARS = 4_000;
    private static final int MAX_VTABLE_SLOT_INDEX = 4_095;
    private static final int MAX_STRUCTURE_SIZE = 0x00ff_ffff;
    private static final int MAX_JSON_DEPTH = 128;
    private static final Pattern LOCAL_PATH_PATTERN = Pattern.compile(
        "(?i)(?:[A-Z]:[\\\\/]|\\\\\\\\|(?:^|[\\s\\\"'])" +
        "/(?:[A-Za-z0-9._+@=-]+/)*[A-Za-z0-9._+@=-]+" +
        "[^\\s\\\"'<>|]*)");

    private int appliedFunctions;
    private int appliedStructures;
    private int appliedFields;
    private int appliedVtables;
    private int appliedSlots;
    private int appliedEvents;
    private int skippedRecords;

    private static JsonObject object(JsonElement element, String field) {
        if (element == null || !element.isJsonObject()) {
            throw new IllegalArgumentException(field + " must be an object");
        }
        return element.getAsJsonObject();
    }

    private static JsonArray array(JsonObject parent, String field) {
        JsonElement value = parent.get(field);
        if (value == null || !value.isJsonArray()) {
            throw new IllegalArgumentException(field + " must be an array");
        }
        return value.getAsJsonArray();
    }

    private static String string(JsonObject parent, String field) {
        JsonElement value = parent.get(field);
        if (value == null || !value.isJsonPrimitive() ||
            !value.getAsJsonPrimitive().isString()) {
            throw new IllegalArgumentException(field + " must be a string");
        }
        return safeText(value.getAsString(), field, true);
    }

    private static String optionalString(JsonObject parent, String field) {
        if (!parent.has(field) || parent.get(field).isJsonNull()) return null;
        return string(parent, field);
    }

    private static String boundedString(
            JsonObject parent, String field, int maximum, boolean identifier) {
        JsonElement value = parent.get(field);
        if (value == null || !value.isJsonPrimitive() ||
            !value.getAsJsonPrimitive().isString()) {
            throw new IllegalArgumentException(field + " must be a string");
        }
        String text = safeText(value.getAsString(), field, identifier);
        if (text.codePointCount(0, text.length()) > maximum) {
            throw new IllegalArgumentException(field + " exceeds its text limit");
        }
        return text;
    }

    private static String safeText(
            String value, String field, boolean identifier) {
        if (value == null || value.indexOf('\0') >= 0 ||
            (identifier && (value.indexOf('\n') >= 0 || value.indexOf('\r') >= 0))) {
            throw new IllegalArgumentException(
                field + " contains an invalid control character");
        }
        int maximum = identifier ? MAX_IDENTIFIER_CHARS : 1_000_000;
        if (value.codePointCount(0, value.length()) > maximum) {
            throw new IllegalArgumentException(field + " exceeds its text limit");
        }
        if (LOCAL_PATH_PATTERN.matcher(value).find()) {
            throw new IllegalArgumentException(
                field + " contains a local filesystem path");
        }
        return value;
    }

    private static int exactInteger(
            JsonElement value, String field, int minimum, int maximum) {
        if (value == null || !value.isJsonPrimitive() ||
            !value.getAsJsonPrimitive().isNumber()) {
            throw new IllegalArgumentException(field + " must be an integer");
        }
        String raw = value.getAsString();
        if (!raw.matches("-?(?:0|[1-9][0-9]*)")) {
            throw new IllegalArgumentException(field + " must be an integer");
        }
        BigInteger integer = new BigInteger(raw);
        if (integer.compareTo(BigInteger.valueOf(minimum)) < 0 ||
            integer.compareTo(BigInteger.valueOf(maximum)) > 0) {
            throw new IllegalArgumentException(field + " is outside its allowed range");
        }
        return integer.intValueExact();
    }

    private static void onlyKeys(
            JsonObject value, String field, String... allowedNames) {
        Set<String> allowed = new HashSet<>(Arrays.asList(allowedNames));
        for (String key : value.keySet()) {
            if (!allowed.contains(key)) {
                throw new IllegalArgumentException(
                    field + " contains unknown field: " + key);
            }
        }
    }

    private static void validateNoPrivateText(JsonElement value, String field) {
        if (value == null || value.isJsonNull()) return;
        if (value.isJsonPrimitive()) {
            if (value.getAsJsonPrimitive().isString()) {
                safeText(value.getAsString(), field, false);
            }
            return;
        }
        if (value.isJsonArray()) {
            int index = 0;
            for (JsonElement item : value.getAsJsonArray()) {
                validateNoPrivateText(item, field + "[" + index++ + "]");
            }
            return;
        }
        for (String key : value.getAsJsonObject().keySet()) {
            validateNoPrivateText(value.getAsJsonObject().get(key), field + "." + key);
        }
    }

    private static long parseHex32(String value, String field) {
        if (!value.matches("0x[0-9A-F]{8}")) {
            throw new IllegalArgumentException(
                field + " must use canonical 0x00000000 form");
        }
        return Long.parseUnsignedLong(value.substring(2), 16);
    }

    private Address address(long rva) {
        return currentProgram.getImageBase().add(rva);
    }

    private static boolean applyEligible(JsonObject owner, String field) {
        JsonObject assessment = object(owner.get("assessment"), field + ".assessment");
        onlyKeys(assessment, field + ".assessment",
            "status", "confidence", "evidence", "apply_eligible");
        String status = string(assessment, "status");
        if (!Set.of("confirmed", "conditional", "hypothesis", "unknown")
                .contains(status)) {
            throw new IllegalArgumentException(
                field + ".assessment.status is invalid");
        }
        int confidence = exactInteger(
            assessment.get("confidence"),
            field + ".assessment.confidence", 0, 3);
        JsonArray evidence = array(assessment, "evidence");
        for (JsonElement entry : evidence) {
            JsonObject item = object(entry, field + ".assessment.evidence[]");
            onlyKeys(item, field + ".assessment.evidence[]", "id", "summary");
            String id = string(item, "id");
            if (!id.matches("EV-[0-9]{8}-[0-9]{3}")) {
                throw new IllegalArgumentException(
                    field + " has a non-canonical evidence ID");
            }
            boundedString(
                item, "summary", MAX_SUMMARY_CHARS, false);
        }
        JsonElement eligibleValue = assessment.get("apply_eligible");
        if (eligibleValue == null || !eligibleValue.isJsonPrimitive() ||
            !eligibleValue.getAsJsonPrimitive().isBoolean()) {
            throw new IllegalArgumentException(
                field + ".assessment.apply_eligible must be boolean");
        }
        boolean eligible = eligibleValue.getAsBoolean();
        if (eligible && (!"confirmed".equals(status) || confidence < 2 ||
            evidence.isEmpty())) {
            throw new IllegalArgumentException(
                field + " is apply-eligible without confirmed confidence>=2 evidence");
        }
        return eligible;
    }

    private static void validateNotes(JsonObject owner, String field) {
        JsonObject notes = object(owner.get("notes"), field + ".notes");
        onlyKeys(notes, field + ".notes", "comments", "hypotheses");
        for (String arrayName : List.of("comments", "hypotheses")) {
            for (JsonElement entry : array(notes, arrayName)) {
                if (!entry.isJsonPrimitive() ||
                    !entry.getAsJsonPrimitive().isString()) {
                    throw new IllegalArgumentException(
                        field + ".notes." + arrayName + " must contain strings");
                }
                safeText(entry.getAsString(), field + ".notes." + arrayName, false);
                if (entry.getAsString().codePointCount(
                        0, entry.getAsString().length()) > MAX_NOTE_CHARS) {
                    throw new IllegalArgumentException(
                        field + ".notes." + arrayName + " exceeds its text limit");
                }
            }
        }
    }

    private static String knowledgeComment(JsonObject record, String title) {
        JsonObject assessment = record.getAsJsonObject("assessment");
        StringBuilder text = new StringBuilder("Airfix Knowledge Bridge: ")
            .append(title).append('\n');
        text.append("confidence=").append(assessment.get("confidence").getAsInt());
        JsonArray evidence = assessment.getAsJsonArray("evidence");
        if (!evidence.isEmpty()) {
            text.append(" evidence=");
            for (int index = 0; index < evidence.size(); ++index) {
                if (index != 0) text.append(',');
                text.append(evidence.get(index).getAsJsonObject()
                    .get("id").getAsString());
            }
        }
        JsonObject notes = record.getAsJsonObject("notes");
        for (JsonElement comment : notes.getAsJsonArray("comments")) {
            text.append('\n').append(comment.getAsString());
        }
        return text.toString();
    }

    private DataType parseDataType(String text) throws Exception {
        DataTypeManager manager = currentProgram.getDataTypeManager();
        DataTypeParser parser = new DataTypeParser(
            manager, manager, null, DataTypeParser.AllowedDataTypes.ALL);
        return parser.parse(safeText(text, "data type", true));
    }

    private Namespace namespaceFor(String qualifiedName) throws Exception {
        String[] parts = qualifiedName.split("::", -1);
        if (parts.length < 2) return currentProgram.getGlobalNamespace();
        Namespace namespace = currentProgram.getGlobalNamespace();
        SymbolTable table = currentProgram.getSymbolTable();
        for (int index = 0; index + 1 < parts.length; ++index) {
            if (parts[index].isBlank()) {
                throw new IllegalArgumentException("function namespace is empty");
            }
            namespace = table.getOrCreateNameSpace(
                namespace, parts[index], SourceType.USER_DEFINED);
        }
        return namespace;
    }

    private static String leafName(String qualifiedName) {
        int delimiter = qualifiedName.lastIndexOf("::");
        return delimiter < 0 ? qualifiedName : qualifiedName.substring(delimiter + 2);
    }

    private void createStructures(JsonArray records) throws Exception {
        DataTypeManager manager = currentProgram.getDataTypeManager();
        for (int index = 0; index < records.size(); ++index) {
            JsonObject record = object(records.get(index), "structures[]");
            onlyKeys(record, "structures[]",
                "name", "size", "fields", "assessment", "notes");
            String name = string(record, "name");
            long sizeValue = parseHex32(string(record, "size"), "size");
            if (sizeValue <= 0 || sizeValue > MAX_STRUCTURE_SIZE) {
                throw new IllegalArgumentException(
                    "structure size is outside the allowed range");
            }
            int size = Math.toIntExact(sizeValue);
            array(record, "fields");
            validateNotes(record, "structures[]");
            if (!applyEligible(record, "structures[]")) {
                skippedRecords++;
                continue;
            }
            DataType existing = manager.getDataType(CATEGORY, name);
            if (existing == null) {
                StructureDataType proposed =
                    new StructureDataType(CATEGORY, name, size, manager);
                existing = manager.addDataType(
                    proposed, DataTypeConflictHandler.KEEP_HANDLER);
            }
            if (!(existing instanceof Structure) || existing.getLength() != size) {
                throw new IllegalStateException(
                    "structure conflicts with existing data type: " + name);
            }
            appliedStructures++;
        }
    }

    private void applyStructureFields(JsonArray records) throws Exception {
        DataTypeManager manager = currentProgram.getDataTypeManager();
        for (JsonElement element : records) {
            JsonObject record = element.getAsJsonObject();
            if (!record.getAsJsonObject("assessment")
                    .get("apply_eligible").getAsBoolean()) continue;
            String name = record.get("name").getAsString();
            Structure structure = (Structure)manager.getDataType(CATEGORY, name);
            JsonArray fields = record.getAsJsonArray("fields");
            for (JsonElement fieldElement : fields) {
                JsonObject field = object(fieldElement, "structures[].fields[]");
                onlyKeys(field, "structures[].fields[]",
                    "offset", "name", "type", "assessment", "notes");
                long offsetValue = parseHex32(
                    string(field, "offset"), "field offset");
                if (offsetValue >= structure.getLength()) {
                    throw new IllegalArgumentException(
                        "structure field offset is outside the structure");
                }
                String fieldName = string(field, "name");
                String typeName = string(field, "type");
                validateNotes(field, "structures[].fields[]");
                if (!applyEligible(field, "structures[].fields[]")) {
                    skippedRecords++;
                    continue;
                }
                DataType type = parseDataType(typeName);
                int offset = Math.toIntExact(offsetValue);
                long fieldEndValue = offsetValue + (long)type.getLength();
                if (fieldEndValue > structure.getLength()) {
                    throw new IllegalArgumentException(
                        "structure field extends beyond the declared size");
                }
                int fieldEnd = Math.toIntExact(fieldEndValue);
                boolean alreadyEquivalent = false;
                for (DataTypeComponent existing : structure.getDefinedComponents()) {
                    int existingStart = existing.getOffset();
                    int existingEnd = existingStart + existing.getLength();
                    if (existingStart >= fieldEnd || existingEnd <= offset) continue;
                    if (existingStart == offset && existingEnd == fieldEnd &&
                        existing.getDataType().isEquivalent(type) &&
                        fieldName.equals(existing.getFieldName())) {
                        alreadyEquivalent = true;
                        continue;
                    }
                    throw new IllegalStateException(
                        "structure field overlaps existing data at " +
                        name + "+" + offset);
                }
                if (alreadyEquivalent) continue;
                structure.replaceAtOffset(
                    offset, type, type.getLength(), fieldName,
                    knowledgeComment(field, name + "+" + string(field, "offset")));
                appliedFields++;
            }
        }
    }

    private void validateThisFields(JsonObject functionRecord) {
        for (JsonElement element : array(functionRecord, "this_fields")) {
            JsonObject field = object(element, "functions[].this_fields[]");
            onlyKeys(field, "functions[].this_fields[]",
                "offset", "name", "type", "assessment", "notes");
            parseHex32(string(field, "offset"), "this field offset");
            string(field, "name");
            string(field, "type");
            validateNotes(field, "functions[].this_fields[]");
            applyEligible(field, "functions[].this_fields[]");
        }
    }

    private void applyFunctions(JsonArray records) throws Exception {
        for (JsonElement element : records) {
            JsonObject record = object(element, "functions[]");
            onlyKeys(record, "functions[]",
                "rva", "name", "signature", "this_fields",
                "assessment", "notes");
            long rva = parseHex32(string(record, "rva"), "function RVA");
            String qualifiedName = string(record, "name");
            if (leafName(qualifiedName).isBlank()) {
                throw new IllegalArgumentException("function name is empty");
            }
            validateThisFields(record);
            validateNotes(record, "functions[]");
            JsonObject signature = object(record.get("signature"), "signature");
            onlyKeys(signature, "signature",
                "calling_convention", "return_type", "arguments");
            String convention = string(signature, "calling_convention");
            String returnTypeName = string(signature, "return_type");
            JsonArray arguments = array(signature, "arguments");
            if (!applyEligible(record, "functions[]")) {
                skippedRecords++;
                continue;
            }
            Address entry = address(rva);
            Function function = currentProgram.getFunctionManager()
                .getFunctionAt(entry);
            if (function == null) {
                Function containing = currentProgram.getFunctionManager()
                    .getFunctionContaining(entry);
                if (containing != null) {
                    throw new IllegalStateException(
                        "confirmed function entry is inside another function at " +
                        string(record, "rva"));
                }
                ghidra.program.model.mem.MemoryBlock block =
                    currentProgram.getMemory().getBlock(entry);
                if (block == null || !block.isExecute()) {
                    throw new IllegalStateException(
                        "confirmed function is outside executable memory at " +
                        string(record, "rva"));
                }
                function = createFunction(entry, null);
            }
            if (function == null || function.isExternal()) {
                throw new IllegalStateException(
                    "confirmed function could not be created at " +
                    string(record, "rva"));
            }
            DataType returnType = parseDataType(returnTypeName);
            List<Variable> parameters = new ArrayList<>();
            List<String> requestedStorages = new ArrayList<>();
            for (JsonElement argumentElement : arguments) {
                JsonObject argument = object(argumentElement, "signature.arguments[]");
                onlyKeys(argument, "signature.arguments[]", "name", "type", "storage");
                String parameterName = string(argument, "name");
                DataType parameterType = parseDataType(string(argument, "type"));
                requestedStorages.add(optionalString(argument, "storage"));
                parameters.add(new ParameterImpl(
                    parameterName, parameterType, currentProgram,
                    SourceType.USER_DEFINED));
            }
            function.updateFunction(
                convention,
                new ReturnParameterImpl(returnType, currentProgram),
                parameters,
                Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
                true,
                SourceType.USER_DEFINED);
            Parameter[] appliedParameters = function.getParameters();
            if (appliedParameters.length != requestedStorages.size()) {
                throw new IllegalStateException(
                    "applied function parameter count disagrees with knowledge");
            }
            for (int index = 0; index < appliedParameters.length; ++index) {
                String requestedStorage = requestedStorages.get(index);
                if (requestedStorage != null && !requestedStorage.equals(
                        appliedParameters[index].getVariableStorage().toString())) {
                    throw new IllegalStateException(
                        "applied parameter storage disagrees with knowledge");
                }
            }
            function.setParentNamespace(namespaceFor(qualifiedName));
            function.setName(leafName(qualifiedName), SourceType.USER_DEFINED);
            function.setRepeatableComment(
                knowledgeComment(record, string(record, "rva")));
            appliedFunctions++;
        }
    }

    private long readPointer(Address address) throws Exception {
        int pointerSize = currentProgram.getDefaultPointerSize();
        if (pointerSize != 4) {
            throw new IllegalStateException(
                "vtable application currently requires a 32-bit program");
        }
        return currentProgram.getMemory().getInt(address) & 0xffffffffL;
    }

    private void applyVtables(JsonArray records) throws Exception {
        SymbolTable symbols = currentProgram.getSymbolTable();
        for (JsonElement element : records) {
            JsonObject record = object(element, "vtables[]");
            onlyKeys(record, "vtables[]",
                "rva", "class_name", "slots", "assessment", "notes");
            long tableRva = parseHex32(string(record, "rva"), "vtable RVA");
            String className = string(record, "class_name");
            JsonArray slots = array(record, "slots");
            validateNotes(record, "vtables[]");
            if (!applyEligible(record, "vtables[]")) {
                skippedRecords++;
                continue;
            }
            Address tableAddress = address(tableRva);
            String labelName = "vtable_" + className.replaceAll("[^A-Za-z0-9_]", "_");
            Symbol existing = symbols.getSymbol(
                labelName, tableAddress, currentProgram.getGlobalNamespace());
            if (existing == null) {
                symbols.createLabel(
                    tableAddress, labelName, SourceType.USER_DEFINED);
            }
            currentProgram.getListing().setComment(
                tableAddress, ghidra.program.model.listing.CodeUnit.PLATE_COMMENT,
                knowledgeComment(record, className + " vtable"));
            appliedVtables++;
            Set<Integer> seenIndexes = new HashSet<>();
            for (JsonElement slotElement : slots) {
                JsonObject slot = object(slotElement, "vtables[].slots[]");
                onlyKeys(slot, "vtables[].slots[]",
                    "index", "target_rva", "name", "assessment", "notes");
                int slotIndex = exactInteger(
                    slot.get("index"), "vtable slot index", 0,
                    MAX_VTABLE_SLOT_INDEX);
                if (!seenIndexes.add(slotIndex)) {
                    throw new IllegalArgumentException(
                        "vtable slot index is duplicated");
                }
                long targetRva = parseHex32(
                    string(slot, "target_rva"), "vtable target RVA");
                optionalString(slot, "name");
                validateNotes(slot, "vtables[].slots[]");
                if (!applyEligible(slot, "vtables[].slots[]")) {
                    skippedRecords++;
                    continue;
                }
                Address slotAddress = tableAddress.add((long)slotIndex * 4L);
                long storedPointer = readPointer(slotAddress);
                long expectedPointer = address(targetRva).getOffset() & 0xffffffffL;
                if (storedPointer != expectedPointer) {
                    throw new IllegalStateException(
                        "vtable slot bytes disagree with confirmed target");
                }
                Address target = address(targetRva);
                Function targetFunction = currentProgram.getFunctionManager()
                    .getFunctionAt(target);
                if (targetFunction == null) {
                    throw new IllegalStateException(
                        "confirmed vtable target is not a function");
                }
                currentProgram.getReferenceManager().addMemoryReference(
                    slotAddress, target, RefType.DATA,
                    SourceType.USER_DEFINED, 0);
                currentProgram.getListing().setComment(
                    slotAddress, ghidra.program.model.listing.CodeUnit.EOL_COMMENT,
                    knowledgeComment(slot, className + " slot " + slotIndex));
                appliedSlots++;
            }
        }
    }

    private void applyEvents(JsonArray records) throws Exception {
        List<JsonObject> eligibleRecords = new ArrayList<>();
        for (JsonElement element : records) {
            JsonObject record = object(element, "event_ids[]");
            onlyKeys(record, "event_ids[]",
                "id", "name", "assessment", "notes");
            parseHex32(string(record, "id"), "event ID");
            string(record, "name");
            validateNotes(record, "event_ids[]");
            if (applyEligible(record, "event_ids[]")) {
                eligibleRecords.add(record);
            }
            else {
                skippedRecords++;
            }
        }
        if (eligibleRecords.isEmpty()) return;

        DataTypeManager manager = currentProgram.getDataTypeManager();
        DataType existing = manager.getDataType(CATEGORY, "AirfixEventId");
        ghidra.program.model.data.Enum eventEnum;
        if (existing == null) {
            eventEnum = (ghidra.program.model.data.Enum)manager.addDataType(
                new EnumDataType(CATEGORY, "AirfixEventId", 4, manager),
                DataTypeConflictHandler.KEEP_HANDLER);
        }
        else if (existing instanceof ghidra.program.model.data.Enum) {
            eventEnum = (ghidra.program.model.data.Enum)existing;
        }
        else {
            throw new IllegalStateException(
                "AirfixEventId conflicts with a non-enum data type");
        }
        for (JsonObject record : eligibleRecords) {
            long id = parseHex32(string(record, "id"), "event ID");
            String name = string(record, "name");
            if (eventEnum.contains(name)) {
                if (eventEnum.getValue(name) != id) {
                    throw new IllegalStateException(
                        "event name conflicts with existing value");
                }
                continue;
            }
            eventEnum.add(name, id, knowledgeComment(record, string(record, "id")));
            appliedEvents++;
        }
    }

    private static void validateStrictJson(JsonReader reader, int depth)
            throws IOException {
        if (depth > MAX_JSON_DEPTH) {
            throw new IllegalArgumentException(
                "knowledge JSON exceeds the nesting-depth limit");
        }
        JsonToken token = reader.peek();
        switch (token) {
            case BEGIN_OBJECT:
                reader.beginObject();
                Set<String> names = new HashSet<>();
                while (reader.hasNext()) {
                    String name = reader.nextName();
                    if (!names.add(name)) {
                        throw new IllegalArgumentException(
                            "knowledge JSON contains duplicate object key: " + name);
                    }
                    validateStrictJson(reader, depth + 1);
                }
                reader.endObject();
                return;
            case BEGIN_ARRAY:
                reader.beginArray();
                while (reader.hasNext()) {
                    validateStrictJson(reader, depth + 1);
                }
                reader.endArray();
                return;
            case STRING:
                reader.nextString();
                return;
            case NUMBER:
                String number = reader.nextString();
                if (!number.matches("-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?" +
                        "(?:[eE][+-]?[0-9]+)?")) {
                    throw new IllegalArgumentException(
                        "knowledge JSON contains an invalid or non-finite number");
                }
                return;
            case BOOLEAN:
                reader.nextBoolean();
                return;
            case NULL:
                reader.nextNull();
                return;
            default:
                throw new IllegalArgumentException(
                    "knowledge JSON contains an unexpected token: " + token);
        }
    }

    private JsonObject readKnowledge(File input) throws Exception {
        Path path = input.toPath();
        if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS) ||
                Files.isSymbolicLink(path)) {
            throw new IllegalArgumentException(
                "knowledge input must be a regular non-symlink file");
        }
        ByteBuffer bytes = ByteBuffer.allocate(
            Math.toIntExact(MAX_INPUT_BYTES + 1));
        try (SeekableByteChannel channel = Files.newByteChannel(
                path, Set.of(StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS))) {
            while (bytes.hasRemaining() && channel.read(bytes) != -1) {
                // Continue until EOF or the bounded buffer proves oversize input.
            }
        }
        if (bytes.position() <= 0 || bytes.position() > MAX_INPUT_BYTES) {
            throw new IllegalArgumentException(
                "knowledge input size is outside the allowed range");
        }
        bytes.flip();
        String json;
        try {
            json = StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(bytes).toString();
        }
        catch (java.nio.charset.CharacterCodingException error) {
            throw new IllegalArgumentException(
                "knowledge input must be valid UTF-8", error);
        }
        try (StringReader buffered = new StringReader(json)) {
            JsonReader reader = new JsonReader(buffered);
            reader.setLenient(false);
            validateStrictJson(reader, 0);
            if (reader.peek() != JsonToken.END_DOCUMENT) {
                throw new IllegalArgumentException(
                    "knowledge JSON contains trailing data");
            }
        }
        try (StringReader reader = new StringReader(json)) {
            return object(JsonParser.parseReader(reader), "knowledge root");
        }
    }

    private JsonObject summary(String sha256) {
        JsonObject root = new JsonObject();
        root.addProperty("schema", "airfix.re.ghidra-apply-result.v1");
        JsonObject source = new JsonObject();
        source.addProperty("sha256", sha256);
        root.add("source", source);
        JsonObject counts = new JsonObject();
        counts.addProperty("functions", appliedFunctions);
        counts.addProperty("structures", appliedStructures);
        counts.addProperty("fields", appliedFields);
        counts.addProperty("vtables", appliedVtables);
        counts.addProperty("vtable_slots", appliedSlots);
        counts.addProperty("event_ids", appliedEvents);
        counts.addProperty("skipped", skippedRecords);
        root.add("applied", counts);
        return root;
    }

    private static void writeAtomic(File output, JsonObject document)
            throws IOException {
        File parentFile = output.getAbsoluteFile().getParentFile();
        if (parentFile == null) {
            throw new IllegalArgumentException("output must have a parent directory");
        }
        Files.createDirectories(parentFile.toPath());
        Path temporary = Files.createTempFile(
            parentFile.toPath(), ".airfix-ghidra-apply-", ".partial");
        boolean published = false;
        try {
            Gson gson = new GsonBuilder().disableHtmlEscaping()
                .setPrettyPrinting().create();
            try (BufferedWriter writer = Files.newBufferedWriter(
                    temporary, StandardCharsets.UTF_8)) {
                gson.toJson(document, writer);
                writer.write('\n');
            }
            try {
                Files.move(temporary, output.toPath(),
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
            }
            catch (AtomicMoveNotSupportedException error) {
                throw new IllegalStateException(
                    "atomic apply-result publication is not supported", error);
            }
            published = true;
        }
        finally {
            if (!published) Files.deleteIfExists(temporary);
        }
    }

    private static void moveAtomic(Path source, Path destination) throws IOException {
        try {
            Files.move(source, destination,
                StandardCopyOption.ATOMIC_MOVE,
                StandardCopyOption.REPLACE_EXISTING);
        }
        catch (AtomicMoveNotSupportedException error) {
            throw new IllegalStateException(
                "atomic apply-result publication is not supported", error);
        }
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 2) {
            throw new IllegalArgumentException(
                "usage: ApplyAirfixKnowledge.java <output-path> " +
                "<knowledge-json>");
        }
        JsonObject root = readKnowledge(new File(arguments[1]).getAbsoluteFile());
        validateNoPrivateText(root, "knowledge root");
        onlyKeys(root, "knowledge root", "schema", "modules");
        if (!SCHEMA.equals(string(root, "schema"))) {
            throw new IllegalArgumentException("knowledge schema is invalid");
        }
        String programSha = currentProgram.getExecutableSHA256();
        if (programSha == null || !programSha.matches("(?i)[0-9a-f]{64}")) {
            throw new IllegalStateException(
                "current program has no canonical executable SHA-256");
        }
        programSha = programSha.toLowerCase(Locale.ROOT);
        JsonObject selected = null;
        int recordCount = 0;
        for (JsonElement element : array(root, "modules")) {
            JsonObject module = object(element, "modules[]");
            onlyKeys(module, "modules[]",
                "sha256", "functions", "structures", "vtables", "event_ids");
            String sha = string(module, "sha256");
            if (!sha.matches("[0-9a-f]{64}")) {
                throw new IllegalArgumentException(
                    "module SHA-256 must be canonical lowercase hex");
            }
            recordCount += array(module, "functions").size();
            recordCount += array(module, "structures").size();
            recordCount += array(module, "vtables").size();
            recordCount += array(module, "event_ids").size();
            if (sha.equals(programSha)) {
                if (selected != null) {
                    throw new IllegalArgumentException(
                        "knowledge contains duplicate module SHA-256");
                }
                selected = module;
            }
        }
        if (recordCount > MAX_RECORDS) {
            throw new IllegalArgumentException("knowledge contains too many records");
        }
        if (selected == null) {
            throw new IllegalStateException(
                "knowledge has no module matching the current SHA-256");
        }

        File output = new File(arguments[0]).getAbsoluteFile();
        File outputParent = output.getParentFile();
        if (outputParent == null) {
            throw new IllegalArgumentException("output must have a parent directory");
        }
        Files.createDirectories(outputParent.toPath());
        Path stagedResult = Files.createTempFile(
            outputParent.toPath(), ".airfix-ghidra-apply-staged-", ".json");
        Path previousResult = null;
        boolean newResultPublished = false;
        int transaction = currentProgram.startTransaction(
            "Apply Airfix confirmed knowledge");
        boolean transactionEnded = false;
        try {
            JsonArray structures = selected.getAsJsonArray("structures");
            createStructures(structures);
            applyStructureFields(structures);
            applyEvents(selected.getAsJsonArray("event_ids"));
            applyFunctions(selected.getAsJsonArray("functions"));
            applyVtables(selected.getAsJsonArray("vtables"));
            writeAtomic(stagedResult.toFile(), summary(programSha));
            if (Files.exists(output.toPath())) {
                if (Files.isSymbolicLink(output.toPath())) {
                    throw new IllegalArgumentException(
                        "apply-result output must not be a symbolic link");
                }
                previousResult = Files.createTempFile(
                    outputParent.toPath(),
                    ".airfix-ghidra-apply-previous-", ".json");
                Files.delete(previousResult);
                moveAtomic(output.toPath(), previousResult);
            }
            moveAtomic(stagedResult, output.toPath());
            newResultPublished = true;
            currentProgram.endTransaction(transaction, true);
            transactionEnded = true;
        }
        finally {
            if (!transactionEnded) {
                try {
                    currentProgram.endTransaction(transaction, false);
                }
                finally {
                    if (newResultPublished) {
                        Files.deleteIfExists(output.toPath());
                    }
                    if (previousResult != null && Files.exists(previousResult)) {
                        moveAtomic(previousResult, output.toPath());
                    }
                }
            }
            if (transactionEnded && previousResult != null) {
                Files.deleteIfExists(previousResult);
            }
            Files.deleteIfExists(stagedResult);
        }
        println("Applied Airfix knowledge: functions=" + appliedFunctions +
            " structures=" + appliedStructures + " fields=" + appliedFields +
            " vtables=" + appliedVtables + " slots=" + appliedSlots +
            " events=" + appliedEvents + " skipped=" + skippedRecords);
    }
}
