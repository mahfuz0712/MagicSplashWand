# Magic Splash Wand — C++ GUI (Windows)

A native Win32 GUI port of [Magic Splash Wand](https://github.com/MlgmXyysd/Magic-Splash-Wand)
(originally PHP scripts by NekoYuzu / MlgmXyysd, MeowCat Studio, Meow Mobile).
It unpacks and repacks the **splash (boot logo) image** of Qualcomm devices.

| Format | Magic | Images | Notes |
|---|---|---|---|
| Qualcomm | `SPLASH!!` @ 0x0 | raw BGR or RLE24 | max 25 images, 512-byte header |
| Qualcomm, padded (Lenovo Tab M10 HD etc.) | `SPLASH!!` @ 0x1000 | raw BGR or RLE24 | records of `[0x1000 zeros][512-byte header: magic, w, h, type, blocks][data]`; usually 2 images (boot logo, fastboot) |
| OPlus (OPPO/OnePlus/Realme) | `SPLASH LOGO!` @ 0x4000 | BMP, optional GZIP | needs `.metadata` to repack |

No PHP and no extra DLLs: the build is a single static `.exe`.

## Build (MSYS2 UCRT64)

```bash
# one time, in the "MSYS2 UCRT64" shell
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain make

# in this folder
make            # -> dist/MagicSplashWand.exe
make run        # build + start
make clean
```

Layout:

```
Makefile
src/     all source code (+ resource.rc, app.manifest, app.ico)
build/   object files (created by make)
dist/    MagicSplashWand.exe  (created by make)
```

(`mingw32-make` works too, as long as it runs inside the MSYS2 shell.)

## Using it

1. **Unpack** – pick `splash.img` (or drag & drop it). The format is detected from the
   magic bytes (classic / padded / OPlus). Images appear in the list with a preview. Qualcomm images become `0.png, 1.png, …`;
   OPlus images become `<name>.bmp` plus a `.metadata` file.
2. Edit the images (keep the same sizes!).
3. **Repack** – pick the folder (or drop it) and the output `.img`.
   * Qualcomm: images are packed in *natural* file-name order (`2.png` before `10.png`).
     Input may be PNG / JPEG / BMP / GIF / TIFF. “RLE24” can be switched off to store raw pixels.
   * OPlus: only the images listed in `.metadata` are packed, in that order. Non-BMP files
     are converted to 24-bit BMP, GZIP level 9 is used when the original was compressed.
     `.metadata` is JSON-compatible with the original PHP tool.

For the padded (Lenovo) format name the files `0.png` (boot logo) and `1.png` (fastboot screen),
keep the original resolution (e.g. 800x1280) and leave RLE24 enabled.

Always keep a backup of the original splash partition before flashing anything.

## Differences from the PHP tool

* Natural sort order for Qualcomm repack (the PHP version used directory order).
* Header limits are checked (25 / 125 images) and malformed images are rejected cleanly.
* File names inside an OPlus image are sanitised (no `..\` path tricks when unpacking).
* If several files share a name in an OPlus folder (e.g. `logo.bmp` and `logo.png`) the newest one wins.
* New: raw (uncompressed) Qualcomm repack option, format auto-detection, preview, drag & drop.

## Licence / credits

The original project ships **without a licence** (“all rights reserved” by MeowCat Studio,
Meow Mobile and NekoYuzu / MlgmXyysd). This port re-implements its file-format logic;
please respect the original authors’ rights and credits, and ask them before redistributing.
