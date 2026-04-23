BUILDDIR ?= builddir

.PHONY: setup build test clean reconfigure

setup:
	meson setup $(BUILDDIR)

build: setup
	ninja -C $(BUILDDIR)

test: build
	meson test -C $(BUILDDIR) --print-errorlogs

clean:
	rm -rf $(BUILDDIR)

reconfigure:
	meson setup --reconfigure $(BUILDDIR)
