{
  description = "Lime - runtime-extensible LALR(1) parser generator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # NOTE: hegel no longer has a server component.  The old
    # `hegel-core` (Python server) + `gburd/hegel-c` (socket client)
    # pair is gone: gburd/hegel-c is deprecated/read-only and its
    # socket protocol was removed upstream, and hegel-core's `hegel`
    # binary speaks stdio, not sockets -- so the two never actually
    # talked to each other and every PBT skipped at runtime.
    #
    # The replacement, libhegel from hegeldev/hegel-rust, is a pure
    # in-process C-ABI shared library: no subprocess, no socket, no
    # server binary.  It is consumed below as the official prebuilt
    # release artifacts (see the hegel-c derivation for why prebuilt
    # rather than a source build).
  };

  outputs = { self, nixpkgs, flake-utils }:
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
        # clang-format comes from llvmPackages_21.clang-tools (formatter
        # only; Lime has no LLVM dependency).
        llvmPkgs = pkgs.llvmPackages_21;

        # ----------------------------------------------------------------
        #  Hegel: property-based testing
        # ----------------------------------------------------------------
        # libhegel (hegeldev/hegel-rust, crate `hegeltest-c`) is the
        # official C-ABI binding to Hegel's native engine.  It runs
        # entirely in-process: generation, shrinking and the example
        # database all live inside the shared library, so there is no
        # server process and nothing to put on PATH.  The only thing
        # the dev shell has to provide is the header plus the shared
        # library, and a hegel.pc so meson's
        # `dependency('hegel', required : false)` in tests/meson.build
        # resolves.
        #
        # Prebuilt release artifacts rather than a source build:
        # upstream's nix/flake.nix exposes lib.mkLibhegel via
        # rustPlatform.buildRustPackage, which would cover every system
        # in `systems` above -- but it needs ~140 crates fetched from
        # crates.io at eval/build time, which is exactly the kind of
        # dependency that makes an offline or restricted-network `nix
        # develop` fail.  The published libhegel-<os>-<arch> assets are
        # a single pinned hash each and cost nothing to fetch.  The
        # tradeoff is per-platform coverage: only the platforms upstream
        # publishes are supported, and the rest degrade to "no hegel",
        # which tests/meson.build already handles.
        hegelVersion = "0.36.5";
        hegelBase =
          "https://github.com/hegeldev/hegel-rust/releases/download/v${hegelVersion}";

        # Platforms upstream publishes a libhegel artifact for, keyed
        # by nix system.  windows-{amd64,arm64} are also published but
        # are not in `systems`.  Missing here: riscv64-linux,
        # x86_64-darwin, *-freebsd -- those get the graceful skip.
        hegelArtifacts = {
          "x86_64-linux" = {
            file = "libhegel-linux-amd64.so";
            hash = "sha256-MJzKFaazWIibe6ytz7rf9MzTLElm58/KZkXAsBb8j5I=";
          };
          "aarch64-linux" = {
            file = "libhegel-linux-arm64.so";
            hash = "sha256-U0VfJDNb7Y63H55CWHq6i/rOnPwudK1gswItglU0Lt0=";
          };
          "aarch64-darwin" = {
            file = "libhegel-darwin-arm64.dylib";
            hash = "sha256-9YHZTATM/nm6NRhvSIhHzYUqFOhWrrZ0lSx/UkRsicY=";
          };
        };

        hegelArtifact = hegelArtifacts.${system} or null;

        hegel-c =
          if hegelArtifact == null then null else
          pkgs.stdenv.mkDerivation {
            pname = "hegel-c";
            version = hegelVersion;
            dontUnpack = true;
            nativeBuildInputs = lib.optionals isLinux [ pkgs.patchelf ];

            header = pkgs.fetchurl {
              url = "${hegelBase}/hegel.h";
              hash = "sha256-iJbxepWn+55g0/8ASFROyEW+KqbtZUGreRwhnH585/k=";
            };
            libhegel = pkgs.fetchurl {
              url = "${hegelBase}/${hegelArtifact.file}";
              inherit (hegelArtifact) hash;
            };

            soname = "libhegel${stdenv.hostPlatform.extensions.sharedLibrary}";

            installPhase = ''
              mkdir -p "$out/lib/pkgconfig" "$out/include"
              cp "$header" "$out/include/hegel.h"
              install -m755 "$libhegel" "$out/lib/$soname"
            ''
            # The released .so carries no SONAME and no RPATH, so on
            # NixOS it would not find libgcc_s / libc at load time.
            + lib.optionalString isLinux ''
              patchelf --set-soname "$soname" \
                --set-rpath "${stdenv.cc.cc.lib}/lib" "$out/lib/$soname"
            '' + lib.optionalString isDarwin ''
              install_name_tool -id "$out/lib/$soname" "$out/lib/$soname"
            '' + ''
              chmod 555 "$out/lib/$soname"
              cat > "$out/lib/pkgconfig/hegel.pc" <<EOF
              prefix=$out
              exec_prefix=$out
              libdir=$out/lib
              includedir=$out/include

              Name: hegel
              Description: libhegel -- Hegel's native property-based testing engine (C ABI)
              Version: ${hegelVersion}
              Libs: -L$out/lib -lhegel -Wl,-rpath,$out/lib
              Cflags: -I$out/include
              EOF
            '';
          };

        hegelInputs = lib.optional (hegel-c != null) hegel-c;

        commonTools = with pkgs; [
          meson
          ninja
          pkg-config
          python3
          doxygen
          # Code formatter -- pins clang-format-21 so `make format`
          # and the `.clang-format` config behave identically across
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

      in
      {
        devShells.default = pkgs.mkShell {
          # stdenv provides a working C/C++ compiler for every platform:
          #   Linux        -> gcc
          #   Darwin       -> clang
          #   FreeBSD      -> clang
          #   riscv64-linux -> gcc (cross or native)
          packages = commonTools ++ linuxTools ++ coverageTools
                     ++ hegelInputs;

          shellHook = ''
            echo "Lime development environment (${system})"
            echo "  cc:            $(${stdenv.cc}/bin/cc --version | head -1)"
            echo "  meson:         $(meson --version)"
            echo "  ninja:         $(ninja --version 2>/dev/null || echo missing)"
            echo "  clang-format:  $(clang-format --version 2>/dev/null | head -1 || echo missing)"
            echo "  flex:          $(flex --version 2>/dev/null || echo missing)"
            echo "  bison:         $(bison --version 2>/dev/null | head -1 || echo missing)"
          '' + lib.optionalString (hegel-c != null) ''
            export PKG_CONFIG_PATH="${hegel-c}/lib/pkgconfig:''${PKG_CONFIG_PATH:-}"
            # libhegel runs in-process: there is no hegel server binary
            # to report or to put on PATH.  The whole engine is the
            # shared library the PBTs link against.
            echo "  libhegel:      $(pkg-config --modversion hegel 2>/dev/null || echo '${hegelVersion}') (in-process, no server)"
          '' + lib.optionalString (hegel-c == null) ''
            echo "  libhegel:      not available on ${system} (no upstream prebuilt artifact; PBTs skip)"
          '';
        };

        packages.default = stdenv.mkDerivation {
          pname = "lime";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [ meson ninja pkg-config ];

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
