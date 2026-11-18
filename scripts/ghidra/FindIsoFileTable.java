// Labels and reports the ISO file table used by SLUS_203.93.
//
//   0x002B4660: 1416 records of 8 bytes: {u32 lba; u32 size;}
//   0x002B42B8: u16 table A, indexes into the file table
//   0x002B45D0: u16 table B, indexes into the file table
//
// The loader functions (0x001243C0, 0x00124570, 0x001245E0, 0x001248D0)
// read table[index].lba / table[index].size and queue a CD read.
//@category PS2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FindIsoFileTable extends GhidraScript {

    private static final long TABLE = 0x002B4660L;
    private static final int COUNT = 1416;

    private long u32(Address address) throws Exception {
        return Integer.toUnsignedLong(currentProgram.getMemory().getInt(address));
    }

    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        File directory = new File(outDir);
        if (!directory.exists() && !directory.mkdirs()) throw new RuntimeException("cannot create " + outDir);

        Address table = toAddr(TABLE);
        createLabel(table, "g_iso_file_table", true);
        setPlateComment(table, "ISO file table: 1416 x {u32 lba; u32 size;} = 8 bytes. "
            + "Index is the file number used by the loaders at 0x001243C0 / 0x00124570 / 0x001245E0 / 0x001248D0.");

        PrintWriter csv = new PrintWriter(new FileWriter(new File(directory, "iso_file_table_ghidra.csv")));
        csv.println("index,lba,size,sectors");
        for (int i = 0; i < COUNT; ++i) {
            Address at = table.add((long) i * 8);
            long lba = u32(at);
            long size = u32(at.add(4));
            csv.printf("%d,%d,%d,%d%n", i, lba, size, (size + 2047) / 2048);
        }
        csv.close();

        createLabel(toAddr(0x002B42B8L), "g_scene_file_index_a", true);
        createLabel(toAddr(0x002B45D0L), "g_scene_file_index_b", true);
        setPlateComment(toAddr(0x002B42B8L), "u16[396]: scene/module id -> g_iso_file_table index");
        setPlateComment(toAddr(0x002B45D0L), "u16[72]: scene/module id -> g_iso_file_table index");

        PrintWriter refs = new PrintWriter(new FileWriter(new File(directory, "iso_table_refs.txt")));
        for (long target : new long[] { TABLE, TABLE + 4 }) {
            refs.println("references to 0x" + Long.toHexString(target));
            for (Reference reference : getReferencesTo(toAddr(target))) {
                Address from = reference.getFromAddress();
                Function function = getFunctionContaining(from);
                refs.println("  from 0x" + from + "  function "
                    + (function == null ? "(none)" : function.getName() + " @ " + function.getEntryPoint()));
            }
            refs.println();
        }
        refs.close();

        Address first = table;
        Address last = table.add((long) (COUNT - 1) * 8);
        println("table 0x" + Long.toHexString(TABLE) + " .. 0x" + last
            + " entries=" + COUNT + " first lba=" + u32(first) + " size=" + u32(first.add(4)));
    }
}
