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
# Originally Coded by Tkkg1994 @GrifoDev, BlackMesa @XDAdevelopers
# Reworked by Ananjaser1211 & corsicanu @XDAdevelopers with some code from 6h0st@ghost.com.ro
#
# Few things that need to be set even if unrooted
#

# Permissive
    setenforce 0

# Tamper fuse prop set to 0 on running system
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
    /sbin/fakeprop -n ro.knox.enhance.zygote.aslr "1"



