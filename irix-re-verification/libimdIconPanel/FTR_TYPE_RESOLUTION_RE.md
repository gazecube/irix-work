# Model-backed FTR type resolution: direct binary evidence

This note records direct local reverse engineering of the preserved original IRIX N32 binaries. The binaries are MIPS-III, contain DWARF/debug information, and are not stripped.

## ImdModelBasedIconView::getType()

Original `libimdIconPanel.so`:

```text
0x0967d8f0  ImdModelBasedIconView::getType()  size 1568
```

The function returns an `ImdDNA`, not a string or enum. Its imported/static references include:

```text
ImdBroker::contextBroker()
ImdBroker::getModel(const ImdDNA &)
ImdBroker::releaseModel(ImdModel *)
ImdDNA::type()
ImdDNA::selector()
ImdDNA::getAttr(...)
ImdDNA::operator=(...)
imdGetAttrValue(..., ImdDNA &)
ImdTypedModel::defaultDB
ImdTypedModel::dnaSecondaryDBKey
ImdTypedModel::dnaSecondaryTypeKey
ImdRefModel::kTypeName
ImdDirModel::kTypeName
ImdFileModel::kTypeName
ImdFileModel::kUnknownType
```

The original reads model attribute ID `0xEFFF0080` and converts it to an `ImdDNA`. This is the effective FTR type DNA used by `createContents()`.

If the model is not an `ImdTypedModel`, the recovered fallback is equivalent to:

```cpp
ImdDNA("Unknown", ImdTypedModel::defaultDB)
```

The function also contains reference-model/cache handling and accesses attribute IDs `0xEFFF00C5` and `0xEFFF00C9`; their original symbolic names are still unresolved.

## ImdTypedModel::getAttr(0xEFFF0080)

Original `libimdModel.so`:

```text
0x0975665c  ImdTypedModel::getAttr(unsigned int) const
```

For attribute `0xEFFF0080`, the original lazily computes the value through:

```text
getName()
 -> ImdTypedModel::typeFromName(ftrNameSpace, name)
 -> ImdAttrTypeDNA(0xEFFF0080, ...)
 -> privileged attribute storage
 -> ImdModel::getAttr(...)
```

Direct strings recovered from the original binary are:

```text
ftrNameSpace       = "imdFTR_Type"
defaultDB          = "/usr/lib/filetype/desktop.otr"
dnaNameSpaceKey    = "imdTypeNameSpace"
dnaSecondaryDBKey  = "imdSecondaryTypeDB"
dnaSecondaryTypeKey= "imdSecondaryType"
```

## ImdTypedModel::typeFromName()

Original `libimdModel.so`:

```text
0x097568a4  ImdTypedModel::typeFromName(const ImdString &, const ImdString &)
```

The function verifies the namespace against `ftrNameSpace`, lazily loads `_fileIconDB` from `defaultDB`, and performs:

```text
imdFtrLoadDB("/usr/lib/filetype/desktop.otr")
 -> imdFtrIconTypeFromFileName(db, filename)
 -> imdFtrTypeName(type)
```

Thus normal model typing is explicitly filename/content driven by the compiled SGI FTR database. It is not an extension-to-icon lookup table.

`ImdTypedModel::typeFromTypeName()` is the parallel type-name path and calls `imdFtrIconTypeFromTypeName()`.

## libimdFtrDB entry points

Original `libimdFtrDB.so` exports:

```text
0x09dca7b4  imdFtrIconTypeFromFileName          size 56
0x09dca7ec  imdFtrIconTypeFromFileDescriptor    size 1612
0x09dcf8c0  imdFtrIconTypeFromTypeName          size 88
0x09dc5afc  imdFtrTypeName                      size 276
0x09dcd468  imdFtrLoadDB                        size 296
```

`imdFtrIconTypeFromFileName(db, filename)` is a thin wrapper around:

```text
imdFtrIconTypeFromFileDescriptor(db, 0, filename)
```

## ImdModelBasedIconView::createContents(bool)

The already recovered original path consumes `getType()` as follows:

```text
getType()
 -> typeDNA.selector() as FTR DB
 -> typeDNA.type() as FTR type name
 -> imdFtrLoadDB()
 -> imdFtrIconTypeFromTypeName()
 -> ImdFtrGalleryX
 -> setGallery(force)
 -> setGalleryFileName(ImdFileModel::fileNameFromDNA(dna))
```

Optional secondary content is constructed through the binary-proven `imdSecondaryTypeDB` / `imdSecondaryType` DNA keys and `ImdFtrIconX`.

## Shipped core FTR rules

The preserved original `/usr/lib/filetype/default/sgidefault.ftr` confirms the core type names and normal-state icon compositions used by the portable execution seam, including:

```text
EmptyFile
CharSpecial
BlockSpecial
NamedPipe
GenericExecutable
Script
Dumpster
Directory
AsciiTextFile
BinaryDataFile
Unknown
UnknownSpecial
```

Examples of shipped compositions:

```text
Directory:
  ../iconlib/generic.folder.closed.fti

Script:
  ../iconlib/generic.exec.closed.fti
  ../iconlib/generic.script.fti

AsciiTextFile:
  ../iconlib/generic.doc.fti
  iconlib/AsciiText.closed.fti

Dumpster:
  iconlib/Dumpster.pre.fti
  iconlib/Dumpster.closed.fti
```

## Portable MATCH execution

`FtrMatchEvaluator.c++` now executes source MATCH rules from the same four source classes used to build `desktop.otr`, in original class order:

```text
local
install
system
default
```

Files are visited lexically inside each source class and the first matching TYPE wins. The evaluator is source-level compatibility code; it is not presented as an ABI reimplementation of `libimdFtrDB.so`.

The implemented expression vocabulary is the one recovered from the binary interpreter and checked against shipped IRIX FTR examples:

```text
char uchar short ushort long ulong string
glob dircontains
size mode ascii tag mactype maccreator true false
! ~
|| &&
== != < <= > >=
& | ^
<< >>
+ - * / %
```

Additional behavior recovered from shipped documentation and rules:

- MATCH may span lines and terminates at an unquoted semicolon.
- `#` begins a comment only outside a quoted string.
- `SPECIALFILE` is a marker rule for non-plain files and may appear before or after MATCH within a TYPE.
- `&&` and `||` short-circuit.
- `string(offset,length)` reads exactly the requested bytes.
- integer reads use byte order as written in the file. Shipped TIFF rules, for example, compare `long(0)` against both `0x49492a00` and `0x4d4d002a`.
- N32/ELF32 `tag` follows `tag(1)`: the matcher reads `SHT_MIPS_REGINFO` (`0x70000006`), requires the second word tag-present flag, and takes the third word as the tag value. Script tags are read from the documented second-line `#Tag number` form.

`FtrResolver.c++` now tries this source evaluator first. The previous `classifyCore()` path remains only as a compatibility fallback when the source tree is unavailable or no rule can currently be evaluated. No MIME or extension mapping is introduced.

Known unproven or incomplete pieces remain explicit:

- `mactype` and `maccreator` return the original failure value `????` until AppleSingle/AppleDouble versus `.HSAncillary` backend precedence is recovered from the original binary.
- ELF64 `ODK_TAGS` tag extraction is not yet implemented; the current desktop target and preserved application specimens are N32/ELF32.
- Exact original pathname-versus-leaf dispatch for `glob()` is reconstructed from shipped rule usage and still needs binary confirmation.
- SGI `dircontains` documents shell filename expansion including braces; the current host matcher uses `fnmatch()` and therefore still needs brace-expansion parity.

A standalone smoke test covers typed reads, strings containing `#`, glob/ascii matching, source first-match behavior, SPECIALFILE parsing, and logical short-circuiting. A GitHub Actions smoke workflow is also present so the checked-in evaluator can be compiled and exercised independently of Motif.
