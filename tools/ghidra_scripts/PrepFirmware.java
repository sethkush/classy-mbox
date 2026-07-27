// Prepare a raw Mbox firmware image for high-fidelity analysis:
//   1. label all 150 TAS1020B SFRs in EXTMEM from the TI header map
//   2. mark known data regions as data (descriptors, tables, padding) so
//      the gap sweep can't start disassembly mid-descriptor
//   3. seed reset + interrupt vectors, then sweep remaining gaps
// arg[0] = sfr map file ("ffff NAME" per line)
// arg[1] = data region spec ("start:len:comment,..." hex)
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.data.*;
import java.io.*;
import java.util.*;

public class PrepFirmware extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Listing listing = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();

        // ---- 1. SFR labels in EXTMEM ----
        AddressSpace ext = currentProgram.getAddressFactory().getAddressSpace("EXTMEM");
        int labelled = 0;
        if (args.length > 0 && ext != null) {
            BufferedReader br = new BufferedReader(new FileReader(args[0]));
            String line;
            while ((line = br.readLine()) != null) {
                String[] p = line.trim().split("\\s+");
                if (p.length < 2) continue;
                Address a = ext.getAddress(Long.parseLong(p[0], 16));
                createLabel(a, p[1], true, SourceType.USER_DEFINED);
                labelled++;
            }
            br.close();
        }
        println("SFR labels created: " + labelled);

        // ---- 2. known data regions ----
        int dataRegions = 0;
        if (args.length > 1 && !args[1].isEmpty()) {
            for (String spec : args[1].split(",")) {
                String[] p = spec.split(":");
                if (p.length < 2) continue;
                long start = Long.parseLong(p[0], 16);
                long len = Long.parseLong(p[1], 16);
                String cmt = p.length > 2 ? p[2] : "data";
                Address a = toAddr("CODE:" + String.format("%04x", start));
                Address end = a.add(len - 1);
                clearListing(a, end);
                try {
                    createData(a, new ArrayDataType(new ByteDataType(), (int) len, 1));
                } catch (Exception e) { /* already defined */ }
                setPlateComment(a, cmt + " (" + len + " bytes)");
                dataRegions++;
            }
        }
        println("data regions defined: " + dataRegions);

        // ---- 3. seed vectors + sweep ----
        disassemble(toAddr("CODE:0000"));
        analyzeAll(currentProgram);
        for (long v : new long[]{0x03, 0x0b, 0x13, 0x1b, 0x23, 0x2b}) {
            Address va = toAddr("CODE:" + String.format("%04x", v));
            if (listing.getInstructionContaining(va) == null && listing.getDataContaining(va) == null) {
                disassemble(va);
                analyzeAll(currentProgram);
            }
        }

        for (int pass = 0; pass < 12; pass++) {
            int before = count(listing);
            for (MemoryBlock blk : mem.getBlocks()) {
                if (!blk.isInitialized()) continue;
                Address a = blk.getStart(), end = blk.getEnd();
                while (a.compareTo(end) < 0) {
                    Instruction ic = listing.getInstructionContaining(a);
                    if (ic != null) {
                        Address nx = ic.getMaxAddress();
                        if (nx.compareTo(end) >= 0) break;
                        a = nx.add(1);
                        continue;
                    }
                    Data dc = listing.getDefinedDataContaining(a);
                    if (dc != null) {
                        Address nx = dc.getMaxAddress();
                        if (nx.compareTo(end) >= 0) break;
                        a = nx.add(1);
                        continue;
                    }
                    int b = mem.getByte(a) & 0xff;
                    if (b != 0xff && b != 0x00) disassemble(a);
                    if (a.compareTo(end) == 0) break;
                    a = a.add(1);
                }
            }
            analyzeAll(currentProgram);
            int after = count(listing);
            println("sweep pass " + pass + ": " + before + " -> " + after);
            if (after == before) break;
        }
    }

    private int count(Listing l) {
        int n = 0;
        InstructionIterator it = l.getInstructions(true);
        while (it.hasNext()) { it.next(); n++; }
        return n;
    }
}
