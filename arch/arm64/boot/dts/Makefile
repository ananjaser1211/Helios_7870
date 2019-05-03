# SPDX-License-Identifier: GPL-2.0
dtb-y +=  exynos7870-a6lte_eur_open_00.dtb
dtb-y +=  exynos7870-a6lte_eur_open_01.dtb
dtb-y +=  exynos7870-a6lte_eur_open_02.dtb
dtb-y +=  exynos7870-a6lte_eur_open_03.dtb

targets += dtbs
DTB_LIST  := $(dtb-y) $(dtbo-y)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := *.dtb*
