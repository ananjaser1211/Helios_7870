#ifndef __K2DH_DEV_H
#define __K2DH_DEV_H


/*Platform data */
struct k2hh_platform_data {
        signed char orientation[9];
	unsigned int irq_gpio;
};

#endif