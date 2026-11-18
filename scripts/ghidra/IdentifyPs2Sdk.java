// Identifies PS2 SDK/CRT functions in a stripped EE executable and marks them
// in Ghidra without losing the original FUN_/SONY_ names.
//
// Evidence used:
//   1. References to SDK banner/error strings (strong anchors). Module names
//      such as "LIBSD.IRX" are deliberately NOT anchors: the game loader also
//      references them.
//   2. Tight, verified library ranges (libgraph/libdma/libvu0/libc/libkernel/
//      libcdvd/libmc/libsd/libpad2). The old broad 0x25C610..0x285D80 range is
//      not used because it contains game code.
//   3. EE kernel syscall wrappers: "li v1,<n>; syscall" -> libkernel.
//   4. Existing SONY_<library>_<address> names from SonyLibs.java.
//
// False positives found in the first pass and explicitly excluded:
//   0x00149BF0  game init/loader (references "LIBSD.IRX")
//   0x00160A30  game wrapper around sceMcInit (references "sceMcInit Failed !")
//
// Usage (headless):
//   analyzeHeadless <projdir> <project> -process SLUS_203.93 -noanalysis \
//     -scriptPath scripts/ghidra \
//     -postScript IdentifyPs2Sdk.java <outdir>
//
// Outputs:
//   <outdir>/ps2_sdk_functions.csv
//   <outdir>/ps2_sdk_ranges.txt     (one exact function range per line)
//   <outdir>/ps2_sdk_summary.txt
//
// It also puts each identified function in namespace PS2SDK.<library> and
// adds a plate comment. Functions that were marked by an older run but are no
// longer classified are moved back to the global namespace.
//@category PS2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class IdentifyPs2Sdk extends GhidraScript {

    private static final class Range {
        final long start;
        final long end;
        final String library;
        final boolean confirmed;
        final int score;
        Range(long start, long end, String library, boolean confirmed, int score) {
            this.start = start;
            this.end = end;
            this.library = library;
            this.confirmed = confirmed;
            this.score = score;
        }
        boolean contains(long address) {
            return address >= start && address < end;
        }
    }

    private static final class Hit {
        String library;
        String evidence;
        String source;
        int score;
        boolean confirmed;
        int syscall = -1;
        boolean cop2 = false;
        Hit(String library, String evidence, String source, int score, boolean confirmed) {
            this.library = library;
            this.evidence = evidence;
            this.source = source;
            this.score = score;
            this.confirmed = confirmed;
        }
    }

    // Functions that look like SDK by string but are game code. Never mark them.
    private static final Set<Long> EXCLUDED = new LinkedHashSet<>();
    static {
        EXCLUDED.add(0x00149BF0L);   // game init/loader, references LIBSD.IRX
        EXCLUDED.add(0x00160A30L);   // game wrapper around sceMcInit
    }

    // Verified ranges. Order matters: more specific clusters first.
    // Do not add the broad 0x25C610..0x285D80 libpad2 span here.
    private static final Range[] RANGES = {
        // 0x00100150 is sceGsResetGraph, not CRT: it programs GS_CSR and
        // calls GsPutIMR. The CRT entry helper ends at 0x0010014F.
        new Range(0x00100000L, 0x00100150L, "crt",      true,  3),
        new Range(0x00100150L, 0x00101550L, "libgraph", true,  3),
        new Range(0x00101550L, 0x00101FE8L, "libdma",   true,  3),
        new Range(0x00101FE8L, 0x00102D30L, "libvu0",   false, 2),
        new Range(0x00102D30L, 0x00103360L, "libc",     false, 2),
        new Range(0x00103360L, 0x00104500L, "libkernel", false, 2),
        // CRT module init/exit (0x10C5F0 is exit, called from _start after
        // main). It references the libcdvd banner only through the module
        // table, so it is libc, not libcdvd.
        new Range(0x0010C490L, 0x0010C7D8L, "libc",     false, 2),
        new Range(0x0010C7D8L, 0x0010E130L, "libcdvd", true,  4),
        new Range(0x0010E280L, 0x0010E500L, "libmc",   true,  4),
        new Range(0x0025C650L, 0x0025C950L, "libsd",   true,  4),
        new Range(0x00285398L, 0x00285D80L, "libpad2", true,  4),
    };

    // Only error/banner strings that belong to the library implementation.
    // Generic prefixes (scePad, sceCd, sceGs, sceSif) and "*.IRX" module names
    // are NOT used: game wrappers and the loader reference them too.
    // Version banners ("PsIIlibcdvd 2400", ...) are deliberately NOT anchors:
    // the CRT module-init table references them too. The error strings are
    // emitted by the library implementation itself.
    private static final String[][] MARKERS = {
        {"sceGsDefDispEnv", "libgraph"},
        {"sceGsPutDrawEnv", "libgraph"},
        {"sceGsSyncPath", "libgraph"},
        {"libdma: sync timeout", "libdma"},
        {"Libcdvd bind err", "libcdvd"},
        {"sceCdCbfunc", "libcdvd"},
        {"sceCdStRead", "libcdvd"},
        {"sceCdStPause", "libcdvd"},
        {"sceCdStResume", "libcdvd"},
        {"sceCdStStat", "libcdvd"},
        {"libmc: too old release", "libmc"},
        {"bind error libmc", "libmc"},
        {"libdbc", "libpad2"},
        {"sceDbc", "libpad2"},
        {"sceSdRemoteInit", "libsd"},
    };

    // Ghidra already resolved these EE kernel names. Used as extra evidence.
    private static final Set<String> KERNEL_NAMES = new LinkedHashSet<>();
    static {
        String[] names = {
            "SetGsCrt", "_Exit", "_LoadExecPS2", "SetVTLBRefillHandler",
            "AddIntcHandler", "RemoveIntcHandler", "AddDmacHandler",
            "RemoveDmacHandler", "_EnableIntc", "_DisableIntc", "_EnableDmac",
            "_DisableDmac", "SetAlarm", "_iEnableIntc", "_iDisableIntc",
            "_iEnableDmac", "_iDisableDmac", "CreateThread", "DeleteThread",
            "_StartThread", "ExitDeleteThread", "TerminateThread",
            "ChangeThreadPriority", "RotateThreadReadyQueue", "GetThreadId",
            "ReferThreadStatus", "SleepThread", "WakeupThread", "_iWakeupThread",
            "SuspendThread", "_iSuspendThread", "EndOfHeap", "CreateSema",
            "DeleteSema", "SignalSema", "iSignalSema", "WaitSema", "PollSema",
            "SetOsdConfigParam", "GetOsdConfigParam", "FlushCache",
            "sceSifStopDma", "GsPutIMR", "SetVSyncFlag", "sceSifDmaStat",
            "sceSifSetDma", "isceSifSetDma", "sceSifSetDChain",
            "isceSifSetDChain", "sceSifSetReg", "sceSifGetReg", "_ExecOSD",
            "Deci2Call", "GetMemorySize", "_InitTLB",
        };
        for (String name : names) KERNEL_NAMES.add(name);
    }

    private static final Pattern SYSCALL_V1 = Pattern.compile(
        "(?:addiu|ori|li)\\s+\\$?v1\\s*,\\s*(?:\\$?zero\\s*,\\s*)?(-?0x[0-9a-fA-F]+|-?\\d+)");

    private static boolean nativeLibrary(String library) {
        return library.equals("libgraph") || library.equals("libdma")
            || library.equals("libcdvd") || library.equals("libmc")
            || library.equals("libsd") || library.equals("libpad2");
    }

    private static String libraryOf(String text) {
        for (String[] marker : MARKERS) {
            if (text.contains(marker[0])) return marker[1];
        }
        return null;
    }

    private static String libraryFromName(String name) {
        Matcher matcher = Pattern.compile("^SONY_([A-Za-z0-9]+)_[0-9a-fA-F]{8}$").matcher(name);
        return matcher.matches() ? matcher.group(1).toLowerCase(Locale.ROOT) : null;
    }

    private static Range rangeOf(long address) {
        for (Range range : RANGES) {
            if (range.contains(address)) return range;
        }
        return null;
    }

    private static int parseNumber(String text) {
        String value = text.trim().toLowerCase(Locale.ROOT);
        if (value.startsWith("0x")) return (int) Long.parseLong(value.substring(2), 16);
        if (value.startsWith("-0x")) return -(int) Long.parseLong(value.substring(3), 16);
        return Integer.parseInt(value);
    }

    private static boolean isCop2(Instruction instruction) {
        String text = instruction.toString().toLowerCase(Locale.ROOT);
        return text.contains("$vf") || text.contains("lqc2") || text.contains("sqc2")
            || text.contains("qmfc2") || text.contains("qmtc2") || text.contains("cfc2")
            || text.contains("ctc2") || text.contains("vcall") || text.contains("vwaitq")
            || text.contains("vlqi") || text.contains("vsqi");
    }

    private static String escapeCsv(String text) {
        return text == null ? "" : text.replace("\"", "'").replace('\n', ' ').replace('\r', ' ');
    }

    private static void merge(Map<Long, Hit> hits, long address, Hit candidate) {
        Hit current = hits.get(address);
        if (current == null || candidate.score > current.score
            || (candidate.score == current.score && candidate.confirmed && !current.confirmed)) {
            hits.put(address, candidate);
        }
    }

    private static boolean underPs2Sdk(Namespace namespace) {
        Namespace current = namespace;
        while (current != null) {
            if ("PS2SDK".equals(current.getName())) return true;
            current = current.getParentNamespace();
        }
        return false;
    }

    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        File directory = new File(outDir);
        if (!directory.exists() && !directory.mkdirs()) {
            throw new RuntimeException("cannot create " + outDir);
        }

        // Find SDK banner/error strings and remember their addresses.
        Map<Long, String[]> markerStrings = new LinkedHashMap<>();
        Memory memory = currentProgram.getMemory();
        for (MemoryBlock block : memory.getBlocks()) {
            if (!block.isInitialized()) continue;
            long size = block.getSize();
            if (size <= 0 || size > (64L << 20)) continue;

            byte[] bytes;
            try {
                bytes = new byte[(int) size];
                block.getBytes(block.getStart(), bytes);
            } catch (Exception e) {
                continue;
            }

            for (int i = 0; i < bytes.length - 4; ) {
                int c = bytes[i] & 0xFF;
                if (c < 0x20 || c > 0x7E) {
                    ++i;
                    continue;
                }
                int end = i;
                while (end < bytes.length && end - i < 220) {
                    int d = bytes[end] & 0xFF;
                    if (d < 0x20 || d > 0x7E) break;
                    ++end;
                }
                if (end - i >= 4) {
                    String text = new String(bytes, i, end - i, java.nio.charset.StandardCharsets.US_ASCII);
                    String library = libraryOf(text);
                    if (library != null) {
                        long address = block.getStart().getOffset() + i;
                        markerStrings.put(address, new String[] {text, library});
                    }
                }
                i = Math.max(end, i + 1);
            }
        }

        Map<Long, Hit> hits = new LinkedHashMap<>();
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        int totalFunctions = 0;

        while (functions.hasNext()) {
            Function function = functions.next();
            ++totalFunctions;
            long address = function.getEntryPoint().getOffset();
            if (EXCLUDED.contains(address)) continue;

            // 1) Existing SONY_* names keep their anchored evidence.
            String name = function.getName();
            String nameLibrary = libraryFromName(name);
            // 0x10C490 is the CRT module-init function; it only references the
            // libcdvd banner through the module table. SonyLibs.java named it
            // libcdvd by mistake.
            if (address == 0x0010C490L) nameLibrary = "libc";
            if (nameLibrary != null) {
                int score = nameLibrary.equals("libmc") ? 3 : 4;
                merge(hits, address, new Hit(nameLibrary,
                    "SONY name " + name, "name", score, true));
            }

            // 2) String evidence: only error/banner strings, never module names.
            Set<String> stringLibraries = new LinkedHashSet<>();
            String stringEvidence = "";
            InstructionIterator instructions =
                currentProgram.getListing().getInstructions(function.getBody(), true);
            int syscall = -1;
            boolean cop2 = false;
            while (instructions.hasNext()) {
                Instruction instruction = instructions.next();
                if (instruction.getMnemonicString().equalsIgnoreCase("syscall") && syscall == -1) {
                    Instruction previous = instruction.getPrevious();
                    for (int i = 0; i < 4 && previous != null; ++i, previous = previous.getPrevious()) {
                        Matcher matcher = SYSCALL_V1.matcher(
                            previous.toString().toLowerCase(Locale.ROOT));
                        if (matcher.find()) {
                            try {
                                syscall = parseNumber(matcher.group(1));
                            } catch (NumberFormatException e) {
                                syscall = -1;
                            }
                            break;
                        }
                    }
                    if (syscall == -1) syscall = -2;
                }
                if (!cop2 && isCop2(instruction)) cop2 = true;

                for (Reference reference : instruction.getReferencesFrom()) {
                    Address to = reference.getToAddress();
                    if (to == null) continue;
                    String[] marker = markerStrings.get(to.getOffset());
                    if (marker != null) {
                        stringLibraries.add(marker[1]);
                        stringEvidence = marker[0];
                    }
                }
            }

            Range range = rangeOf(address);

            // A syscall is kernel evidence, but not inside crt/libc: _start and
            // printf also execute syscalls and still belong to their libraries.
            if (syscall != -1 && (range == null
                || (!range.library.equals("crt") && !range.library.equals("libc")))) {
                Hit hit = new Hit("libkernel", "syscall " + (syscall >= 0
                    ? String.format("0x%X", syscall) : "?"), "pattern", 5, true);
                hit.syscall = syscall;
                merge(hits, address, hit);
            }
            if (KERNEL_NAMES.contains(name)) {
                merge(hits, address, new Hit("libkernel", "kernel name " + name,
                    "name", syscall != -1 ? 5 : 3, syscall != -1));
            }
            for (String library : stringLibraries) {
                merge(hits, address, new Hit(library,
                    "string \"" + stringEvidence + "\"", "string", 5, true));
            }

            // 3) Verified range.
            if (range != null) {
                Hit hit = new Hit(range.library,
                    "range 0x" + Long.toHexString(address), "range", range.score, range.confirmed);
                hit.cop2 = cop2;
                // A syscall inside a range is still a kernel wrapper, unless the
                // range is crt/libc or libcdvd and the function is clearly not a
                // wrapper.
                if (syscall != -1 && !range.library.equals("libc")
                    && !range.library.equals("crt")) {
                    hit.library = "libkernel";
                    hit.evidence = "syscall " + (syscall >= 0
                        ? String.format("0x%X", syscall) : "?") + " in range";
                    hit.confirmed = true;
                    hit.score = 5;
                }
                hit.syscall = syscall;
                merge(hits, address, hit);
            } else if (cop2 && address >= 0x00101FE8L && address < 0x00102D30L) {
                Hit hit = new Hit("libvu0", "COP2 cluster", "pattern", 2, false);
                hit.cop2 = true;
                merge(hits, address, hit);
            }
        }

        // Stable output by address.
        List<Map.Entry<Long, Hit>> ordered = new ArrayList<>(hits.entrySet());
        ordered.sort(Comparator.comparingLong(Map.Entry::getKey));

        SymbolTable symbols = currentProgram.getSymbolTable();
        Namespace global = currentProgram.getGlobalNamespace();
        Namespace root = symbols.getNamespace("PS2SDK", global);
        if (root == null) {
            root = symbols.createNameSpace(global, "PS2SDK", SourceType.USER_DEFINED);
        }

        // Remove stale marks from older runs (the first pass marked game code
        // 0x149BF0 and 0x25C610..0x25C640 as SDK).
        Set<Long> keep = new LinkedHashSet<>();
        for (Map.Entry<Long, Hit> entry : ordered) keep.add(entry.getKey());
        int cleaned = 0;
        FunctionIterator all = currentProgram.getFunctionManager().getFunctions(true);
        while (all.hasNext()) {
            Function function = all.next();
            long address = function.getEntryPoint().getOffset();
            Namespace parent = function.getParentNamespace();
            if (parent != null && underPs2Sdk(parent) && !keep.contains(address)) {
                try {
                    function.setParentNamespace(global);
                    String comment = function.getComment();
                    if (comment != null) {
                        int marker = comment.indexOf("PS2 SDK (");
                        if (marker == 0) {
                            function.setComment(null);
                        } else if (marker > 0) {
                            function.setComment(comment.substring(0, marker).trim());
                        }
                    }
                    ++cleaned;
                } catch (Exception e) {
                    println("IdentifyPs2Sdk: no se pudo limpiar 0x"
                        + Long.toHexString(address) + ": " + e.getMessage());
                }
            }
        }

        // Mark in Ghidra with namespaces + comments, preserving names.
        // This runs before the CSV so the corrected SONY_* name is exported.
        Map<String, Namespace> namespaces = new LinkedHashMap<>();
        int marked = 0;
        for (Map.Entry<Long, Hit> entry : ordered) {
            Function function = getFunctionAt(toAddr(entry.getKey()));
            if (function == null) continue;
            Hit hit = entry.getValue();
            Namespace ns = namespaces.get(hit.library);
            if (ns == null) {
                ns = symbols.getNamespace(hit.library, root);
                if (ns == null) {
                    ns = symbols.createNameSpace(root, hit.library, SourceType.USER_DEFINED);
                }
                namespaces.put(hit.library, ns);
            }
            try {
                function.setParentNamespace(ns);
            } catch (Exception e) {
                // Name conflict in the target namespace: keep the function where
                // it is, but still add the comment below.
            }

            // Correct the known mislabel SONY_libpad2_0025c650 -> libsd.
            String name = function.getName();
            String nameLibrary = libraryFromName(name);
            if (nameLibrary != null && !nameLibrary.equals(hit.library)) {
                String corrected = "SONY_" + hit.library + "_"
                    + String.format("%08x", entry.getKey());
                try {
                    function.setName(corrected, SourceType.USER_DEFINED);
                } catch (Exception e) {
                    // Ignore name conflicts; the comment still records the truth.
                }
            }

            String comment = "PS2 SDK (" + hit.library + ", "
                + (hit.confirmed ? "confirmed" : "candidate") + "): "
                + hit.evidence + " [" + hit.source + "]";
            String old = function.getComment();
            if (old == null || !old.contains("PS2 SDK (")) {
                function.setComment(old == null || old.isEmpty() ? comment : old + " | " + comment);
            }
            ++marked;
        }

        PrintWriter csv = new PrintWriter(new FileWriter(new File(directory, "ps2_sdk_functions.csv")));
        csv.println("address,size,library,confidence,name,evidence,source,syscall,cop2,native");
        PrintWriter ranges = new PrintWriter(new FileWriter(new File(directory, "ps2_sdk_ranges.txt")));
        ranges.println("# Exact SDK function ranges, end exclusive.");
        ranges.println("# Generated by IdentifyPs2Sdk.java; pass this file to ExportAtlas.java.");
        Map<String, Integer> counts = new LinkedHashMap<>();
        int nativeCount = 0;
        for (Map.Entry<Long, Hit> entry : ordered) {
            Function function = getFunctionAt(toAddr(entry.getKey()));
            if (function == null) continue;
            Hit hit = entry.getValue();
            int size = (int) function.getBody().getNumAddresses();
            counts.merge(hit.library, 1, Integer::sum);
            boolean isNative = nativeLibrary(hit.library);
            if (isNative) ++nativeCount;
            csv.printf("0x%08X,%d,%s,%s,%s,\"%s\",%s,%d,%d,%s%n",
                entry.getKey(), size, hit.library,
                hit.confirmed ? "CONFIRMED" : "CANDIDATE",
                escapeCsv(function.getName()), escapeCsv(hit.evidence), hit.source,
                hit.syscall, hit.cop2 ? 1 : 0, isNative ? "yes" : "no");
            ranges.printf("0x%08X 0x%08X   # %-8s %s%n",
                entry.getKey(), entry.getKey() + size, hit.library, function.getName());
        }
        csv.close();
        ranges.close();

        PrintWriter summary = new PrintWriter(new FileWriter(new File(directory, "ps2_sdk_summary.txt")));
        summary.println("Ghidra functions: " + totalFunctions);
        summary.println("SDK functions: " + ordered.size() + ", marked: " + marked
            + ", native stubs: " + nativeCount + ", stale marks cleaned: " + cleaned);
        for (Map.Entry<String, Integer> count : counts.entrySet()) {
            summary.printf("%-10s %d%n", count.getKey(), count.getValue());
        }
        summary.close();

        try {
            currentProgram.save("IdentifyPs2Sdk", monitor);
        } catch (Exception e) {
            println("IdentifyPs2Sdk: save skipped: " + e.getMessage());
        }
        println("IdentifyPs2Sdk: " + ordered.size() + " SDK functions, " + marked
            + " marked, " + nativeCount + " native, " + cleaned + " stale cleaned");
    }
}
