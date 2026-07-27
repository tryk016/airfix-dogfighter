// Decompiles functions that call selected named functions or import thunks.

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

public class ExportCallersOfNamedFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportCallersOfNamedFunctions.java <output-path> <callee-pattern> [pattern ...]");
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
        Map<Function, Set<String>> matchesByCaller = new LinkedHashMap<>();
        InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
        while (instructions.hasNext()) {
            monitor.checkCancelled();
            Instruction instruction = instructions.next();
            if (!instruction.getFlowType().isCall()) {
                continue;
            }
            for (Reference reference : instruction.getReferencesFrom()) {
                Function target = functionManager.getFunctionAt(reference.getToAddress());
                String targetName = "";
                String resolvedName = "";
                if (target != null) {
                    Function resolvedTarget = target.getThunkedFunction(true);
                    if (resolvedTarget == null) {
                        resolvedTarget = target;
                    }
                    targetName = target.getName(true);
                    resolvedName = resolvedTarget.getName(true);
                }
                else {
                    Symbol symbol =
                        currentProgram.getSymbolTable().getPrimarySymbol(reference.getToAddress());
                    if (symbol != null) {
                        targetName = symbol.getName(true);
                        resolvedName = targetName;
                    }
                }

                String searchable = (targetName + "\n" + resolvedName).toLowerCase(Locale.ROOT);
                if (patterns.stream().noneMatch(searchable::contains)) {
                    continue;
                }

                Function caller = functionManager.getFunctionContaining(instruction.getAddress());
                if (caller == null || caller.isExternal()) {
                    continue;
                }
                matchesByCaller.computeIfAbsent(caller, ignored -> new LinkedHashSet<>())
                    .add(instruction.getAddress() + " -> " + resolvedName);
            }
        }

        List<Function> callers = new ArrayList<>(matchesByCaller.keySet());
        callers.sort(Comparator.comparing(Function::getEntryPoint));

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
            writer.println("callerFunctions=" + callers.size());

            for (Function caller : callers) {
                monitor.checkCancelled();
                writer.println();
                writer.println("================================================================================");
                writer.println("entry=" + caller.getEntryPoint());
                writer.println("name=" + caller.getName(true));
                writer.println("signature=" + caller.getSignature(true));
                for (String match : matchesByCaller.get(caller)) {
                    writer.println("call=" + match);
                }

                DecompileResults result = decompiler.decompileFunction(caller, 90, monitor);
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

        println("Exported " + callers.size() + " caller functions to " + output);
    }
}
