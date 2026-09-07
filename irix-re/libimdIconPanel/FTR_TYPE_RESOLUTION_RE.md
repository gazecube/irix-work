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

The current host compatibility resolver evaluates only these directly recovered core predicates and reads the actual `.ftr` ICON include lists. It intentionally does not invent MIME or extension mappings. Full execution of every compiled `.otr` MATCH operation remains a separate reconstruction task.
