// Finds the Sony SDK library anchors in a PS2 ELF and labels them in Ghidra.
//
// Sony's static EE libraries keep their version banners ("PsIIlibgraph2400",
// "PsIIlibcdvd 2400", ...) and diagnostic strings ("libdma: sync timeout",
// "Libcdvd bind err ...", "sceGsSyncPath: ...") in .rodata. A function that
// references one of those strings belongs to that library, which gives a real
// anchor even though the ELF is stripped.
//
// Usage (headless):
//   analyzeHeadless <proj> <name> -import <elf> -postScript SonyLibs.java <outdir>
//
// Writes <outdir>/sony_refs.csv and <outdir>/sony_functions.csv, renames the
// anchored functions to SONY_<lib>_<address>, and marks the banner strings.
//@category PS2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public class SonyLibs extends GhidraScript {

    private static final String[] MARKERS = {
        "PsIIlib", "Libcdvd", "libcdvd", "libdma", "libmc", "libdbc",
        "sceGs", "sceCd", "sceMc", "sceDbc", "sceSd", "sceSif",
        "Sony Computer", "PlayStation2"
    };

    private static String libraryOf(String text) {
        String s = text.toLowerCase(Locale.ROOT);
        if (s.contains("psiilibgraph") || s.contains("libgraph") || s.contains("scegs")) return "libgraph";
        if (s.contains("psiilibdma") || s.contains("libdma")) return "libdma";
        if (s.contains("psiilibipu") || s.contains("libipu")) return "libipu";
        if (s.contains("psiilibkernl") || s.contains("libkernl")) return "libkernl";
        if (s.contains("psiilibcdvd") || s.contains("libcdvd") || s.contains("scecd")) return "libcdvd";
        if (s.contains("psiilibmc") || s.contains("libmc") || s.contains("scemc")) return "libmc";
        if (s.contains("psiilibpad2") || s.contains("libpad2") || s.contains("libdbc") || s.contains("scedbc") || s.contains("scesd")) return "libpad2";
        if (s.contains("scesif") || s.contains("libsif")) return "libsif";
        return "sony";
    }

    private static boolean interesting(String text) {
        for (String marker : MARKERS) {
            if (text.contains(marker)) return true;
        }
        return false;
    }

    private static String printable(byte[] bytes, int from, int to) {
        StringBuilder text = new StringBuilder();
        for (int i = from; i < to; ++i) {
            int c = bytes[i] & 0xFF;
            if (c < 0x20 || c > 0x7E) break;
            text.append((char) c);
        }
        return text.toString();
    }

    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        File directory = new File(outDir);
        if (!directory.exists() && !directory.mkdirs()) throw new RuntimeException("cannot create " + outDir);

        Memory memory = currentProgram.getMemory();
        List<Object[]> strings = new ArrayList<>(); // address, text, library

        for (MemoryBlock block : memory.getBlocks()) {
            if (!block.isInitialized()) continue;
            long size = block.getSize();
            if (size <= 0 || size > (64L << 20)) continue;
            byte[] bytes = new byte[(int) size];
            try {
                block.getBytes(block.getStart(), bytes);
            } catch (Exception e) {
                continue;
            }
            for (int i = 0; i < bytes.length - 4; ) {
                int c = bytes[i] & 0xFF;
                if (c < 0x20 || c > 0x7E) { ++i; continue; }
                int end = i;
                while (end < bytes.length && end - i < 200) {
                    int d = bytes[end] & 0xFF;
                    if (d < 0x20 || d > 0x7E) break;
                    ++end;
                }
                if (end - i >= 4) {
                    String text = printable(bytes, i, end);
                    if (interesting(text)) {
                        strings.add(new Object[] { block.getStart().add(i), text, libraryOf(text) });
                    }
                }
                i = Math.max(end, i + 1);
            }
        }

        Map<String, int[]> ranges = new LinkedHashMap<>();
        PrintWriter refs = new PrintWriter(new FileWriter(new File(directory, "sony_refs.csv")));
        PrintWriter funcs = new PrintWriter(new FileWriter(new File(directory, "sony_functions.csv")));
        refs.println("library,string_addr,string,ref_addr,ref_kind,func_entry,func_size,func_name");
        funcs.println("library,func_entry,func_size,func_name,evidence");

        int anchors = 0;
        for (Object[] entry : strings) {
            Address stringAddress = (Address) entry[0];
            String text = (String) entry[1];
            String library = (String) entry[2];
            String clean = text.replace('\n', ' ').replace('\r', ' ');
            if (text.startsWith("PsIIlib")) {
                createLabel(stringAddress, "SONY_BANNER_" + library.toUpperCase(Locale.ROOT), true);
            }
            for (Reference reference : getReferencesTo(stringAddress)) {
                Address from = reference.getFromAddress();
                Function function = getFunctionContaining(from);
                ++anchors;
                refs.printf("%s,%s,\"%s\",%s,%s,%s,%d,%s%n",
                    library, stringAddress, clean.replace("\"", "'"), from,
                    reference.getReferenceType(),
                    function == null ? "" : function.getEntryPoint(),
                    function == null ? 0 : function.getBody().getNumAddresses(),
                    function == null ? "" : function.getName());
                if (function != null) {
                    String name = function.getName();
                    if (!name.startsWith("SONY_")) {
                        function.setName("SONY_" + library + "_" + function.getEntryPoint(), SourceType.USER_DEFINED);
                    }
                    function.setComment("Ancla de la libreria Sony " + library + ": \"" + clean + "\"");
                    int[] range = ranges.get(library);
                    long start = function.getEntryPoint().getOffset();
                    long end = start + function.getBody().getNumAddresses();
                    if (range == null) {
                        ranges.put(library, new int[] { (int) start, (int) end });
                    } else {
                        range[0] = (int) Math.min(range[0], start);
                        range[1] = (int) Math.max(range[1], end);
                    }
                }
            }
        }

        // Every function body that holds one of the string references is a
        // library function; list it once per library with the evidence.
        for (Object[] entry : strings) {
            Address stringAddress = (Address) entry[0];
            String text = (String) entry[1];
            String library = (String) entry[2];
            for (Reference reference : getReferencesTo(stringAddress)) {
                Function function = getFunctionContaining(reference.getFromAddress());
                if (function != null) {
                    funcs.printf("%s,%s,%d,%s,\"%s\"%n", library, function.getEntryPoint(),
                        function.getBody().getNumAddresses(), function.getName(),
                        text.replace('\n', ' ').replace("\"", "'"));
                }
            }
        }

        PrintWriter summary = new PrintWriter(new FileWriter(new File(directory, "sony_summary.txt")));
        summary.println("Sony library anchors: " + anchors + " references, " + strings.size() + " strings");
        for (Map.Entry<String, int[]> range : ranges.entrySet()) {
            summary.printf("%-10s anchored functions span %08x..%08x%n",
                range.getKey(), range.getValue()[0], range.getValue()[1]);
        }
        int total = 0;
        for (FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true); it.hasNext(); ) {
            it.next();
            ++total;
        }
        summary.println("Ghidra functions: " + total);
        summary.close();
        refs.close();
        funcs.close();
        println("SonyLibs: " + anchors + " anchors, " + strings.size() + " strings, " + total + " functions");
    }
}
