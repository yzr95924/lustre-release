#!/bin/bash
umount -t lustre -a -l

mkfs.lustre --fsname=l_lfs --mgs --servicenode=192.168.31.206@tcp --reformat --mkfsoptions="" /dev/nvme0n2 &

mkfs.lustre --fsname=l_lfs --mdt --servicenode=192.168.31.206@tcp --mgsnode=192.168.31.206@tcp --index=0 --reformat --mkfsoptions="" /dev/nvme0n3 &
mkfs.lustre --fsname=l_lfs --mdt --servicenode=192.168.31.206@tcp --mgsnode=192.168.31.206@tcp --index=1 --reformat --mkfsoptions="" /dev/nvme0n4 &

mkfs.lustre --fsname=l_lfs --ost --servicenode=192.168.31.206@tcp --mgsnode=192.168.31.206@tcp --index=0 --reformat --mkfsoptions="" /dev/nvme0n5 &
mkfs.lustre --fsname=l_lfs --ost --servicenode=192.168.31.206@tcp --mgsnode=192.168.31.206@tcp --index=1 --reformat --mkfsoptions="" /dev/nvme0n6 &

mkdir -p /mnt/mgt
mkdir -p /mnt/mdt0
mkdir -p /mnt/mdt1
mkdir -p /mnt/ost0
mkdir -p /mnt/ost1
mkdir -p /mnt/l_lfs
