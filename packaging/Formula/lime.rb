class Lime < Formula
  desc "Runtime-extensible LALR(1) parser generator"
  homepage "https://codeberg.org/gregburd/lime"
  url "https://codeberg.org/gregburd/lime/archive/v0.2.4.tar.gz"
  # sha256 placeholder -- regenerate with:
  #   shasum -a 256 lime-parser-0.2.4.tar.gz
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license :public_domain
  head "https://codeberg.org/gregburd/lime.git", branch: "main"

  depends_on "meson"           => :build
  depends_on "ninja"           => :build
  depends_on "pkg-config"      => :build
  depends_on "llvm" # for the optional JIT; --without-llvm omits it

  def install
    args = ["-Dllvm=auto"]
    args << "-Dllvm=disabled" if build.without?("llvm")

    system "meson", "setup", "builddir", *args, *std_meson_args
    system "meson", "compile", "-C", "builddir"
    system "meson", "test",    "-C", "builddir", "--no-rebuild"
    system "meson", "install", "-C", "builddir"
  end

  test do
    # Quick smoke test: have lime regenerate a tiny grammar.
    (testpath/"calc.lime").write <<~LIME
      %name_prefix Calc
      %token NUM PLUS.
      %start_symbol expr.
      expr ::= expr PLUS NUM.
      expr ::= NUM.
    LIME
    system bin/"lime", "-d", testpath, testpath/"calc.lime"
    assert_predicate testpath/"calc.c", :exist?
    assert_predicate testpath/"calc.h", :exist?
  end
end
