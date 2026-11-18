// One-off diagnostic: lists every Ghidra function with syscall/COP2 evidence.
// Usage: analyzeHeadless <proj> <name> -process SLUS_203.93 -noanalysis \
//          -scriptPath scripts/ghidra -postScript ScanPs2Patterns.java <outdir>
//@category PS2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class ScanPs2Patterns extends GhidraScript {

    private static final Pattern SYSCALL_V1 =
        Pattern.compile("addiu\\s+\\$?v1\\s*,\\s*\\$?zero\\s*,\\s*(-?0x[0-9a-fA-F]+|-?\\d+)");

    private static int parseNumber(String text) {
        String value = text.trim().toLowerCase(Locale.ROOT);
        if (value.startsWith("0x")) return (int) Long.parseLong(value.substring(2), 16);
        return Integer.parseInt(value);
    }

    private static boolean isCop2(String text) {
        String s = text.toLowerCase(Locale.ROOT);
        return s.contains("$vf") || s.contains("lqc2") || s.contains("sqc2")
            || s.contains("qmfc2") || s.contains("qmtc2") || s.contains("cfc2")
            || s.contains("ctc2") || s.contains("vcall") || s.contains("vwaitq")
            || s.contains("vlqi") || s.contains("vsqi");
    }

    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        File directory = new File(outDir);
        if (!directory.exists() && !directory.mkdirs()) throw new RuntimeException("cannot create " + outDir);

        PrintWriter csv = new PrintWriter(new FileWriter(new File(directory, "ps2_function_scan.csv")));
        csv.println("address,size,name,syscall,cop2_count,first_cop2,first_instructions");

        int syscalls = 0, cop2Functions = 0, total = 0;
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext()) {
            Function function = functions.next();
            ++total;
            long address = function.getEntryPoint().getOffset();
            int syscall = -1;
            int cop2 = 0;
            String firstCop2 = "";
            StringBuilder first = new StringBuilder();

            InstructionIterator instructions =
                currentProgram.getListing().getInstructions(function.getBody(), true);
            while (instructions.hasNext()) {
                Instruction instruction = instructions.next();
                String text = instruction.toString();
                if (first.length() < 160) {
                    if (first.length() > 0) first.append(" ; ");
                    first.append(text);
                }
                if (instruction.getMnemonicString().equalsIgnoreCase("syscall")) {
                    Instruction previous = instruction.getPrevious();
                    for (int i = 0; i < 4 && previous != null; ++i, previous = previous.getPrevious()) {
                        String before = previous.toString().toLowerCase(Locale.ROOT);
                        Matcher matcher = SYSCALL_V1.matcher(before);
                        if (matcher.find()) {
                            try {
                                syscall = parseNumber(matcher.group(1));
                            } catch (NumberFormatException e) {
                                syscall = -2;
                            }
                            break;
                        }
                    }
                    if (syscall == -1) syscall = -2;
                }
                if (isCop2(text)) {
                    if (cop2 == 0) firstCop2 = text;
                    ++cop2;
                }
            }

            if (syscall != -1) ++syscalls;
            if (cop2 > 0) ++cop2Functions;
            csv.printf("0x%08X,%d,%s,%d,%d,\"%s\",\"%s\"%n",
                address, function.getBody().getNumAddresses(), function.getName(),
                syscall, cop2, firstCop2.replace('"', '\''),
                first.toString().replace('"', '\''));
        }
        csv.close();
        println("ScanPs2Patterns: " + total + " functions, " + syscalls
            + " with syscall, " + cop2Functions + " with COP2");
    }
}
