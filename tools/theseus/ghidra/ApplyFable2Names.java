// Apply the reviewed naming-pass names (docs/re/NAMES_EVIDENCE.tsv) to the
// Fable II Ghidra program, or undo them from a revert list.
//
//   apply  <names.tsv> <revert.tsv>   rename functions still called FUN_...,
//                                     add a plate comment (rule + confidence),
//                                     write what was changed to revert.tsv
//   revert <revert.tsv>               restore old names/comments, but only on
//                                     functions that still carry our name
//
// Only functions whose entry point is exactly the TSV address are touched.
// Names use SourceType.IMPORTED: above Ghidra's auto names, below hand renames.
// A name Ghidra rejects (characters, duplicates) gets a cleaned / address-
// suffixed fallback; the original is kept in the comment.
//
// Run via tools/theseus/ghidra/apply_names.sh (it stops the ghidra-mcp
// container first: the MCP server holds the project lock).
//@category Fable2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;
import ghidra.util.exception.DuplicateNameException;
import ghidra.util.exception.InvalidInputException;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;

public class ApplyFable2Names extends GhidraScript {

    static final String MARKER = "Naming pass N3b";

    private static String esc(String s) {
        if (s == null) return "";
        return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
    }

    private static String unesc(String s) {
        if (s.isEmpty()) return null;
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '\\' && i + 1 < s.length()) {
                char n = s.charAt(++i);
                b.append(n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n);
            } else b.append(c);
        }
        return b.toString();
    }

    private static String clean(String name) {
        return name.replace("::", "__").replaceAll("[^A-Za-z0-9_]", "_").replaceAll("_+", "_");
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length >= 3 && args[0].equals("apply")) apply(new File(args[1]), new File(args[2]));
        else if (args.length >= 2 && args[0].equals("revert")) revert(new File(args[1]));
        else throw new IllegalArgumentException("usage: apply <names.tsv> <revert.tsv> | revert <revert.tsv>");
    }

    private Address addr(String hex) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace()
                .getAddress(Long.parseLong(hex.replace("0x", ""), 16));
    }

    /** Try the name as-is, then cleaned, then cleaned + address. Returns the name used. */
    private String rename(Function f, String name, String addrHex) throws Exception {
        String[] tries = {name, clean(name), clean(name) + "_" + addrHex.replace("0x", "")};
        Exception last = null;
        for (String t : tries) {
            try {
                f.setName(t, SourceType.IMPORTED);
                return t;
            } catch (InvalidInputException | DuplicateNameException e) {
                last = e;
            }
        }
        throw last;
    }

    private void apply(File tsv, File revertFile) throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        Map<String, Integer> counts = new HashMap<>();
        int fallback = 0;
        try (BufferedReader in = new BufferedReader(new FileReader(tsv));
             PrintWriter rev = new PrintWriter(revertFile, "UTF-8")) {
            rev.println("addr\told_name\tnew_name\told_comment");
            String header = in.readLine();
            String[] cols = header.split("\t", -1);
            Map<String, Integer> ix = new HashMap<>();
            for (int i = 0; i < cols.length; i++) ix.put(cols[i], i);
            String line;
            while ((line = in.readLine()) != null) {
                monitor.checkCancelled();
                String[] r = line.split("\t", -1);
                String a = r[ix.get("addr")], name = r[ix.get("proposed_name")];
                String conf = r[ix.get("confidence")], rule = r[ix.get("rule")];
                Function f = fm.getFunctionAt(addr(a));
                if (f == null) { counts.merge("skip_no_function_at_addr", 1, Integer::sum); continue; }
                String old = f.getName();
                if (!old.startsWith("FUN_")) { counts.merge("skip_already_named", 1, Integer::sum); continue; }

                String used = rename(f, name, a);
                if (!used.equals(name) && ++fallback <= 15)
                    println("ApplyFable2Names fallback: " + a + " " + name + " -> " + used);

                StringBuilder c = new StringBuilder(MARKER + " (docs/re/NAMES_EVIDENCE.tsv): ");
                c.append("rule=").append(rule).append(", confidence=").append(conf);
                String[] extra = {"lua_table", "library", "class"};
                for (String k : extra) {
                    Integer i = ix.get(k);
                    if (i != null && i < r.length && !r[i].isEmpty()) c.append(", ").append(k).append('=').append(r[i]);
                }
                if (rule.equals("vtable") && conf.equals("medium")) c.append(" (inherited slot: class may be a base/sibling)");
                if (!used.equals(name)) c.append("\nProposed name (Ghidra rejected it as-is): ").append(name);
                String oldComment = f.getComment();
                f.setComment(oldComment == null ? c.toString() : oldComment + "\n" + c);

                rev.println(a + "\t" + esc(old) + "\t" + esc(used) + "\t" + esc(oldComment));
                counts.merge("renamed_" + rule + "_" + conf, 1, Integer::sum);
                counts.merge("renamed_total", 1, Integer::sum);
            }
        }
        println("ApplyFable2Names apply: " + counts + ", fallback_names=" + fallback);
        println("ApplyFable2Names revert list written: " + revertFile);
    }

    private void revert(File revertFile) throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        Map<String, Integer> counts = new HashMap<>();
        try (BufferedReader in = new BufferedReader(new FileReader(revertFile))) {
            in.readLine();
            String line;
            while ((line = in.readLine()) != null) {
                String[] r = line.split("\t", -1);
                Function f = fm.getFunctionAt(addr(r[0]));
                if (f == null || !f.getName().equals(unesc(r[2]))) {
                    counts.merge("skip_changed_since", 1, Integer::sum);
                    continue;
                }
                String old = unesc(r[1]);
                // FUN_ names are Ghidra defaults: DEFAULT source restores the auto name.
                if (old.startsWith("FUN_")) {
                    try { f.setName(null, SourceType.DEFAULT); }
                    catch (Exception e) { f.setName(old, SourceType.DEFAULT); }
                } else f.setName(old, SourceType.USER_DEFINED);
                f.setComment(unesc(r[3]));
                counts.merge("reverted", 1, Integer::sum);
            }
        }
        println("ApplyFable2Names revert: " + counts);
    }
}
