
# Fuse Driver for SPI bit-banged FatFS Interface

This driver is to enable FAT filesystems on SD cards that are using a 
bit-banged SPI interface to seamlessly appear like standard filesystems
on Linux hosts.

This driver primarily exists for Raspberry Pi Zero devices that cannot
easily support MMC interfaces/device-tree overlays to access a secondary
SD card. It also uses the slower SPI as opposed to SDIO as an ultra-minimal
option that only needs 4 pins.

The implementation uses [FUSE](<https://github.com/libfuse/libfuse>)
and [Elm Chan's FatFS/Generic](<http://elm-chan.org/fsw/ff/00index_e.html>)
interfaced via Mike McCauley's [BCM2835](<https://www.airspayce.com/mikem/bcm2835/>) library and uses the FatFS implementation explained in [this post](https://www.raspberrypi.org/forums/viewtopic.php?t=34968) as a starting point.

# Current Status

This is a very experimental repository. Currently working:

* Directory listing with attributes
* File read (cp, cat, etc....)

To be implemented:

* File write

# Hardware

The implementation has been tested on a Raspberry Pi Zero with a 
second MicroSD card added as a raw 3.3V interface. It's also been tested 
using a Catalex MicroSD holder which has integrated level shifting.

A basic schematic can be found in Eagle format in the 'pcb' directory.

# Notes

The addition of a secondary SD card to the Raspberry Pi Zero turned into
a far more annoying job that expected. I've collated some notes on what I
tried and what results I got to save other people's time!

Things I tried:

* Using mmc_spi device-tree overlay in conjunction with the "disable DMA" hack. This didn't work. I just couldn't get the SD card to probe despite the module being loaded. The current kernel mmc_spi.c has now advanced past the code outlined and there appears to be no further clues. This was the source page outlining this method at [RalimTEk](<https://ralimtek.com/raspberry%20pi/electronics/software/raspberry_pi_secondary_sd_card/>).
* Use FatFs with WiringPi. This worked but very erratically with occasional filename corruption, skipped files when scanning the directory and so on. Migrating to BCM2835 completely cleared the issues.

# License

This package is covered by the [GNU Lesser General Public License](LICENSE.txt).
