obj-$(CONFIG_EXYNOS_DECON_LCD) += dsim_panel.o
obj-$(CONFIG_EXYNOS_DECON_MDNIE_LITE) += mdnie_lite.o
obj-$(CONFIG_DISPLAY_USE_INFO) += dpui.o

obj-$(and $(CONFIG_EXYNOS_DECON_MDNIE_LITE),$(CONFIG_SEC_GPIO_DVS),$(subst undefined,y,$(origin CONFIG_SAMSUNG_PRODUCT_SHIP))) += dd_mdnie.o
obj-$(and $(CONFIG_SEC_GPIO_DVS),$(subst undefined,y,$(origin CONFIG_SAMSUNG_PRODUCT_SHIP))) += dd_lcd.o dd_backlight.o dd_dpu.o dd_param.o

obj-$(CONFIG_PANEL_EA8064G_DYNAMIC) += ea8064g_lcd_ctrl.o dimming_core.o
obj-$(CONFIG_PANEL_S6E3FA3_J7XE) += s6e3fa3_a7xe_lcd_ctrl.o dynamic_aid.o
obj-$(CONFIG_PANEL_EA8061S_J7XE) += ea8061_lcd_ctrl.o ea8061s_lcd_ctrl.o dimming_core.o
obj-$(CONFIG_PANEL_LTL101AL06) += ltl101al06_lcd_ctrl.o
obj-$(CONFIG_PANEL_S6D7AA0) += s6d7aa0_degas2_lcd_ctrl.o
obj-$(CONFIG_PANEL_HX8279D) += hx8279d_lcd_ctrl.o
obj-$(CONFIG_PANEL_S6D7AA0_BV055HDM) += s6d7aa0_bv055hdm_lcd_ctrl.o
obj-$(CONFIG_PANEL_S6E3AA2_AMS474KF09) += s6e3aa2_ams474kf09_lcd_ctrl.o dynamic_aid.o
obj-$(CONFIG_PANEL_TD4300) += td4300_lcd_ctrl.o td4310_lcd_ctrl.o
obj-$(CONFIG_PANEL_S6E3AA2_A3Y17) += s6e3aa2_a3y17_lcd_ctrl.o dynamic_aid.o
obj-$(CONFIG_PANEL_S6E8AA5X01)	+= s6e8aa5x01_j5y17_lcd_ctrl.o dynamic_aid.o
obj-$(CONFIG_PANEL_TD4100_J7POP) += td4100_j7pop_lcd_ctrl.o
obj-$(CONFIG_PANEL_S6E3FA3_J7Y17) += s6e3fa3_j7y17_lcd_ctrl.o dynamic_aid.o
obj-$(CONFIG_PANEL_EA8061S_J7VE) += ea8061_lcd_ctrl.o dimming_core.o ea8061s_j7lite_lcd_ctrl.o dynamic_aid.o
obj-$(CONFIG_PANEL_S6D7AA0_GTACTIVE2) += s6d7aa0_gtactive2_lcd_ctrl.o
obj-$(CONFIG_PANEL_S6D78A0_GPPIRIS) += s6d78a0_qhd_gppiris_lcd_ctrl.o
obj-$(CONFIG_PANEL_S6D7AT0B_J7TOP) += s6d7at0b_j7top_lcd_ctrl.o
obj-$(CONFIG_PANEL_S6E8AA5X01_A6LTE) += s6e8aa5x01_a6lte_lcd_ctrl.o dynamic_aid.o
obj-$(CONFIG_PANEL_S6E8AA5X01_J6Y18) += s6e8aa5x01_a6lte_lcd_ctrl.o dynamic_aid.o
obj-$(CONFIG_PANEL_S6D7AT0B_M10LTE) += s6d7at0b_m10lte_lcd_ctrl.o
obj-$(CONFIG_PANEL_TD4101_A2CORELTE) += td4101_a2corelte_lcd_ctrl.o
