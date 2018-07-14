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
# Coded by mwilky
#

LOGFILE=/data/refined/dalvik.log
REBOOTLOGFILE=/data/refined/reboot.log

log_print() {
  echo "$1"
  echo "$1" >> $LOGFILE
}

rebootlog_print() {
  echo "$1"
  echo "$1" >> $REBOOTLOGFILE
}

log_print "------------------------------------------------------"
log_print "**dalvik script started at $( date +"%d-%m-%Y %H:%M:%S" )**"

if [ -f /data/magisk.apk ]; then
	pm install /data/magisk.apk
	rm /data/magisk.apk
else 
if [ -f /data/adb/magisk/magisk.apk ]; then
	mv /data/adb/magisk/magisk.apk /data/magisk.apk
	pm install -r /data/magisk.apk
	rm /data/magisk.apk
fi
fi

if [ -z "$(ls -A /data/dalvik-cache/arm64)" ]; then
   rebootlog_print "dalvik cache not built, rebooted at $( date +"%d-%m-%Y %H:%M:%S" )"
   reboot
else
   log_print "dalvik cache already built, nothing to do"
   exit 0
fi
