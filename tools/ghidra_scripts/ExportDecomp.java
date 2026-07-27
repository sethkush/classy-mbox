// Decompile every function to C. arg[0] = output path.
//@category Export
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.io.FileWriter;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0] : "/tmp/decomp.c";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        DecompInterface d = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        d.setOptions(opts);
        d.toggleCCode(true);
        d.toggleSyntaxTree(true);
        d.setSimplificationStyle("decompile");
        if (!d.openProgram(currentProgram)) {
            println("DECOMPILER FAILED TO OPEN: " + d.getLastMessage());
            out.println("// decompiler unavailable: " + d.getLastMessage());
            out.close();
            return;
        }

        int ok = 0, fail = 0;
        FunctionIterator fi = currentProgram.getFunctionManager().getFunctions(true);
        while (fi.hasNext() && !monitor.isCancelled()) {
            Function f = fi.next();
            DecompileResults res = d.decompileFunction(f, 120, monitor);
            out.println();
            out.println("/* ================================================================");
            out.println(" * " + f.getName() + " @ " + f.getEntryPoint());
            out.println(" * ============================================================== */");
            if (res != null && res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
                ok++;
            } else {
                out.println("// DECOMPILE FAILED: " + (res == null ? "null" : res.getErrorMessage()));
                fail++;
            }
        }
        d.dispose();
        out.close();
        println("decompiled ok=" + ok + " failed=" + fail + " -> " + outPath);
    }
}
