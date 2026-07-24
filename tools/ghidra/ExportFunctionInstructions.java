// Exports deterministic instruction listings for functions containing
// explicitly supplied addresses. Generated output is local evidence and
// belongs under the ignored artifacts/ tree.

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class ExportFunctionInstructions extends GhidraScript {
    private static Address parseAddress(
            GhidraScript script, String argument) {
        String text = argument;
        if (text.startsWith("0x") || text.startsWith("0X")) {
            text = text.substring(2);
        }
        try {
            return script.toAddr(Long.parseUnsignedLong(text, 16));
        }
        catch (RuntimeException error) {
            throw new IllegalArgumentException(
                "invalid hexadecimal address: " + argument, error);
        }
    }

    private static String hexBytes(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) {
            result.append(String.format("%02x", value & 0xff));
        }
        return result.toString();
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportFunctionInstructions.java <output-path> " +
                "<address> [address ...]");
        }

        File output = new File(arguments[0]).getAbsoluteFile();
        File parent = output.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException(
                "cannot create output directory: " + parent);
        }

        FunctionManager functionManager = currentProgram.getFunctionManager();
        Map<Address, Function> selectedByEntry = new LinkedHashMap<>();
        List<String> unresolved = new ArrayList<>();
        for (int index = 1; index < arguments.length; ++index) {
            monitor.checkCancelled();
            Address address = parseAddress(this, arguments[index]);
            Function function = functionManager.getFunctionContaining(address);
            if (function == null || function.isExternal()) {
                unresolved.add(arguments[index]);
            }
            else {
                selectedByEntry.put(function.getEntryPoint(), function);
            }
        }

        List<Function> selected =
            new ArrayList<>(selectedByEntry.values());
        selected.sort(Comparator.comparing(Function::getEntryPoint));

        try (PrintWriter writer = new PrintWriter(new OutputStreamWriter(
                new FileOutputStream(output), StandardCharsets.UTF_8))) {
            writer.println("program=" + currentProgram.getName());
            writer.println("language=" + currentProgram.getLanguageID());
            writer.println("selectedFunctions=" + selected.size());
            writer.println("unresolvedAddresses=" + unresolved.size());
            for (String address : unresolved) {
                writer.println("unresolved=" + address);
            }

            for (Function function : selected) {
                monitor.checkCancelled();
                writer.println();
                writer.println(
                    "================================================================================");
                writer.println("entry=" + function.getEntryPoint());
                writer.println("name=" + function.getName(true));
                writer.println("signature=" + function.getSignature(true));

                InstructionIterator instructions =
                    currentProgram.getListing().getInstructions(
                        function.getBody(), true);
                while (instructions.hasNext()) {
                    monitor.checkCancelled();
                    Instruction instruction = instructions.next();
                    writer.println(
                        instruction.getAddress() + "|" +
                        hexBytes(instruction.getBytes()) + "|" +
                        instruction.toString());
                }
            }
        }

        println("Exported instruction listings for " + selected.size() +
            " functions to " + output);
        if (!unresolved.isEmpty()) {
            throw new IllegalStateException(
                "unresolved function addresses: " + unresolved);
        }
    }
}
