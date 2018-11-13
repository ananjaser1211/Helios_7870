#!/system/bin/sh
# Killer to save battery for Fly-On Mod™ by Slaid480!
# Adapted to Phanom Kernel (Note 8) by Ghost (6h0st)
 #============ Copyright (C) 2015 Salah Abouabdallah(Slaid480)===========#
 
 see <http://www.gnu.org/licenses/>.
 
#=======================================================================#
 #Interval between 2 killer runs, in seconds, 14400=4hours
RUN_EVERY=14400
 mount -o rw,seclabel,remount rootfs /
 chown 0:0 /data/.refined/killer.log
chmod 755 /data/.refined/killer.log
 FLY=/data/.refined/killer.log
 busybox rm -f $FLY
busybox touch $FLY
 echo "# Fly-On Mod LOGGING ENGINE" | tee -a $FLY
echo "" | tee -a $FLY
echo "$( date +"%m-%d-%Y %H:%M:%S" ) killer service running..." | tee -a $FLY;
 killall -9 android.process.media
killall -9 mediaserver
killall -9 com.google.android.gms
killall -9 com.google.android.gsf
killall -9 medias.codec
 echo "" | tee -a $FLY
echo "$( date +"%m-%d-%Y %H:%M:%S" ) Killing services is done!" | tee -a $FLY;
 chown 0:0 /data/.refined/killer.log
chmod 755 /data/.refined/killer.log
 mount -o ro,seclabel,remount rootfs /
