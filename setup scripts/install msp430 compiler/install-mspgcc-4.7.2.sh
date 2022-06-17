#! /bin/sh
################################################################################
# This script builds mspgcc using gcc 4.7.2 as the basis.                      #
#                                                                              #
# This script is a modified version (by Daniele Alessandrelli) of the script   #
# created by Hossein Shafagh and hosted here:                                  #
# http://wiki.contiki-os.org/doku.php?id=msp430x                               #
#                                                                              #
# By default the compiler is installed in `~/mspgcc-4.7.2`. You can change     #
# this by modifying INSTALL_PREFIX.                                            #
#                                                                              # 
# IMPORTANT: This script creates a temporary directory `tmp` in the current    #
# path. Remember to remove it after the script execution.                      #
################################################################################

INSTALL_PREFIX="${HOME}/mspgcc-4.7.2"
echo The installation prefix:$INSTALL_PREFIX
# Switch to the tmp directory
mkdir $INSTALL_PREFIX
cd $INSTALL_PREFIX
mdkir tmp
cd tmp

# Getting
wget http://sourceforge.net/projects/mspgcc/files/mspgcc/DEVEL-4.7.x/mspgcc-20120911.tar.bz2
wget http://sourceforge.net/projects/mspgcc/files/msp430mcu/msp430mcu-20130321.tar.bz2
wget http://sourceforge.net/projects/mspgcc/files/msp430-libc/msp430-libc-20120716.tar.bz2
wget http://ftpmirror.gnu.org/binutils/binutils-2.22.tar.bz2
wget http://mirror.ibcp.fr/pub/gnu/gcc/gcc-4.7.2/gcc-4.7.2.tar.bz2
wget http://sourceforge.net/p/mspgcc/bugs/352/attachment/0001-SF-352-Bad-code-generated-pushing-a20-from-stack.patch
wget http://sourceforge.net/p/mspgcc/bugs/_discuss/thread/fd929b9e/db43/attachment/0001-SF-357-Shift-operations-may-produce-incorrect-result.patch

# Unpacking the tars 
tar xvfj binutils-2.22.tar.bz2
tar xvfj gcc-4.7.2.tar.bz2
tar xvfj mspgcc-20120911.tar.bz2
tar xvfj msp430mcu-20130321.tar.bz2
tar xvfj msp430-libc-20120716.tar.bz2 

# 1) Incorporating the changes contained in the patch delievered in mspgcc-20120911
cd binutils-2.22
patch -p1<../mspgcc-20120911/msp430-binutils-2.22-20120911.patch
cd ..

# 2) Incorporating the changes contained in the patch delievered in mspgcc-20120911
cd gcc-4.7.2
patch --force -p1<../mspgcc-20120911/msp430-gcc-4.7.0-20120911.patch
patch --force -p1<../0001-SF-352-Bad-code-generated-pushing-a20-from-stack.patch
patch --force -p1<../0001-SF-357-Shift-operations-may-produce-incorrect-result.patch
cd ..

# 3) Creating new directories
mkdir binutils-2.22-msp430
mkdir gcc-4.7.2-msp430

# 4) installing binutils in INSTALL_PREFIX
cd binutils-2.22-msp430/
../binutils-2.22/configure --target=msp430 --program-prefix="msp430-" --prefix=$INSTALL_PREFIX
make
make install

# 5) Download the prerequisites
cd ../gcc-4.7.2
./contrib/download_prerequisites

# 6) compiling gcc-4.7.0 in INSTALL_PREFIX
cd ../gcc-4.7.2-msp430
../gcc-4.7.2/configure --target=msp430 --enable-languages=c --program-prefix="msp430-" --prefix=$INSTALL_PREFIX
make
make install

# 7) compiping msp430mcu in INSTALL_PREFIX
cd ../msp430mcu-20130321
MSP430MCU_ROOT=`pwd` ./scripts/install.sh ${INSTALL_PREFIX}/

# 8) compiling the msp430 lib in INSTALL_PREFIX
cd ../msp430-libc-20120716
cd src
PATH=${INSTALL_PREFIX}/bin:$PATH
make
make PREFIX=$INSTALL_PREFIX install

# cleanup
# no need since every thing created in tmp
echo Reminder: remove tmp