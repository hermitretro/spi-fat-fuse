
# Fuse Driver for SPI bit-banged FatFS Interface

This driver is to enable FAT filesystems on SD cards that are using a 
bit-banged SPI interface to seamlessly appear like standard filesystems
on Linux hosts.

This driver primarily exists for Raspberry Pi Zero devices that cannot
easily support MMC interfaces/device-tree overlays to access a secondary
SD card.

The implementation uses FUSE and Elm Chan's FatFS + Generic code interfaced
via WiringPi.

