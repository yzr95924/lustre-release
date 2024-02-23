#!/bin/bash

fuser -k /mnt/l_lfs/
umount -l /mnt/l_lfs
echo "Umount Lustre client done"

umount -l /mnt/mdt0
umount -l /mnt/mdt1
echo "Umount MDT done"

umount -l /mnt/mgt
echo "Umount MGT done"

umount -l /mnt/ost0
umount -l /mnt/ost1
echo "Umount OST done"

lustre_rmmod
echo "Unload Lustre"
