make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- INSTALL_MOD_PATH=/home/asa/work/OpenGL/devkit8000/diskless/Root-5.16/ omap2plus_defconfig
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- INSTALL_MOD_PATH=/home/asa/work/OpenGL/devkit8000/diskless/Root-5.16/ uImage LOADADDR=0x81000000
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- INSTALL_MOD_PATH=/home/asa/work/OpenGL/devkit8000/diskless/Root-5.16/ modules
sudo make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- INSTALL_MOD_PATH=/home/asa/work/OpenGL/devkit8000/diskless/Root-5.16/ modules_install
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- INSTALL_MOD_PATH=/home/asa/work/OpenGL/devkit8000/diskless/Root-5.16/ dtbs
