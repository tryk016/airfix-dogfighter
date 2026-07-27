// Finds selected strings, resolves their code xrefs, and decompiles containing functions.

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class ExportStringXrefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportStringXrefs.java <output-path> <pattern> [pattern ...]");
        }

        File output = new File(arguments[0]).getAbsoluteFile();
        File parent = output.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException("cannot create output directory: " + parent);
        }

        List<String> patterns = new ArrayList<>();
        for (int index = 1; index < arguments.length; ++index) {
            patterns.add(arguments[index].toLowerCase(Locale.ROOT));
        }

        FunctionManager functionManager = currentProgram.getFunctionManager();
        Map<Address, Function> functions = new LinkedHashMap<>();
        List<String> matches = new ArrayList<>();
        DataIterator strings = currentProgram.getListing().getDefinedData(true);
        while (strings.hasNext()) {
            monitor.checkCancelled();
            Data data = strings.next();
            Object value = data.getValue();
            if (!(value instanceof String)) {
                continue;
            }
            String text = (String)value;
            String lowered = text.toLowerCase(Locale.ROOT);
            boolean selected = patterns.stream().anyMatch(lowered::contains);
            if (!selected) {
                continue;
            }

            matches.add(data.getAddress() + "\t" + text.replace("\n", "\\n"));
            // MSVC code sometimes references an interior address (for example,
            // one byte past an adjacent string), so inspect the full data range.
            for (int offset = 0; offset < data.getLength(); ++offset) {
                ReferenceIterator references = currentProgram.getReferenceManager()
                    .getReferencesTo(data.getAddress().add(offset));
                while (references.hasNext()) {
                    Reference reference = references.next();
                    Function function = functionManager.getFunctionContaining(reference.getFromAddress());
                    if (function != null) {
                        functions.put(function.getEntryPoint(), function);
                    }
                }
            }
        }

        List<Function> ordered = new ArrayList<>(functions.values());
        ordered.sort(Comparator.comparing(Function::getEntryPoint));

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
            writer.println("matchedStrings=" + matches.size());
            writer.println("xrefFunctions=" + ordered.size());
            writer.println();
            writer.println("MATCHED STRINGS");
            for (String match : matches) {
                writer.println(match);
            }

            for (Function function : ordered) {
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

        println("Exported " + ordered.size() + " string-xref functions to " + output);
    }
}
