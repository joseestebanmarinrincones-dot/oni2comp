// Prints functions that reference the given addresses (SDK banners/strings).
// Usage: -postScript FindSdkRefs.java <outdir> <hexaddr> [hexaddr...]
//@category PS2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Locale;

public class FindSdkRefs extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new RuntimeException("usage: FindSdkRefs.java <outdir> <hexaddr> [...]");
        File dir = new File(args[0]);
        if (!dir.exists() && !dir.mkdirs()) throw new RuntimeException("cannot create " + args[0]);
        PrintWriter out = new PrintWriter(new FileWriter(new File(dir, "ps2_sdk_refs.txt")));
        for (int i = 1; i < args.length; ++i) {
            String text = args[i].trim().replaceFirst("^0[xX]", "");
            long value = Long.parseLong(text, 16);
            Address target = toAddr(value);
            out.printf("=== target 0x%08X ===%n", value);
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
            boolean any = false;
            while (refs.hasNext()) {
                Reference ref = refs.next();
                Address from = ref.getFromAddress();
                Function f = getFunctionContaining(from);
                any = true;
                out.printf("  from 0x%08X type=%s function=%s entry=0x%08X%n",
                    from.getOffset(), ref.getReferenceType(),
                    f != null ? f.getName() : "<none>",
                    f != null ? f.getEntryPoint().getOffset() : 0);
            }
            if (!any) out.println("  (no references)");
            out.flush();
        }
        out.close();
        println("FindSdkRefs: wrote ps2_sdk_refs.txt");
    }
}
