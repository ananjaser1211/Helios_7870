  #!/bin/bash
#
# Cronos Build Script V4.0
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
# TODO: add ODM mount variant
CR_DTS_TREBLE=arch/arm64/boot/exynos7870_Treble.dtsi
CR_DTS_ONEUI=arch/arm64/boot/exynos7870_Oneui.dtsi
CR_DTS_X6LTE=arch/arm64/boot/exynos7870_x6lte.dtsi
# Define boot.img out dir
CR_OUT=$CR_DIR/Cronos/Out
CR_PRODUCT=$CR_DIR/Cronos/Product
CR_TWRP_OUT=$CR_DIR/Cronos/TWRP
# Compiled image name and location (Image/zImage)
CR_KERNEL=$CR_DIR/arch/arm64/boot/Image
# Compiled dtb by dtbtool
CR_DTB=$CR_DIR/boot.img-dtb
# Kernel Name and Version
CR_VERSION=V5.0
CR_NAME=CronosTWRP
# Thread count
CR_JOBS=$(nproc --all)
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
CR_DTSFILES_J530X="exynos7870-j5y17lte_eur_open_00.dtb exynos7870-j5y17lte_eur_open_01.dtb exynos7870-j5y17lte_eur_open_02.dtb exynos7870-j5y17lte_eur_open_03.dtb exynos7870-j5y17lte_eur_open_05.dtb exynos7870-j5y17lte_eur_open_07.dtb"
CR_CONFG_J530X=j5y17lte_defconfig
CR_VARIANT_J530X=J530X
# Device specific Variables [SM-J530_3GB (Y/YM/FM/GM)]
CR_DTSFILES_J530M="exynos7870-j5y17lte_sea_openm_03.dtb exynos7870-j5y17lte_sea_openm_05.dtb exynos7870-j5y17lte_sea_openm_07.dtb"
CR_CONFG_J530M=j5y17lte_defconfig
CR_VARIANT_J530M=J530Y
# Device specific Variables [SM-J730X]
CR_DTSFILES_J730X="exynos7870-j7y17lte_eur_open_00.dtb exynos7870-j7y17lte_eur_open_01.dtb exynos7870-j7y17lte_eur_open_02.dtb exynos7870-j7y17lte_eur_open_03.dtb exynos7870-j7y17lte_eur_open_04.dtb exynos7870-j7y17lte_eur_open_05.dtb exynos7870-j7y17lte_eur_open_06.dtb exynos7870-j7y17lte_eur_open_07.dtb"
CR_CONFG_J730X=j7y17lte_defconfig
CR_VARIANT_J730X=J730X
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
# Common configs
CR_CONFIG_TREBLE=treble_defconfig
CR_CONFIG_ONEUI=oneui_defconfig
CR_CONFIG_SPLIT=NULL
CR_CONFIG_HELIOS=helios_defconfig
CR_CONFIG_TWRP=twrp_defconfig
#####################################################

# Script functions

read -p "Clean source (y/n) > " yn
if [ "$yn" = "Y" -o "$yn" = "y" ]; then
     echo "Clean Build"
     CR_CLEAN="1"
else
     echo "Dirty Build"
     CR_CLEAN="0"
fi

BUILD_CLEAN()
{
if [ $CR_CLEAN = 1 ]; then
     echo " "
     echo " Cleaning build dir"
     make clean && make mrproper
     rm -r -f $CR_DTB
     rm -rf $CR_DTS/.*.tmp
     rm -rf $CR_DTS/.*.cmd
     rm -rf $CR_DTS/*.dtb
     rm -rf $CR_DIR/.config
     rm -rf $CR_DTS/exynos7870.dtsi
     rm -rf $CR_OUT/*.img
     rm -rf $CR_OUT/*.zip
fi
if [ $CR_CLEAN = 0 ]; then
     echo " "
     echo " Skip Full cleaning"
     rm -r -f $CR_DTB
     rm -rf $CR_DTS/.*.tmp
     rm -rf $CR_DTS/.*.cmd
     rm -rf $CR_DTS/*.dtb
     rm -rf $CR_DIR/.config
     rm -rf $CR_DTS/exynos7870.dtsi
fi
}

BUILD_IMAGE_NAME()
{
	CR_IMAGE_NAME=$CR_NAME-$CR_VERSION-$CR_VARIANT-$CR_DATE
}

BUILD_GENERATE_CONFIG()
{
  # Only use for devices that are unified with 2 or more configs
  echo "----------------------------------------------"
	echo " "
	echo "Building defconfig for $CR_VARIANT"
  echo " TWRP OVERRIDE "
  echo " "
  # Respect CLEAN build rules
  BUILD_CLEAN
  if [ -e $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig ]; then
    echo " cleanup old configs "
    rm -rf $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  echo " Copy $CR_CONFIG "
  cp -f $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  if [ $CR_CONFIG_SPLIT = NULL ]; then
    echo " No split config support! "
  else
    echo " Copy $CR_CONFIG_SPLIT "
    cat $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG_SPLIT >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  fi
  echo " Copy $CR_CONFIG_TWRP "
  cat $CR_DIR/arch/$CR_ARCH/configs/$CR_CONFIG_TWRP >> $CR_DIR/arch/$CR_ARCH/configs/tmp_defconfig
  echo " Set $CR_VARIANT to generated config "
  CR_CONFIG=tmp_defconfig
}

BUILD_ZIMAGE()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building zImage for $CR_VARIANT"
	export LOCALVERSION=-$CR_IMAGE_NAME
  cp $CR_DTB_MOUNT $CR_DTS/exynos7870.dtsi
	echo "Make $CR_CONFIG"
	make $CR_CONFIG
	make -j$CR_JOBS
	if [ ! -e $CR_KERNEL ]; then
	exit 0;
	echo "Image Failed to Compile"
	echo " Abort "
	fi
    du -k "$CR_KERNEL" | cut -f1 >sizT
    sizT=$(head -n 1 sizT)
    rm -rf sizT
  rm -rf $CR_CONFIG
	echo " "
	echo "----------------------------------------------"
}
BUILD_DTB()
{
	echo "----------------------------------------------"
	echo " "
	echo "Building DTB for $CR_VARIANT"
	# This source compiles dtbs while doing Image
	./scripts/dtbTool/dtbTool -o $CR_DTB -d $CR_DTS/ -s 2048
	if [ ! -e $CR_DTB ]; then
    exit 0;
    echo "DTB Failed to Compile"
    echo " Abort "
	fi
	rm -rf $CR_DTS/.*.tmp
	rm -rf $CR_DTS/.*.cmd
	rm -rf $CR_DTS/*.dtb
  rm -rf $CR_DTS/exynos7870.dtsi
    du -k "$CR_DTB" | cut -f1 >sizdT
    sizdT=$(head -n 1 sizdT)
    rm -rf sizdT
	echo " "
	echo "----------------------------------------------"
}

BUILD_EXPORT()
{
  # Move Compiled kernel and dtb to TWRP Folder
echo "----------------------------------------------"
echo "$CR_NAME $CR_VERSION Export files"
echo "----------------------------------------------"
  rm -rf $CR_TWRP_OUT
  mkdir $CR_TWRP_OUT
  mkdir $CR_TWRP_OUT/$CR_VARIANT
  mv $CR_KERNEL $CR_TWRP_OUT/$CR_VARIANT/kernel
  mv $CR_DTB $CR_TWRP_OUT/$CR_VARIANT/dt.img
}

# Main Menu
clear
echo "----------------------------------------------"
echo "$CR_NAME $CR_VERSION Build Script"
echo "----------------------------------------------"
PS3='Please select your option (1-3): '
menuvar=("SM-J530X" "SM-J730X" "Exit")
select menuvar in "${menuvar[@]}"
do
    case $menuvar in
        "SM-J530X")
            clear
            echo "Starting $CR_VARIANT_J530X kernel build..."
            CR_CONFIG=$CR_CONFG_J530X
            CR_DTSFILES=$CR_DTSFILES_J530X
            CR_VARIANT=$CR_VARIANT_J530X
            CR_DTB_MOUNT=$CR_DTS_ONEUI
            BUILD_IMAGE_NAME
            BUILD_GENERATE_CONFIG
            BUILD_ZIMAGE
            BUILD_DTB
            BUILD_EXPORT
            echo " "
            echo "----------------------------------------------"
            echo "$CR_VARIANT kernel build finished."
            echo "Compiled DTB Size = $sizdT Kb"
            echo "Kernel Image Size = $sizT Kb"
            echo "Press Any key to end the script"
            echo "----------------------------------------------"
            read -n1 -r key
            break
            ;;
        "SM-J730X")
            clear
            echo "Starting $CR_VARIANT_J730X kernel build..."
            CR_CONFIG=$CR_CONFG_J730X
            CR_DTSFILES=$CR_DTSFILES_J730X
            CR_VARIANT=$CR_VARIANT_J730X
            CR_DTB_MOUNT=$CR_DTS_ONEUI
            BUILD_IMAGE_NAME
            BUILD_GENERATE_CONFIG
            BUILD_ZIMAGE
            BUILD_DTB
            BUILD_EXPORT
            echo " "
            echo "----------------------------------------------"
            echo "$CR_VARIANT kernel build finished."
            echo "Compiled DTB Size = $sizdT Kb"
            echo "Kernel Image Size = $sizT Kb"
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
