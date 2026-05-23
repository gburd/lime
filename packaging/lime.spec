Name:           lime-parser
Version:        0.2.4
Release:        1%{?dist}
Summary:        Runtime-extensible LALR(1) parser generator
License:        Public Domain
URL:            https://codeberg.org/gregburd/lime
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc >= 13, gcc-c++ >= 13, meson, ninja-build, pkgconfig
BuildRequires:  pkgconfig(libllvm) >= 14
%if 0%{?fedora} || 0%{?rhel} >= 9
BuildRequires:  llvm-devel
%endif

%description
Lime is a runtime-extensible LALR(1) parser generator derived from
the Lemon parser generator (SQLite project).  Lime reads a context-
free grammar and emits a C parser, and -- unlike yacc/bison -- the
generated parser can load and unload grammar extensions at runtime
without recompilation.  Optional features include SIMD-accelerated
tokenization (AVX2 / NEON) and LLVM JIT compilation of the action
table.

%package        devel
Summary:        Development headers and runtime library for lime-parser
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description    devel
Headers and the static runtime library for embedding the Lime
parser engine in C/C++ projects.  Includes the snapshot and
extension-registry interfaces.

%prep
%autosetup -n %{name}-%{version}

%build
%meson -Dllvm=auto
%meson_build

%install
%meson_install

%check
%meson_test --no-rebuild

%files
%license README.md
%doc README.md docs/COMPARISON.md docs/BENCHMARKS_VS_BISON.md
%{_bindir}/lime
%{_mandir}/man1/lime.1*
%{_mandir}/man5/lime_grammar.5*
%{_mandir}/man5/lime_lex.5*

%files devel
%{_includedir}/lime/
%{_libdir}/liblime_parser.a

%changelog
* Sat May 23 2026 Greg Burd <greg@burd.me> - 0.2.4-1
- Compact JIT codegen for large grammars; PG SQL grammar JITs in 19 ms.
- Bayesian disambiguation strategy.
- %symbol_prefix directive for namespace isolation.
- int16 -> int32 widening of yy_shift_ofst / yy_reduce_ofst.
- Substantial COBOL example.
- JSON benchmark vs Bison+Flex (1.81x faster) and vs simdjson.

* Tue May 06 2026 Greg Burd <greg@burd.me> - 0.2.0-1
- Lexer subsystem (.lex compiler) added.

* Mon Mar 17 2026 Greg Burd <greg@burd.me> - 0.1.0-1
- Initial RPM packaging.
