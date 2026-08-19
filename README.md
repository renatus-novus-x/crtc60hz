# crtc60hz

[English](README.md) | [日本語](README.ja.md)

<p align="center">
  <img src="images/teaser.gif" alt="X68000 31 kHz 60 Hz CRTC measurement teaser" width="824" height="580">
</p>

<p align="center">
  <strong><a href="https://uraraworks.github.io/WebX68k/?cpu=10&ram=12&fd1=https://cdn.jsdelivr.net/gh/renatus-novus-x/crtc60hz@main/images/crtc60hz.zip&run=1">▶ Run crtc60hz.zip in WebX68k</a></strong>
</p>

An X68000/Human68k C program that generates a 525-line, approximately 60 Hz
vertical refresh in 31 kHz mode and measures the actual rate using V-DISP and
IOCS `_ONTIME`.

## What it does

The standard X68000 512 x 512 / 31 kHz mode runs at approximately 55.5 Hz.
This program first selects IOCS mode 12, then changes only the vertical CRTC
registers to create a 525-line frame:

```text
R04 = 0x020C  vertical total: 525 lines
R05 = 0x0001  VSYNC:           2 lines
R06 = 0x0022  display start:   line 34
R07 = 0x0202  display end:     line 514
```

The resulting intervals are 2 lines of VSYNC, a 33-line back porch, 480 visible
lines, and a 10-line front porch. With an approximately 31.5 kHz horizontal
frequency, the expected vertical rate is:

```text
31500 / 525 = 60.0 Hz
```

## Measurement

The program:

- Reads V-DISP from MC68901 MFP GPIP bit 4 at `$E88001`.
- Waits for one V-DISP transition per frame.
- Measures exactly 600 frame intervals with IOCS `_ONTIME`.
- Calculates the measured refresh rate without floating-point arithmetic.
- Displays a lightweight FPS counter and one-pixel flow line during measurement.

Expected results:

```text
525-line mode: 600 V-DISP in approximately 10.00 seconds = 60.00 Hz
Standard mode: 600 V-DISP in approximately 10.82 seconds = 55.5 Hz
```

## Safety and IOCS mode re-selection

Among the X680x0 CRTC registers, only R20 and R21 retain readable
last-written values. R04-R07 return zero when read, so their current values
cannot be saved by this program. Instead, the program saves only the current
IOCS CRT mode number with `_CRTMOD`. Normal completion, ESC cancellation, and V-DISP timeout all
re-select the saved IOCS CRT mode before returning to Human68k.

This restores the standard CRTC settings associated with that IOCS mode. It
cannot exactly restore custom R04-R07 timing configured by another application
before this program started.

The implementation and documentation were corrected after feedback on X
pointed out the read restrictions of R04-R07. We appreciate this important
technical correction.

Changing CRTC timing can produce an unsupported video signal. Use a display and
connection known to support the X68000 31 kHz mode, and test on real hardware at
your own risk.

## Build

### Requirements

- X68000 / Human68k target environment
- [elf2x68k](https://github.com/yunkya2/elf2x68k)
- GNU Make

### Compile and package

```sh
cd src
make
```

The Makefile builds `crtc60hz.x` and packages a bootable image and WebX68k-ready
ZIP archive. Generated files such as `.o`, `.elf`, `.x`, `.xdf`, `.hdf`, and
ordinary `.zip` files are ignored by Git.

## Repository layout

```text
.
├── README.md
├── README.ja.md
├── images/
│   ├── teaser.gif
│   ├── crtc60hz.zip
│   ├── crtc60hz_technical_guide_en.pdf
│   └── crtc60hz_technical_guide_ja.pdf
└── src/
    ├── Makefile
    └── crtc60hz.c
```

## Documentation

- [Technical guide (English PDF)](images/crtc60hz_technical_guide_en.pdf)

## Tested environments

Approximately 60 Hz operation and safe screen restoration have been confirmed
on real X68000 hardware, XEiJ, XM6 TypeG, and WebX68k. WebX68k confirmation was
performed after the CRTC-derived timing change was merged into px68k-libretro
in [pull request #1](https://github.com/uraraworks/px68k-libretro/pull/1).
