/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "Lime Parser Generator", "index.html", [
    [ "Overview", "index.html#autotoc_md1", null ],
    [ "Motivation", "index.html#autotoc_md2", null ],
    [ "Why Lime over Yacc/Bison?", "index.html#autotoc_md3", null ],
    [ "Quick Start", "index.html#autotoc_md4", null ],
    [ "Project Layout", "index.html#autotoc_md5", null ],
    [ "Documentation", "index.html#autotoc_md6", [
      [ "Doxygen", "index.html#autotoc_md7", null ]
    ] ],
    [ "Usage", "index.html#autotoc_md8", [
      [ "Extension Development", "index.html#autotoc_md9", null ]
    ] ],
    [ "Performance", "index.html#autotoc_md10", null ],
    [ "Testing", "index.html#autotoc_md11", null ],
    [ "Dependencies", "index.html#autotoc_md12", null ],
    [ "Contributing", "index.html#autotoc_md13", null ],
    [ "Acknowledgements", "index.html#autotoc_md14", null ],
    [ "License", "index.html#autotoc_md15", null ],
    [ "References", "index.html#autotoc_md16", null ],
    [ "Getting Started", "md_docs_2_g_e_t_t_i_n_g___s_t_a_r_t_e_d.html", [
      [ "Prerequisites", "md_docs_2_g_e_t_t_i_n_g___s_t_a_r_t_e_d.html#autotoc_md18", null ],
      [ "Building the Generator", "md_docs_2_g_e_t_t_i_n_g___s_t_a_r_t_e_d.html#autotoc_md19", null ],
      [ "Your First Grammar", "md_docs_2_g_e_t_t_i_n_g___s_t_a_r_t_e_d.html#autotoc_md20", null ],
      [ "Writing a Driver", "md_docs_2_g_e_t_t_i_n_g___s_t_a_r_t_e_d.html#autotoc_md21", null ],
      [ "Building the Extension Framework", "md_docs_2_g_e_t_t_i_n_g___s_t_a_r_t_e_d.html#autotoc_md22", null ],
      [ "Useful Flags", "md_docs_2_g_e_t_t_i_n_g___s_t_a_r_t_e_d.html#autotoc_md23", null ],
      [ "Next Steps", "md_docs_2_g_e_t_t_i_n_g___s_t_a_r_t_e_d.html#autotoc_md24", null ]
    ] ],
    [ "Concepts", "md_docs_2_c_o_n_c_e_p_t_s.html", [
      [ "Grammars and Parsers", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md26", null ],
      [ "Snapshots", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md27", null ],
      [ "Extensions", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md28", [
        [ "Extension Lifecycle", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md29", null ]
      ] ],
      [ "Conflict Detection", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md30", null ],
      [ "Disambiguation Strategies", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md31", null ],
      [ "Execution Policies", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md32", null ],
      [ "Copy-on-Write and Thread Safety", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md33", null ],
      [ "SIMD Tokenization", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md34", null ],
      [ "JIT Compilation", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md35", null ],
      [ "Module System", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md36", null ],
      [ "Further Reading", "md_docs_2_c_o_n_c_e_p_t_s.html#autotoc_md37", null ]
    ] ],
    [ "Integrating Lime in Your Project", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html", [
      [ "Option 1: Generator Only (No Extension Framework)", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md39", null ],
      [ "Option 2: Generator + Extension Framework (Meson)", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md40", [
        [ "As a Meson subproject", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md41", null ],
        [ "As a system library", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md42", null ]
      ] ],
      [ "Option 3: Generator + Extension Framework (Make / CMake)", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md43", [
        [ "Makefile", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md44", null ],
        [ "CMake", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md45", null ]
      ] ],
      [ "Linking", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md46", null ],
      [ "Headers", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md47", null ],
      [ "Runtime Extension Loading", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md48", null ],
      [ "Generating Parsers at Build Time", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md49", null ],
      [ "Avoiding Symbol Collisions", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md50", [
        [ "Generated parser symbols", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md51", null ],
        [ "Library (extension framework) symbols", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md52", null ]
      ] ],
      [ "Parser Allocation and Reuse", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md53", [
        [ "Pattern A: Allocate once, reset between parses", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md54", null ],
        [ "Pattern B: Stack-allocated parser (zero mallocs)", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md55", null ]
      ] ],
      [ "NDEBUG and Performance", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md56", null ],
      [ "Thread Safety Checklist", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md57", null ],
      [ "Further Reading", "md_docs_2_i_n_t_e_g_r_a_t_i_o_n.html#autotoc_md58", null ]
    ] ],
    [ "Examples", "md_docs_2_e_x_a_m_p_l_e_s.html", [
      [ "Calculator (<span class=\"tt\">examples/calc/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md60", null ],
      [ "JSONB Extension (<span class=\"tt\">examples/jsonb_extension.c</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md61", null ],
      [ "Plugin Template (<span class=\"tt\">examples/plugin_template/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md62", null ],
      [ "PostgreSQL Full Grammar (<span class=\"tt\">examples/pg/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md63", null ],
      [ "PostgreSQL Modular Grammar (<span class=\"tt\">examples/pg_modular/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md64", null ],
      [ "PostgreSQL Subsystem Parsers", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md65", [
        [ "pgbench Expressions (<span class=\"tt\">examples/pgbench/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md66", null ],
        [ "Bootstrap (BKI) Parser (<span class=\"tt\">examples/bootstrap/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md67", null ],
        [ "Isolation Test Parser (<span class=\"tt\">examples/isolation/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md68", null ],
        [ "Synchronous Replication Config (<span class=\"tt\">examples/syncrep/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md69", null ],
        [ "Replication Protocol (<span class=\"tt\">examples/replication/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md70", null ]
      ] ],
      [ "Query Language Parsers", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md71", [
        [ "JSONPath (<span class=\"tt\">examples/jsonpath/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md72", null ],
        [ "XPath 1.0 (<span class=\"tt\">examples/xpath/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md73", null ],
        [ "XQuery 1.0 (<span class=\"tt\">examples/xquery/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md74", null ],
        [ "MongoDB Query Language (<span class=\"tt\">examples/mongodb/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md75", null ],
        [ "Datalog/EDN (<span class=\"tt\">examples/datalog/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md76", null ]
      ] ],
      [ "LLM Oracle Disambiguation (<span class=\"tt\">examples/llm_oracle/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md77", null ],
      [ "Literate Grammar Format (<span class=\"tt\">examples/literate/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md78", null ],
      [ "SQL Dialect Extensions (<span class=\"tt\">contrib/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md79", null ],
      [ "PostgreSQL Integration Guide (<span class=\"tt\">examples/lime_postgres/</span>)", "md_docs_2_e_x_a_m_p_l_e_s.html#autotoc_md80", null ]
    ] ],
    [ "Architecture", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html", [
      [ "Overview", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md82", null ],
      [ "Component Overview", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md83", null ],
      [ "Copy-on-Write Snapshot Architecture", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md84", [
        [ "ParserSnapshot", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md85", null ],
        [ "Lifecycle", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md86", null ],
        [ "Copy-on-Write Modification", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md87", null ]
      ] ],
      [ "LALR(1) Algorithm", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md88", [
        [ "Processing Pipeline (in <span class=\"tt\">lime.c</span>)", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md89", null ],
        [ "Action Table Layout", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md90", null ]
      ] ],
      [ "Extension System", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md91", [
        [ "Design", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md92", null ],
        [ "Extension Lifecycle", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md93", null ],
        [ "Extension Callbacks", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md94", null ],
        [ "Grammar Modifications", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md95", null ],
        [ "Conflict Detection", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md96", null ]
      ] ],
      [ "SIMD Tokenization", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md97", [
        [ "Character Classification", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md98", null ],
        [ "Implementation Tiers", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md99", null ],
        [ "Token Table", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md100", null ]
      ] ],
      [ "LLVM JIT Integration", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md101", [
        [ "Architecture", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md102", null ],
        [ "Graceful Degradation", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md103", null ],
        [ "Compilation Trigger", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md104", null ],
        [ "Runtime Dispatch", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md105", null ]
      ] ],
      [ "Thread Safety", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md106", [
        [ "Synchronization Mechanisms", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md107", null ],
        [ "Key Invariants", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md108", null ]
      ] ],
      [ "Memory Management", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md109", [
        [ "Ownership Model", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md110", null ],
        [ "Allocation Strategy", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md111", null ]
      ] ],
      [ "Build System", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md112", [
        [ "Build Targets", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md113", null ],
        [ "Conditional Compilation", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md114", null ],
        [ "Directory Layout", "md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md115", null ]
      ] ]
    ] ],
    [ "Extensible SQL Parser &ndash; API Reference", "md_docs_2_a_p_i.html", [
      [ "Table of Contents", "md_docs_2_a_p_i.html#autotoc_md117", null ],
      [ "Library Version", "md_docs_2_a_p_i.html#autotoc_md119", null ],
      [ "Snapshot API", "md_docs_2_a_p_i.html#autotoc_md121", [
        [ "Types", "md_docs_2_a_p_i.html#autotoc_md122", null ],
        [ "Functions", "md_docs_2_a_p_i.html#autotoc_md123", [
          [ "lime_snapshot_create", "md_docs_2_a_p_i.html#autotoc_md124", null ],
          [ "lime_snapshot_acquire", "md_docs_2_a_p_i.html#autotoc_md125", null ],
          [ "lime_snapshot_release", "md_docs_2_a_p_i.html#autotoc_md126", null ]
        ] ]
      ] ],
      [ "Parse Context API", "md_docs_2_a_p_i.html#autotoc_md128", [
        [ "Types", "md_docs_2_a_p_i.html#autotoc_md129", null ],
        [ "Functions", "md_docs_2_a_p_i.html#autotoc_md130", [
          [ "parse_begin", "md_docs_2_a_p_i.html#autotoc_md131", null ],
          [ "parse_token", "md_docs_2_a_p_i.html#autotoc_md132", null ],
          [ "parse_end", "md_docs_2_a_p_i.html#autotoc_md133", null ],
          [ "parse_get_snapshot", "md_docs_2_a_p_i.html#autotoc_md134", null ]
        ] ],
        [ "Snapshot-Indirected Table Access", "md_docs_2_a_p_i.html#autotoc_md135", null ]
      ] ],
      [ "Tokenizer API", "md_docs_2_a_p_i.html#autotoc_md137", [
        [ "Types", "md_docs_2_a_p_i.html#autotoc_md138", null ],
        [ "Functions", "md_docs_2_a_p_i.html#autotoc_md139", [
          [ "tokenizer_create", "md_docs_2_a_p_i.html#autotoc_md140", null ],
          [ "tokenizer_destroy", "md_docs_2_a_p_i.html#autotoc_md141", null ],
          [ "tokenizer_next", "md_docs_2_a_p_i.html#autotoc_md142", null ],
          [ "tokenizer_peek", "md_docs_2_a_p_i.html#autotoc_md143", null ],
          [ "tokenizer_position", "md_docs_2_a_p_i.html#autotoc_md144", null ],
          [ "tokenizer_line", "md_docs_2_a_p_i.html#autotoc_md145", null ],
          [ "tokenizer_column", "md_docs_2_a_p_i.html#autotoc_md146", null ]
        ] ],
        [ "SIMD Acceleration", "md_docs_2_a_p_i.html#autotoc_md147", null ]
      ] ],
      [ "Token Table API", "md_docs_2_a_p_i.html#autotoc_md149", [
        [ "Types", "md_docs_2_a_p_i.html#autotoc_md150", null ],
        [ "Functions", "md_docs_2_a_p_i.html#autotoc_md151", [
          [ "create_token_table", "md_docs_2_a_p_i.html#autotoc_md152", null ],
          [ "destroy_token_table", "md_docs_2_a_p_i.html#autotoc_md153", null ],
          [ "lookup_token", "md_docs_2_a_p_i.html#autotoc_md154", null ],
          [ "add_token", "md_docs_2_a_p_i.html#autotoc_md155", null ],
          [ "remove_tokens_by_extension", "md_docs_2_a_p_i.html#autotoc_md156", null ]
        ] ]
      ] ],
      [ "SIMD Character Classification API", "md_docs_2_a_p_i.html#autotoc_md158", [
        [ "Types", "md_docs_2_a_p_i.html#autotoc_md159", null ],
        [ "Functions", "md_docs_2_a_p_i.html#autotoc_md160", [
          [ "get_classify_func", "md_docs_2_a_p_i.html#autotoc_md161", null ],
          [ "classify_scalar", "md_docs_2_a_p_i.html#autotoc_md162", null ],
          [ "classify_simd_avx2 (x86_64 only)", "md_docs_2_a_p_i.html#autotoc_md163", null ],
          [ "classify_simd_neon (ARM only)", "md_docs_2_a_p_i.html#autotoc_md164", null ]
        ] ]
      ] ],
      [ "Extension API", "md_docs_2_a_p_i.html#autotoc_md166", [
        [ "High-Level API (parser.h)", "md_docs_2_a_p_i.html#autotoc_md167", null ],
        [ "Internal Registry API (src/extension.h)", "md_docs_2_a_p_i.html#autotoc_md168", [
          [ "Types", "md_docs_2_a_p_i.html#autotoc_md169", null ],
          [ "Extension Callbacks", "md_docs_2_a_p_i.html#autotoc_md170", null ],
          [ "Registry Functions", "md_docs_2_a_p_i.html#autotoc_md171", null ]
        ] ],
        [ "Grammar Modification Types", "md_docs_2_a_p_i.html#autotoc_md172", null ],
        [ "Conflict Resolution", "md_docs_2_a_p_i.html#autotoc_md173", null ]
      ] ],
      [ "JIT Compilation API", "md_docs_2_a_p_i.html#autotoc_md175", [
        [ "Types", "md_docs_2_a_p_i.html#autotoc_md176", null ],
        [ "Functions", "md_docs_2_a_p_i.html#autotoc_md177", [
          [ "High-Level (parser.h)", "md_docs_2_a_p_i.html#autotoc_md178", null ],
          [ "Low-Level (jit_context.h)", "md_docs_2_a_p_i.html#autotoc_md179", null ],
          [ "Snapshot Integration", "md_docs_2_a_p_i.html#autotoc_md180", null ]
        ] ]
      ] ],
      [ "JIT Policy API", "md_docs_2_a_p_i.html#autotoc_md182", [
        [ "Types", "md_docs_2_a_p_i.html#autotoc_md183", null ],
        [ "Functions", "md_docs_2_a_p_i.html#autotoc_md184", null ]
      ] ],
      [ "Data Structures Reference", "md_docs_2_a_p_i.html#autotoc_md186", [
        [ "ParserSnapshot (src/snapshot.h)", "md_docs_2_a_p_i.html#autotoc_md187", null ]
      ] ],
      [ "Token Type Codes", "md_docs_2_a_p_i.html#autotoc_md189", null ],
      [ "Error Handling Conventions", "md_docs_2_a_p_i.html#autotoc_md191", null ],
      [ "Thread Safety", "md_docs_2_a_p_i.html#autotoc_md193", null ],
      [ "Usage Examples", "md_docs_2_a_p_i.html#autotoc_md195", [
        [ "Basic Tokenization", "md_docs_2_a_p_i.html#autotoc_md196", null ],
        [ "Snapshot Lifecycle", "md_docs_2_a_p_i.html#autotoc_md197", null ],
        [ "Parse Session", "md_docs_2_a_p_i.html#autotoc_md198", null ],
        [ "Extension Registration", "md_docs_2_a_p_i.html#autotoc_md199", null ],
        [ "JIT Compilation with Policy", "md_docs_2_a_p_i.html#autotoc_md200", null ]
      ] ]
    ] ],
    [ "LALR(1) Parsing Algorithm", "md_docs_2_a_l_g_o_r_i_t_h_m.html", [
      [ "Table of Contents", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md202", null ],
      [ "Introduction to LR Parsing", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md204", [
        [ "A Shift-Reduce Example", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md205", null ]
      ] ],
      [ "The LR Parsing Family", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md207", [
        [ "LR(0)", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md208", null ],
        [ "SLR(1) &ndash; Simple LR", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md209", null ],
        [ "LALR(1) &ndash; Look-Ahead LR", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md210", null ],
        [ "Canonical LR(1)", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md211", null ],
        [ "Comparison", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md212", null ]
      ] ],
      [ "LALR(1) vs LL(k) Parsing", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md214", [
        [ "Comparison Table", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md215", null ],
        [ "Why LALR(1)?", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md216", null ]
      ] ],
      [ "Lime's Approach", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md218", [
        [ "Key Differences from Yacc/Bison", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md219", null ],
        [ "Internal Terminology", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md220", null ]
      ] ],
      [ "The 10-Step Pipeline", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md222", [
        [ "Step 1: Parse", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md223", null ],
        [ "Step 2: FindRulePrecedences", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md224", null ],
        [ "Step 3: FindFirstSets", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md225", null ],
        [ "Step 4: FindStates", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md226", null ],
        [ "Step 5: FindLinks", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md227", null ],
        [ "Step 6: FindFollowSets", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md228", null ],
        [ "Step 7: FindActions", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md229", null ],
        [ "Step 8: CompressTables", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md230", null ],
        [ "Step 9: ResortStates", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md231", null ],
        [ "Step 10: ReportTable", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md232", null ]
      ] ],
      [ "State Machine Construction", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md234", [
        [ "Configurations (Items)", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md235", null ],
        [ "Closure", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md236", null ],
        [ "GOTO Function", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md237", null ],
        [ "State Identity", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md238", null ],
        [ "Complete Example", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md239", null ]
      ] ],
      [ "Conflict Resolution", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md241", [
        [ "Types of Conflicts", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md242", null ],
        [ "Precedence-Based Resolution", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md243", null ],
        [ "Reduce/Reduce Resolution", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md244", null ],
        [ "Conflict Action Types", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md245", null ]
      ] ],
      [ "Table Compression", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md247", [
        [ "The Five Arrays", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md248", null ],
        [ "Lookup Algorithm", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md249", null ],
        [ "Action Table Packing (acttab)", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md250", null ],
        [ "State Reordering", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md251", null ],
        [ "Space Savings", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md252", null ]
      ] ],
      [ "Generated Parser Runtime", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md254", [
        [ "Parser State", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md255", null ],
        [ "Parse Loop", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md256", null ],
        [ "Error Recovery", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md257", null ]
      ] ],
      [ "Performance Characteristics", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md259", [
        [ "Time Complexity", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md260", null ],
        [ "Space Complexity", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md261", null ],
        [ "Runtime Parsing Performance", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md262", null ]
      ] ],
      [ "References", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md264", [
        [ "Foundational Works", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md265", null ],
        [ "Textbooks", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md266", null ],
        [ "Related Tools", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md267", null ],
        [ "Action Table Compression", "md_docs_2_a_l_g_o_r_i_t_h_m.html#autotoc_md268", null ]
      ] ]
    ] ],
    [ "Extension Development Guide", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html", [
      [ "Overview", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md270", null ],
      [ "Extension Lifecycle", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md271", [
        [ "1. Registration", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md272", null ],
        [ "2. Loading", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md273", null ],
        [ "3. Unloading", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md274", null ],
        [ "4. Cleanup", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md275", null ]
      ] ],
      [ "Grammar Modifications", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md276", [
        [ "Adding Tokens (<span class=\"tt\">MOD_ADD_TOKEN</span>)", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md277", null ],
        [ "Adding Grammar Rules (<span class=\"tt\">MOD_ADD_RULE</span>)", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md278", null ],
        [ "Modifying Precedence (<span class=\"tt\">MOD_MODIFY_PRECEDENCE</span>)", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md279", null ],
        [ "Adding Non-Terminal Types (<span class=\"tt\">MOD_ADD_TYPE</span>)", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md280", null ],
        [ "Removing Rules (<span class=\"tt\">MOD_REMOVE_RULE</span>)", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md281", null ]
      ] ],
      [ "Implementing Callbacks", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md282", [
        [ "get_modifications (required)", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md283", null ],
        [ "on_conflict (optional)", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md284", null ],
        [ "on_unload (optional)", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md285", null ]
      ] ],
      [ "Complete Walkthrough: JSONB Extension", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md286", [
        [ "What the extension adds", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md287", null ],
        [ "Step 1: Define the modifications array", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md288", null ],
        [ "Step 2: Implement the callbacks", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md289", null ],
        [ "Step 3: Define the ExtensionInfo descriptor", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md290", null ],
        [ "Step 4: Convenience registration function", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md291", null ],
        [ "Compiling the example", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md292", null ]
      ] ],
      [ "How Extensions Are Applied", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md293", null ],
      [ "Conflict Detection Types", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md294", null ],
      [ "Best Practices", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md295", [
        [ "Keep modifications minimal", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md296", null ],
        [ "Use auto-assigned token codes", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md297", null ],
        [ "Inspect the base snapshot", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md298", null ],
        [ "Handle conflicts gracefully", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md299", null ],
        [ "Free dynamically allocated modifications", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md300", null ],
        [ "Use user_data for state", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md301", null ],
        [ "Thread safety", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md302", null ],
        [ "Extension ordering matters", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md303", null ]
      ] ],
      [ "Troubleshooting", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md304", [
        [ "Extension fails to load", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md305", null ],
        [ "Token conflicts", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md306", null ],
        [ "Grammar rule not taking effect", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md307", null ],
        [ "Segmentation fault in on_unload", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md308", null ],
        [ "Parser tables grow too large", "md_docs_2_e_x_t_e_n_s_i_o_n_s.html#autotoc_md309", null ]
      ] ]
    ] ],
    [ "Performance Characteristics", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html", [
      [ "Tokenizer Throughput", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md311", [
        [ "Token Table Performance", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md312", null ]
      ] ],
      [ "Snapshot Operations", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md313", [
        [ "Snapshot Memory", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md314", null ]
      ] ],
      [ "SIMD Character Classification", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md315", [
        [ "When SIMD Helps Most", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md316", null ],
        [ "When SIMD Helps Least", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md317", null ]
      ] ],
      [ "JIT Compilation", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md318", [
        [ "Interpreted (Table-Driven) Baseline", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md319", null ],
        [ "JIT Performance (When LLVM Available)", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md320", null ],
        [ "JIT Policy", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md321", null ],
        [ "JIT Compilation Modes", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md322", null ]
      ] ],
      [ "Extension Overhead", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md323", [
        [ "Loading Costs (One-Time)", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md324", null ],
        [ "Per-Token Runtime Costs", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md325", null ],
        [ "Disambiguation Strategies", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md326", null ],
        [ "Memory Overhead Per Extension", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md327", null ],
        [ "Recommendations", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md328", null ]
      ] ],
      [ "Scaling Behavior", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md329", [
        [ "Concurrent Parsers", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md330", null ],
        [ "Multiple Extensions", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md331", null ]
      ] ],
      [ "Memory Budget Guide", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md332", null ],
      [ "Performance Tuning", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md333", [
        [ "Quick Wins", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md334", null ],
        [ "JIT Tuning", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md335", null ],
        [ "Memory Optimization", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md336", null ]
      ] ],
      [ "Running Benchmarks", "md_docs_2_p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md337", null ]
    ] ],
    [ "Extension Framework Performance Characteristics", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html", [
      [ "Table of Contents", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md340", null ],
      [ "Architecture Overview", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md342", null ],
      [ "Component Overhead Breakdown", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md344", [
        [ "Extension Registry", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md345", null ],
        [ "Conflict Detection", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md346", null ],
        [ "Disambiguation Framework", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md347", null ],
        [ "Parser Forking", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md348", null ]
      ] ],
      [ "Memory Overhead", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md350", [
        [ "Per-Extension Memory", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md351", null ],
        [ "Per-Fork Memory", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md352", null ],
        [ "Aggregate Memory Budget", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md353", null ]
      ] ],
      [ "CPU Cost Per Token", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md355", [
        [ "Baseline (No Extensions)", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md356", null ],
        [ "With Extensions (No Conflicts)", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md357", null ],
        [ "With Extensions (Active Conflicts)", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md358", null ]
      ] ],
      [ "Conflict Type Comparison", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md360", [
        [ "Token-Level Conflicts", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md361", null ],
        [ "Rule-Level Conflicts", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md362", null ],
        [ "Semantic-Level Conflicts", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md363", null ]
      ] ],
      [ "Disambiguation Strategy Performance", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md365", [
        [ "Priority Strategy", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md366", null ],
        [ "Fork-Resolve Strategy", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md367", null ],
        [ "Strategy Comparison", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md368", null ]
      ] ],
      [ "JIT Interaction with Extensions", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md370", [
        [ "JIT Invalidation on Extension Load", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md371", null ],
        [ "Break-Even Analysis with Extensions", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md372", null ],
        [ "JIT Compilation Cost by Grammar Size", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md373", null ]
      ] ],
      [ "Scaling Behavior", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md375", [
        [ "1 Extension", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md376", null ],
        [ "5 Extensions", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md377", null ],
        [ "10 Extensions", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md378", null ],
        [ "50 Extensions", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md379", null ],
        [ "Scaling Summary", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md380", null ]
      ] ],
      [ "Tiebreaker Rule Overhead", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md382", null ],
      [ "Performance Tuning Recommendations", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md384", [
        [ "Extension Loading", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md385", null ],
        [ "Disambiguation Strategy Selection", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md386", null ],
        [ "JIT Integration", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md387", null ],
        [ "Conflict Reduction", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md388", null ],
        [ "Memory Management", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md389", null ]
      ] ],
      [ "Benchmark Reproduction", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md391", [
        [ "Running the Parser Benchmark Suite", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md392", null ],
        [ "Measuring Extension Overhead", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md393", null ],
        [ "Key Metrics to Compare", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md394", null ],
        [ "Memory Profiling", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md395", null ]
      ] ],
      [ "Appendix: Data Structure Sizes", "md_docs_2_e_x_t_e_n_s_i_o_n___p_e_r_f_o_r_m_a_n_c_e.html#autotoc_md397", null ]
    ] ],
    [ "Parser Generator Comparison Guide", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html", [
      [ "Overview", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md400", null ],
      [ "Feature Comparison Matrix", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md401", [
        [ "Grammar and Parsing", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md402", null ],
        [ "Code Generation and Integration", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md403", null ],
        [ "Error Handling", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md404", null ],
        [ "Performance", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md405", null ],
        [ "Runtime Extensibility", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md406", null ],
        [ "Build and Tooling", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md407", null ]
      ] ],
      [ "Detailed Comparisons", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md408", [
        [ "Lime vs Yacc", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md409", null ],
        [ "Lime vs Bison", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md410", null ],
        [ "Lime vs ANTLR", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md411", null ],
        [ "Lime vs Menhir", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md412", null ]
      ] ],
      [ "When to Use Each Tool", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md413", [
        [ "Use Lime when you need:", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md414", null ],
        [ "Use Yacc when you need:", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md415", null ],
        [ "Use Bison when you need:", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md416", null ],
        [ "Use ANTLR when you need:", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md417", null ],
        [ "Use Menhir when you need:", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md418", null ]
      ] ],
      [ "Migration Paths", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md419", null ],
      [ "Performance Benchmarks", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md420", [
        [ "Parse Latency", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md421", null ],
        [ "Tokenizer Throughput", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md422", null ],
        [ "Memory Usage", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md423", null ]
      ] ],
      [ "License Comparison", "md_docs_2_c_o_m_p_a_r_i_s_o_n.html#autotoc_md424", null ]
    ] ],
    [ "Migrating from Bison to Lime", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html", [
      [ "Directive Mapping", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md426", null ],
      [ "Grammar Syntax Differences", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md427", [
        [ "Rule Syntax", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md428", null ],
        [ "Token Declarations", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md429", null ],
        [ "Type Declarations", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md430", null ],
        [ "Precedence with prec", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md431", null ],
        [ "Error Handling", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md432", null ],
        [ "Mid-Rule Actions", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md433", null ]
      ] ],
      [ "Build System Changes", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md434", [
        [ "Bison Build", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md435", null ],
        [ "Lime Build", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md436", null ],
        [ "Parser Interface", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md437", null ]
      ] ],
      [ "Worked Example: PostgreSQL Bootstrap Parser", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md438", [
        [ "Original Bison (bootparse.y excerpt)", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md439", null ],
        [ "Converted Lime (boot_grammar.lime excerpt)", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md440", null ],
        [ "What Changed", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md441", null ]
      ] ],
      [ "Common Gotchas", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md442", null ],
      [ "Quick Reference Card", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___b_i_s_o_n.html#autotoc_md443", null ]
    ] ],
    [ "Migrating from Yacc to Lime", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html", [
      [ "Key Differences from Yacc", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html#autotoc_md445", [
        [ "Parser Model", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html#autotoc_md446", null ],
        [ "Reentrancy", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html#autotoc_md447", null ],
        [ "No yylex Integration", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html#autotoc_md448", null ]
      ] ],
      [ "Directive Mapping (Yacc-Specific)", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html#autotoc_md449", null ],
      [ "Rule Syntax Translation", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html#autotoc_md450", null ],
      [ "Handling <span class=\"tt\">union</span>", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html#autotoc_md451", null ],
      [ "Compatibility Notes", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html#autotoc_md452", null ],
      [ "Quick Migration Checklist", "md_docs_2_m_i_g_r_a_t_i_o_n___f_r_o_m___y_a_c_c.html#autotoc_md453", null ]
    ] ],
    [ "JIT Compilation Analysis for Lime Parser Generator", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html", [
      [ "Overview", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md455", null ],
      [ "Parse Time Breakdown", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md457", null ],
      [ "Cost-Benefit Analysis", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md459", [
        [ "Speedup from JIT on Action Table Lookups", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md460", null ],
        [ "Compilation Cost", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md461", null ],
        [ "Break-Even Point", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md462", null ]
      ] ],
      [ "When JIT Is Beneficial", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md464", null ],
      [ "When JIT Is Not Beneficial", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md465", null ],
      [ "AOT vs Runtime JIT", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md467", [
        [ "AOT Compilation (<span class=\"tt\">lime -j</span>)", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md468", null ],
        [ "Runtime JIT", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md469", null ],
        [ "Recommendation", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md470", null ]
      ] ],
      [ "MCJIT vs OrcJIT Migration", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md472", [
        [ "MCJIT (Legacy)", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md473", null ],
        [ "OrcJIT (Recommended)", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md474", null ],
        [ "Migration Path", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md475", null ]
      ] ],
      [ "Tokenizer JIT - Highest Value Optimization", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md477", [
        [ "Why Tokenizer JIT Matters Most", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md478", null ],
        [ "Trie-Based Keyword Classifier", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md479", null ],
        [ "Expected Performance", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md480", null ],
        [ "Tokenizer JIT vs Action Table JIT", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md481", null ]
      ] ],
      [ "Recommended Configuration", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md483", [
        [ "Default Thresholds", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md484", null ],
        [ "When to Tune", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md485", null ],
        [ "Deployment Decision Tree", "md_docs_2_j_i_t___a_n_a_l_y_s_i_s.html#autotoc_md486", null ]
      ] ]
    ] ],
    [ "Parser Plugin Design", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html", [
      [ "Goals", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md488", null ],
      [ "Non-Goals", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md489", null ],
      [ "Architecture Overview", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md490", null ],
      [ "Plugin Interface (<span class=\"tt\">LimeParserPlugin</span>)", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md491", [
        [ "Required Callbacks", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md492", null ],
        [ "Optional Callbacks", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md493", null ],
        [ "ABI Versioning", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md494", null ]
      ] ],
      [ "Dynamic Loading", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md495", [
        [ "Shared Library Plugins", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md496", null ],
        [ "Static Plugins", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md497", null ],
        [ "Unloading", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md498", null ]
      ] ],
      [ "Plugin Registry", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md499", [
        [ "Data Structure", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md500", null ],
        [ "Concurrency Model", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md501", null ],
        [ "Handle Assignment", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md502", null ]
      ] ],
      [ "Active Parser and Hot-Swap", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md503", [
        [ "Setting the Active Plugin", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md504", null ],
        [ "Hot-Swap Guarantee", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md505", null ],
        [ "Session Isolation", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md506", null ]
      ] ],
      [ "Version Compatibility", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md507", [
        [ "Plugin Version Checks", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md508", null ],
        [ "Upgrade / Downgrade Flow", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md509", null ]
      ] ],
      [ "Error Handling", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md510", null ],
      [ "Memory Management", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md511", [
        [ "Ownership Rules", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md512", null ],
        [ "Snapshot Lifecycle Under the Manager", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md513", null ]
      ] ],
      [ "Integration with Existing Systems", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md514", [
        [ "Extension System", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md515", null ],
        [ "JIT Compilation", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md516", null ],
        [ "Tokenizer", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md517", null ]
      ] ],
      [ "Example: Plugin Implementation", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md518", null ],
      [ "Future Considerations", "md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md519", null ]
    ] ],
    [ "Lime Module Format", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html", [
      [ "Overview", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md521", [
        [ "Why Directive Format?", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md522", null ],
        [ "Comparison to Bison/Yacc", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md523", null ]
      ] ],
      [ "Directive Syntax", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md524", [
        [ "Module Identity", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md525", null ],
        [ "Dependencies", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md526", null ],
        [ "Exports", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md527", null ],
        [ "Imports", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md528", null ]
      ] ],
      [ "Complete Example", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md529", null ],
      [ "Module Composition", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md530", [
        [ "Using lime-compose", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md531", null ],
        [ "How Composition Works", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md532", null ],
        [ "Composed Output", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md533", null ]
      ] ],
      [ "Migration Guide", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md534", [
        [ "From Markdown + YAML Format", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md535", null ],
        [ "From Monolithic Grammar", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md536", null ]
      ] ],
      [ "Best Practices", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md537", [
        [ "Module Organization", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md538", null ],
        [ "Naming Conventions", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md539", null ],
        [ "Dependency Management", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md540", null ],
        [ "File Layout", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md541", null ]
      ] ],
      [ "Validation", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md542", [
        [ "Module Metadata Validation", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md543", null ],
        [ "Composition Validation", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md544", null ]
      ] ],
      [ "Troubleshooting", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md545", [
        [ "\"Unknown declaration keyword: %module_name\"", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md546", null ],
        [ "\"Module 'foo' requires 'bar', which is not available\"", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md547", null ],
        [ "\"Cyclic dependency detected among modules\"", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md548", null ],
        [ "\"Symbol 'X' is exported by both 'A' and 'B'\"", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md549", null ]
      ] ],
      [ "Advanced Topics", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md550", [
        [ "Version Constraints", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md551", null ],
        [ "Re-exporting Symbols", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md552", null ],
        [ "Optional Dependencies", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md553", null ],
        [ "Conditional Compilation", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md554", null ]
      ] ],
      [ "References", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md555", null ],
      [ "Examples", "md_docs_2_m_o_d_u_l_e___f_o_r_m_a_t.html#autotoc_md556", null ]
    ] ],
    [ "Literate Grammar Format Specification", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html", [
      [ "Overview", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md559", null ],
      [ "File Structure", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md560", null ],
      [ "Metadata Block", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md561", [
        [ "Required Fields", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md562", null ],
        [ "Optional Fields", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md563", null ],
        [ "Naming Rules", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md564", null ],
        [ "Capabilities", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md565", null ]
      ] ],
      [ "Code Blocks", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md566", [
        [ "Content Rules", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md567", null ]
      ] ],
      [ "Composition Process", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md568", [
        [ "Cyclic Dependencies", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md569", null ],
        [ "Duplicate Detection", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md570", null ]
      ] ],
      [ "Output Format", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md571", null ],
      [ "Example", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md572", null ],
      [ "Tool Reference", "md_docs_2_l_i_t_e_r_a_t_e___f_o_r_m_a_t.html#autotoc_md573", null ]
    ] ],
    [ "Topics", "topics.html", "topics" ],
    [ "Data Structures", "annotated.html", [
      [ "Data Structures", "annotated.html", "annotated_dup" ],
      [ "Data Structure Index", "classes.html", null ],
      [ "Data Fields", "functions.html", [
        [ "All", "functions.html", null ],
        [ "Variables", "functions_vars.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "Globals", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Functions", "globals_func.html", null ],
        [ "Typedefs", "globals_type.html", null ],
        [ "Enumerations", "globals_enum.html", null ],
        [ "Enumerator", "globals_eval.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"annotated.html",
"group__registry__api.html#gabdb471d2f940b45aaca6d0f45adfafac",
"md_docs_2_e_x_a_m_p_l_e_s.html",
"md_docs_2_p_a_r_s_e_r___p_l_u_g_i_n___d_e_s_i_g_n.html#autotoc_md490"
];

var SYNCONMSG = 'click to disable panel synchronization';
var SYNCOFFMSG = 'click to enable panel synchronization';
var LISTOFALLMEMBERS = 'List of all members';