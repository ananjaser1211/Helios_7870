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
# Originally Coded by Tkkg1994 @GrifoDev, enhanced by BlackMesa @XDAdevelopers
#

LOGFILE=/data/refined/initd.log

log_print() {
  echo "$1"
  echo "$1" >> $LOGFILE
}

log_print "------------------------------------------------------"
log_print "**hades initd script started at $( date +"%d-%m-%Y %H:%M:%S" )**"
mount -o remount,rw /;
mount -o rw,remount /system

# init.d support
if [ ! -e /system/etc/init.d ]; then
	mkdir /system/etc/init.d
	chown -R root.root /system/etc/init.d
	chmod -R 755 /system/etc/init.d
fi

# Start init.d
for FILE in /system/etc/init.d/*; do
	sh $FILE >/dev/null
done;
   log_print "** initd script finished at $( date +"%d-%m-%Y %H:%M:%S" )**"
   log_print "------------------------------------------------------"

