DAALA_VERSION := e248823a04292a8c2f56aa260f5c0b369d41d64e
DAALA_GITURL := https://gitlab.xiph.org/xiph/daala.git

# Default disabled for now
# PKGS += daala
ifeq ($(call need_pkg,"daala"),)
PKGS_FOUND += daala
endif

$(TARBALLS)/daala-$(DAALA_VERSION).tar.xz:
	$(call download_git,$(DAALA_GITURL),,$(DAALA_VERSION))

.sum-daala: daala-$(DAALA_VERSION).tar.xz
	$(call check_githash,$(DAALA_VERSION))
	touch $@

daala: daala-$(DAALA_VERSION).tar.xz .sum-daala
	$(UNPACK)
	# $(call update_autoconfig,build-aux)
	$(call pkg_static,"daaladec.pc.in")
	$(call pkg_static,"daalaenc.pc.in")
	$(MOVE)

DAALACONF := \
	--disable-tools \
	--disable-unit-tests \
	--disable-examples \
	--disable-player

.daala: daala
	mkdir -p daala/m4
	$(RECONF)
	$(MAKEBUILDDIR)
	$(MAKECONFIGURE) $(DAALACONF)
	+$(MAKEBUILD)
	+$(MAKEBUILD) install
	touch $@
