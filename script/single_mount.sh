fuser -k /mnt/l_lfs/
umount -t lustre -a -l

mount -t lustre /dev/nvme0n2 /mnt/mgt
echo "Mount MGT done"

mount -t lustre /dev/nvme0n3 /mnt/mdt0
mount -t lustre /dev/nvme0n4 /mnt/mdt1
echo "Mount MDT done"

mount -t lustre /dev/nvme0n5 /mnt/ost0
mount -t lustre /dev/nvme0n6 /mnt/ost1
echo "Mount OST done"

mount -t lustre 192.168.31.206@tcp:/l_lfs /mnt/l_lfs
echo "Mount Lustre client done"
