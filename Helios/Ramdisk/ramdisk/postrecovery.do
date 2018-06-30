
# for vold (post recovery)

# only run command csc_factory
on exec-multi-csc-data
    precondition -f mounted /data
    precondition -f file /data/.layout_version
    ls /data/
    cp -y -f -r -v --with-fmode=0644 --with-dmode=0771 --with-owner=system.system /data/csc/common /
    cp -y -f -r -v --with-fmode=0644 --with-dmode=0771 --with-owner=system.system /data/csc/<salse_code> /
    rm -v -r -f --limited-file-size=0 --type=file --except-root-dir /data/app
    rm -v -r -f /data/csc
    precondition -f mounted  /efs
    mkdir -f radio system 0771 /efs/recovery
    write -f /efs/recovery/postrecovery "exec-multi-csc-data:done\n"
    
# run condition wipe-data and csc_factory
on exec-install-preload

    echo "-- Copying media files..."
    precondition -f mounted  /data
    precondition -f file /data/.layout_version
    ls /data/
    
    mkdir media_rw media_rw 0770 /data/media
    cp -y -r -v -f --with-fmode=0664 --with-dmode=0775 --with-owner=media_rw.media_rw /system/hidden/INTERNAL_SDCARD/ /data/media/0/
    cmp -r /system/hidden/INTERNAL_SDCARD/ /data/media/0/

    echo "--  preload checkin..."
    mount -f /preload
    precondition mounted /preload

    cp -y -r -v -f --with-fmode=0664 --with-dmode=0775 --with-owner=media_rw.media_rw /preload/INTERNAL_SDCARD/ /data/media/0/
    cmp -r /preload/INTERNAL_SDCARD/ /data/media/0/
    unmount /preload
	
    echo "-- Set Factory Reset done..."
    precondition -f mounted  /efs
    # mount -f /efs
    mkdir -f radio system 0771 /efs/recovery
    write -f /efs/recovery/currentlyFactoryReset "done"
    mkdir -f radio system 0771 /efs/recovery
    write -f /efs/recovery/postrecovery "exec-install-preload:done\n"
    ls /efs/imei/
    # unmount /efs
    
on post-exec-install-preload
    #for KOR

    precondition -f mounted /data
    mkdir system system 0775 /data/app
    cp -y -f -v --with-fmode=0664 --with-owner=system.system /system/preload/*.ppk /data/app/*.apk
    mkdir -f radio system 0771 /efs/recovery
    write -f /efs/recovery/postrecovery "post-exec-install-preload:done\n"
    ls /efs/imei/
    
