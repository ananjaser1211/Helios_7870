#
# Makefile for the kernel IFPMIC device drivers.
#

# Object files in subdirectories

#modified by khoonk 2018.01.03
obj-$(CONFIG_MFD_S2MU004)	+= s2mu004_core.o s2mu004_irq.o
obj-$(CONFIG_USE_MUIC)		+= muic/
#modified by khoonk 2018.01.08
obj-$(CONFIG_BATTERY_SAMSUNG)   += battery_v2/
obj-$(CONFIG_USB_TYPEC_MANAGER_NOTIFIER)	+= manager/
obj-$(CONFIG_PDIC)			+= pdic/
#obj-$(CONFIG_LED)			+= led/
#obj-$(CONFIG_MOTOR)			+= motor/
#obj-$(CONFIG_CHARGER)		+= charger/
#obj-$(CONFIG_FUELGAUGE)		+= fuelgauge/
