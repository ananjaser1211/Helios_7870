################################################################################

1. How to Build
	- get Toolchain
		From android git server , codesourcery and etc ..
		- arm64-eabi-4.9
		
	- edit Makefile
                edit "CROSS_COMPILE" to right toolchain path(You downloaded).
		    EX)  CROSS_COMPILE= $(android platform directory you download)/android/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
		    Ex)  CROSS_COMPILE=/usr/local/toolchain/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-		// check the location of toolchain
				
	- make
		$ make exynos7870-j5y17lte_defconfig
		$ make -j64

2. Output files
	- Kernel : arch/arm64/boot/Image
	- module : drivers/*/built-in.o

3. How to Clean	
		$ make clean
		$ make ARCH=arm64 distclean
################################################################################
