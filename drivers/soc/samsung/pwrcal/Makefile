# arch/arm/mach-exynos/pwrcal/Makefile
#
# Copyright (c) 2014-2014 Samsung Electronics Co., Ltd.
#		http://www.samsung.com/
#
# Licensed under GPLv2


obj-y				+= power-cal.o pwrcal-cmu.o pwrcal-pll.o pwrcal-dfs.o \
				pwrcal-pmu.o pwrcal-rae.o pwrcal-vclk.o pwrcal-dbg.o
ifeq ($(CONFIG_SOC_EXYNOS8890), y)
ifeq ($(CONFIG_SOC_EXYNOS8890_EVT1), y)
obj-y				+= S5E8890/S5E8890-cmu.o S5E8890/S5E8890-pmu.o \
				S5E8890/S5E8890-dfs.o S5E8890/S5E8890-asv.o \
				S5E8890/S5E8890-syspwr.o S5E8890/S5E8890-vclk.o \
				S5E8890/S5E8890-drampara.o S5E8890/S5E8890-pll.o \
				S5E8890/S5E8890-rae.o
else
obj-y				+= S5E8890_EVT0/S5E8890-cmu.o S5E8890_EVT0/S5E8890-pmu.o \
				S5E8890_EVT0/S5E8890-dfs.o S5E8890_EVT0/S5E8890-asv.o \
				S5E8890_EVT0/S5E8890-syspwr.o S5E8890_EVT0/S5E8890-vclk.o \
				S5E8890_EVT0/S5E8890-drampara.o S5E8890_EVT0/S5E8890-pll.o \
				S5E8890_EVT0/S5E8890-rae.o
endif
endif
obj-$(CONFIG_SOC_EXYNOS7870)	+= S5E7870/S5E7870-cmu.o S5E7870/S5E7870-pmu.o \
				S5E7870/S5E7870-dfs.o S5E7870/S5E7870-asv.o \
				S5E7870/S5E7870-syspwr.o S5E7870/S5E7870-vclk.o \
				S5E7870/S5E7870-drampara.o S5E7870/S5E7870-pll.o \
				S5E7870/S5E7870-rae.o \
