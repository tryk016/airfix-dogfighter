// Decompiles only functions whose qualified names contain selected patterns.

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportNamedFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportNamedFunctions.java <output-path> <name-pattern> [pattern ...]");
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

        List<Function> selected = new ArrayList<>();
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext()) {
            monitor.checkCancelled();
            Function function = functions.next();
            String name = function.getName(true).toLowerCase(Locale.ROOT);
            if (patterns.stream().anyMatch(name::contains)) {
                selected.add(function);
            }
        }
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

        println("Exported " + selected.size() + " named functions to " + output);
    }
}
