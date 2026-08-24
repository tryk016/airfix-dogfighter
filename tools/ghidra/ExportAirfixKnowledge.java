// Exports selected Ghidra function names and signatures into an unreviewed
// Airfix Knowledge Bridge document. The result is never apply-eligible until
// a researcher adds confirmed evidence outside Ghidra.

import java.io.BufferedWriter;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Parameter;

public class ExportAirfixKnowledge extends GhidraScript {
    private static final String SCHEMA = "airfix.re.ghidra-knowledge.v1";
    private static final long MAX_RVA = 0xffffffffL;
    private static final int MAX_REPORT_BYTES = 8 * 1024 * 1024;
    private static final int MAX_FUNCTIONS = 10_000;
    private static final int MAX_PARAMETERS = 4_096;
    private static final int MAX_TOTAL_PARAMETERS = 100_000;
    private static final int MAX_TOTAL_TEXT_CODE_POINTS = 1_000_000;
    private static final int MAX_IDENTIFIER_CODE_POINTS = 160;
    private static final int MAX_NOTE_CODE_POINTS = 4_000;
    private static final Pattern LOCAL_PATH_PATTERN = Pattern.compile(
        "(?i)(?:[A-Z]:[\\\\/]|\\\\\\\\|(?:^|[\\s\\\"'])" +
        "/(?:[A-Za-z0-9._+@=-]+/)*[A-Za-z0-9._+@=-]+" +
        "[^\\s\\\"'<>|]*)");

    private static int codePoints(String value) {
        return value == null ? 0 : value.codePointCount(0, value.length());
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
            throw new IllegalArgumentException("address is outside PE32 image");
        }
        return value;
    }

    private static String safeText(String value, String field, int maximum) {
        if (value == null) return null;
        if (value.indexOf('\0') >= 0 || value.indexOf('\r') >= 0 ||
            value.codePointCount(0, value.length()) > maximum ||
            LOCAL_PATH_PATTERN.matcher(value).find()) {
            throw new IllegalStateException(
                field + " contains a local path, control character, or excess text");
        }
        return value;
    }

    private static String safeIdentifier(String value, String field) {
        String result = safeText(value, field, MAX_IDENTIFIER_CODE_POINTS);
        if (result != null && result.indexOf('\n') >= 0) {
            throw new IllegalStateException(
                field + " contains a newline");
        }
        return result;
    }

    private static JsonObject assessment() {
        JsonObject result = new JsonObject();
        result.addProperty("status", "unknown");
        result.addProperty("confidence", 0);
        result.add("evidence", new JsonArray());
        result.addProperty("apply_eligible", false);
        return result;
    }

    private static JsonObject notes(Function function) {
        JsonObject notes = new JsonObject();
        JsonArray comments = new JsonArray();
        String comment = function.getComment();
        if (comment != null && !comment.isBlank()) {
            comments.add(safeText(
                comment, "function comment", MAX_NOTE_CODE_POINTS));
        }
        String repeatable = function.getRepeatableComment();
        if (repeatable != null && !repeatable.isBlank() &&
            !repeatable.equals(comment)) {
            comments.add(safeText(
                repeatable, "function repeatable comment", MAX_NOTE_CODE_POINTS));
        }
        notes.add("comments", comments);
        notes.add("hypotheses", new JsonArray());
        return notes;
    }

    private static JsonObject functionKnowledge(Function function, String rva) {
        JsonObject item = new JsonObject();
        item.addProperty("rva", rva);
        item.addProperty("name", safeIdentifier(
            function.getName(true), "function name"));
        JsonObject signature = new JsonObject();
        signature.addProperty(
            "calling_convention",
            safeIdentifier(function.getCallingConventionName(), "calling convention"));
        signature.addProperty(
            "return_type",
            safeIdentifier(function.getReturnType().getDisplayName(), "return type"));
        JsonArray arguments = new JsonArray();
        Parameter[] parameters = function.getParameters();
        if (parameters.length > MAX_PARAMETERS) {
            throw new IllegalStateException(
                "function exceeds the parameter limit");
        }
        for (Parameter parameter : parameters) {
            JsonObject argument = new JsonObject();
            argument.addProperty(
                "name", safeIdentifier(parameter.getName(), "parameter name"));
            argument.addProperty(
                "type", safeIdentifier(
                    parameter.getDataType().getDisplayName(), "parameter type"));
            argument.addProperty(
                "storage", safeIdentifier(
                    parameter.getVariableStorage().toString(), "parameter storage"));
            arguments.add(argument);
        }
        signature.add("arguments", arguments);
        item.add("signature", signature);
        item.add("this_fields", new JsonArray());
        item.add("assessment", assessment());
        item.add("notes", notes(function));
        return item;
    }

    private static void writeAtomic(File output, JsonObject document)
            throws IOException {
        File parentFile = output.getAbsoluteFile().getParentFile();
        if (parentFile == null) {
            throw new IllegalArgumentException("output must have a parent directory");
        }
        Files.createDirectories(parentFile.toPath());
        Path temporary = Files.createTempFile(
            parentFile.toPath(), ".airfix-ghidra-knowledge-", ".partial");
        boolean published = false;
        try {
            Gson gson = new GsonBuilder().disableHtmlEscaping()
                .setPrettyPrinting().create();
            try (BufferedWriter writer = Files.newBufferedWriter(
                    temporary, StandardCharsets.UTF_8)) {
                gson.toJson(document, writer);
                writer.write('\n');
            }
            long size = Files.size(temporary);
            if (size <= 0 || size > MAX_REPORT_BYTES) {
                throw new IllegalStateException(
                    "knowledge export size is outside the allowed range");
            }
            try {
                Files.move(temporary, output.toPath(),
                    StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
            }
            catch (AtomicMoveNotSupportedException error) {
                throw new IllegalStateException(
                    "atomic knowledge publication is not supported", error);
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
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportAirfixKnowledge.java <output-path> " +
                "<rva> [rva ...]");
        }
        if (arguments.length - 1 > MAX_FUNCTIONS) {
            throw new IllegalArgumentException(
                "knowledge export exceeds the function limit");
        }
        String sha256 = currentProgram.getExecutableSHA256();
        if (sha256 == null || !sha256.matches("(?i)[0-9a-f]{64}")) {
            throw new IllegalStateException(
                "current program has no canonical executable SHA-256");
        }

        List<Function> functions = new ArrayList<>();
        for (int index = 1; index < arguments.length; ++index) {
            long requestedRva = parseRva(arguments[index]);
            Address address = currentProgram.getImageBase().add(requestedRva);
            Function function = currentProgram.getFunctionManager().getFunctionAt(address);
            if (function == null || function.isExternal()) {
                throw new IllegalStateException(
                    "unresolvedAddresses=1\nunresolved=" + formatRva(requestedRva));
            }
            if (!functions.contains(function)) functions.add(function);
        }
        functions.sort(Comparator.comparing(Function::getEntryPoint));

        long totalParameters = 0;
        long totalText = 0;
        for (Function function : functions) {
            Parameter[] parameters = function.getParameters();
            totalParameters += parameters.length;
            totalText += codePoints(function.getName(true));
            totalText += codePoints(function.getCallingConventionName());
            totalText += codePoints(function.getReturnType().getDisplayName());
            totalText += codePoints(function.getComment());
            totalText += codePoints(function.getRepeatableComment());
            for (Parameter parameter : parameters) {
                totalText += codePoints(parameter.getName());
                totalText += codePoints(parameter.getDataType().getDisplayName());
                totalText += codePoints(parameter.getVariableStorage().toString());
            }
            if (totalParameters > MAX_TOTAL_PARAMETERS ||
                    totalText > MAX_TOTAL_TEXT_CODE_POINTS) {
                throw new IllegalStateException(
                    "knowledge export exceeds its aggregate content budget");
            }
        }

        JsonObject root = new JsonObject();
        root.addProperty("schema", SCHEMA);
        JsonArray modules = new JsonArray();
        JsonObject module = new JsonObject();
        module.addProperty("sha256", sha256.toLowerCase(Locale.ROOT));
        JsonArray functionItems = new JsonArray();
        for (Function function : functions) {
            functionItems.add(functionKnowledge(
                function, formatRva(rva(function.getEntryPoint()))));
        }
        module.add("functions", functionItems);
        module.add("structures", new JsonArray());
        module.add("vtables", new JsonArray());
        module.add("event_ids", new JsonArray());
        modules.add(module);
        root.add("modules", modules);
        writeAtomic(new File(arguments[0]).getAbsoluteFile(), root);
        println("Exported " + functions.size() +
            " unreviewed Airfix knowledge records");
    }
}
