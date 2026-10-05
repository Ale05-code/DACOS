#config = 'release'
#arch = 'i686'
imageType = 'disk'
imageFS = 'fat32'
imageSize = '250m'
toolchain = '../.toolchains'

# Metodi per copiare i file nell'immagine "disk":
# mtools  - usa mcopy/mmd direttamente sull'immagine: niente sudo, funziona ovunque (default)
# guestfs - usa libguestfs (guestmount), niente sudo ma su Ubuntu richiede /boot/vmlinuz leggibile
# mount   - usa losetup + mount, richiede sudo (la password viene chiesta durante la build)
mountMethod = 'mtools'
