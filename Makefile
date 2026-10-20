# =============================================================================
#  glass-widget - the one entrypoint for producing shippable apps.
#
#  CI (.github/workflows/build.yml) runs these exact targets, so a green CI run
#  and a local run are the same commands in the same order. Nothing else is a
#  supported way to produce an artifact.
#
#    make version    print the canonical version string
#    make exe        Windows portable .exe drop  -> dist/windows/
#    make appimage   Linux .AppImage             -> dist/linux/
#    make appdir     stage the AppDir only (no appimagetool / no network)
#    make verify     assert both artifact layouts are complete
#    make all        exe + appimage + verify  (Windows-hosted builds only)
#    make clean      remove dist/
#
#  On Linux, `make appimage` needs the Windows exe: either build it on Windows
#  and drop it at dist/windows/glass-widget/glass-widget.exe, or pass
#  EXE=/path/to/glass-widget.exe. CI wires that up across jobs automatically.
#
#  Every recipe uses paths relative to the repo root. GNU make cannot carry a
#  path containing spaces through $(abspath)/prerequisites, and plenty of real
#  Windows checkouts live under "C:\Users\First Last\...", so the rule here is:
#  run make from the repo root (or `make -C "<repo>"`) and never build an
#  absolute path out of it.
# =============================================================================

SHELL        := /bin/bash

ifeq ($(wildcard packaging/version.sh),)
$(error run make from the repository root, or use `make -C <repo-root>` - \
        packaging/version.sh was not found in $(CURDIR))
endif

VERSION      ?= $(shell packaging/version.sh)
CONFIGURATION?= Release
PLATFORM     ?= x64
EXE          ?=
NATIVE       ?=
PWSH         := $(shell command -v pwsh 2>/dev/null || command -v powershell 2>/dev/null)

APPIMAGE_ARGS := --version "$(VERSION)"
ifneq ($(strip $(EXE)),)
APPIMAGE_ARGS += --exe "$(EXE)"
endif
ifneq ($(strip $(NATIVE)),)
APPIMAGE_ARGS += --native "$(NATIVE)"
endif

.PHONY: all exe appimage appdir verify version clean help

help:
	@sed -n '3,20p' $(firstword $(MAKEFILE_LIST))

version:
	@echo "$(VERSION)"

exe:
	@test -n "$(PWSH)" || { echo "make exe: PowerShell not found - the .exe compiler needs Windows + MSBuild"; exit 1; }
	"$(PWSH)" -NoProfile -ExecutionPolicy Bypass \
		-File packaging/windows/build_exe.ps1 \
		-Configuration $(CONFIGURATION) -Platform $(PLATFORM) -Version "$(VERSION)"

appimage:
	packaging/linux/build_appimage.sh $(APPIMAGE_ARGS)

appdir:
	packaging/linux/build_appimage.sh $(APPIMAGE_ARGS) --appdir-only

verify:
	packaging/verify.sh

all: exe appimage verify

clean:
	rm -rf dist
