ARCH          := arm64

KERNEL_SRC ?= ../sdk_kernel/kernel
CROSS_COMPILE := $(PWD)/../aarch64-linux-android-4.9/bin/aarch64-linux-android-

KBUILD_OPTIONS := WLAN_ROOT=$(PWD)
KBUILD_OPTIONS += MODNAME=wlan

# Determine if the driver license is Open source or proprietary
# This is determined under the assumption that LICENSE doesn't change.
# Please change here if driver license text changes.
LICENSE_FILE ?= $(PWD)/$(WLAN_ROOT)/CORE/HDD/src/wlan_hdd_main.c
WLAN_OPEN_SOURCE = $(shell if grep -q "MODULE_LICENSE(\"Dual BSD/GPL\")" \
		$(LICENSE_FILE); then echo 1; else echo 0; fi)

#By default build for CLD
WLAN_SELECT := CONFIG_QCA_CLD_WLAN=m
KBUILD_OPTIONS += CONFIG_QCA_WIFI_ISOC=0
KBUILD_OPTIONS += CONFIG_QCA_WIFI_2_0=1
KBUILD_OPTIONS += LSI_PLATFORM=1
KBUILD_OPTIONS += CONFIG_CNSS_SDIO=y
KBUILD_OPTIONS += $(WLAN_SELECT)
KBUILD_OPTIONS += WLAN_OPEN_SOURCE=$(WLAN_OPEN_SOURCE)
KBUILD_OPTIONS += $(KBUILD_EXTRA) # Extra config if any
KBUILD_OPTIONS += CONFIG_SCPC_FEATURE=y
KBUILD_OPTIONS += CONFIG_LINUX_QCMBR=y

all:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_SRC) M=$(shell pwd) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 -C $(KERNEL_SRC) M=$(shell pwd) modules_install

clean:
	#$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_SRC) M=$(PWD) clean
