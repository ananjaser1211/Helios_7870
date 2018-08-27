#!/bin/bash
#
# Cronos Build Script
# For Exynos7870
# Coded by BlackMesa/AnanJaser1211 @2018
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software

# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Directory Contol
CR_DIR=$(pwd)
CR_TC=~/Android/Kernels/Toolchains/aarch64-linux-android-6.0-kernel/bin/aarch64-linux-android-
CR_DTS=arch/arm64/boot/dts
CR_OUT=$CR_DIR/Helios/Out
CR_AIK=$CR_DIR/Helios/A.I.K
CR_RAMDISK=$CR_DIR/Helios/Ramdisk
CR_KERNEL=$CR_DIR/arch/arm64/boot/Image
CR_DTB=$CR_DIR/boot.img-dtb
# Kernel Variables
CR_VERSION=V2.0
CR_NAME=HeliosPro_Kernel
CR_JOBS=5
CR_ANDROID=o
CR_PLATFORM=8.0.0
CR_ARCH=arm64
CR_DATE=$(date +%Y%m%d)
# Init build
export CROSS_COMPILE=$CR_TC
export ANDROID_MAJOR_VERSION=$CR_ANDROID
export PLATFORM_VERSION=$CR_PLATFORM
export $CR_ARCH
##########################################
# Device specific Variables [SM-J530_2GB (F/G/S/L/K)]
CR_DTSFILES_J530F="exynos7870-j5y17lte_eur_open_00.dtb exynos7870-j5y17lte_eur_open_01.dtb exynos7870-j5y17lte_eur_open_02.dtb exynos7870-j5y17lte_eur_open_03.dtb exynos7870-j5y17lte_eur_open_05.dtb exynos7870-j5y17lte_eur_open_07.dtb"
CR_CONFG_J530F=j5y17lte_2G_defconfig
CR_VARIANT_J530F=J530F_2GB
# Device specific Variables [SM-J530_3GB (Y/YM/FM/GM)]
CR_DTSFILES_J530M="exynos7870-j5y17lte_sea_openm_03.dtb exynos7870-j5y17lte_sea_openm_05.dtb exynos7870-j5y17lte_sea_openm_07.dtb"
CR_CONFG_J530M=j5y17lte_3G_defconfig
CR_VARIANT_J530M=J530Y_3GB
# Device specific Variables [SM-J730F/G]
CR_DTSFILES_J730F="exynos7870-j7y17lte_eur_open_00.dtb exynos7870-j7y17lte_eur_open_01.dtb exynos7870-j7y17lte_eur_open_02.dtb exynos7870-j7y17lte_eur_open_03.dtb exynos7870-j7y17lte_eur_open_04.dtb exynos7870-j7y17lte_eur_open_05.dtb exynos7870-j7y17lte_eur_open_06.dtb exynos7870-j7y17lte_eur_open_07.dtb"
CR_CONFG_J730F=j7y17lte_eur_open_defconfig
CR_VARIANT_J730F=J730F-G
#####################################################
################# TEST VERSION ######################
#####################################################
# Device specific Variables [TEST]
CR_DTSFILES_TEST="exynos7870-j5y17lte_eur_open_00.dtb exynos7870-j5y17lte_eur_open_01.dtb exynos7870-j5y17lte_eur_open_02.dtb exynos7870-j5y17lte_eur_open_03.dtb exynos7870-j5y17lte_eur_open_05.dtb exynos7870-j5y17lte_eur_open_07.dtb"
CR_CONFG_TEST=j5y17lte_TEST_defconfig
CR_VARIANT_TEST=J530F
CR_VERSION_TEST=V0.1
CR_NAME_TEST=Helios_TEST
CR_RAMDISK_TEST=$CR_DIR/Helios/Ramdisk_TEST
#####################################################
################# TEST VERSION ######################
#####################################################

# Script functions
CLEAN_SOURCE()
{
echo "----------------------------------------------"
echo " "
echo "Cleaning"	
#make clean
#make mrproper
# rm -r -f $CR_OUT/*
rm -r -f $CR_DTB
rm -rf $CR_DTS/.*.tmp
rm -rf $CR_DTS/.*.cmd
rm -rf $CR_DTS/*.dtb	
echo " "
echo "----------------------------------------------"	
}
DIRTY_SOURCE()
{
echo "----------------------------------------------"
echo " "
echo "Cleaning"	
# make clean
# make mrproper
# rm -r -f $CR_OUT/*
rm -r -f $CR_DTB
rm -rf $CR_DTS/.*.tmp
rm -rf $CR_DTS/.*.cmd
rm -rf $CR_DTS/*.dtb	
echo " "
echo "----------------------------------------------"	
}
BUILD_ZIMAGE()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building zImage for $CR_VARIANT"	
	export LOCALVERSION=-$CR_NAME-$CR_VERSION-$CR_VARIANT-$CR_DATE
	make  $CR_CONFG
	make -j$CR_JOBS
	echo " "
	echo "----------------------------------------------"	
}
BUILD_DTB()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building DTB for $CR_VARIANT"	
	export $CR_ARCH
	export CROSS_COMPILE=$CR_TC
	export ANDROID_MAJOR_VERSION=$CR_ANDROID
	make  $CR_CONFG
	make $CR_DTSFILES
	./scripts/dtbTool/dtbTool -o ./boot.img-dtb -d $CR_DTS/ -s 2048
	du -k "./boot.img-dtb" | cut -f1 >sizT
	sizT=$(head -n 1 sizT)
	rm -rf sizT
	echo "Combined DTB Size = $sizT Kb"
	rm -rf $CR_DTS/.*.tmp
	rm -rf $CR_DTS/.*.cmd
	rm -rf $CR_DTS/*.dtb	
	echo " "
	echo "----------------------------------------------"
}
PACK_BOOT_IMG()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building Boot.img for $CR_VARIANT"
	cp -rf $CR_RAMDISK/* $CR_AIK
	mv $CR_KERNEL $CR_AIK/split_img/boot.img-zImage
	mv $CR_DTB $CR_AIK/split_img/boot.img-dtb
	$CR_AIK/repackimg.sh
	echo -n "SEANDROIDENFORCE" » $CR_AIK/image-new.img
	mv $CR_AIK/image-new.img $CR_OUT/$CR_NAME-$CR_VERSION-$CR_DATE-$CR_VARIANT.img
	$CR_AIK/cleanup.sh
}
#####################################################
################# TEST VERSION ######################
#####################################################
TEST_KERNEL()
{
echo "Cleaning"	
# make clean
# make mrproper
# rm -r -f $CR_OUT/*
CR_VARIANT=$CR_VARIANT_TEST
CR_VERSION2=$CR_VERSION_TEST
CR_NAME=$CR_NAME_TEST
rm -r -f $CR_DTB
rm -rf $CR_DTS/.*.tmp
rm -rf $CR_DTS/.*.cmd
rm -rf $CR_DTS/*.dtb
echo "Building zImage for $CR_VARIANT"	
export LOCALVERSION=-$CR_NAME-$CR_VERSION2-$CR_VARIANT-$CR_DATE
make  $CR_CONFG_TEST
make -j$CR_JOBS
echo "Building DTB for $CR_VARIANT"	
export $CR_ARCH
export CROSS_COMPILE=$CR_TC
export ANDROID_MAJOR_VERSION=$CR_ANDROID
make  $CR_CONFG_TEST
make $CR_DTSFILES_TEST
./scripts/dtbTool/dtbTool -o ./boot.img-dtb -d $CR_DTS/ -s 2048
du -k "./boot.img-dtb" | cut -f1 >sizT
sizT=$(head -n 1 sizT)
rm -rf sizT
echo "Combined DTB Size = $sizT Kb"
rm -rf $CR_DTS/.*.tmp
rm -rf $CR_DTS/.*.cmd
rm -rf $CR_DTS/*.dtb	
echo "Building Boot.img for $CR_VARIANT_TEST"
cp -rf $CR_RAMDISK_TEST/* $CR_AIK
mv $CR_KERNEL $CR_AIK/split_img/boot.img-zImage
mv $CR_DTB $CR_AIK/split_img/boot.img-dtb
$CR_AIK/repackimg.sh
echo -n "SEANDROIDENFORCE" » $CR_AIK/image-new.img
mv $CR_AIK/image-new.img $CR_OUT/$CR_NAME-$CR_VERSION2-$CR_DATE-$CR_VARIANT.img
$CR_AIK/cleanup.sh  
}
#####################################################
################# TEST VERSION ######################
#####################################################
# Main Menu
clear
echo "----------------------------------------------"
echo "$CR_NAME $CR_VERSION Build Script"
echo "----------------------------------------------"
PS3='Please select your option (1-5): '
menuvar=("SM-J530_2G" "SM-J530_3G" "SM-J730F-G" "Exit" "TEST")
select menuvar in "${menuvar[@]}"
do
    case $menuvar in
        "SM-J530_2G")
            clear
            CLEAN_SOURCE
            echo "Starting $CR_VARIANT_J530F kernel build..."
	    CR_VARIANT=$CR_VARIANT_J530F
	    CR_CONFG=$CR_CONFG_J530F
            CR_DTSFILES=$CR_DTSFILES_J530F
	    BUILD_ZIMAGE
            BUILD_DTB
            PACK_BOOT_IMG
            echo " "
            echo "----------------------------------------------"
            echo "$CR_VARIANT kernel build finished."
            echo "$CR_VARIANT Ready at $CR_OUT"
            echo "Combined DTB Size = $sizT Kb"
	    echo "Press Any key to end the script"
            echo "----------------------------------------------"
            read -n1 -r key
            break
            ;;
        "SM-J530_3G")
            clear
            CLEAN_SOURCE
            echo "Starting $CR_VARIANT_J530M kernel build..."
	    CR_VARIANT=$CR_VARIANT_J530M
	    CR_CONFG=$CR_CONFG_J530M
            CR_DTSFILES=$CR_DTSFILES_J530M
	    BUILD_ZIMAGE
            BUILD_DTB
            PACK_BOOT_IMG
            echo " "
            echo "----------------------------------------------"
            echo "$CR_VARIANT kernel build finished."
            echo "$CR_VARIANT Ready at $CR_OUT"
            echo "Combined DTB Size = $sizT Kb"
	    echo "Press Any key to end the script"
            echo "----------------------------------------------"
            read -n1 -r key
            break
            ;;
        "SM-J730F-G")
            clear
            CLEAN_SOURCE
            echo "Starting $CR_VARIANT_J730F kernel build..."
            CR_VARIANT=$CR_VARIANT_J730F
	    CR_CONFG=$CR_CONFG_J730F
            CR_DTSFILES=$CR_DTSFILES_J730F
	    BUILD_ZIMAGE
            BUILD_DTB
            PACK_BOOT_IMG
            echo " "
            echo "----------------------------------------------"
            echo "$CR_VARIANT kernel build finished."
            echo "$CR_VARIANT Ready at $CR_OUT"
            echo "Combined DTB Size = $sizT Kb"
	    echo "Press Any key to end the script"
            echo "----------------------------------------------"
            read -n1 -r key
            break
            ;;
#####################################################
################# TEST VERSION ######################
#####################################################            
        "TEST")
            clear
            CLEAN_SOURCE
            echo "Starting $CR_VARIANT_TEST kernel build..."
            CR_VARIANT=$CR_VARIANT_TEST
            CR_CONFG=$CR_CONFG_TEST
            CR_DTSFILES=$CR_DTSFILES_TEST
            TEST_KERNEL
            echo " "
            echo "----------------------------------------------"
            echo "$CR_VARIANT_TEST kernel build finished."
            echo "$CR_VARIANT_TEST Ready at $CR_OUT"
            echo "Combined DTB Size = $sizT Kb"
            echo "Press Any key to end the script"
            echo "----------------------------------------------"
            read -n1 -r key
            break
            ;;	            
#####################################################
################# TEST VERSION ######################
#####################################################            
        "Exit")
            break
            ;;
        *) echo Invalid option.;;
    esac
done
