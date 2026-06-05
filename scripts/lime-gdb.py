# Lime GDB pretty-printers.
#
# Loads automatically if you `source` this file from your `.gdbinit` or
# from a per-project gdb session.  Drop into your repo's working tree:
#
#   echo "source $PWD/scripts/lime-gdb.py" >> ~/.gdbinit
#
# Or one-shot inside gdb:
#
#   (gdb) source scripts/lime-gdb.py
#
# Provides pretty-printers for the most-debugged Lime types:
#
#   ParserSnapshot     -- with magic / refcount / nrule + pointers
#   ParseContext       -- with the snapshot it pins + engine state
#   ParseEngine        -- runtime parser internals (stack depth etc.)
#   ParseStack         -- depth and capacity at a glance
#   LimeLexCompiled    -- emitted-lexer compile state (for emit_*.c bugs)
#
# And convenience commands:
#
#   lime-snapshot <expr>   -- decode magic + dump key fields of a snapshot
#   lime-stack <ctx>       -- print parse stack contents (state + symbol)
#   lime-actions <snap>    -- show yy_action / yy_lookahead head and tail
#
# Maintenance: the pretty-printers walk struct fields by name, so
# changes to layout (cf. v0.10.0's hot-field reordering) don't break
# them as long as field names stay the same.  If a struct definition
# moves, update the matching class below.

import gdb
import re


def _str(val):
    """Best-effort safe-string conversion of a gdb.Value."""
    try:
        return str(val)
    except Exception as e:
        return f"<unprintable: {e}>"


# ----------------------------------------------------------------------
# Pretty-printers
# ----------------------------------------------------------------------

class ParserSnapshotPrinter:
    """Pretty-printer for struct ParserSnapshot."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        if int(self.val.address) == 0:
            return "(ParserSnapshot *) NULL"
        try:
            magic = int(self.val["magic"]) & 0xFFFFFFFF
            magic_str = ("LIME" if magic == 0x4C494D45 else f"BAD(0x{magic:08x})")
            abi = int(self.val["abi_version"])
            ver = int(self.val["version"])
            # refcount is _Atomic; fetch via ._M_i if needed (gcc's libstdc++)
            try:
                rc = int(self.val["refcount"])
            except Exception:
                rc = int(self.val["refcount"]["_M_i"])
            nrule = int(self.val["nrule"])
            nstate = int(self.val["nstate"])
            return (f"ParserSnapshot{{magic={magic_str} abi={abi} ver={ver} "
                    f"refcount={rc} nrule={nrule} nstate={nstate}}}")
        except Exception as e:
            return f"<ParserSnapshot: error reading fields: {e}>"

    def display_hint(self):
        return "string"


class ParseContextPrinter:
    """Pretty-printer for struct ParseContext."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        if int(self.val.address) == 0:
            return "(ParseContext *) NULL"
        try:
            snap = self.val["snapshot"]
            engine = self.val["engine"]
            try:
                borrowed = int(self.val["borrowed_snapshot"])
                bowed_str = " borrowed" if borrowed else ""
            except Exception:
                bowed_str = ""
            engine_str = ("none" if int(engine) == 0
                          else f"@0x{int(engine):x}")
            snap_str = ("NULL" if int(snap) == 0
                        else f"@0x{int(snap):x}")
            return f"ParseContext{{snap={snap_str} engine={engine_str}{bowed_str}}}"
        except Exception as e:
            return f"<ParseContext: error: {e}>"


class ParseEnginePrinter:
    """Pretty-printer for struct ParseEngine (parse_engine.c internal)."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            init = int(self.val["initialised"])
            acc = int(self.val["accepted"])
            err = int(self.val["errored"])
            stk = self.val["stack"]
            base = int(stk["base"])
            top = int(stk["top"])
            cap = int(stk["capacity"])
            depth = (top - base) // 8 if base and top else 0  # entries are uint64_t pairs
            flags = []
            if init: flags.append("init")
            if acc: flags.append("ACCEPTED")
            if err: flags.append("ERRORED")
            flag_str = "|".join(flags) if flags else "fresh"
            return (f"ParseEngine{{{flag_str} stack:depth={depth}/{cap}}}")
        except Exception as e:
            return f"<ParseEngine: error: {e}>"


class LimeLexCompiledPrinter:
    """Pretty-printer for struct LimeLexCompiled (emit_*.c debug)."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            n_states = int(self.val["state_count"])
            return f"LimeLexCompiled{{n_states={n_states}}}"
        except Exception as e:
            return f"<LimeLexCompiled: error: {e}>"


def _lookup(val):
    """Pretty-printer dispatcher."""
    t = val.type
    if t.code == gdb.TYPE_CODE_PTR:
        if int(val) == 0:
            return None
        try:
            t = t.target()
            val = val.dereference()
        except Exception:
            return None
    name = t.strip_typedefs().name
    if name is None:
        return None
    return {
        "ParserSnapshot":   ParserSnapshotPrinter,
        "ParseContext":     ParseContextPrinter,
        "ParseEngine":      ParseEnginePrinter,
        "LimeLexCompiled":  LimeLexCompiledPrinter,
    }.get(name, lambda v: None)(val)


gdb.pretty_printers.append(_lookup)


# ----------------------------------------------------------------------
# Convenience commands
# ----------------------------------------------------------------------

class LimeSnapshotCmd(gdb.Command):
    """Dump key fields of a ParserSnapshot, including magic decode.

    Usage:  lime-snapshot <expr>
    Example: lime-snapshot ctx->snapshot
    """
    def __init__(self):
        super().__init__("lime-snapshot", gdb.COMMAND_USER)

    def invoke(self, arg, _from_tty):
        val = gdb.parse_and_eval(arg)
        if val.type.code == gdb.TYPE_CODE_PTR:
            if int(val) == 0:
                print(f"{arg}: NULL")
                return
            val = val.dereference()
        magic = int(val["magic"]) & 0xFFFFFFFF
        ok = (magic == 0x4C494D45)
        print(f"  magic:          0x{magic:08x}  {'(LIME, ok)' if ok else '(BAD; truncated/corrupt?)'}")
        print(f"  abi_version:    {int(val['abi_version'])}")
        print(f"  version:        {int(val['version'])}")
        try:
            rc = int(val["refcount"])
        except Exception:
            rc = int(val["refcount"]["_M_i"])
        print(f"  refcount:       {rc}")
        print(f"  nstate:         {int(val['nstate'])}")
        print(f"  nrule:          {int(val['nrule'])}")
        print(f"  nsymbol:        {int(val['nsymbol'])}")
        print(f"  nterminal:      {int(val['nterminal'])}")
        print(f"  nfallback:      {int(val['nfallback'])}")
        print(f"  yy_action:      {val['yy_action']}")
        print(f"  yy_lookahead:   {val['yy_lookahead']}")
        print(f"  yy_default:     {val['yy_default']}")
        print(f"  yy_max_shift:   {int(val['yy_max_shift'])}")
        print(f"  yy_min_reduce:  {int(val['yy_min_reduce'])}")
        print(f"  jit_ctx:        {val['jit_ctx']}")
        print(f"  jit_find_fn:    {val['jit_find_shift_fn']}")


class LimeStackCmd(gdb.Command):
    """Dump a ParseContext's parse stack contents.

    Usage:  lime-stack <ctx>
    Example: lime-stack ctx
    """
    def __init__(self):
        super().__init__("lime-stack", gdb.COMMAND_USER)

    def invoke(self, arg, _from_tty):
        ctx = gdb.parse_and_eval(arg)
        if ctx.type.code == gdb.TYPE_CODE_PTR:
            if int(ctx) == 0:
                print(f"{arg}: NULL")
                return
            ctx = ctx.dereference()
        engine = ctx["engine"]
        if int(engine) == 0:
            print(f"{arg}: no engine attached (parse_token never called)")
            return
        eng = engine.cast(gdb.lookup_type("ParseEngine").pointer()).dereference()
        stk = eng["stack"]
        base = stk["base"]
        top = stk["top"]
        cap = int(stk["capacity"])
        if int(base) == 0:
            print(f"{arg}: stack base is NULL (engine not initialised yet)")
            return
        # Each ParseStackEntry is {uint16_t state; uint16_t major; ...}
        entry_t = gdb.lookup_type("ParseStackEntry")
        depth = (int(top) - int(base)) // entry_t.sizeof
        print(f"  depth:    {depth} / {cap}")
        print(f"  flags:    {('ACC ' if int(eng['accepted']) else '')}{('ERR ' if int(eng['errored']) else '')}{'init ' if int(eng['initialised']) else 'fresh'}")
        print(f"  initialised:")
        for i in range(min(depth, 32)):
            e = (base + i).dereference()
            try:
                state = int(e["state"])
                major = int(e["major"])
                print(f"    [{i:3d}] state={state:4d}  major={major:3d}")
            except Exception:
                # Different field names possible; show raw
                print(f"    [{i:3d}] {e}")
        if depth > 32:
            print(f"    ... +{depth - 32} more")


class LimeActionsCmd(gdb.Command):
    """Show yy_action / yy_lookahead head and tail for a snapshot.

    Usage:  lime-actions <snap> [count=8]
    """
    def __init__(self):
        super().__init__("lime-actions", gdb.COMMAND_USER)

    def invoke(self, arg, _from_tty):
        parts = arg.split()
        if not parts:
            print("usage: lime-actions <snap> [count]")
            return
        snap = gdb.parse_and_eval(parts[0])
        n = int(parts[1]) if len(parts) > 1 else 8
        if snap.type.code == gdb.TYPE_CODE_PTR:
            if int(snap) == 0:
                print(f"{parts[0]}: NULL")
                return
            snap = snap.dereference()
        ya = snap["yy_action"]
        yl = snap["yy_lookahead"]
        ac = int(snap["action_count"])
        print(f"  action_count={ac}")
        print(f"  yy_action[head]:    ", end="")
        for i in range(min(n, ac)):
            print(int((ya + i).dereference()), end=" ")
        print()
        print(f"  yy_lookahead[head]: ", end="")
        for i in range(min(n, ac)):
            print(int((yl + i).dereference()), end=" ")
        print()
        if ac > 2 * n:
            print(f"  yy_action[tail]:    ", end="")
            for i in range(ac - n, ac):
                print(int((ya + i).dereference()), end=" ")
            print()


LimeSnapshotCmd()
LimeStackCmd()
LimeActionsCmd()


print("lime-gdb: pretty-printers loaded for ParserSnapshot, ParseContext, "
      "ParseEngine, LimeLexCompiled")
print("lime-gdb: commands registered: lime-snapshot, lime-stack, lime-actions")
