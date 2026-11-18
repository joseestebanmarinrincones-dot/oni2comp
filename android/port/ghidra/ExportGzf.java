// Ghidra headless script: export the current program as a .gzf file.
// Usage:
//   analyzeHeadless <project_dir> <project_name> -process <program> -noanalysis \
//     -scriptPath <this_dir> -postScript ExportGzf.java <output.gzf>

import ghidra.app.script.GhidraScript;
import ghidra.app.util.exporter.GzfExporter;
import ghidra.framework.model.DomainFile;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.File;

public class ExportGzf extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            throw new IllegalArgumentException(
                "Uso: ExportGzf.java <salida.gzf>");
        }

        File output = new File(args[0]).getAbsoluteFile();
        File parent = output.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IllegalStateException(
                "No se pudo crear el directorio " + parent);
        }

        GzfExporter exporter = new GzfExporter();
        ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();
        DomainFile domainFile = currentProgram.getDomainFile();

        boolean ok;
        if (domainFile != null && exporter.canExportDomainFile(domainFile)) {
            ok = exporter.export(output, domainFile, monitor);
        } else {
            ok = exporter.export(output, currentProgram, null, monitor);
        }

        String log = exporter.getMessageLog().toString();
        if (!log.isEmpty()) {
            println("GZF log: " + log);
        }
        if (!ok) {
            throw new RuntimeException(
                "GzfExporter devolvio false para " + output);
        }

        println("GZF: " + output.getAbsolutePath() +
                " (" + output.length() + " bytes)");
    }
}
