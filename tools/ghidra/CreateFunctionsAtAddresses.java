// Creates functions at verified entry addresses that automatic analysis missed.
//
// Use only after an independent source (for example, an RTTI/vtable pointer)
// establishes that each supplied address is a function entry.

import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

public class CreateFunctionsAtAddresses extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length == 0) {
            throw new IllegalArgumentException(
                "usage: CreateFunctionsAtAddresses.java <address> [address ...]");
        }

        FunctionManager functionManager = currentProgram.getFunctionManager();
        List<String> conflicts = new ArrayList<>();
        int createdCount = 0;
        int existingCount = 0;

        for (String argument : arguments) {
            monitor.checkCancelled();
            String text = argument;
            if (text.startsWith("0x") || text.startsWith("0X")) {
                text = text.substring(2);
            }

            Address address;
            try {
                address = toAddr(Long.parseUnsignedLong(text, 16));
            }
            catch (RuntimeException error) {
                throw new IllegalArgumentException(
                    "invalid hexadecimal address: " + argument, error);
            }

            Function existing = functionManager.getFunctionContaining(address);
            if (existing != null) {
                if (existing.getEntryPoint().equals(address)) {
                    ++existingCount;
                }
                else {
                    conflicts.add(
                        argument + " is inside " + existing.getName(true) +
                        " at " + existing.getEntryPoint());
                }
                continue;
            }

            Function created = createFunction(address, null);
            if (created == null) {
                conflicts.add(argument + " could not be created");
            }
            else {
                ++createdCount;
            }
        }

        println(
            "Created " + createdCount + " functions; " + existingCount +
            " already existed; " + conflicts.size() + " conflicts");
        for (String conflict : conflicts) {
            println("CONFLICT: " + conflict);
        }
        if (!conflicts.isEmpty()) {
            throw new IllegalStateException("one or more function entries were not created");
        }
    }
}
