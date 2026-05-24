{
  description = "Lime - runtime-extensible LALR(1) parser generator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # Hegel-core: the Python implementation of the Hegel
    # property-based testing protocol.  Provides the `hegel`
    # server binary that the C99 binding (hegel-c, built below
    # as an in-flake derivation) talks to over the Hegel wire
    # protocol.  The flake lives in the repo's nix/ subdir.
    hegel-core = {
      url = "github:hegeldev/hegel-core?dir=nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };

    # Hegel-c source: the C99 client-library bindings.  Pinned as a
    # raw GitHub source (no flake.nix upstream); the nix derivation
    # below builds it via the upstream CMake.
    hegel-c-src = {
      url = "github:gburd/hegel-c";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, hegel-core, hegel-c-src }:
    let
      # Supported systems. flake-utils' eachDefaultSystem only covers
      # Linux/Darwin x86_64+aarch64; extend explicitly for FreeBSD and
      # RISC-V. Package availability varies per system and is handled
      # via lib.optionals below.
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "riscv64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
        "x86_64-freebsd"
        "aarch64-freebsd"
      ];
    in
    flake-utils.lib.eachSystem systems (system:
      let
        pkgs = import nixpkgs { inherit system; };
        inherit (pkgs) lib stdenv;

        # Platform capability flags
        isLinux = stdenv.hostPlatform.isLinux;
        isDarwin = stdenv.hostPlatform.isDarwin;

        # Tools that don't build on every platform:
        #   valgrind:   Linux only (+ FreeBSD, but flaky)
        #   gdb:        Linux; not reliably available on Darwin
        #   lcov/gcovr: portable, but gcovr is Python so more portable
        #   llvm:       available on most platforms; gate on meta.available
        #               in case a specific platform combo isn't supported
        llvmPkgs = pkgs.llvmPackages_21;
        hasLLVM = llvmPkgs.libllvm.meta.available or false;

        # ----------------------------------------------------------------
        #  Hegel: property-based testing
        # ----------------------------------------------------------------
        # Two pieces are wired in:
        #
        #   * hegel-server -- the Hypothesis-backed Hegel protocol
        #                     server, a Python application from the
        #                     hegel-core flake input.  Provides the
        #                     `hegel` binary that hegel-c spawns as
        #                     a subprocess (HEGEL_SERVER_COMMAND).
        #   * hegel-c      -- the C99 client-library bindings, built
        #                     in-flake via the upstream CMakeLists.
        #                     We synthesise a hegel.pc file at install
        #                     time so meson's pkg-config lookup
        #                     (`dependency('hegel', required : false)`
        #                     in tests/meson.build) works.
        #
        # When this flake's devShell is in use, `nix develop` puts
        # the `hegel` server on PATH, the libhegel headers and shared
        # library on the right include / link paths, and meson picks
        # up the hegel.pc so the Hegel-gated PBTs (tests/test_*_pbt.c)
        # build and run.
        hegel-server =
          if (hegel-core.packages.${system} or { }) ? default
          then hegel-core.packages.${system}.default
          else null;

        hegel-c = pkgs.stdenv.mkDerivation {
          pname = "hegel-c";
          version = "0.1.0-dev";
          src = hegel-c-src;
          nativeBuildInputs = with pkgs; [ cmake pkg-config ];
          buildInputs = with pkgs; [ libcbor zlib ];
          # Skip the test suite (cmocka-dependent) and the
          # conformance binaries -- we only need the library to
          # link against from the Lime PBTs.
          cmakeFlags = [
            "-DHEGEL_BUILD_TESTS=OFF"
            "-DHEGEL_BUILD_CONFORMANCE=OFF"
          ];
          # Synthesise a hegel.pc so meson's pkg-config-driven
          # `dependency('hegel', required : false)` resolves to
          # this build.
          postInstall = ''
            mkdir -p "$out/lib/pkgconfig"
            cat > "$out/lib/pkgconfig/hegel.pc" <<EOF
            prefix=$out
            exec_prefix=$out
            libdir=$out/lib
            includedir=$out/include

            Name: hegel
            Description: Hegel C99 property-based testing client library
            Version: 0.1.0
            Requires.private: libcbor zlib
            Libs: -L$out/lib -lhegel
            Cflags: -I$out/include
            EOF
          '';
          # hegel-c uses -Werror by default; soften so future
          # Nix toolchain bumps don't fail us on a new warning.
          NIX_CFLAGS_COMPILE = "-Wno-error";
        };

        # Whether the Hegel pieces are actually buildable on this
        # system.  hegel-core's flake exposes packages on
        # lib.systems.flakeExposed; not every system in our list
        # is in that set (FreeBSD / RISC-V).  hegel-c needs cmake
        # + libcbor which are available everywhere we test.
        hegelInputs = lib.optionals (hegel-server != null) [
          hegel-server
          hegel-c
        ];

        commonTools = with pkgs; [
          meson
          ninja
          pkg-config
          python3
          doxygen
          # Code formatter -- ships clang-format-21 to match the
          # bundled LLVM toolchain so `make format` and the
          # `.clang-format` config behave identically across
          # contributor machines.
          llvmPkgs.clang-tools
          # Flex + Bison are required by the optional Flex/Bison
          # comparison benchmark in bench/bench_flex_bison_compare/.
          # The harness skips cleanly when they are missing, but
          # putting them in the dev shell means contributors get
          # reproducible numbers without a separate apt/brew install.
          flex
          bison
          # Real `git` rather than relying on the macOS Xcode shim,
          # which prints "error: tool 'git' not found" when the
          # command-line tools are not installed.  Needed for cr
          # workflows, git log/blame in the dev shell, etc.
          git
          # simdjson is used by bench/bench_simdjson_compare/ to
          # measure Lime's JSON throughput against a purpose-built
          # SIMD JSON parser.  Skipped automatically when absent.
          simdjson
        ];

        linuxTools = with pkgs; lib.optionals isLinux [
          valgrind
          gdb
          lcov
        ];

        coverageTools = with pkgs; [ gcovr ];

        llvmInputs = lib.optionals hasLLVM [
          llvmPkgs.libllvm
          # System libraries that llvm-config --system-libs --link-static
          # reports as transitive dependencies.  Needed when building
          # lime with -Dllvm-static=true; harmless otherwise.
          pkgs.libxml2
          pkgs.zlib
          pkgs.libffi
        ];
      in
      {
        devShells.default = pkgs.mkShell {
          # stdenv provides a working C/C++ compiler for every platform:
          #   Linux        -> gcc
          #   Darwin       -> clang
          #   FreeBSD      -> clang
          #   riscv64-linux -> gcc (cross or native)
          packages = commonTools ++ linuxTools ++ coverageTools
                     ++ llvmInputs ++ hegelInputs;

          shellHook = ''
            echo "Lime development environment (${system})"
            echo "  cc:            $(${stdenv.cc}/bin/cc --version | head -1)"
            echo "  meson:         $(meson --version)"
            echo "  ninja:         $(ninja --version 2>/dev/null || echo missing)"
            echo "  clang-format:  $(clang-format --version 2>/dev/null | head -1 || echo missing)"
            echo "  flex:          $(flex --version 2>/dev/null || echo missing)"
            echo "  bison:         $(bison --version 2>/dev/null | head -1 || echo missing)"
          '' + lib.optionalString hasLLVM ''
            echo "  llvm:          $(llvm-config --version 2>/dev/null || echo 'not in PATH')"
            export PKG_CONFIG_PATH="${llvmPkgs.libllvm.dev}/lib/pkgconfig:''${PKG_CONFIG_PATH:-}"
            # Pin meson's LLVM config-tool probe to the nix-provided
            # llvm-config.  Without this, meson's dependency('llvm',
            # method:'config-tool') scans for llvm-config-XX candidates
            # and picks the highest version on PATH, which on distros
            # like Fedora selects /usr/bin/llvm-config-22 instead of the
            # nix LLVM and causes link failures against host /usr/lib64.
            export LLVM_CONFIG="${llvmPkgs.libllvm.dev}/bin/llvm-config"
          '' + lib.optionalString (!hasLLVM) ''
            echo "  llvm:          not available on ${system} (JIT disabled)"
          '' + lib.optionalString (hegel-server != null) ''
            echo "  hegel-server:  $(hegel --version 2>/dev/null || echo 'on PATH')"
            echo "  hegel-c:       $(pkg-config --modversion hegel 2>/dev/null || echo 'on PATH')"
            export PKG_CONFIG_PATH="${hegel-c}/lib/pkgconfig:''${PKG_CONFIG_PATH:-}"
            # hegel-c spawns the Hegel server as a subprocess; we
            # provide it via the hegel-core flake input so the PBTs
            # work without any additional install steps.  The library
            # searches PATH by default; HEGEL_SERVER_COMMAND overrides.
            export HEGEL_SERVER_COMMAND="${hegel-server}/bin/hegel"
          '' + lib.optionalString (hegel-server == null) ''
            echo "  hegel:         not available on ${system}"
          '';
        };

        packages.default = stdenv.mkDerivation {
          pname = "lime";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [ meson ninja pkg-config ];
          buildInputs = llvmInputs;

          mesonFlags = lib.optionals (!hasLLVM) [ "-Dllvm=disabled" ];

          meta = with lib; {
            description = "Runtime-extensible LALR(1) parser generator";
            homepage = "https://codeberg.org/gregburd/lime";
            license = licenses.publicDomain;
            platforms = systems;
          };
        };
      }
    );
}
