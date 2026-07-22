// Decompiles the functions containing explicitly supplied addresses.

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

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

public class ExportAddressFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportAddressFunctions.java <output-path> <address> [address ...]");
        }

        File output = new File(arguments[0]).getAbsoluteFile();
        File parent = output.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException("cannot create output directory: " + parent);
        }

        FunctionManager functionManager = currentProgram.getFunctionManager();
        Map<Address, Function> selectedByEntry = new LinkedHashMap<>();
        List<String> unresolved = new ArrayList<>();
        for (int index = 1; index < arguments.length; ++index) {
            monitor.checkCancelled();
            String text = arguments[index];
            if (text.startsWith("0x") || text.startsWith("0X")) {
                text = text.substring(2);
            }
            Address address;
            try {
                address = toAddr(Long.parseUnsignedLong(text, 16));
            }
            catch (RuntimeException error) {
                throw new IllegalArgumentException(
                    "invalid hexadecimal address: " + arguments[index], error);
            }
            Function function = functionManager.getFunctionContaining(address);
            if (function == null || function.isExternal()) {
                unresolved.add(arguments[index]);
            }
            else {
                selectedByEntry.put(function.getEntryPoint(), function);
            }
        }

        List<Function> selected = new ArrayList<>(selectedByEntry.values());
        selected.sort(Comparator.comparing(Function::getEntryPoint));

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        decompiler.setSimplificationStyle("decompile");
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException(
                "decompiler failed to open program: " + decompiler.getLastMessage());
        }

        try (PrintWriter writer = new PrintWriter(new OutputStreamWriter(
                new FileOutputStream(output), StandardCharsets.UTF_8))) {
            writer.println("program=" + currentProgram.getName());
            writer.println("selectedFunctions=" + selected.size());
            writer.println("unresolvedAddresses=" + unresolved.size());
            for (String address : unresolved) {
                writer.println("unresolved=" + address);
            }

            for (Function function : selected) {
                monitor.checkCancelled();
                writer.println();
                writer.println("================================================================================");
                writer.println("entry=" + function.getEntryPoint());
                writer.println("name=" + function.getName(true));
                writer.println("signature=" + function.getSignature(true));
                DecompileResults result = decompiler.decompileFunction(function, 90, monitor);
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    writer.println(result.getDecompiledFunction().getC());
                }
                else {
                    writer.println("DECOMPILATION_FAILED=" + result.getErrorMessage());
                }
            }
        }
        finally {
            decompiler.dispose();
        }

        println("Exported " + selected.size() + " address-selected functions to " + output);
    }
}
