make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- bcmrpi_knowbox_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage modules dtbs
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- INSTALL_MOD_PATH=/media/gamut/rootfs modules_install
cp arch/arm/boot/dts/*.dtb /media/gamut/boot/
cp arch/arm/boot/zImage /media/gamut/boot/kernel-knowbox.img
cp arch/arm/boot/dts/overlays/*.dtb* /media/gamut/boot/overlays
