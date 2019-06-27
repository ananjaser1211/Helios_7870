# SPDX-License-Identifier: GPL-2.0
dtb-y += exynos7870-j7y17lte_eur_open_00.dtb
dtb-y += exynos7870-j7y17lte_eur_open_01.dtb
dtb-y += exynos7870-j7y17lte_eur_open_02.dtb
dtb-y += exynos7870-j7y17lte_eur_open_03.dtb
dtb-y += exynos7870-j7y17lte_eur_open_04.dtb
dtb-y += exynos7870-j7y17lte_eur_open_05.dtb
dtb-y += exynos7870-j7y17lte_eur_open_06.dtb
dtb-y += exynos7870-j7y17lte_eur_open_07.dtb

targets += dtbs
DTB_LIST  := $(dtb-y) $(dtbo-y)
always    := $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := *.dtb*
