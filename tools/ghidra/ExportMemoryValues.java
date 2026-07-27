// Exports deterministic scalar interpretations for explicitly supplied
// addresses. Generated output is local evidence and belongs under the ignored
// artifacts/ tree.

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;

public class ExportMemoryValues extends GhidraScript {
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

    private static long unsigned32(
            byte[] bytes, boolean bigEndian) {
        long result = 0L;
        if (bigEndian) {
            for (byte value : bytes) {
                result = (result << 8) | (value & 0xffL);
            }
        }
        else {
            for (int index = bytes.length - 1; index >= 0; --index) {
                result = (result << 8) | (bytes[index] & 0xffL);
            }
        }
        return result;
    }

    private static String hexBytes(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) {
            result.append(String.format("%02x", value & 0xff));
        }
        return result.toString();
    }

    private static String textOrEmpty(Object value) {
        return value == null ? "" : value.toString().replace('\n', ' ');
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportMemoryValues.java <output-path> " +
                "<address> [address ...]");
        }

        File output = new File(arguments[0]).getAbsoluteFile();
        File parent = output.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException(
                "cannot create output directory: " + parent);
        }

        Set<Address> unique = new LinkedHashSet<>();
        for (int index = 1; index < arguments.length; ++index) {
            unique.add(parseAddress(this, arguments[index]));
        }
        List<Address> addresses = new ArrayList<>(unique);
        addresses.sort(Comparator.naturalOrder());

        Memory memory = currentProgram.getMemory();
        boolean bigEndian = currentProgram.getLanguage().isBigEndian();
        List<String> unresolved = new ArrayList<>();

        try (PrintWriter writer = new PrintWriter(new OutputStreamWriter(
                new FileOutputStream(output), StandardCharsets.UTF_8))) {
            writer.println("program=" + currentProgram.getName());
            writer.println("language=" + currentProgram.getLanguageID());
            writer.println("imageBase=" + currentProgram.getImageBase());
            writer.println("bigEndian=" + bigEndian);
            writer.println("requestedAddresses=" + addresses.size());

            for (Address address : addresses) {
                monitor.checkCancelled();
                byte[] bytes = new byte[4];
                int copied;
                try {
                    copied = memory.getBytes(address, bytes);
                }
                catch (RuntimeException error) {
                    copied = 0;
                }
                if (copied != bytes.length) {
                    unresolved.add(address.toString());
                    continue;
                }

                long unsigned = unsigned32(bytes, bigEndian);
                int signed = (int)unsigned;
                float floating = Float.intBitsToFloat(signed);
                MemoryBlock block = memory.getBlock(address);
                Symbol symbol =
                    currentProgram.getSymbolTable().getPrimarySymbol(address);
                Data data = currentProgram.getListing().getDataContaining(address);

                writer.println();
                writer.println("address=" + address);
                writer.println("block=" +
                    (block == null ? "" : block.getName()));
                writer.println("symbol=" +
                    (symbol == null ? "" : symbol.getName(true)));
                writer.println("dataType=" +
                    (data == null ? "" :
                        textOrEmpty(data.getDataType().getDisplayName())));
                writer.println("bytes=" + hexBytes(bytes));
                writer.println("u32=" + Long.toUnsignedString(unsigned));
                writer.println("u32hex=" + String.format("0x%08x", unsigned));
                writer.println("s32=" + signed);
                writer.println("f32=" + Float.toString(floating));
                writer.println("f32hex=" + Float.toHexString(floating));
                writer.println("finite=" + Float.isFinite(floating));
            }

            writer.println();
            writer.println("resolvedAddresses=" +
                (addresses.size() - unresolved.size()));
            writer.println("unresolvedAddresses=" + unresolved.size());
            for (String address : unresolved) {
                writer.println("unresolved=" + address);
            }
        }

        println("Exported " + (addresses.size() - unresolved.size()) +
            " memory values to " + output);
        if (!unresolved.isEmpty()) {
            throw new IllegalStateException(
                "unresolved memory addresses: " + unresolved);
        }
    }
}
