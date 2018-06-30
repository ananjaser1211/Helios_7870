#!/system/bin/sh
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

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