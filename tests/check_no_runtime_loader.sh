#!/bin/sh
# check_no_runtime_loader.sh NM liblime_parser.a liblime_compiler.a [more.a...]
#
# The owner's rule: at runtime Lime cannot invoke a compiler or load a
# shared object.  So no library a consumer links may reference process
# creation or the dynamic loader.  Also: liblime_parser (what runs per
# parse) must not mention the in-process compiler at all -- not even as
# a weak reference with a fallback.  Composing means linking
# liblime_compiler explicitly; forgetting is a link error.
#
# Exit 0 clean, 1 violation, 77 skip (no nm).
nm=$1; shift
if [ -z "$nm" ] || ! command -v "$nm" >/dev/null 2>&1; then
  echo "SKIP: nm not found"; exit 77
fi
forbidden='^(fork|vfork|clone|execl|execlp|execle|execv|execvp|execvpe|execve|fexecve|dlopen|dlmopen|dlsym|dlvsym|dlclose|LoadLibrary|LoadLibraryA|LoadLibraryW|LoadLibraryExA|LoadLibraryExW|GetProcAddress|CreateProcessA|CreateProcessW|posix_spawn|posix_spawnp|system|popen|_popen)$'
bad=0
for lib in "$@"; do
  # undefined symbols, one name per line, without Mach-O '_' or ELF @version
  syms=$("$nm" -u "$lib" 2>/dev/null | awk 'NF{print $NF}' | sed -e 's/^_//' -e 's/@.*//' | sort -u)
  hits=$(printf '%s\n' "$syms" | grep -E "$forbidden" | tr '\n' ' ')
  if [ -n "$hits" ]; then
    echo "FAIL $(basename "$lib"): references $hits"; bad=1
  else
    echo "ok   $(basename "$lib"): no process/loader references"
  fi
  case $(basename "$lib") in
    liblime_parser.*)
      c=$("$nm" "$lib" 2>/dev/null | grep -c 'lime_compile_grammar_in_process')
      if [ "$c" -ne 0 ]; then
        echo "FAIL $(basename "$lib"): mentions lime_compile_grammar_in_process* ($c symbol entries; weak or strong)"
        "$nm" "$lib" | grep 'lime_compile_grammar_in_process' | sed 's/^/       /'
        bad=1
      else
        echo "ok   $(basename "$lib"): no reference to the in-process compiler"
      fi ;;
  esac
done
exit $bad
