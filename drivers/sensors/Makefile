#
# Makefile for the sensors drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_SENSORS)          += sensors_core.o

# accelerometer_sensor
obj-$(CONFIG_SENSORS_K2HH)          += k2hh.o
obj-$(CONFIG_SENSORS_BMA2X2)        += bma2x2.o
obj-$(CONFIG_SENSORS_LIS2DS)            += lis2ds_core.o
obj-$(CONFIG_SENSORS_LIS2DS_I2C)            += lis2ds_i2c.o
obj-$(CONFIG_SENSORS_LIS2DS_SPI)            += lis2ds_spi.o

# combo sensor
obj-$(CONFIG_ST_LSM6DS3) += st_lsm6ds3.o
st_lsm6ds3-objs := st_lsm6ds3_core.o
obj-$(CONFIG_ST_LSM6DS3_I2C) 	+= st_lsm6ds3_i2c.o
obj-$(CONFIG_ST_LSM6DS3_SPI) 	+= st_lsm6ds3_spi.o
obj-$(CONFIG_SENSORS_LSM6DSL)            += lsm6dsl_core.o
obj-$(CONFIG_SENSORS_LSM6DSL_I2C)        += lsm6dsl_i2c.o
obj-$(CONFIG_SENSORS_LSM6DSL_SPI)        += lsm6dsl_spi.o

# optical_sensor
obj-$(CONFIG_SENSORS_BH1733)	    += bh1733.o
obj-$(CONFIG_SENSORS_TCS3490)	    += tcs3490.o
obj-$(CONFIG_SENSORS_CM3323)	    += cm3323.o
obj-$(CONFIG_SENSORS_CM36652)	    += cm36652.o
obj-$(CONFIG_SENSORS_CM36655)       += cm36655.o
obj-$(CONFIG_SENSORS_CM36672P)      += cm36672p.o
obj-$(CONFIG_SENSORS_CM36686)       += cm36686.o
obj-$(CONFIG_SENSORS_TMD3782)       += tmd3782.o
obj-$(CONFIG_SENSORS_TMD3700)       += tmd3700.o
obj-$(CONFIG_SENSORS_TMD3725)       += tmd3725.o
obj-$(CONFIG_SENSORS_STK3013)       += stk3013.o
obj-$(CONFIG_SENSORS_GP2AP070S)     += gp2ap070s.o
# grip_sensor
obj-$(CONFIG_SENSORS_SX9306)        += sx9306.o
obj-$(CONFIG_SENSORS_SX9306_SUB)    += sx9306_sub.o
obj-$(CONFIG_SENSORS_SX9306_WIFI)   += sx9306_wifi.o
obj-$(CONFIG_SENSORS_SX9310)        += sx9310.o
obj-$(CONFIG_SENSORS_SX9310_WIFI)   += sx9310_wifi.o
obj-$(CONFIG_SENSORS_SX9320)	    += sx9320.o
obj-$(CONFIG_SENSORS_SX9320_WIFI)   += sx9320_wifi.o
obj-$(CONFIG_SENSORS_SX9500)        += sx9500.o
obj-$(CONFIG_SENSORS_SX9500_WIFI)   += sx9500_wifi.o
obj-$(CONFIG_SENSORS_ABOV_A96T316)	+= abov_a96t316.o
obj-$(CONFIG_SENSORS_A96T3X6)		+= a96t3x6.o
# hrm_sensor
obj-$(CONFIG_SENSORS_PPSX60)		+= ppsx60.o
obj-$(CONFIG_SENSORS_MAX86900)		+= max86900.o
obj-$(CONFIG_SENSORS_MAX86902)		+= max86902.o
obj-$(CONFIG_SENSORS_MAX_NOTCHFILTER)	+= max_notchfilter/
# magnetic_sensor
obj-$(CONFIG_SENSORS_BMM050)	+= bmm050_driver.o bmm050.o
obj-$(CONFIG_SENSORS_AK09916C)	+= ak09916c.o
obj-$(CONFIG_SENSORS_YAS537)        += yas537/yas_mag_drv-yas537.o yas537/yas_mag_kernel.o
obj-$(CONFIG_SENSORS_YAS539)        += yas539/yas_mag_drv-yas539.o yas539/yas_mag_kernel.o
