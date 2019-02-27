obj-$(CONFIG_USE_DIRECT_IS_CONTROL) += fimc-is-hw-api-common.o \
		fimc-is-hw-api-vra.o

obj-$(CONFIG_CAMERA_FIMC_SCALER_USE) += fimc-is-hw-api-scp.o
obj-$(CONFIG_MC_SCALER_V1_20) += fimc-is-hw-api-mcscaler-v120.o
obj-$(CONFIG_MC_SCALER_V2_10) += fimc-is-hw-api-mcscaler-v210.o

obj-$(CONFIG_USE_HW_API_COMMON) += fimc-is-hw-api-common.o

EXTRA_CFLAGS += -Idrivers/media/platform/exynos/fimc-is2
EXTRA_CFLAGS += -Idrivers/media/platform/exynos/fimc-is2/include
EXTRA_CFLAGS += -Idrivers/media/platform/exynos/fimc-is2/hardware
EXTRA_CFLAGS += -Idrivers/media/platform/exynos/fimc-is2/hardware/api
EXTRA_CFLAGS += -Idrivers/media/platform/exynos/fimc-is2/vendor
