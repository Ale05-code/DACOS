#!/bin/bash
# Copies the files you created inside DACOS from the disk image into image/root/,
# so they are put back in the image the next time it is rebuilt (scons rebuilds
# the image from scratch every time the code changes).
#
#   scons savefiles      (or: ./scripts/save_files.sh disk build/i686_debug/image.img)

if [ "$#" -le 1 ]; then
    echo "Usage: ./save_files.sh <image_type> <image>"
    exit 1
fi

IMAGE="$2"
case "$1" in
    "floppy")   TARGET="$IMAGE" ;;
    "disk")     TARGET="$IMAGE@@1048576" ;;      # the partition starts at sector 2048
    *)          echo "Unknown image type $1."; exit 2 ;;
esac

if [ ! -f "$IMAGE" ]; then
    echo "$IMAGE not found: run scons first"
    exit 1
fi

export MTOOLS_SKIP_CHECK=1
mkdir -p image/root

# everything in the root, except /boot (the kernel is built from the sources)
mdir -b -i "$TARGET" ::/ 2>/dev/null | while IFS= read -r item; do
    name="${item#::/}"
    name="${name%/}"
    [ -z "$name" ] && continue
    [ "${name,,}" = "boot" ] && continue
    mcopy -s -o -n -m -i "$TARGET" "::/$name" image/root/
done

echo "Files saved in image/root/:"
ls -R image/root
