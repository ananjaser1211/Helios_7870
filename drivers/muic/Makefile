#
# Makefile for muic devices
#

obj-y					+= muic-core.o
obj-$(CONFIG_MUIC_NOTIFIER)		+= muic_notifier.o
obj-$(CONFIG_MUIC_MAX77804)		+= max77804-muic.o
obj-$(CONFIG_MUIC_MAX77804K)		+= max77804k-muic.o
obj-$(CONFIG_MUIC_MAX77828)		+= max77828-muic.o
obj-$(CONFIG_MUIC_MAX77843)		+= max77843-muic.o
obj-$(CONFIG_HV_MUIC_MAX77843_AFC)	+= max77843-muic-afc.o
obj-$(CONFIG_MUIC_MAX77833)             += max77833-muic.o
obj-$(CONFIG_HV_MUIC_MAX77833_AFC)      += max77833-muic-afc.o
obj-$(CONFIG_MUIC_MAX77888)		+= max77888-muic.o
obj-$(CONFIG_MUIC_FSA9480)		+= fsa9480.o
obj-$(CONFIG_MUIC_S2MM001)		+= s2mm001.o
obj-$(CONFIG_MUIC_S2MU005)		+= s2mu005-muic.o
obj-$(CONFIG_MUIC_S2MU004)		+= s2mu004-muic.o
obj-$(CONFIG_HV_MUIC_S2MU004_AFC)	+= s2mu004-muic-afc.o
obj-$(CONFIG_MUIC_TSU6721)		+= tsu6721-muic.o
obj-$(CONFIG_MUIC_SM5703)		+= sm5703-muic.o
obj-$(CONFIG_MUIC_SM5705)		+= sm5705-muic.o
obj-$(CONFIG_MUIC_SM5705_AFC)		+= sm5705-muic-afc.o
obj-$(CONFIG_MUIC_SUPPORT_CCIC)		+= muic_ccic.o

# MUIC_UNIVERSAL
obj-$(CONFIG_MUIC_UNIVERSAL_CCIC)	+= universal/muic_ccic.o
obj-$(CONFIG_MUIC_UNIVERSAL)		+= universal/muic_task.o
obj-$(CONFIG_MUIC_UNIVERSAL)		+= universal/muic_state.o
obj-$(CONFIG_MUIC_UNIVERSAL)		+= universal/muic_apis.o
obj-$(CONFIG_MUIC_UNIVERSAL)		+= universal/muic_sysfs.o
obj-$(CONFIG_MUIC_UNIVERSAL)		+= universal/muic_debug.o
obj-$(CONFIG_MUIC_UNIVERSAL)		+= universal/muic_dt.o
obj-$(CONFIG_MUIC_UNIVERSAL)		+= universal/muic_i2c.o
obj-$(CONFIG_MUIC_UNIVERSAL)		+= universal/muic_regmap.o
obj-$(CONFIG_MUIC_UNIVERSAL)		+= universal/muic_vps.o
obj-$(CONFIG_MUIC_UNIVERSAL_SM5705)	+= universal/muic_regmap_sm5705.o
obj-$(CONFIG_MUIC_UNIVERSAL_SM5705_AFC)	+= universal/muic_afc.o
obj-$(CONFIG_MUIC_UNIVERSAL_SM5504)	+= universal/muic_regmap_sm5504.o
