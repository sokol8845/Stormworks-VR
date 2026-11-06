// Bounded instruction export, without following/decompiling a dispatcher.
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import ghidra.app.util.headless.HeadlessScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.AddressSet;

public class ExportInstructions extends HeadlessScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3 || args.length % 2 != 1)
            throw new IllegalArgumentException("OUTPUT START_VA END_VA [START_VA END_VA ...]");
        StringBuilder output = new StringBuilder("SHA256 " + currentProgram.getExecutableSHA256() + "\n");
        for (int i=1; i<args.length; i+=2) {
            var start=toAddr(args[i]);
            var body=new AddressSet(start,toAddr(args[i+1]).subtract(1));
            if (!new DisassembleCommand(start,body,true).applyTo(currentProgram,monitor))
                throw new IllegalStateException("Could not disassemble " + start);
            var instructions=currentProgram.getListing().getInstructions(body,true);
            while(instructions.hasNext()) {
                var instruction=instructions.next();
                output.append(instruction.getAddress()).append(" ").append(instruction).append("\n");
            }
        }
        Files.writeString(Path.of(args[0]),output,StandardCharsets.UTF_8);
    }
}
