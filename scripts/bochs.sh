#!/bin/bash

if [ "$#" -le 1 ]; then
    echo "Usage: ./bochs.sh <image_type> <image>"
    exit 1
fi

case "$1" in
    "floppy")   DISK_CFG="floppya: 1_44=\"$2\", status=inserted"
                BOOT_CFG="boot: floppy"
    ;;
    "disk")     DISK_CFG="ata0-master: type=disk, path=\"$2\", mode=flat"     # geometry is autodetected
                BOOT_CFG="boot: disk"
    ;;
    *)          echo "Unknown image type $1."
                exit 2
esac


cat > .bochs_config << EOF
megs: 128
romimage: file=/usr/share/bochs/BIOS-bochs-legacy
vgaromimage: file=/usr/share/bochs/VGABIOS-lgpl-latest
mouse: enabled=0
clock: sync=realtime, time0=local
display_library: sdl2, options="gui_debug"

$DISK_CFG
$BOOT_CFG
EOF

bochs -q -f .bochs_config
rm -f .bochs_config