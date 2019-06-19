#!/bin/bash
#
# Cronos Build Script V3.0
# For Exynos7870
# Coded by BlackMesa/AnanJaser1211 @2019
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

# Main Dir
CR_DIR=$(pwd)
# Define toolchan path
CR_TC=~/Android/Toolchains/linaro-7.4.1-aarch64-linux/bin/aarch64-linux-gnu-
# Define proper arch and dir for dts files
CR_DTS=arch/arm64/boot/dts
# Define boot.img out dir
CR_OUT=$CR_DIR/Helios/Out
# Presistant A.I.K Location
CR_AIK=$CR_DIR/Helios/A.I.K
# Main Ramdisk Location
CR_RAMDISK=$CR_DIR/Helios/Ramdisk
CR_RAMDISK_TREBLE=$CR_DIR/Helios/Treble
# Compiled image name and location (Image/zImage)
CR_KERNEL=$CR_DIR/arch/arm64/boot/Image
# Compiled dtb by dtbtool
CR_DTB=$CR_DIR/boot.img-dtb
# Kernel Name and Version
CR_VERSION=V3.1
CR_NAME=Helios_Pie_Kernel
# Thread count
CR_JOBS=16
# Target android version and platform (7/n/8/o/9/p)
CR_ANDROID=p
CR_PLATFORM=9.0.0
# Target ARCH
CR_ARCH=arm64
# Current Date
CR_DATE=$(date +%Y%m%d)
# Init build
export CROSS_COMPILE=$CR_TC
# General init
export ANDROID_MAJOR_VERSION=$CR_ANDROID
export PLATFORM_VERSION=$CR_PLATFORM
export $CR_ARCH
# J710X Specific
CR_ANDROID_J710X=n
CR_PLATFORM_J710X=7.0.0
##########################################
# Device specific Variables [SM-J530_2GB (F/G/S/L/K)]
CR_DTSFILES_J530F="exynos7870-j5y17lte_eur_open_00.dtb exynos7870-j5y17lte_eur_open_01.dtb exynos7870-j5y17lte_eur_open_02.dtb exynos7870-j5y17lte_eur_open_03.dtb exynos7870-j5y17lte_eur_open_05.dtb exynos7870-j5y17lte_eur_open_07.dtb"
CR_CONFG_J530F=j5y17lte_defconfig
CR_VARIANT_J530F=J530X
# Device specific Variables [SM-J530_3GB (Y/YM/FM/GM)]
CR_DTSFILES_J530M="exynos7870-j5y17lte_sea_openm_03.dtb exynos7870-j5y17lte_sea_openm_05.dtb exynos7870-j5y17lte_sea_openm_07.dtb"
CR_CONFG_J530M=j5y17lte_defconfig
CR_VARIANT_J530M=J530Y
# Device specific Variables [SM-J730F/G]
CR_DTSFILES_J730F="exynos7870-j7y17lte_eur_open_00.dtb exynos7870-j7y17lte_eur_open_01.dtb exynos7870-j7y17lte_eur_open_02.dtb exynos7870-j7y17lte_eur_open_03.dtb exynos7870-j7y17lte_eur_open_04.dtb exynos7870-j7y17lte_eur_open_05.dtb exynos7870-j7y17lte_eur_open_06.dtb exynos7870-j7y17lte_eur_open_07.dtb"
CR_CONFG_J730F=j7y17lte_defconfig
CR_VARIANT_J730F=J730X
# Device specific Variables [SM-J710X]
CR_DTSFILES_J710X="exynos7870-j7xelte_eur_open_00.dtb exynos7870-j7xelte_eur_open_01.dtb exynos7870-j7xelte_eur_open_02.dtb exynos7870-j7xelte_eur_open_03.dtb exynos7870-j7xelte_eur_open_04.dtb"
CR_CONFG_J710X=j7xelte_defconfig
CR_VARIANT_J710X=J710X
# Device specific Variables [SM-J701X]
CR_DTSFILES_J701X="exynos7870-j7velte_sea_open_00.dtb exynos7870-j7velte_sea_open_01.dtb exynos7870-j7velte_sea_open_03.dtb"
CR_CONFG_J701X=j7veltesea_defconfig
CR_VARIANT_J701X=J701X
# Device specific Variables [SM-G610X]
CR_DTSFILES_G610X="exynos7870-on7xelte_swa_open_00.dtb exynos7870-on7xelte_swa_open_01.dtb exynos7870-on7xelte_swa_open_02.dtb"
CR_CONFG_G610X=on7xelteswa_defconfig
CR_VARIANT_G610X=G610X
# Device specific Variables [SM-J600X]
CR_DTSFILES_J600X="exynos7870-j6lte_ltn_00.dtb exynos7870-j6lte_ltn_02.dtb"
CR_CONFG_J600X=j6lte_defconfig
CR_VARIANT_J600X=J600X
# Device specific Variables [SM-A600X]
CR_DTSFILES_A600X="exynos7870-a6lte_eur_open_00.dtb exynos7870-a6lte_eur_open_01.dtb exynos7870-a6lte_eur_open_02.dtb exynos7870-a6lte_eur_open_03.dtb"
CR_CONFG_A600X=a6lte_defconfig
CR_VARIANT_A600X=A600X
# Script functions

read -p "Clean source (y/n) > " yn
if [ "$yn" = "Y" -o "$yn" = "y" ]; then
     echo "Clean Build"    
     make clean && make mrproper    
     rm -r -f $CR_DTB
     rm -rf $CR_DTS/.*.tmp
     rm -rf $CR_DTS/.*.cmd
     rm -rf $CR_DTS/*.dtb      
else
     echo "Dirty Build"
     rm -r -f $CR_DTB
     rm -rf $CR_DTS/.*.tmp
     rm -rf $CR_DTS/.*.cmd
     rm -rf $CR_DTS/*.dtb          
fi

BUILD_ZIMAGE()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building zImage for $CR_VARIANT"
	export LOCALVERSION=-$CR_NAME-$CR_VERSION-$CR_VARIANT-$CR_DATE
	make  $CR_CONFG
	make -j$CR_JOBS
	if [ ! -e ./arch/arm64/boot/Image ]; then
	exit 0;
	echo "zImage Failed to Compile"
	echo " Abort "
	fi
	echo " "
	echo "----------------------------------------------"
}
BUILD_DTB()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building DTB for $CR_VARIANT"
	# Use the DTS list provided in the build script.
	# This source does not compile dtbs while doing Image
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
    # Copy Ramdisk
    cp -rf $CR_RAMDISK/* $CR_AIK
	# Move Compiled kernel and dtb to A.I.K Folder
	mv $CR_KERNEL $CR_AIK/split_img/boot.img-zImage
	mv $CR_DTB $CR_AIK/split_img/boot.img-dtb
	# Create boot.img
	$CR_AIK/repackimg.sh
	# Remove red warning at boot
	echo -n "SEANDROIDENFORCE" » $CR_AIK/image-new.img
	# Move boot.img to out dir
	mv $CR_AIK/image-new.img $CR_OUT/$CR_NAME-$CR_VERSION-$CR_DATE-$CR_VARIANT.img
	$CR_AIK/cleanup.sh
}
PACK_BOOT_IMG_TREBLE()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building Boot.img for $CR_VARIANT"
	cp -rf $CR_RAMDISK_TREBLE/* $CR_AIK
    # Copy Ramdisk
    cp -rf $CR_RAMDISK_TREBLE/* $CR_AIK
	# Move Compiled kernel and dtb to A.I.K Folder
	mv $CR_KERNEL $CR_AIK/split_img/boot.img-zImage
	mv $CR_DTB $CR_AIK/split_img/boot.img-dtb
	# Create boot.img
	$CR_AIK/repackimg.sh
	# Remove red warning at boot
	echo -n "SEANDROIDENFORCE" » $CR_AIK/image-new.img
	# Move boot.img to out dir
	mv $CR_AIK/image-new.img $CR_OUT/$CR_NAME-$CR_VERSION-$CR_DATE-$CR_VARIANT-Treble.img
	$CR_AIK/cleanup.sh
}
# Main Menu
clear
echo "----------------------------------------------"
echo "$CR_NAME $CR_VERSION Build Script"
echo "----------------------------------------------"
PS3='Please select your option (1-8): '
menuvar=("SM-J530X" "SM-J530Y" "SM-J730X" "SM-J710X" "SM-J701X" "SM-G610X" "SM-J600X" "SM-A600X" "Exit")
select menuvar in "${menuvar[@]}"
do
    case $menuvar in
        "SM-J530X")
            clear
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
        "SM-J530Y")
            clear
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
        "SM-J730X")
            clear
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
        "SM-J710X")
            clear
            echo "Starting $CR_VARIANT_J710X kernel build..."
            CR_VARIANT=$CR_VARIANT_J710X
            export ANDROID_MAJOR_VERSION=$CR_ANDROID_J710X
            export PLATFORM_VERSION=$CR_PLATFORM_J710X
            CR_CONFG=$CR_CONFG_J710X
            CR_DTSFILES=$CR_DTSFILES_J710X
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
        "SM-J701X")
            clear
            echo "Starting $CR_VARIANT_J701X kernel build..."
            CR_VARIANT=$CR_VARIANT_J701X
            export ANDROID_MAJOR_VERSION=$CR_ANDROID_J710X
            export PLATFORM_VERSION=$CR_PLATFORM_J710X
            CR_CONFG=$CR_CONFG_J701X
            CR_DTSFILES=$CR_DTSFILES_J701X
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
        "SM-J600X")
            clear
            echo "Starting $CR_VARIANT_J600X kernel build..."
            CR_VARIANT=$CR_VARIANT_J600X
            CR_CONFG=$CR_CONFG_J600X
            CR_DTSFILES=$CR_DTSFILES_J600X
            export ANDROID_MAJOR_VERSION=$CR_ANDROID
            export PLATFORM_VERSION=$CR_PLATFORM            
            BUILD_ZIMAGE
            BUILD_DTB
            PACK_BOOT_IMG_TREBLE
            echo " "
            echo "----------------------------------------------"
            echo "$CR_VARIANT_J600X kernel build finished."
            echo "$CR_VARIANT_J600X Ready at $CR_OUT"
            echo "Combined DTB Size = $sizT Kb"
            echo "Press Any key to end the script"
            echo "----------------------------------------------"
            read -n1 -r key
            break
            ;;   
        "SM-A600X")
            clear
            echo "Starting $CR_VARIANT_A600X kernel build..."
            CR_VARIANT=$CR_VARIANT_A600X
            CR_CONFG=$CR_CONFG_A600X
            CR_DTSFILES=$CR_DTSFILES_A600X
            export ANDROID_MAJOR_VERSION=$CR_ANDROID
            export PLATFORM_VERSION=$CR_PLATFORM            
            BUILD_ZIMAGE
            BUILD_DTB
            PACK_BOOT_IMG_TREBLE
            echo " "
            echo "----------------------------------------------"
            echo "$CR_VARIANT_A600X kernel build finished."
            echo "$CR_VARIANT_A600X Ready at $CR_OUT"
            echo "Combined DTB Size = $sizT Kb"
            echo "Press Any key to end the script"
            echo "----------------------------------------------"
            read -n1 -r key
            break
            ;;                        
        "SM-G610X")
            clear
            echo "Starting $CR_VARIANT_G610X kernel build..."
            CR_VARIANT=$CR_VARIANT_G610X
            CR_CONFG=$CR_CONFG_G610X
            CR_DTSFILES=$CR_DTSFILES_G610X
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
        "Exit")
            break
            ;;
        *) echo Invalid option.;;
    esac
done
