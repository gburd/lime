#!/bin/bash
# Compare lime-built and bison-built bc on bc's own Test/*.b and Examples,
# plus syntax-error inputs.  Each run: timeout 10; quit appended so
# interactive programs end.  BC_LIME / BC_REF = the two built bc trees
# (default: Track D's /tmp/tD-bc2/{l,b}).
L=${BC_LIME:-/tmp/tD-bc2/l}/bc/bc; B=${BC_REF:-/tmp/tD-bc2/b}/bc/bc; LD=${BC_LIME:-/tmp/tD-bc2/l}; BD=${BC_REF:-/tmp/tD-bc2/b}
same=0; differ=0; hang=0
cmp1() {  # name, then stdin text via file $2, args...
  local n=$1 in=$2; shift 2
  # checklib.b computes ~10k library values: give it 120 s, the rest 10 s.
  local to=10; case "$n" in checklib*) to=120 ;; esac
  lo=$(cd $LD/Test && timeout $to $L "$@" < $in 2>&1; echo "rc=$?")
  bo=$(cd $BD/Test && timeout $to $B "$@" < $in 2>&1; echo "rc=$?")
  lo=${lo//$LD/X}; bo=${bo//$BD/X}
  case "$lo$bo" in *rc=124*) hang=$((hang+1)); echo "HANG $n";; esac
  if [ "$lo" = "$bo" ]; then same=$((same+1)); else differ=$((differ+1)); echo "DIFF $n"; diff <(echo "$bo") <(echo "$lo") | head -5; fi
}
Q=$(mktemp); echo quit > $Q
for f in $LD/Test/*.b $LD/Test/BUG.bc; do
  b=$(basename $f); [ $b = timetest ] && continue
  # ckbook.b reads transactions until a 0: with only `quit` it waits; both hang the same.
  cmp1 "$b -l" $Q -l $f
done
CK=$(mktemp); printf '100\n-20\n35.5\n0\nquit\n' > $CK   # ckbook.b read()s transactions
for f in /tmp/compat-dl/bc-1.07.1/Examples/*.b; do
  in=$Q; case $f in *ckbook.b) in=$CK ;; esac
  cmp1 "ex $(basename $f)" $in -l $f
done
# error-recovery inputs, via stdin
E=$(mktemp)
for t in '1+\n2\n' '1 2 3\n4\n' 'if (1) {\n' 'x = (1\ny=2\ny\n' 'define f(x) { return x+1 }\nf(2)\n' 'for(i=0;i<3;i++) i\n' '}}\n5\n' 'a[1]=3; a[1]\n' 'scale=20; 4*a(1)\n' 'print "hi\\n"\n' 'auto x\n' 'break\n7\n' '1++2\n' '2^10\n' 'obase=16; 255\n'; do
  printf "$t" > $E; cmp1 "stdin $(printf "$t" | head -1)" $E -l
done
echo "same=$same differ=$differ hang=$hang"
