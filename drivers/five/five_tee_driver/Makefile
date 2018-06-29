#
# Integrity TEE Driver
#
TEEC_CLIENT_PATH := multibuild/source/gp-api/client
PROTOCOL_PATH := multibuild/source/gp-api/protocol

obj-$(CONFIG_FIVE_TEE_DRIVER) += itee_driver.o

obj-$(CONFIG_FIVE_USE_TRUSTONIC) += $(TEEC_CLIENT_PATH)/teec_common_tbase.o
obj-$(CONFIG_FIVE_USE_TZDEV) += $(TEEC_CLIENT_PATH)/teec_common_teegris.o

itee_driver-objs += five_tee_driver.o \
                    $(TEEC_CLIENT_PATH)/tee_client_api.o \
                    $(PROTOCOL_PATH)/teec_param_utils.o
ccflags-$(CONFIG_FIVE_TEE_DRIVER) += -Isecurity/integrity/five \
                                -Idrivers/five/five_tee_driver/$(PROTOCOL_PATH) \
                                -Idrivers/five/five_tee_driver/$(TEEC_CLIENT_PATH) \
                                -Idrivers/five/five_tee_driver/multibuild/include/gp-api

ccflags-$(CONFIG_FIVE_USE_TRUSTONIC) += -DCONFIG_TRUSTONIC
ccflags-$(CONFIG_FIVE_USE_TZDEV) += -DCONFIG_TEEGRIS

ifeq ($(CONFIG_FIVE_USE_TRUSTONIC), y)
  ifeq ($(CONFIG_SOC_EXYNOS9810), y)
    PLATFORM := exynos9810
    ccflags-$(CONFIG_FIVE_TEE_DRIVER) += -Idrivers/gud/gud-$(PLATFORM)/MobiCoreDriver
    ccflags-$(CONFIG_FIVE_TEE_DRIVER) += -Idrivers/gud/gud-$(PLATFORM)/MobiCoreDriver/public
  else ifeq ($(CONFIG_SOC_EXYNOS7570), y)
    PLATFORM := exynos7570
    ccflags-$(CONFIG_FIVE_TEE_DRIVER) += -Idrivers/gud/gud-$(PLATFORM)/MobiCoreDriver
    ccflags-$(CONFIG_FIVE_TEE_DRIVER) += -Idrivers/gud/gud-$(PLATFORM)/MobiCoreDriver/public
  else ifeq ($(CONFIG_SOC_EXYNOS7870), y)
    PLATFORM := exynos7870
    ccflags-$(CONFIG_FIVE_TEE_DRIVER) += -Idrivers/gud/gud-$(PLATFORM)/MobiCoreDriver
    ccflags-$(CONFIG_FIVE_TEE_DRIVER) += -Idrivers/gud/gud-$(PLATFORM)/MobiCoreDriver/public
  else
    $(error "Unknown SoC")
  endif
endif

ifeq ($(CONFIG_FIVE_USE_TZDEV),y)
    ccflags-$(CONFIG_FIVE_TEE_DRIVER) += -Idrivers/misc/tzdev/include
endif
