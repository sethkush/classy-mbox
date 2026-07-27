// Export full disassembly listing: address, raw bytes, instruction, xrefs,
// function boundaries. Output path passed as script arg[0].
//@category Export
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.io.FileWriter;

public class ExportListing extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0] : "/tmp/listing.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        ReferenceManager refMgr = currentProgram.getReferenceManager();
        FunctionManager funcMgr = currentProgram.getFunctionManager();

        // Raw binary import leaves no entry point: seed the reset vector,
        // analyze, then seed each interrupt vector ONLY if it isn't already
        // mid-instruction (some firmwares route straight-line code through
        // unused vector slots — blind seeding there splits real instructions).
        if (!listing.getInstructions(true).hasNext()) {
            disassemble(toAddr("CODE:0000"));
            analyzeAll(currentProgram);
            long[] vectors = {0x03, 0x0b, 0x13, 0x1b, 0x23, 0x2b};
            for (long v : vectors) {
                Address va = toAddr("CODE:" + String.format("%04x", v));
                if (listing.getInstructionContaining(va) == null) {
                    disassemble(va);
                    analyzeAll(currentProgram);
                }
            }

            // Sweep: recursive descent can't reach code behind computed
            // jumps (Rev 20/22 both use ljmp dispatch tables). Walk every
            // remaining gap and force disassembly at each byte, skipping
            // runs of 0xFF/0x00 padding. Repeat until no new code appears.
            ghidra.program.model.mem.Memory m0 = currentProgram.getMemory();
            for (int pass = 0; pass < 12; pass++) {
                int before = 0;
                InstructionIterator ci = listing.getInstructions(true);
                while (ci.hasNext()) { ci.next(); before++; }

                for (ghidra.program.model.mem.MemoryBlock blk : m0.getBlocks()) {
                    if (!blk.isInitialized()) continue;
                    Address a = blk.getStart();
                    Address end = blk.getEnd();
                    while (a.compareTo(end) < 0) {
                        Instruction ic = listing.getInstructionContaining(a);
                        if (ic != null) {
                            Address nx = ic.getMaxAddress();
                            if (nx.compareTo(end) >= 0) break;
                            a = nx.add(1);
                            continue;
                        }
                        int b = m0.getByte(a) & 0xff;
                        if (b != 0xff && b != 0x00) disassemble(a);
                        if (a.compareTo(end) == 0) break;
                        a = a.add(1);
                    }
                }
                analyzeAll(currentProgram);

                int after = 0;
                ci = listing.getInstructions(true);
                while (ci.hasNext()) { ci.next(); after++; }
                println("sweep pass " + pass + ": " + before + " -> " + after + " instructions");
                if (after == before) break;
            }
        }

        InstructionIterator it = listing.getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            Address a = ins.getAddress();

            Function f = funcMgr.getFunctionAt(a);
            if (f != null) {
                out.println();
                out.println("; ======== FUNCTION " + f.getName() + " @ " + a + " ========");
            }

            // labels
            Symbol[] syms = currentProgram.getSymbolTable().getSymbols(a);
            for (Symbol s : syms) {
                if (s.getSource() != SourceType.DEFAULT || f == null) {
                    out.println(s.getName() + ":");
                }
            }

            StringBuilder bytes = new StringBuilder();
            for (byte b : ins.getBytes()) bytes.append(String.format("%02x", b));

            StringBuilder xrefs = new StringBuilder();
            ReferenceIterator ri = refMgr.getReferencesTo(a);
            int n = 0;
            while (ri.hasNext() && n < 8) {
                Reference r = ri.next();
                if (xrefs.length() > 0) xrefs.append(",");
                xrefs.append(r.getFromAddress());
                n++;
            }

            out.printf("%s  %-8s  %-30s%s%n",
                a, bytes, ins.toString(),
                xrefs.length() > 0 ? "  ; XREF from " + xrefs : "");
        }

        // Gap dump: every byte not covered by an instruction, as hex rows.
        // Guarantees no byte of the image is invisible in this export.
        out.println();
        out.println("; ======== GAPS (bytes not disassembled — data tables or unreached code) ========");
        ghidra.program.model.mem.Memory mem = currentProgram.getMemory();
        for (ghidra.program.model.mem.MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized()) continue;
            Address a = blk.getStart();
            Address end = blk.getEnd();
            boolean done = false;
            while (!done && a.compareTo(end) <= 0) {
                Instruction ic = listing.getInstructionContaining(a);
                if (ic != null) {
                    Address next = ic.getMaxAddress();
                    if (next.compareTo(end) >= 0) break;
                    a = next.add(1);
                    continue;
                }
                // start of a gap — collect until next instruction or block end
                Address gs = a;
                StringBuilder hex = new StringBuilder();
                int count = 0;
                while (listing.getInstructionContaining(a) == null) {
                    hex.append(String.format("%02x", mem.getByte(a) & 0xff));
                    count++;
                    if (a.compareTo(end) == 0) { done = true; break; }
                    a = a.add(1);
                    if (count % 16 == 0) hex.append("\n" + a + "  ");
                    else hex.append(" ");
                }
                out.println();
                out.println("; GAP " + gs + " (" + count + " bytes):");
                out.println(gs + "  " + hex.toString().trim());
            }
        }

        out.close();
        println("Exported to " + outPath);
    }
}
