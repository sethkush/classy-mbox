// Enable the Aggressive Instruction Finder analyzer (finds code reachable
// only via computed jumps, which recursive descent misses). Name varies
// across Ghidra versions, so match by substring.
//@category Analysis
import ghidra.app.script.GhidraScript;
import java.util.Map;

public class PreEnableAIF extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<String, String> opts = getCurrentAnalysisOptionsAndValues(currentProgram);
        for (String name : opts.keySet()) {
            if (name.contains("Aggressive") && !name.contains(".")) {
                setAnalysisOption(currentProgram, name, "true");
                println("Enabled: " + name);
            }
        }
    }
}
