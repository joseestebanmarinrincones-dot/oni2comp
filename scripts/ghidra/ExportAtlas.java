// Exports Ghidra decompilation and call metadata for the non-SDK functions of
// a PS2 program, one file per function. It is the readable half of the hybrid:
// the eerecomp units are the faithful C++ implementation, this is the
// pseudocode that explains them.
//
// Usage:
//   analyzeHeadless <proj> <name> -process <program> -noanalysis \
//     -scriptPath scripts/ghidra \
//     -postScript ExportAtlas.java <outdir> <sdk_ranges.txt> [start] [end]
//
// sdk_ranges.txt: one "LO HI" pair per line in hexadecimal, end exclusive.
// Use games/onimusha2/disasm/sdk_report/ps2_sdk_ranges.txt for exact SDK
// functions, or the older broad sony_ranges.txt.
// start/end are optional hexadecimal addresses (default: whole program).
//@category PS2

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;

public class ExportAtlas extends GhidraScript {

    private static long parseHex(String text) {
        return Long.parseUnsignedLong(text.trim().replaceFirst("^0[xX]", ""), 16);
    }

    private static String safe(String text) {
        return text.replaceAll("[^A-Za-z0-9_.-]", "_");
    }

    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new RuntimeException("usage: ExportAtlas.java <outdir> <sdk_ranges.txt> [start] [end]");
        }
        File outDir = new File(args[0]);
        File codeDir = new File(outDir, "ghidra");
        if (!codeDir.exists() && !codeDir.mkdirs()) throw new RuntimeException("cannot create " + codeDir);

        List<long[]> sdk = new ArrayList<>();
        for (String line : Files.readAllLines(new File(args[1]).toPath())) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] parts = line.split("\\s+");
            if (parts.length >= 2) sdk.add(new long[] { parseHex(parts[0]), parseHex(parts[1]) });
        }
        long start = args.length > 2 ? parseHex(args[2]) : 0;
        long end = args.length > 3 ? parseHex(args[3]) : 0xFFFFFFFFL;

        // Functions our own analysis found but Ghidra did not. Create them so
        // the atlas covers the whole game and not only Ghidra's discoveries.
        if (args.length > 4) {
            int created = 0, failedCreate = 0;
            for (String line : Files.readAllLines(new File(args[4]).toPath())) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] parts = line.split("\\s+");
                long address;
                try {
                    address = parseHex(parts[0]);
                } catch (Exception e) {
                    continue;
                }
                if (address < start || address >= end) continue;
                boolean isSdk = false;
                for (long[] range : sdk) {
                    if (address >= range[0] && address < range[1]) { isSdk = true; break; }
                }
                if (isSdk) continue;
                Address entry = toAddr(address);
                if (getFunctionAt(entry) != null) continue;
                try {
                    if (getInstructionAt(entry) == null) disassemble(entry);
                    if (createFunction(entry, null) != null) ++created;
                    else ++failedCreate;
                } catch (Exception e) {
                    ++failedCreate;
                }
            }
            println("ExportAtlas: " + created + " functions created from the analysis list, "
                + failedCreate + " failed");
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        decompiler.setSimplificationStyle("decompile");
        if (!decompiler.openProgram(currentProgram)) {
            throw new RuntimeException("the decompiler cannot open the program");
        }

        PrintWriter csv = new PrintWriter(new FileWriter(new File(outDir, "ghidra_functions.csv")));
        csv.println("address,size,name,decompiled,file,callers,callees");

        int written = 0, failed = 0, skipped = 0;
        for (FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true); it.hasNext(); ) {
            Function function = it.next();
            long address = function.getEntryPoint().getOffset();
            if (address < start || address >= end) continue;
            boolean isSdk = false;
            for (long[] range : sdk) {
                if (address >= range[0] && address < range[1]) { isSdk = true; break; }
            }
            if (isSdk) { ++skipped; continue; }

            String name = function.getName();
            String file = String.format("%08x_%s.c", address, safe(name));
            String header = String.format(
                "/* Onimusha 2 (SLUS_203.93) - %s%n" +
                " * address 0x%08x, body %d bytes%n" +
                " * Ghidra 12.2 + ghidra-emotionengine-reloaded (r5900:LE:32:default)%n" +
                " * This is readable pseudocode, not the compilable translation: the%n" +
                " * compilable implementation is the eerecomp unit named in the atlas.%n" +
                " */%n%n", name, address, function.getBody().getNumAddresses());

            String code = null;
            try {
                DecompileResults results = decompiler.decompileFunction(function, 90, monitor);
                if (results != null && results.decompileCompleted()) {
                    code = results.getDecompiledFunction().getC();
                }
            } catch (Exception e) {
                code = null;
            }

            try (PrintWriter out = new PrintWriter(new FileWriter(new File(codeDir, file)))) {
                out.print(header);
                if (code != null) {
                    out.print(code);
                } else {
                    out.print("/* Decompilation failed for this function (usually an MMI/COP2 sequence).\n" +
                              " * Use the eerecomp unit for the exact semantics. */\n");
                }
            }

            Set<String> callers = new TreeSet<>();
            for (Function caller : function.getCallingFunctions(monitor)) callers.add(String.format("%08x", caller.getEntryPoint().getOffset()));
            Set<String> callees = new TreeSet<>();
            for (Function callee : function.getCalledFunctions(monitor)) callees.add(String.format("%08x", callee.getEntryPoint().getOffset()));

            csv.printf("%08x,%d,%s,%s,%s,%s,%s%n", address, function.getBody().getNumAddresses(),
                name, code != null ? "yes" : "no", "ghidra/" + file,
                String.join(" ", callers), String.join(" ", callees));
            if (code != null) ++written; else ++failed;
            if ((written + failed) % 250 == 0) println("ExportAtlas: " + (written + failed) + " functions...");
        }
        csv.close();
        decompiler.dispose();
        println(String.format("ExportAtlas: %d decompiled, %d failed, %d SDK skipped", written, failed, skipped));
    }
}
