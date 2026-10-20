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
# =============================================================================

SHELL        := /bin/bash
REPO_ROOT    := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
VERSION      ?= $(shell $(REPO_ROOT)/packaging/version.sh)
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
	cd "$(REPO_ROOT)" && "$(PWSH)" -NoProfile -ExecutionPolicy Bypass \
		-File packaging/windows/build_exe.ps1 \
		-Configuration $(CONFIGURATION) -Platform $(PLATFORM) -Version "$(VERSION)"

appimage:
	"$(REPO_ROOT)/packaging/linux/build_appimage.sh" $(APPIMAGE_ARGS)

appdir:
	"$(REPO_ROOT)/packaging/linux/build_appimage.sh" $(APPIMAGE_ARGS) --appdir-only

verify:
	"$(REPO_ROOT)/packaging/verify.sh"

all: exe appimage verify

clean:
	rm -rf "$(REPO_ROOT)/dist"
