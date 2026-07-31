// Exports functions containing indirect CALL instructions with a selected
// displacement (for example, a recovered virtual-function-table slot).

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
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

public class ExportIndirectCallsByDisplacement extends GhidraScript {
    private static long parseUnsignedHex(String argument) {
        String text = argument;
        if (text.startsWith("0x") || text.startsWith("0X")) {
            text = text.substring(2);
        }
        try {
            return Long.parseUnsignedLong(text, 16);
        }
        catch (RuntimeException error) {
            throw new IllegalArgumentException(
                "invalid hexadecimal displacement: " + argument, error);
        }
    }

    private static boolean hasDisplacement(
            Instruction instruction, long expected) {
        if (!instruction.toString().contains("[")) {
            return false;
        }
        for (int index = 0; index < instruction.getNumOperands(); ++index) {
            Scalar scalar = instruction.getScalar(index);
            if (scalar != null && scalar.getUnsignedValue() == expected) {
                return true;
            }
            for (Object object : instruction.getOpObjects(index)) {
                if (object instanceof Scalar &&
                    ((Scalar)object).getUnsignedValue() == expected) {
                    return true;
                }
            }
        }
        return false;
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportIndirectCallsByDisplacement.java " +
                "<output-path> <hex-displacement> [displacement ...]");
        }

        File output = new File(arguments[0]).getAbsoluteFile();
        File parent = output.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException(
                "cannot create output directory: " + parent);
        }

        Set<Long> requested = new LinkedHashSet<>();
        for (int index = 1; index < arguments.length; ++index) {
            requested.add(parseUnsignedHex(arguments[index]));
        }
        List<Long> displacements = new ArrayList<>(requested);
        displacements.sort(Long::compareUnsigned);
        FunctionManager functions = currentProgram.getFunctionManager();
        Map<Function, List<Instruction>> matchesByFunction =
            new LinkedHashMap<>();

        InstructionIterator instructions =
            currentProgram.getListing().getInstructions(true);
        while (instructions.hasNext()) {
            monitor.checkCancelled();
            Instruction instruction = instructions.next();
            if (!instruction.getFlowType().isCall() ||
                displacements.stream().noneMatch(
                    value -> hasDisplacement(instruction, value))) {
                continue;
            }
            Function function =
                functions.getFunctionContaining(instruction.getAddress());
            if (function == null || function.isExternal()) {
                continue;
            }
            matchesByFunction.computeIfAbsent(
                function, ignored -> new ArrayList<>()).add(instruction);
        }

        List<Function> selected =
            new ArrayList<>(matchesByFunction.keySet());
        selected.sort(Comparator.comparing(Function::getEntryPoint));

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        decompiler.setSimplificationStyle("decompile");
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException(
                "decompiler failed to open program: " +
                decompiler.getLastMessage());
        }

        try (PrintWriter writer = new PrintWriter(new OutputStreamWriter(
                new FileOutputStream(output), StandardCharsets.UTF_8))) {
            writer.println("program=" + currentProgram.getName());
            writer.print("displacements=");
            for (int index = 0; index < displacements.size(); ++index) {
                if (index != 0) {
                    writer.print(",");
                }
                writer.print("0x" + Long.toUnsignedString(
                    displacements.get(index), 16).toUpperCase(Locale.ROOT));
            }
            writer.println();
            writer.println("selectedFunctions=" + selected.size());

            for (Function function : selected) {
                monitor.checkCancelled();
                writer.println();
                writer.println(
                    "============================================================" +
                    "====================");
                writer.println("entry=" + function.getEntryPoint());
                writer.println("name=" + function.getName(true));
                writer.println("signature=" + function.getSignature(true));
                for (Instruction instruction :
                        matchesByFunction.get(function)) {
                    Address address = instruction.getAddress();
                    writer.println("call=" + address + "|" +
                        instruction.toString());
                }

                DecompileResults result =
                    decompiler.decompileFunction(function, 90, monitor);
                if (result.decompileCompleted() &&
                    result.getDecompiledFunction() != null) {
                    writer.println(result.getDecompiledFunction().getC());
                }
                else {
                    writer.println("DECOMPILATION_FAILED=" +
                        result.getErrorMessage());
                }
            }
        }
        finally {
            decompiler.dispose();
        }

        println("Exported " + selected.size() +
            " functions containing indirect calls through " +
            displacements.size() + " selected displacement(s) to " + output);
    }
}
