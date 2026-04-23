{
  description = "Extensible SQL Parser - Lemon-based parser generator with extensions";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            gcc13
            meson
            ninja
            python311
            llvm_17
            libllvm
            llvmPackages_17.libllvm
            pkg-config
            valgrind
            gdb
            lcov  # For code coverage
            gcovr # Alternative coverage tool
          ];

          shellHook = ''
            echo "Extensible SQL Parser development environment"
            echo "  gcc:    $(gcc --version | head -1)"
            echo "  meson:  $(meson --version)"
            echo "  ninja:  $(ninja --version)"
            echo "  python: $(python3 --version)"
            echo "  llvm:   $(llvm-config --version 2>/dev/null || echo 'not in PATH')"
            export PKG_CONFIG_PATH="${pkgs.llvmPackages_17.libllvm.dev}/lib/pkgconfig:$PKG_CONFIG_PATH"
          '';
        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "lemon-parser";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
          ];

          buildInputs = with pkgs; [
            llvm_17
          ];
        };
      }
    );
}
