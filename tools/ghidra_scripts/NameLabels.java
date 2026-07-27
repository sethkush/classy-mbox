// Name the remaining decompiler-generated artifacts:
//   1. LAB_CODE_xxxx basic-block join points, given explicit labels so the
//      decompiled C reads `goto <meaningful_name>` instead of an address.
//   2. The CODE-space aliases of SETPACK. The 8051 model has separate CODE
//      and EXTMEM spaces, so a pointer the firmware builds into the SETUP
//      packet block surfaces as DAT_CODE_ffxx with no label attached. We
//      create an uninitialized CODE block over 0xFF28-0xFF2F purely so the
//      same eight names can be attached in that space too. It holds no
//      bytes and changes no analysis — it exists to carry labels.
// arg[0] = "addr:name,addr:name,..." (hex addr, CODE space)
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class NameLabels extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();

        int n = 0;
        if (args.length > 0 && !args[0].isEmpty()) {
            for (String spec : args[0].split(",")) {
                String[] p = spec.split(":");
                if (p.length < 2) continue;
                Address a = toAddr("CODE:" + p[0]);
                createLabel(a, p[1], true, SourceType.USER_DEFINED);
                n++;
            }
        }
        println("block labels named: " + n);

        // CODE-space SETPACK alias block
        AddressSpace code = currentProgram.getAddressFactory().getAddressSpace("CODE");
        Address base = code.getAddress(0xFF28);
        MemoryBlock existing = currentProgram.getMemory().getBlock(base);
        if (existing == null) {
            MemoryBlock b = currentProgram.getMemory().createUninitializedBlock(
                "SETPACK_CODE_ALIAS", base, 8, false);
            b.setComment("CODE-space view of the USB SETUP packet block. The same "
                + "eight bytes are labelled in EXTMEM; this block exists only so "
                + "pointers the decompiler resolves into CODE space carry the same "
                + "names instead of appearing as DAT_CODE_ffxx.");
            b.setRead(true);
            b.setWrite(true);
        }
        String[] fields = {"bmRequestType", "bRequest", "wValueL", "wValueH",
                           "wIndexL", "wIndexH", "wLengthL", "wLengthH"};
        for (int i = 0; i < fields.length; i++) {
            createLabel(code.getAddress(0xFF28 + i), "SETPACK_" + fields[i] + "_code",
                        true, SourceType.USER_DEFINED);
        }
        println("SETPACK CODE-space aliases named: " + fields.length);
    }
}
