// Create call references from dispatch-table entries to their targets, so
// reachability analysis can follow table-driven control flow.
// arg[0] = specs "base:count:stride:offset:label,..." (all hex except count)
//          entry i target = BE16 at (base + i*stride + offset)
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;

public class TableRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) return;
        Memory mem = currentProgram.getMemory();
        ReferenceManager rm = currentProgram.getReferenceManager();
        int made = 0;

        for (String spec : args[0].split(",")) {
            String[] p = spec.split(":");
            if (p.length < 4) continue;
            long base   = Long.parseLong(p[0], 16);
            int  count  = Integer.parseInt(p[1]);
            int  stride = Integer.parseInt(p[2], 16);
            int  off    = Integer.parseInt(p[3], 16);
            String label = p.length > 4 ? p[4] : "table";

            for (int i = 0; i < count; i++) {
                Address src = toAddr("CODE:" + String.format("%04x", base + (long) i * stride));
                Address at  = src.add(off);
                int hi = mem.getByte(at) & 0xff;
                int lo = mem.getByte(at.add(1)) & 0xff;
                int tgt = (hi << 8) | lo;
                if (tgt == 0 || tgt > 0x1fee) continue;
                Address dst = toAddr("CODE:" + String.format("%04x", tgt));
                rm.addMemoryReference(src, dst, RefType.COMPUTED_CALL,
                                      SourceType.USER_DEFINED, 0);
                made++;
                // make sure the target is a function so the call graph sees it
                if (getFunctionAt(dst) == null && getInstructionAt(dst) != null) {
                    createFunction(dst, null);
                }
            }
            println("table " + label + " @0x" + p[0] + ": " + count + " entries wired");
        }
        println("total references created: " + made);
    }
}
