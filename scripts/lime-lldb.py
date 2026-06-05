# Lime LLDB pretty-printers + commands.
#
# Loads via:
#
#   echo "command script import $PWD/scripts/lime-lldb.py" >> ~/.lldbinit
#
# Or one-shot:
#
#   (lldb) command script import scripts/lime-lldb.py
#
# Mirrors scripts/lime-gdb.py: same pretty-printer surface, same
# convenience commands (lime-snapshot, lime-stack, lime-actions),
# adapted to LLDB's Python API.

import lldb


def _safe_int(val, default=0):
    """Best-effort SBValue -> int."""
    try:
        e = lldb.SBError()
        v = val.GetValueAsUnsigned(e, default)
        if e.Success():
            return v
        return default
    except Exception:
        return default


def _resolve_ptr(val):
    """Dereference if this is a pointer; return None if NULL."""
    if val.GetType().IsPointerType():
        if _safe_int(val) == 0:
            return None
        val = val.Dereference()
    return val


# ----------------------------------------------------------------------
# Pretty-printers (LLDB calls these "summaries")
# ----------------------------------------------------------------------

def parser_snapshot_summary(valobj, _internal_dict):
    if valobj.GetType().IsPointerType() and _safe_int(valobj) == 0:
        return "(ParserSnapshot *) NULL"
    snap = _resolve_ptr(valobj)
    if snap is None:
        return "(ParserSnapshot *) NULL"
    magic = _safe_int(snap.GetChildMemberWithName("magic")) & 0xFFFFFFFF
    magic_str = "LIME" if magic == 0x4C494D45 else f"BAD(0x{magic:08x})"
    abi = _safe_int(snap.GetChildMemberWithName("abi_version"))
    ver = _safe_int(snap.GetChildMemberWithName("version"))
    rc_v = snap.GetChildMemberWithName("refcount")
    rc = _safe_int(rc_v)
    if rc == 0:
        # Could be inside an _Atomic wrapper; try ._M_i (libstdc++) or
        # __a_value (libc++).
        for path in ("_M_i", "__a_value"):
            inner = rc_v.GetChildMemberWithName(path)
            if inner.IsValid():
                rc = _safe_int(inner)
                break
    nrule = _safe_int(snap.GetChildMemberWithName("nrule"))
    nstate = _safe_int(snap.GetChildMemberWithName("nstate"))
    return (f"ParserSnapshot{{magic={magic_str} abi={abi} ver={ver} "
            f"refcount={rc} nrule={nrule} nstate={nstate}}}")


def parse_context_summary(valobj, _internal_dict):
    if valobj.GetType().IsPointerType() and _safe_int(valobj) == 0:
        return "(ParseContext *) NULL"
    ctx = _resolve_ptr(valobj)
    if ctx is None:
        return "(ParseContext *) NULL"
    snap = ctx.GetChildMemberWithName("snapshot")
    engine = ctx.GetChildMemberWithName("engine")
    snap_str = "NULL" if _safe_int(snap) == 0 else f"@0x{_safe_int(snap):x}"
    engine_str = "none" if _safe_int(engine) == 0 else f"@0x{_safe_int(engine):x}"
    bow = ctx.GetChildMemberWithName("borrowed_snapshot")
    bow_str = " borrowed" if (bow.IsValid() and _safe_int(bow)) else ""
    return f"ParseContext{{snap={snap_str} engine={engine_str}{bow_str}}}"


def parse_engine_summary(valobj, _internal_dict):
    eng = _resolve_ptr(valobj)
    if eng is None:
        return "(ParseEngine *) NULL"
    init = _safe_int(eng.GetChildMemberWithName("initialised"))
    acc = _safe_int(eng.GetChildMemberWithName("accepted"))
    err = _safe_int(eng.GetChildMemberWithName("errored"))
    stk = eng.GetChildMemberWithName("stack")
    base = _safe_int(stk.GetChildMemberWithName("base"))
    top = _safe_int(stk.GetChildMemberWithName("top"))
    cap = _safe_int(stk.GetChildMemberWithName("capacity"))
    depth = (top - base) // 8 if base and top else 0
    flags = []
    if init: flags.append("init")
    if acc:  flags.append("ACCEPTED")
    if err:  flags.append("ERRORED")
    flag_str = "|".join(flags) if flags else "fresh"
    return f"ParseEngine{{{flag_str} stack:depth={depth}/{cap}}}"


# ----------------------------------------------------------------------
# Convenience commands
# ----------------------------------------------------------------------

def lime_snapshot(debugger, command, result, _internal_dict):
    """lime-snapshot <expr>: dump key fields of a ParserSnapshot."""
    args = command.strip().split(None, 1)
    if not args:
        result.SetError("usage: lime-snapshot <expr>")
        return
    target = debugger.GetSelectedTarget()
    frame = target.GetProcess().GetSelectedThread().GetSelectedFrame()
    val = frame.EvaluateExpression(args[0])
    if val.GetError().Fail():
        result.SetError(f"can't evaluate {args[0]}: {val.GetError().GetCString()}")
        return
    if val.GetType().IsPointerType():
        if _safe_int(val) == 0:
            result.AppendMessage(f"{args[0]}: NULL")
            return
        val = val.Dereference()
    magic = _safe_int(val.GetChildMemberWithName("magic")) & 0xFFFFFFFF
    ok = (magic == 0x4C494D45)
    result.AppendMessage(f"  magic:          0x{magic:08x}  "
                         f"{'(LIME, ok)' if ok else '(BAD; truncated/corrupt?)'}")
    result.AppendMessage(f"  abi_version:    {_safe_int(val.GetChildMemberWithName('abi_version'))}")
    result.AppendMessage(f"  version:        {_safe_int(val.GetChildMemberWithName('version'))}")
    rc_v = val.GetChildMemberWithName("refcount")
    rc = _safe_int(rc_v)
    if rc == 0:
        for path in ("_M_i", "__a_value"):
            inner = rc_v.GetChildMemberWithName(path)
            if inner.IsValid():
                rc = _safe_int(inner)
                break
    result.AppendMessage(f"  refcount:       {rc}")
    for f in ("nstate", "nrule", "nsymbol", "nterminal", "nfallback",
              "yy_max_shift", "yy_min_reduce"):
        v = val.GetChildMemberWithName(f)
        if v.IsValid():
            result.AppendMessage(f"  {f:<14s}  {_safe_int(v)}")
    for f in ("yy_action", "yy_lookahead", "yy_default", "jit_ctx", "jit_find_shift_fn"):
        v = val.GetChildMemberWithName(f)
        if v.IsValid():
            result.AppendMessage(f"  {f:<14s}  0x{_safe_int(v):x}")


def lime_stack(debugger, command, result, _internal_dict):
    """lime-stack <ctx>: dump a ParseContext's parse stack."""
    args = command.strip().split(None, 1)
    if not args:
        result.SetError("usage: lime-stack <ctx>")
        return
    target = debugger.GetSelectedTarget()
    frame = target.GetProcess().GetSelectedThread().GetSelectedFrame()
    val = frame.EvaluateExpression(args[0])
    if val.GetError().Fail():
        result.SetError(f"can't evaluate {args[0]}: {val.GetError().GetCString()}")
        return
    if val.GetType().IsPointerType():
        if _safe_int(val) == 0:
            result.AppendMessage(f"{args[0]}: NULL")
            return
        val = val.Dereference()
    engine = val.GetChildMemberWithName("engine")
    if _safe_int(engine) == 0:
        result.AppendMessage(f"{args[0]}: no engine (parse_token never called)")
        return
    # Cast to ParseEngine *.
    pe_t = target.FindFirstType("ParseEngine")
    if not pe_t.IsValid():
        result.SetError("ParseEngine type not visible -- compile with -g")
        return
    eng_addr = _safe_int(engine)
    eng = target.CreateValueFromAddress(
        "engine", lldb.SBAddress(eng_addr, target), pe_t)
    stk = eng.GetChildMemberWithName("stack")
    base = _safe_int(stk.GetChildMemberWithName("base"))
    top = _safe_int(stk.GetChildMemberWithName("top"))
    cap = _safe_int(stk.GetChildMemberWithName("capacity"))
    if base == 0:
        result.AppendMessage(f"{args[0]}: stack base NULL (engine not initialised)")
        return
    pse_t = target.FindFirstType("ParseStackEntry")
    if not pse_t.IsValid():
        result.SetError("ParseStackEntry type not visible -- compile with -g")
        return
    entry_size = pse_t.GetByteSize()
    depth = (top - base) // entry_size
    result.AppendMessage(f"  depth:    {depth} / {cap}")
    result.AppendMessage(f"  flags:    "
                         f"{'ACC ' if _safe_int(eng.GetChildMemberWithName('accepted')) else ''}"
                         f"{'ERR ' if _safe_int(eng.GetChildMemberWithName('errored')) else ''}"
                         f"{'init' if _safe_int(eng.GetChildMemberWithName('initialised')) else 'fresh'}")
    for i in range(min(depth, 32)):
        addr = lldb.SBAddress(base + i * entry_size, target)
        e = target.CreateValueFromAddress(f"e{i}", addr, pse_t)
        state = _safe_int(e.GetChildMemberWithName("state"))
        major = _safe_int(e.GetChildMemberWithName("major"))
        result.AppendMessage(f"    [{i:3d}] state={state:4d}  major={major:3d}")
    if depth > 32:
        result.AppendMessage(f"    ... +{depth - 32} more")


def lime_actions(debugger, command, result, _internal_dict):
    """lime-actions <snap> [count=8]: show yy_action / yy_lookahead head + tail."""
    args = command.strip().split()
    if not args:
        result.SetError("usage: lime-actions <snap> [count]")
        return
    n = int(args[1]) if len(args) > 1 else 8
    target = debugger.GetSelectedTarget()
    frame = target.GetProcess().GetSelectedThread().GetSelectedFrame()
    val = frame.EvaluateExpression(args[0])
    if val.GetType().IsPointerType():
        if _safe_int(val) == 0:
            result.AppendMessage(f"{args[0]}: NULL")
            return
        val = val.Dereference()
    ya = val.GetChildMemberWithName("yy_action")
    yl = val.GetChildMemberWithName("yy_lookahead")
    ac = _safe_int(val.GetChildMemberWithName("action_count"))
    result.AppendMessage(f"  action_count={ac}")
    ya_base = _safe_int(ya)
    yl_base = _safe_int(yl)
    process = target.GetProcess()

    def _read_u16(addr, i):
        err = lldb.SBError()
        return process.ReadUnsignedFromMemory(addr + i * 2, 2, err)

    def _row(label, base, count):
        line = f"  {label}: "
        for i in range(min(count, ac)):
            line += f"{_read_u16(base, i)} "
        result.AppendMessage(line)

    _row("yy_action[head]   ", ya_base, n)
    _row("yy_lookahead[head]", yl_base, n)
    if ac > 2 * n:
        line = "  yy_action[tail]   : "
        for i in range(ac - n, ac):
            line += f"{_read_u16(ya_base, i)} "
        result.AppendMessage(line)


# ----------------------------------------------------------------------
# Module entry point
# ----------------------------------------------------------------------

def __lldb_init_module(debugger, _internal_dict):
    debugger.HandleCommand(
        "type summary add -F lime-lldb.parser_snapshot_summary "
        "ParserSnapshot")
    debugger.HandleCommand(
        "type summary add -F lime-lldb.parser_snapshot_summary "
        "-x '^ParserSnapshot \\*$'")
    debugger.HandleCommand(
        "type summary add -F lime-lldb.parse_context_summary "
        "ParseContext")
    debugger.HandleCommand(
        "type summary add -F lime-lldb.parse_context_summary "
        "-x '^ParseContext \\*$'")
    debugger.HandleCommand(
        "type summary add -F lime-lldb.parse_engine_summary "
        "ParseEngine")
    debugger.HandleCommand(
        "command script add -f lime-lldb.lime_snapshot lime-snapshot")
    debugger.HandleCommand(
        "command script add -f lime-lldb.lime_stack lime-stack")
    debugger.HandleCommand(
        "command script add -f lime-lldb.lime_actions lime-actions")
    print("lime-lldb: summaries registered for ParserSnapshot, ParseContext, "
          "ParseEngine")
    print("lime-lldb: commands registered: lime-snapshot, lime-stack, lime-actions")
