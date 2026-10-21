# Dumping Your Xbox 360 Discs

Xbox 360 game discs (XGD2/XGD3) **cannot be read by a normal PC DVD drive** with ordinary ripping freeware. The game partition is hidden behind the drive's security sectors. A plain "rip to ISO" tool will only give you the small video partition, which says "this disc must be played in an Xbox 360." You need one of the routes below.

## Option A: Modded Xbox 360 console (easiest if you have one)
If you have an RGH/JTAG console running Aurora or FreeStyle Dash:
1. Insert the disc, then use the dashboard's **copy disc** feature to copy it to the internal or USB drive (as extracted files or GOD format).
2. Move the files to the PC over FTP or USB.
3. You get the game files directly, including `default.xex`. That's all Phase 1 needs, though a full ISO is still good for preservation.

## Option B: PC with a flashed 360-compatible DVD drive
- Needs a specific DVD drive model with custom firmware, e.g. the **Kreon** firmware for certain Lite-On/Philips drives, or an original 360 drive (Lite-On, Hitachi, BenQ, Samsung) flashed and connected over SATA.
- Then dump with **Xbox Backup Creator** and verify (and repair, if needed) the ISO with **ABGX360** against its online database.
- Note: XGD3 discs need extra handling. Check which format your discs use; 2006–2007 titles like F.E.A.R. are most likely **XGD2**, which is easier.

## Option C: Preservation-grade with Redump tooling
For a verifiable dump that matches the Redump database, use **MPF (Media Preservation Frontend)** + **Redumper** with a supported drive. Of the three options, this one produces the best-documented, hash-verifiable ISO.

## After dumping
1. Verify the hash against the Redump database entry for your disc/region.
2. Record title, region, media ID and SHA-1 in `docs/DUMPS.md`. **Don't commit the ISO.**
3. Extract files from the ISO:
   - `extract-xiso` (XDVDFS) or **Xenia** (which can browse/launch ISOs directly) or **Velocity** / **Le Fluffie**
4. Place them under `game/<title>/` in this repo (gitignored).
5. If `default.xex` is encrypted/compressed, decrypt it with **XexTool** (`xextool -e u -c u default.xex`) for the disassembler. XenonRecomp can take the original.

Only dump discs you own. Tool links change often. Get them from their official GitHub/Redump pages, and scan anything you download.
