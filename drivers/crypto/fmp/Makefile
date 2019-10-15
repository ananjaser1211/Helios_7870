# Exynos FMP makefile
obj-$(CONFIG_FIPS_FMP) += first_file.o
obj-$(CONFIG_FMP_MMC) += fmp_mmc.o sha256.o
obj-$(CONFIG_FIPS_FMP) += fmpdev.o fmp_integrity.o fmplib.o
obj-$(CONFIG_FIPS_FMP) += hmac-sha256.o
obj-$(CONFIG_FIPS_FMP) += fmp_mmc_fips.o
obj-$(CONFIG_FIPS_FMP) += last_file.o

ccflags-$(CONFIG_FMP_MMC) += -Idrivers/mmc/host
