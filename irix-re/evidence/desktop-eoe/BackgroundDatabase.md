# IRIX background icon database startup path

This note records direct evidence from the extracted `desktop_eoe` payload. It is intended to guide reconstruction of the IRIX Interactive Desktop background-object startup path without inventing behavior that is not supported by the media or binaries.

## Xsession.dt startup sequence

The extracted `var/X11/xdm/Xsession.dt` establishes these paths:

```text
defBGicons=/var/X11/xdm/DefaultIconList
defBGdevs=/var/X11/xdm/DefaultDeviceList
sysdesktopdir=.desktophost/$host
sysdesktopicondir=$sysdesktopdir/Icons
desktopdir=$HOME/.desktop-$host
```

During desktop startup, the sequence is:

```sh
# Create the overlay directory for system icons before fm/bgicons users.
/usr/sbin/bgicons -overlay

# Start fm in background/desktop mode.
/usr/sbin/fm -b

# Maintain sysadmin background icons.
/usr/sysadm/bin/sabgicons | \
    /usr/sbin/bgicons -icondb $sysdesktopdir/SysadmBgIconDB \
        -icondir $sysdesktopicondir

# Maintain required/default background icons.
/usr/sbin/FLtoPopDB < /var/X11/xdm/DefaultIconList | \
    /usr/sbin/bgicons -icondb $desktopdir/DefaultBackgroundIconDB

# Maintain device icons that are actually present.
/usr/sbin/genDeviceIcons /var/X11/xdm/DefaultDeviceList | \
    /usr/sbin/bgicons -icondir $sysdesktopicondir \
        -icondb $sysdesktopdir/DefaultBackgroundDeviceDB
```

The comments in `Xsession.dt` explicitly state that `DefaultIconList` contains full pathnames, one per line, for required apps/files whose presence on the user's background is maintained automatically. Changes to this list are intended to be reflected on the user's background.

## DefaultIconList

The stock extracted file contains:

```text
$HOME
$HOME/dumpster
/var/www/htdocs/WhatsNew/Welcome_to_SGI
/var/www/htdocs/Register/Register_To_Win
/usr/people/OutOfBox/StartSysTour
```

This is important: the stock desktop is seeded from concrete filesystem objects, not from a hardcoded list of generic icon kinds.

## DefaultDeviceList

The file documents its own format:

```text
class type state ftrType1 ftrType2 ...
```

and explicitly says that each `ftrType` is a file type assigned by the desktop to the discovered device. Those types must be defined in:

```text
/usr/lib/filetype/devices/devices.ftr
```

Examples in the stock file include:

```text
12 2 -1 indyvideo
12 3 0x10 camera
12 6 -1 cameraMic camera
10 -1 -1 microphone
```

This directly ties background device objects to the FTR type system.

## bgicons binary evidence

Extracted binary:

```text
/usr/sbin/bgicons
ELF 32-bit MSB executable, MIPS, N32 MIPS-III, dynamically linked, stripped
```

Useful exposed dynamic symbols include:

```text
BackgroundIcons
BackgroundIcons::updateBackground()
BackgroundIcons::setIconDir()
BackgroundIcons::readNewIcons()
BackgroundIcons::sortNewIcons()
BackgroundIcons::readOldIcons()
BackgroundIcons::processIcons()
BackgroundIcons::writeIconDB()
BackgroundIcons::editIcons(...)
BackgroundIcons::addIcon(...)
ImdRefModel::desktopContainerDNA()
ImdRefModel::overlayContainerDNA()
ImdFileModel::dnaFromFullPath(...)
ImdFileModel::fileNameFromDNA(...)
```

The binary also exposes these command-line strings:

```text
usage: bgicons -icondb <icondb> [ -inputfile <inputfile> ]
-icondir
-icondb
```

The symbol names strongly support a model in which `bgicons` reads a desired object population, compares it with an existing icon DB, edits the IMD object collection, and writes the resulting DB. Exact file format and function bodies remain to be reconstructed from MIPS code before being treated as known.

## Current reconstruction model

Evidence now supports this startup model:

```text
DefaultIconList
    -> FLtoPopDB
    -> bgicons
    -> DefaultBackgroundIconDB
    -> fm -b / desktopContainerDNA

DefaultDeviceList
    -> genDeviceIcons
    -> FTR device types
    -> bgicons
    -> DefaultBackgroundDeviceDB
    -> overlay/system desktop objects

sabgicons
    -> bgicons
    -> SysadmBgIconDB
    -> overlay/system desktop objects
```

The exact serialization consumed by `fm`, the `FLtoPopDB` output format, and the relationship between these databases and `desktop.otr` still require binary/data reconstruction.

## Prototype consequence

The current Linux prototype's synthetic `Home`/`Dumpster` plus directory scan should be considered temporary scaffolding. The more authentic next seam is to support an IRIX-style required-object input list and then evolve it toward the recovered background icon DB model. The FTR renderer/type mapping should remain separate from population policy: one component decides which objects belong on the background, while FTR determines their type/icon representation.
