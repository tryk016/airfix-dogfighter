// Exports instructions and direct calls that can modify the x87 floating-point
// environment. Generated output is local evidence and belongs under the
// ignored artifacts/ tree.

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

public class ExportFloatingPointEnvironmentWrites extends GhidraScript {
    private static final Set<String> ENVIRONMENT_INSTRUCTIONS =
        new HashSet<>(Arrays.asList(
            "FLDCW",
            "FLDENV",
            "FRSTOR",
            "FSTENV",
            "FNSTENV",
            "FSAVE",
            "FNSAVE",
            "FINIT",
            "FNINIT",
            "FCLEX",
            "FNCLEX",
            "FXRSTOR",
            "LDMXCSR",
            "XRSTOR"));

    private static final List<String> CALLEE_PATTERNS =
        Arrays.asList(
            "controlfp",
            "control87",
            "clearfp",
            "fpreset",
            "fesetenv",
            "feupdateenv",
            "fesetround",
            "feclearexcept",
            "feraiseexcept",
            "fesetexceptflag",
            "feholdexcept",
            "feenableexcept",
            "fedisableexcept",
            "setfpucw",
            "fenv");

    private static final class Match {
        private final Instruction instruction;
        private final String kind;
        private final String detail;

        private Match(
                Instruction instruction, String kind, String detail) {
            this.instruction = instruction;
            this.kind = kind;
            this.detail = detail;
        }
    }

    private static String hexBytes(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) {
            result.append(String.format("%02x", value & 0xff));
        }
        return result.toString();
    }

    private static boolean isEnvironmentCallee(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        for (String pattern : CALLEE_PATTERNS) {
            if (lower.contains(pattern)) {
                return true;
            }
        }
        return false;
    }

    private String resolvedTargetName(
            Instruction instruction, FunctionManager functionManager) {
        for (Reference reference : instruction.getReferencesFrom()) {
            Function target =
                functionManager.getFunctionAt(reference.getToAddress());
            if (target != null) {
                Function resolved = target.getThunkedFunction(true);
                if (resolved == null) {
                    resolved = target;
                }
                String name = resolved.getName(true);
                if (isEnvironmentCallee(name)) {
                    return name;
                }
            }

            Symbol symbol = currentProgram.getSymbolTable()
                .getPrimarySymbol(reference.getToAddress());
            if (symbol != null && isEnvironmentCallee(symbol.getName(true))) {
                return symbol.getName(true);
            }
        }
        return null;
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1) {
            throw new IllegalArgumentException(
                "usage: ExportFloatingPointEnvironmentWrites.java <output-path>");
        }

        File output = new File(arguments[0]).getAbsoluteFile();
        File parent = output.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException(
                "cannot create output directory: " + parent);
        }

        FunctionManager functionManager = currentProgram.getFunctionManager();
        List<Match> matches = new ArrayList<>();
        InstructionIterator instructions =
            currentProgram.getListing().getInstructions(true);
        while (instructions.hasNext()) {
            monitor.checkCancelled();
            Instruction instruction = instructions.next();
            String mnemonic =
                instruction.getMnemonicString().toUpperCase(Locale.ROOT);
            if (ENVIRONMENT_INSTRUCTIONS.contains(mnemonic)) {
                matches.add(new Match(instruction, "instruction", mnemonic));
            }
            if (instruction.getFlowType().isCall()) {
                String targetName =
                    resolvedTargetName(instruction, functionManager);
                if (targetName != null) {
                    matches.add(new Match(instruction, "call", targetName));
                }
            }
        }

        matches.sort(Comparator
            .comparing((Match match) -> match.instruction.getAddress())
            .thenComparing(match -> match.kind)
            .thenComparing(match -> match.detail));

        try (PrintWriter writer = new PrintWriter(new OutputStreamWriter(
                new FileOutputStream(output), StandardCharsets.UTF_8))) {
            writer.println("program=" + currentProgram.getName());
            writer.println("language=" + currentProgram.getLanguageID());
            writer.println("matches=" + matches.size());

            for (Match match : matches) {
                monitor.checkCancelled();
                Instruction instruction = match.instruction;
                Function owner =
                    functionManager.getFunctionContaining(
                        instruction.getAddress());
                writer.println();
                writer.println("kind=" + match.kind);
                writer.println("detail=" + match.detail);
                writer.println("address=" + instruction.getAddress());
                writer.println("bytes=" + hexBytes(instruction.getBytes()));
                writer.println("instruction=" + instruction);
                if (owner == null || owner.isExternal()) {
                    writer.println("functionEntry=<none>");
                    writer.println("functionName=<none>");
                    writer.println("functionSignature=<none>");
                }
                else {
                    writer.println("functionEntry=" + owner.getEntryPoint());
                    writer.println("functionName=" + owner.getName(true));
                    writer.println(
                        "functionSignature=" + owner.getSignature(true));
                }
            }
        }

        println("Exported " + matches.size() +
            " floating-point environment matches to " + output);
    }
}
