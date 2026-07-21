// Exports concise headless decompiler output for every PE export/entry point.
// Generated output is local evidence and belongs under the ignored artifacts/ tree.

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

public class ExportDecompilation extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1) {
            throw new IllegalArgumentException(
                "usage: ExportDecompilation.java <output-path>");
        }

        File output = new File(arguments[0]).getAbsoluteFile();
        File parent = output.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException("cannot create output directory: " + parent);
        }

        FunctionManager functions = currentProgram.getFunctionManager();
        AddressIterator entries = currentProgram.getSymbolTable()
            .getExternalEntryPointIterator();
        List<Function> exported = new ArrayList<>();

        while (entries.hasNext()) {
            Address address = entries.next();
            Function function = functions.getFunctionAt(address);
            if (function != null && !function.isExternal()) {
                exported.add(function);
            }
        }

        exported.sort(Comparator.comparing(Function::getEntryPoint));

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
            writer.println("language=" + currentProgram.getLanguageID());
            writer.println("compiler=" + currentProgram.getCompilerSpec().getCompilerSpecID());
            writer.println("imageBase=" + currentProgram.getImageBase());
            writer.println("externalEntryFunctions=" + exported.size());

            for (Function function : exported) {
                monitor.checkCancelled();
                writer.println();
                writer.println("================================================================================");
                writer.println("entry=" + function.getEntryPoint());
                writer.println("name=" + function.getName(true));
                writer.println("signature=" + function.getSignature(true));

                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
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

        println("Exported " + exported.size() + " entry functions to " + output);
    }
}
