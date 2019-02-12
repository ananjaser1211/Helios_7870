#!/system/bin/sh
# Copyright (C) 2012 The Android Open Source Project
#
# IMPORTANT: Do not create world writable files or directories.
# This is a common source of Android security bugs.
#
# HeliosKernel 2018
#
# @Noxxxious//@Ananjaser1211
#########################################################
# Wait until all process are up and running

while pgrep bootanimation > /dev/null; do
  sleep 1
done
while pgrep dex2oat > /dev/null; do
  sleep 1
done
while pgrep com.android.systemui > /dev/null; do
  sleep 1
done
#########################################################
# Dalvik Auto-Reboot Script
# Credits to Mwilky

if [ -f /data/magisk.apk ]; then
	pm install /data/magisk.apk
	rm /data/magisk.apk
elif [ -f /data/adb/magisk/magisk.apk ]; then
	mv /data/adb/magisk/magisk.apk /data/magisk.apk
	pm install -r /data/magisk.apk
	rm /data/magisk.apk
fi;

if [ -z "$(ls -A /data/dalvik-cache/arm)" ]; then
   sleep 1
   reboot
fi;
#########################################################
# Write personalists xml for libpersona.so
# Suggested by Foobar66

if [ ! -f /data/system/users/0/personalist.xml ]; then
	touch /data/system/users/0/personalist.xml
fi;
if [ ! -r /data/system/users/0/personalist.xml ]; then
 	chmod 600 /data/system/users/0/personalist.xml
 	chown system:system /data/system/users/0/personalist.xml
fi;
#########################################################
# Init.d

if [ ! -d /system/etc/init.d ]; then
	mkdir -p /system/etc/init.d/;
	chown -R root.root /system/etc/init.d;
	chmod 777 /system/etc/init.d/;
	chmod 777 /system/etc/init.d/*;
fi;

for FILE in /system/etc/init.d/*; do
	sh $FILE >/dev/null
done;
#########################################################
# Google Wakelock Disabler
# Thanks @Tkkg1994

# Initial
mount -o remount,rw -t auto /
mount -t rootfs -o remount,rw rootfs
mount -o remount,rw -t auto /system
mount -o remount,rw /data
mount -o remount,rw /cache

# Google play services wakelock fix
sleep 1
su -c "pm enable com.google.android.gms/.update.SystemUpdateActivity"
su -c "pm enable com.google.android.gms/.update.SystemUpdateService"
su -c "pm enable com.google.android.gms/.update.SystemUpdateService$ActiveReceiver"
su -c "pm enable com.google.android.gms/.update.SystemUpdateService$Receiver"
su -c "pm enable com.google.android.gms/.update.SystemUpdateService$SecretCodeReceiver"
su -c "pm enable com.google.android.gsf/.update.SystemUpdateActivity"
su -c "pm enable com.google.android.gsf/.update.SystemUpdatePanoActivity"
su -c "pm enable com.google.android.gsf/.update.SystemUpdateService"
su -c "pm enable com.google.android.gsf/.update.SystemUpdateService$Receiver"
su -c "pm enable com.google.android.gsf/.update.SystemUpdateService$SecretCodeReceiver"

mount -o remount,ro -t auto /
mount -t rootfs -o remount,ro rootfs
mount -o remount,ro -t auto /system
mount -o remount,rw /data
mount -o remount,rw /cache
#########################################################
# Safteynet and fake props
# Rely on sbin/fakeprop instead of symlinked sbin/resetprop (needs magisk installation)
# Thanks to @Blackmesa123 @Tkkg1994

# Knox set to 0 on running system
/sbin/fakeprop -n ro.boot.warranty_bit "0"
/sbin/fakeprop -n ro.warranty_bit "0"
# Fix safetynet flags
/sbin/fakeprop -n ro.boot.veritymode "enforcing"
/sbin/fakeprop -n ro.boot.verifiedbootstate "green"
/sbin/fakeprop -n ro.boot.flash.locked "1"
/sbin/fakeprop -n ro.boot.ddrinfo "00000001"
/sbin/fakeprop -n ro.build.selinux "1"
# Samsung related flags
/sbin/fakeprop -n ro.fmp_config "1"
/sbin/fakeprop -n ro.boot.fmp_config "1"
/sbin/fakeprop -n sys.oem_unlock_allowed "0"
#########################################################
