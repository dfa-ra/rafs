if [ -z "$1" ]; then
    make
    sudo insmod rafs.ko
    make clean
    sudo mount -t rafs "TODO" /mnt/rafs
    sudo dmesg | tail -10
fi

if [ "$1" = "--" ]; then
    sudo umount rafs
    sudo rmmod rafs
    sudo dmesg | tail -10
fi



