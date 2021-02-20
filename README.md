
# Fuse Driver for SPI bit-banged FatFS Interface

This driver is to enable FAT filesystems on SD cards that are using a 
bit-banged SPI interface to seamlessly appear like standard filesystems
on Linux hosts.

This driver primarily exists for Raspberry Pi Zero devices that cannot
easily support MMC interfaces/device-tree overlays to access a secondary
SD card, for example, buildroot-based systems. It also uses the slower
SPI as opposed to SDIO as an ultra-minimal option that only needs 4 pins.

The implementation uses [FUSE](https://github.com/libfuse/libfuse)
and [Elm Chan's FatFS/Generic](http://elm-chan.org/fsw/ff/00index_e.html)
interfaced via Mike McCauley's 
[BCM2835](https://www.airspayce.com/mikem/bcm2835/) library and uses the 
FatFS implementation explained in 
[this post](https://www.raspberrypi.org/forums/viewtopic.php?t=34968) as a
starting point.

# Current Status

This is a very experimental repository. Currently working:

* Directory listing with attributes
* File read (cp, cat, etc....)

To be implemented:

* Anything involving write-back

# Hardware

The implementation has been tested on a Raspberry Pi Zero with a 
second MicroSD card added as a raw 3.3V interface. It's also been tested 
using a Catalex MicroSD holder which has integrated level shifting.

A basic schematic can be found in Eagle format in the `pcb` directory.

# Building and running

This project has a dependency on `libfuse3`. You should install that first
via whichever package manager (or source build) you prefer.

This project builds using cmake:

```
% mkdir build
% cmake ..
% make
```

## libfuse3 filesystem

That should result in an executable `spi-fat-fuse` in the `build` directory.

To run it:

```
% spi-fat-fuse mountpoint
```

where `mountpoint` should be an existing directory. By default, `spi-fat-fuse`
will background. You can unmount the filesystem with:

```
% fusermount3 -u mountpoint
```

You can run `spi-fat-fuse` in the foreground via:

```
% spi-fat-fuse -d mountpoint
```

which is also handy for debugging and killing directly with `Ctrl-C`.

The directory you mount onto will not be destroyed, but it will be unavailable
whilst `spi-fat-fuse` runs. It will become available once `spi-fat-fuse` is
unmounted or exits.

## stresssd

During the build process, an executable called `stresssd` is also built. 
`stresssd` tests the SPI/SD card interface without the `libfuse` filesystem
in play and is extremely handy for testing the integrity of your hardware.

`stresssd` creates a directory on your SD card `STRESSSD` and fills it with
a number of test files of a given size containing random data. The defaults
are 32 files each sized 48K.

`stresssd` ensures creation of the test data is correct then runs a number
of read iterations across the files to ensure they are readable and that
the contents can be checksummed.

# Notes

The addition of a secondary SD card to the Raspberry Pi Zero turned into
a far more annoying job that expected. I've collated some notes on what I
tried and what results I got to save other people's time!

## Approaches

* Using `mmc_spi` device-tree overlay in conjunction with the "disable DMA" hack. This didn't work. I just couldn't get the SD card to probe despite the module being loaded. The current kernel `mmc_spi.c` has now advanced past the code outlined and there appears to be no further clues. This was the source page outlining this method at [RalimTEk](https://ralimtek.com/raspberry%20pi/electronics/software/raspberry_pi_secondary_sd_card/).
* Use FatFs with WiringPi. This worked but very erratically with occasional filename corruption, skipped files when scanning the directory and so on. Migrating to BCM2835 completely cleared the issues. (See below for why this turned out
to be incorrect....)

## GPIO Timing

The note above WiringPi ended up being incorrect. Once I migrated the code onto
a buildroot image, the erratic issue manifested once again. After analysis
with an oscilloscope and a few test programs, it turns out that the `sdmm.c`
file's handling of pulsing the various lines was fundamentally too fast for
the pins and the transient pulses were being "muffled" (for want of a better
word).

To solve the issue, a GPIO read has been added after each GPIO pin change
adding around 50ns of delay which ensures the pulses are all well-formed.
This has the side-effect of slightly slowing down the SD interface but
as this is essentially 1-bit bit-banged SPI, performance probably isn't the
primary concern...

# Licensing

* `bcm2835.c` and `bcm2835.h` are covered by GNU GPL V3
* FatFS and Generic code are covered by their own permissive licence. See `sdmm.c`, `ff.c`, `ff.h`, `diskio.h` and `ffconf.h` for specifics
* Everything else within this package is covered by the
  [GNU General Public License V3](LICENSE.txt).
