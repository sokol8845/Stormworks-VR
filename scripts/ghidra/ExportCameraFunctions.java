// Targeted read-only investigation. Bounds come from the PE exception table.
// Run with analyzeHeadless -noanalysis -readOnly -deleteProject.
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import ghidra.app.util.headless.HeadlessScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.SourceType;

public class ExportCameraFunctions extends HeadlessScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3 || args.length % 2 != 1)
            throw new IllegalArgumentException("OUTPUT_JSON START_VA END_VA [START_VA END_VA ...]");
        JsonObject report = new JsonObject();
        report.addProperty("executable_sha256", currentProgram.getExecutableSHA256());
        report.addProperty("analysis", "Targeted disassembly only; no global auto-analysis or complete call graph");
        JsonArray functions = new JsonArray();
        DecompInterface decompiler = new DecompInterface();
        try {
            for (int i=1; i<args.length; i+=2) {
                Address start = toAddr(args[i]);
                Address end = toAddr(args[i+1]);
                AddressSet body = new AddressSet(start, end.subtract(1));
                if (!new DisassembleCommand(start, body, true).applyTo(currentProgram, monitor))
                    throw new IllegalStateException("Could not disassemble " + start);
                Function function = getFunctionAt(start);
                if (function == null)
                    function = currentProgram.getFunctionManager().createFunction(
                        "camera_candidate_" + start, start, body, SourceType.USER_DEFINED);
                JsonObject item = new JsonObject();
                item.addProperty("start", start.toString());
                item.addProperty("end_exclusive", end.toString());
                JsonArray instructions = new JsonArray();
                InstructionIterator iterator = currentProgram.getListing().getInstructions(body, true);
                while (iterator.hasNext()) {
                    Instruction instruction = iterator.next();
                    instructions.add(instruction.getAddress() + " " + instruction.toString());
                }
                item.add("instructions", instructions);
                functions.add(item);
            }
            if (!decompiler.openProgram(currentProgram))
                throw new IllegalStateException(decompiler.getLastMessage());
            for (int i=1; i<args.length; i+=2) {
                Function function = getFunctionAt(toAddr(args[i]));
                DecompileResults result = decompiler.decompileFunction(function, Integer.getInteger("swvr.decompileSeconds", 45), monitor);
                JsonObject item = functions.get((i-1)/2).getAsJsonObject();
                item.addProperty("decompile_completed", result.decompileCompleted());
                item.addProperty("error", result.getErrorMessage());
                if (result.decompileCompleted()) item.addProperty("c", result.getDecompiledFunction().getC());
            }
        } finally { decompiler.dispose(); }
        report.add("functions", functions);
        Files.writeString(Path.of(args[0]), new GsonBuilder().setPrettyPrinting().create().toJson(report), StandardCharsets.UTF_8);
        println("SWVR exported " + functions.size() + " candidate functions");
    }
}
