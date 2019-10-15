# SPDX-License-Identifier: GPL-2.0

dtb-$(CONFIG_BOARD_J5Y17LTE) += exynos7870-j5y17lte_eur_open_00.dtb \
exynos7870-j5y17lte_eur_open_01.dtb \
exynos7870-j5y17lte_eur_open_02.dtb \
exynos7870-j5y17lte_eur_open_03.dtb \
exynos7870-j5y17lte_eur_open_05.dtb \
exynos7870-j5y17lte_eur_open_07.dtb

dtb-$(CONFIG_BOARD_J7Y17LTE) += exynos7870-j7y17lte_eur_open_00.dtb \
exynos7870-j7y17lte_eur_open_01.dtb \
exynos7870-j7y17lte_eur_open_02.dtb \
exynos7870-j7y17lte_eur_open_03.dtb \
exynos7870-j7y17lte_eur_open_04.dtb \
exynos7870-j7y17lte_eur_open_05.dtb \
exynos7870-j7y17lte_eur_open_06.dtb \
exynos7870-j7y17lte_eur_open_07.dtb

dtb-$(CONFIG_BOARD_J7XELTE) += exynos7870-j7xelte_eur_open_00.dtb \
exynos7870-j7xelte_eur_open_01.dtb \
exynos7870-j7xelte_eur_open_02.dtb \
exynos7870-j7xelte_eur_open_03.dtb \
exynos7870-j7xelte_eur_open_04.dtb

dtb-$(CONFIG_BOARD_J7VELTE) += exynos7870-j7velte_sea_open_00.dtb \
exynos7870-j7velte_sea_open_01.dtb \
exynos7870-j7velte_sea_open_03.dtb

dtb-$(CONFIG_BOARD_ON7XELTE) += exynos7870-on7xelte_swa_open_00.dtb \
exynos7870-on7xelte_swa_open_01.dtb \
exynos7870-on7xelte_swa_open_02.dtb

dtb-$(CONFIG_BOARD_J6LTE) += exynos7870-j6lte_ltn_00.dtb \
exynos7870-j6lte_ltn_02.dtb

dtb-$(CONFIG_BOARD_A6LTE) += exynos7870-a6lte_eur_open_00.dtb \
exynos7870-a6lte_eur_open_01.dtb \
exynos7870-a6lte_eur_open_02.dtb \
exynos7870-a6lte_eur_open_03.dtb

targets += dtbs
DTB_LIST  := $(dtb-y) $(dtbo-y)
always    := $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := *.dtb*
