// Exports deterministic incoming references for explicitly supplied
// addresses. Generated output is local evidence and belongs under the
// ignored artifacts/ tree.

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class ExportAddressReferences extends GhidraScript {
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

    private static String functionEntry(Function function) {
        return function == null ? "<none>" :
            function.getEntryPoint().toString();
    }

    private static String functionName(Function function) {
        return function == null ? "<none>" : function.getName(true);
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportAddressReferences.java <output-path> " +
                "<address> [address ...]");
        }

        File output = new File(arguments[0]).getAbsoluteFile();
        File parent = output.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException(
                "cannot create output directory: " + parent);
        }

        FunctionManager functions = currentProgram.getFunctionManager();
        try (PrintWriter writer = new PrintWriter(new OutputStreamWriter(
                new FileOutputStream(output), StandardCharsets.UTF_8))) {
            writer.println("program=" + currentProgram.getName());
            writer.println("language=" + currentProgram.getLanguageID());
            writer.println("selectedAddresses=" + (arguments.length - 1));

            for (int index = 1; index < arguments.length; ++index) {
                monitor.checkCancelled();
                Address target = parseAddress(this, arguments[index]);
                Function targetFunction =
                    functions.getFunctionContaining(target);
                writer.println();
                writer.println(
                    "====================================================" +
                    "============================");
                writer.println("target=" + target);
                writer.println(
                    "targetFunctionEntry=" +
                    functionEntry(targetFunction));
                writer.println(
                    "targetFunctionName=" +
                    functionName(targetFunction));

                List<Reference> references = new ArrayList<>();
                ReferenceIterator iterator =
                    currentProgram.getReferenceManager()
                        .getReferencesTo(target);
                while (iterator.hasNext()) {
                    monitor.checkCancelled();
                    references.add(iterator.next());
                }
                references.sort(
                    Comparator.comparing(Reference::getFromAddress)
                        .thenComparing(
                            reference ->
                                reference.getReferenceType().toString()));
                writer.println("incomingReferences=" + references.size());

                for (Reference reference : references) {
                    monitor.checkCancelled();
                    Address from = reference.getFromAddress();
                    Function caller = functions.getFunctionContaining(from);
                    Instruction instruction =
                        currentProgram.getListing().getInstructionAt(from);
                    writer.println(
                        "from=" + from +
                        "|type=" + reference.getReferenceType() +
                        "|primary=" + reference.isPrimary() +
                        "|callerEntry=" + functionEntry(caller) +
                        "|callerName=" + functionName(caller) +
                        "|instruction=" +
                        (instruction == null ?
                            "<none>" : instruction.toString()));
                }
            }
        }

        println(
            "Exported incoming references for " +
            (arguments.length - 1) + " addresses to " + output);
    }
}
