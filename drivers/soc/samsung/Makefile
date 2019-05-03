#
# Exynos SoC Specific drivers
#

obj-$(CONFIG_ARCH_EXYNOS)	+= exynos-pm.o
obj-$(CONFIG_ARCH_EXYNOS)	+= exynos-pmu.o
obj-$(CONFIG_ARCH_EXYNOS)	+= exynos-powermode.o
obj-$(CONFIG_ARCH_EXYNOS)	+= exynos-el3_mon.o
obj-$(CONFIG_ARCH_EXYNOS)	+= exynos-cpu_hotplug.o
obj-$(CONFIG_EXYNOS_MCINFO)	+= exynos-mcinfo.o

obj-$(CONFIG_ARCH_EXYNOS)	+= secmem.o
obj-$(CONFIG_ARCH_EXYNOS)	+= exynos-smc.o

# CAL
obj-$(CONFIG_PWRCAL)            += pwrcal/
obj-$(CONFIG_EXYNOS_CHIPID)	+= exynos-chipid.o
obj-$(CONFIG_EXYNOS_REBOOT)	+= exynos-reboot.o
obj-$(CONFIG_PM_RUNTIME)        += pm_domains/

# CP
obj-$(CONFIG_SEC_SIPC_MODEM_IF)	+= pmu-cp.o

obj-$(CONFIG_ECT)		+= ect_parser.o

obj-$(CONFIG_EXYNOS_HOTPLUG_GOVERNOR)	+= exynos-hotplug_governor.o
obj-$(CONFIG_SEC_INCELL) += sec_incell.o