#!/system/bin/sh

/sbin/sku/busybox mount -o rw,remount /system
for i in $(ls /lib/modules 2>&1 | awk '{print $1}' | grep -i .ko); do
    if [ ! -h /system/lib/modules/$i ]; then
        if [ -e /system/lib/modules/$i ]; then
            rm -r /system/lib/modules/$i
        fi
        ln -s /lib/modules/$i /system/lib/modules/$i
    fi
done
/sbin/sku/busybox mount -o ro,remount /system