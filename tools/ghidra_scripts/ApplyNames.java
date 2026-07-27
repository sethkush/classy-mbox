// Apply human-assigned names from the annotation pass into the Ghidra DB,
// then emit a reachability / call-graph report.
//   arg[0] = names JSON  {funcs:{"0a09":"name"}, fcmt:{...}, iram:{"2e":"name"}, icmt:{...}}
//   arg[1] = call-graph report output path
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class ApplyNames extends GhidraScript {

    // minimal flat-JSON reader: {"key":{"a":"b",...},...}
    private Map<String, Map<String, String>> readJson(String path) throws IOException {
        StringBuilder sb = new StringBuilder();
        BufferedReader br = new BufferedReader(new FileReader(path));
        String l;
        while ((l = br.readLine()) != null) sb.append(l);
        br.close();
        String s = sb.toString();
        Map<String, Map<String, String>> out = new HashMap<>();
        java.util.regex.Matcher grp = java.util.regex.Pattern
            .compile("\"(\\w+)\"\\s*:\\s*\\{(.*?)\\}", java.util.regex.Pattern.DOTALL)
            .matcher(s);
        while (grp.find()) {
            Map<String, String> m = new LinkedHashMap<>();
            java.util.regex.Matcher kv = java.util.regex.Pattern
                .compile("\"([0-9a-fA-F]+)\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"")
                .matcher(grp.group(2));
            while (kv.find()) m.put(kv.group(1), kv.group(2).replace("\\\"", "\"").replace("\\\\", "\\"));
            out.put(grp.group(1), m);
        }
        return out;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Map<String, Map<String, String>> j = readJson(args[0]);
        Map<String, String> funcs = j.getOrDefault("funcs", new HashMap<>());
        Map<String, String> fcmt  = j.getOrDefault("fcmt",  new HashMap<>());
        Map<String, String> iram  = j.getOrDefault("iram",  new HashMap<>());
        Map<String, String> icmt  = j.getOrDefault("icmt",  new HashMap<>());

        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        // ---- functions ----
        int renamed = 0, created = 0;
        for (Map.Entry<String, String> e : funcs.entrySet()) {
            Address a = toAddr("CODE:" + e.getKey());
            String nm = e.getValue();
            Function f = fm.getFunctionAt(a);
            if (f == null) {
                if (listing.getInstructionAt(a) != null) {
                    f = createFunction(a, nm);
                    if (f != null) created++;
                }
            }
            if (f != null) { f.setName(nm, SourceType.USER_DEFINED); renamed++; }
            else { createLabel(a, nm, true, SourceType.USER_DEFINED); renamed++; }
            String c = fcmt.get(e.getKey());
            if (c != null && !c.isEmpty()) setPlateComment(a, nm + " — " + c);
        }
        println("functions named: " + renamed + " (created " + created + ")");

        // ---- IRAM state variables ----
        AddressSpace intmem = currentProgram.getAddressFactory().getAddressSpace("INTMEM");
        int ir = 0;
        if (intmem != null) {
            for (Map.Entry<String, String> e : iram.entrySet()) {
                Address a = intmem.getAddress(Long.parseLong(e.getKey(), 16));
                createLabel(a, e.getValue(), true, SourceType.USER_DEFINED);
                String c = icmt.get(e.getKey());
                if (c != null && !c.isEmpty()) setEOLComment(a, c);
                ir++;
            }
        }
        println("IRAM variables named: " + ir);

        // ---- named XDATA locations not covered by the SFR map (SETPACK etc) ----
        Map<String, String> xd   = j.getOrDefault("xdata", new HashMap<>());
        Map<String, String> xcmt = j.getOrDefault("xcmt",  new HashMap<>());
        AddressSpace extmem = currentProgram.getAddressFactory().getAddressSpace("EXTMEM");
        int xn = 0;
        if (extmem != null) {
            for (Map.Entry<String, String> e : xd.entrySet()) {
                Address a = extmem.getAddress(Long.parseLong(e.getKey(), 16));
                createLabel(a, e.getValue(), true, SourceType.USER_DEFINED);
                String c = xcmt.get(e.getKey());
                if (c != null && !c.isEmpty()) setEOLComment(a, c);
                xn++;
            }
        }
        println("XDATA locations named: " + xn);

        // ---- reachability / call graph ----
        PrintWriter out = new PrintWriter(new FileWriter(args[1]));
        out.println("# Call graph and reachability");
        out.println();

        // Roots: hardware vectors, PLUS every dispatch-table target. Table
        // entries live in data, so their references belong to no calling
        // function and a pure call-BFS would never reach them — but the
        // dispatcher does jump there at runtime, so they are genuine entry
        // points and must seed the traversal.
        long[] vec = {0x00, 0x03, 0x0b, 0x13, 0x1b, 0x23, 0x2b};
        Set<Address> roots = new LinkedHashSet<>();
        Set<Address> tableTargets = new LinkedHashSet<>();
        for (long v : vec) {
            Address a = toAddr("CODE:" + String.format("%04x", v));
            if (listing.getInstructionAt(a) != null) roots.add(a);
        }
        ReferenceManager rmgr = currentProgram.getReferenceManager();
        java.util.Iterator<Address> srcIt = rmgr.getReferenceSourceIterator(
            currentProgram.getMemory().getMinAddress(), true);
        while (srcIt.hasNext()) {
            Address src = srcIt.next();
            for (Reference r : rmgr.getReferencesFrom(src)) {
                if (r.getReferenceType() == RefType.COMPUTED_CALL
                        && fm.getFunctionContaining(src) == null) {
                    tableTargets.add(r.getToAddress());
                }
            }
        }
        roots.addAll(tableTargets);

        // build callee map
        Map<Address, Set<Address>> callees = new LinkedHashMap<>();
        FunctionIterator fi = fm.getFunctions(true);
        List<Function> all = new ArrayList<>();
        while (fi.hasNext()) all.add(fi.next());
        for (Function f : all) {
            Set<Address> cs = new LinkedHashSet<>();
            for (Function c : f.getCalledFunctions(monitor)) cs.add(c.getEntryPoint());
            callees.put(f.getEntryPoint(), cs);
        }

        // BFS from roots (following calls AND flow into functions)
        Set<Address> reached = new LinkedHashSet<>();
        Deque<Address> work = new ArrayDeque<>();
        for (Address r : roots) {
            Function f = fm.getFunctionContaining(r);
            Address k = (f != null) ? f.getEntryPoint() : r;
            if (reached.add(k)) work.push(k);
        }
        while (!work.isEmpty()) {
            Address cur = work.pop();
            for (Address c : callees.getOrDefault(cur, Collections.emptySet()))
                if (reached.add(c)) work.push(c);
        }

        out.println("## Roots");
        out.println("### Hardware vectors");
        for (long v : vec) {
            Address a = toAddr("CODE:" + String.format("%04x", v));
            if (!roots.contains(a)) continue;
            Function f = fm.getFunctionContaining(a);
            out.println("  " + a + "  " + (f != null ? f.getName() : "(no function)"));
        }
        out.println("### Dispatch-table targets (" + tableTargets.size() + " entry points)");
        for (Address t : tableTargets) {
            Function f = fm.getFunctionContaining(t);
            out.println("  " + t + "  " + (f != null ? f.getName() : "(no function)"));
        }
        out.println();
        out.println("## Reachable from some root: " + reached.size() + " of " + all.size());
        out.println();
        out.println("## NOT reachable from any root");
        out.println("Each of these is either dead code, or reached by a control-flow edge");
        out.println("this model does not yet follow. Investigate before assuming dead.");
        int orphan = 0;
        for (Function f : all) {
            if (!reached.contains(f.getEntryPoint())) {
                Reference[] refs = getReferencesTo(f.getEntryPoint());
                out.println("  " + f.getEntryPoint() + "  " + f.getName()
                            + "   (xrefs: " + refs.length + ")");
                orphan++;
            }
        }
        out.println();
        out.println("total not-directly-reached: " + orphan);
        out.println();
        out.println("## Full call graph");
        for (Function f : all) {
            Set<Address> cs = callees.getOrDefault(f.getEntryPoint(), Collections.emptySet());
            out.print(f.getEntryPoint() + "  " + f.getName() + " ->");
            if (cs.isEmpty()) out.print(" (leaf)");
            for (Address c : cs) {
                Function cf = fm.getFunctionAt(c);
                out.print(" " + (cf != null ? cf.getName() : c.toString()));
            }
            out.println();
        }
        out.close();
        println("call graph written: " + reached.size() + "/" + all.size() + " reached, " + orphan + " not directly reached");
    }
}
