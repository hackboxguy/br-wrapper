#!/bin/sh

BOOT_PARTITION=/dev/mmcblk0p1

#mark update result in next rootfs 
PART_STATUS=$(cat /proc/cmdline | grep -o "root=/dev/mmcblk0p.")
if test "${PART_STATUS}" = "root=/dev/mmcblk0p2" ; then
	NEXT_ROOTFS=/dev/mmcblk0p3
else
	NEXT_ROOTFS=/dev/mmcblk0p2
fi

# Add update marker(note: since our rootfs is readonly, /update-ok file cant be deleted)
#mount ${NEXT_ROOTFS} /mnt/tmproot
#touch /mnt/tmproot/update-ok
#umount /mnt/tmproot


#flip the bootmarker so that during next reboot different rootfs becomes active
mount ${BOOT_PARTITION} /mnt/tmpboot
if test "${PART_STATUS}" = "root=/dev/mmcblk0p2" ; then
	sed -i "s|active_disk=.|active_disk=3|" /mnt/tmpboot/uEnv.txt
else
	sed -i "s|active_disk=.|active_disk=2|" /mnt/tmpboot/uEnv.txt
fi
umount /mnt/tmpboot
reboot
