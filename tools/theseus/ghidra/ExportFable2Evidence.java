// Export naming evidence from the Fable II Ghidra program (read-only).
//
// Writes JSON Lines files to the directory given as the first script argument
// (default /exports/fable2):
//   strings.jsonl  every defined string that is referenced, with the functions
//                  that reference it: {"addr","value","kind","funcs":[...]}
//   rtti.jsonl     MSVC RTTI classes found from their type descriptors, with
//                  the vtables that point at them and each vtable's function
//                  entries: {"class","mangled","td","vtables":[{"col","offset","vtable","funcs":[...]}]}
//   functions.jsonl every function Ghidra knows: {"entry","name","size"}
//
// Run via tools/theseus/ghidra/export.sh (copies the project first, so the
// live, locked project used by the Ghidra MCP server is never touched).
//@category Fable2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class ExportFable2Evidence extends GhidraScript {

    private static String json(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            switch (c) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append("\\n"); break;
                case '\r': b.append("\\r"); break;
                case '\t': b.append("\\t"); break;
                default:
                    if (c < 0x20) b.append(String.format("\\u%04x", (int) c));
                    else b.append(c);
            }
        }
        return b.append('"').toString();
    }

    private static String hex(Address a) {
        return "\"0x" + String.format("%08X", a.getOffset()) + "\"";
    }

    private String functionsReferencing(Address target, FunctionManager fm) {
        Set<String> funcs = new LinkedHashSet<>();
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(target);
        while (it.hasNext()) {
            Reference r = it.next();
            Function f = fm.getFunctionContaining(r.getFromAddress());
            if (f != null) funcs.add(hex(f.getEntryPoint()));
        }
        return "[" + String.join(",", funcs) + "]";
    }

    /** All 4-byte-aligned big-endian words in initialized memory equal to value. */
    private List<Address> findPointersTo(long value) throws Exception {
        List<Address> hits = new ArrayList<>();
        Memory mem = currentProgram.getMemory();
        byte[] pattern = {(byte) (value >>> 24), (byte) (value >>> 16), (byte) (value >>> 8), (byte) value};
        for (MemoryBlock block : mem.getBlocks()) {
            if (!block.isInitialized() || block.isExecute()) continue;
            Address cur = block.getStart();
            while (cur != null && cur.compareTo(block.getEnd()) <= 0) {
                Address hit = mem.findBytes(cur, block.getEnd(), pattern, null, true, monitor);
                if (hit == null) break;
                if ((hit.getOffset() & 3) == 0) hits.add(hit);
                cur = hit.add(1);
            }
        }
        return hits;
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File outDir = new File(args.length > 0 ? args[0] : "/exports/fable2");
        outDir.mkdirs();
        FunctionManager fm = currentProgram.getFunctionManager();
        Memory mem = currentProgram.getMemory();

        // 1. functions
        int nFuncs = 0;
        try (PrintWriter w = new PrintWriter(new File(outDir, "functions.jsonl"), "UTF-8")) {
            FunctionIterator fi = fm.getFunctions(true);
            while (fi.hasNext() && !monitor.isCancelled()) {
                Function f = fi.next();
                AddressSetView body = f.getBody();
                w.println("{\"entry\":" + hex(f.getEntryPoint()) + ",\"name\":" + json(f.getName())
                        + ",\"size\":" + body.getNumAddresses() + "}");
                nFuncs++;
            }
        }
        println("functions: " + nFuncs);

        // 2. referenced strings
        int nStrings = 0, nReferenced = 0;
        try (PrintWriter w = new PrintWriter(new File(outDir, "strings.jsonl"), "UTF-8")) {
            DataIterator di = currentProgram.getListing().getDefinedData(true);
            while (di.hasNext() && !monitor.isCancelled()) {
                Data d = di.next();
                if (!d.hasStringValue()) continue;
                nStrings++;
                String funcs = functionsReferencing(d.getAddress(), fm);
                if (funcs.equals("[]")) continue;
                nReferenced++;
                Object v = d.getValue();
                w.println("{\"addr\":" + hex(d.getAddress()) + ",\"value\":" + json(String.valueOf(v))
                        + ",\"kind\":" + json(d.getDataType().getName()) + ",\"funcs\":" + funcs + "}");
            }
        }
        println("strings: " + nStrings + " (referenced from code: " + nReferenced + ")");

        // 3. RTTI: TypeDescriptor = {vftable*, spare, char name[]}, name at +8.
        //    CompleteObjectLocator = {sig, offset, cdOffset, TypeDescriptor*, ClassHierarchy*};
        //    the word before a vtable points at its COL.
        int nClasses = 0, nVtables = 0;
        try (PrintWriter w = new PrintWriter(new File(outDir, "rtti.jsonl"), "UTF-8")) {
            DataIterator di = currentProgram.getListing().getDefinedData(true);
            List<Data> names = new ArrayList<>();
            while (di.hasNext()) {
                Data d = di.next();
                if (d.hasStringValue() && String.valueOf(d.getValue()).startsWith(".?AV")) names.add(d);
            }
            for (Data name : names) {
                if (monitor.isCancelled()) break;
                String mangled = String.valueOf(name.getValue());
                Address td = name.getAddress().subtract(8);
                StringBuilder vts = new StringBuilder();
                for (Address colField : findPointersTo(td.getOffset())) {
                    Address col = colField.subtract(12);           // COL.pTypeDescriptor is at +12
                    if (mem.getInt(col) != 0) continue;             // COL.signature is 0 on 32-bit
                    int offset = mem.getInt(col.add(4));
                    for (Address colPtr : findPointersTo(col.getOffset())) {
                        Address vtable = colPtr.add(4);
                        List<String> entries = new ArrayList<>();
                        for (int i = 0; i < 1024; i++) {
                            long p = mem.getInt(vtable.add(4L * i)) & 0xFFFFFFFFL;
                            Address pa = toAddr(p);
                            Function f = fm.getFunctionAt(pa);
                            if (f == null) break;
                            entries.add(hex(f.getEntryPoint()));
                        }
                        if (entries.isEmpty()) continue;
                        if (vts.length() > 0) vts.append(',');
                        vts.append("{\"col\":").append(hex(col)).append(",\"offset\":").append(offset)
                           .append(",\"vtable\":").append(hex(vtable)).append(",\"funcs\":[")
                           .append(String.join(",", entries)).append("]}");
                        nVtables++;
                    }
                }
                // ".?AVInner@Outer@@" -> "Outer::Inner" (MSVC lists the innermost
                // name first). Templates ("?$...") are left mangled.
                String inner = mangled.substring(4, mangled.length() - 2);
                String cls = inner;
                if (!inner.contains("?")) {
                    String[] parts = inner.split("@");
                    StringBuilder sb = new StringBuilder();
                    for (int i = parts.length - 1; i >= 0; i--) {
                        if (parts[i].isEmpty()) continue;
                        if (sb.length() > 0) sb.append("::");
                        sb.append(parts[i]);
                    }
                    cls = sb.toString();
                }
                w.println("{\"class\":" + json(cls) + ",\"mangled\":" + json(mangled) + ",\"td\":" + hex(td)
                        + ",\"vtables\":[" + vts + "]}");
                nClasses++;
            }
        }
        println("rtti classes: " + nClasses + " vtables: " + nVtables);
        println("written to " + outDir.getAbsolutePath());
    }
}
