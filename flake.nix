{
  description = "Lime - runtime-extensible LALR(1) parser generator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
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
        #   llvm:       available on most platforms; gate on meta.available
        #               in case a specific platform combo isn't supported
        llvmPkgs = pkgs.llvmPackages_21;
        hasLLVM = llvmPkgs.libllvm.meta.available or false;

        commonTools = with pkgs; [
          meson
          ninja
          pkg-config
          python3
          doxygen
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
          packages = commonTools ++ linuxTools ++ coverageTools ++ llvmInputs;

          shellHook = ''
            echo "Lime development environment (${system})"
            echo "  cc:     $(${stdenv.cc}/bin/cc --version | head -1)"
            echo "  meson:  $(meson --version)"
            echo "  ninja:  $(ninja --version 2>/dev/null || echo missing)"
          '' + lib.optionalString hasLLVM ''
            echo "  llvm:   $(llvm-config --version 2>/dev/null || echo 'not in PATH')"
            export PKG_CONFIG_PATH="${llvmPkgs.libllvm.dev}/lib/pkgconfig:''${PKG_CONFIG_PATH:-}"
            # Pin meson's LLVM config-tool probe to the nix-provided
            # llvm-config.  Without this, meson's dependency('llvm',
            # method:'config-tool') scans for llvm-config-XX candidates
            # and picks the highest version on PATH, which on distros
            # like Fedora selects /usr/bin/llvm-config-22 instead of the
            # nix LLVM and causes link failures against host /usr/lib64.
            export LLVM_CONFIG="${llvmPkgs.libllvm.dev}/bin/llvm-config"
          '' + lib.optionalString (!hasLLVM) ''
            echo "  llvm:   not available on ${system} (JIT disabled)"
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
