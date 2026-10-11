BDInfo for Linux
================

A native Linux port of [BDInfo](https://github.com/UniqProject/BDInfo), the Blu-ray
video and audio analysis tool. It collects the technical specifications of a Blu-ray
disc — disc size, playlist contents, and stream codec and bitrate details — and prints
the familiar BDInfo report, including the forums paste and quick summary.

It builds into a single, statically linked executable with no runtime dependencies: no
.NET, no Mono, no shared libraries. The one file can be copied to any x86-64 Linux
system and run.

The analysis engine and report generator are a faithful C++ translation of BDInfo's C#
code (the UHD-capable UniqProject version, 0.7.6.2), so reports match the original's
output.

Building
--------

Requires GNU make and a C++17 compiler (GCC or Clang). On Debian/Ubuntu:

```
sudo apt install build-essential
make
```

This produces `./bdinfo`. To install it:

```
sudo make install               # /usr/local/bin/bdinfo
make PREFIX=~/.local install    # ~/.local/bin/bdinfo
```

The version reported by `bdinfo --version` and in reports is read from the `VERSION`
file.

Usage
-----

```
bdinfo /media/$USER/MY_DISC                    # report every listed playlist to stdout
bdinfo -l /path/to/MOVIE/BDMV                  # list playlists only (no scan)
bdinfo -m 00800,00801 -o report.txt movie.iso  # selected playlists, to a file
bdinfo -o ~/reports /media/$USER/MY_DISC       # saves ~/reports/BDINFO.<label>.txt
bdinfo --mpls=00800.mpls DISC ~/reports        # BDInfoCLI-ng form, as used by upsies
```

Run `bdinfo --help` for all options. A disc can be a mounted disc, a copied disc folder
(or its `BDMV` folder), or a UDF disc image (`.iso`), which is read directly without
mounting. The report settings of the Windows GUI's Settings dialog are command-line
flags with the same defaults.

When the report is written to a file, progress goes to stdout and stderr only carries
warnings and errors; when the report goes to stdout, progress goes to stderr. This makes
`bdinfo` a drop-in replacement for the BDInfoCLI-ng wrapper used by tools such as
[upsies](https://codeberg.org/plotski/upsies).

BDInfo will not work with copy-protected discs; decrypt them first.

Source layout
-------------

| File | Contents (original C# source) |
| --- | --- |
| `src/bdrom.cpp` | Disc structure and scanning (`BDROM.cs`) |
| `src/playlistfile.cpp` | MPLS playlists (`TSPlaylistFile.cs`) |
| `src/streamfile.cpp` | Transport stream scanner (`TSStreamFile.cs`) |
| `src/clipfile.cpp` | CLPI clip info (`TSStreamClipFile.cs`, `TSStreamClip.cs`) |
| `src/streams.cpp` | Stream types and descriptions (`TSStream.cs`, `ToolBox.cs`) |
| `src/streambuffer.cpp` | Bitstream reader (`TSStreamBuffer.cs`) |
| `src/codecs.cpp`, `src/hevc.cpp` | Codec parsers (`TSCodec*.cs`) |
| `src/languagecodes.cpp` | ISO 639-2 language names (`LanguageCodes.cs`) |
| `src/report.cpp` | Report generator (`FormReport.cs`) |
| `src/main.cpp` | Command-line front end (replaces the GUI) |
| `src/fs.cpp` | Local files and a UDF 2.50/2.60 disc image reader |
| `src/net.cpp` | .NET runtime behaviour the reports depend on |

Class and member names follow the C# sources so the two can be compared side by side.
`src/net.h` reproduces the .NET behaviour the output depends on — `Dictionary`
enumeration order, the (unstable) `Array.Sort` algorithm, saturating float-to-integer
casts, banker's rounding, number formatting and `TimeSpan` components — so that reports
match the original byte for byte.

Differences from the Windows version
------------------------------------

- Command line only; there is no GUI and no bitrate chart.
- Reports use Unix line endings.
- When a stream file fails to scan, the report lists the error message but not a .NET
  stack trace.

Credits and license
-------------------

BDInfo was written by Cinema Squid (<http://www.cinemasquid.com/blu-ray/tools/bdinfo>)
and is maintained with UHD support by UniqProject (<https://github.com/UniqProject/BDInfo>).
This port is a derivative work and, like BDInfo, is licensed under the GNU Lesser General
Public License, version 2.1. See `LICENSE`.
