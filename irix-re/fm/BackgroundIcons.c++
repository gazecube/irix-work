/*
 * IRIX 6.5 /usr/sbin/bgicons reconstruction notes.
 *
 * Source binary:
 *   ELF32 MSB MIPS N32, stripped, with dynamic symbols exposed
 *
 * Defined dynamic symbols:
 *   BackgroundIcons::BackgroundIcons(const char *, const char *) 0x10003614 size 480
 *   BackgroundIcons::setIconDir(const char *)                    0x100037f4 size 348
 *   BackgroundIcons::updateBackground()                          0x10003950 size 108
 *   BackgroundIcons::readNewIcons()                              0x100039bc size 856
 *   BackgroundIcons::sortNewIcons()                              0x10003d14 size 444
 *   BackgroundIcons::readOldIcons()                              0x10003ed0 size 460
 *   BackgroundIcons::processIcons()                              0x1000409c size 1376
 *   BackgroundIcons::editIcons(...)                              0x100045fc size 2272
 *   BackgroundIcons::addIcon(const ImdDNA &)                     0x10004edc size 496
 *   BackgroundIcons::writeIconDB()                               0x100050cc size 684
 *
 * Key imported IMD APIs visible from the binary:
 *   ImdDNA::ImdDNA()
 *   ImdDNA::ImdDNA(const ImdDNA &)
 *   ImdDNA::ImdDNA(const ImdString &, const ImdString &)
 *   ImdDNA::serialize() const
 *   ImdDNA::type() const
 *   ImdDNA::selector() const
 *   ImdDNA::getAttr(...)
 *   ImdDNA::setAttr(...)
 *   ImdBroker::contextBroker()
 *   ImdBroker::normalizeDNA(ImdDNA &)
 *   ImdBroker::getModel(const ImdDNA &)
 *   ImdBroker::releaseModel(ImdModel *)
 *   ImdBroker::altNameFromDNA(const ImdDNA &)
 *   ImdFileModel::dnaFromFullPath(...)
 *   ImdFileModel::fileNameFromDNA(...)
 *   ImdRefModel::desktopContainerDNA()
 *   ImdRefModel::overlayContainerDNA()
 *   ImdRefModel::createReference(...)
 */

/*
 * IMPORTANT CLASS-LAYOUT FINDING
 * ------------------------------
 *
 * BackgroundIcons is an ImdDNA-derived object, not merely a helper that owns
 * a container DNA.
 *
 * Constructor @ 0x10003614 begins by doing:
 *
 *   desktopContainerDNA()
 *   ImdDNA::ImdDNA(const ImdDNA &) on `this`
 *
 * In overlay mode the constructor/set-up path substitutes the overlay
 * container DNA. This explains later calls which pass BackgroundIcons *this
 * directly to APIs taking const ImdDNA & (getModel(), fileNameFromDNA(), and
 * createReference()).
 *
 * Observed fields after the ImdDNA base subobject:
 *
 *   +0x1c  ImdString icon database pathname
 *   +0x24  stream/input helper pointer (exact static type not recovered)
 *   +0x28  filebuf used for input
 *   +0x68  first ImdOrderedCollectionOf<ImdDNA> implementation pointer
 *   +0x6c  second ImdOrderedCollectionOf<ImdDNA> implementation pointer
 *
 * The constructor allocates two 12-byte ImdOrderedCollectionOfImpl objects
 * for +0x68 and +0x6c.
 */

/*
 * High-confidence pipeline:
 *
 *   serialized DNA lines from stdin/input file
 *      -> readNewIcons()
 *      -> ImdDNA(line)
 *      -> normalize/validate against IMD model infrastructure
 *      -> collection of requested background objects
 *      -> sortNewIcons()
 *
 *   existing icon database
 *      -> readOldIcons()
 *      -> collection of existing references
 *
 *   processIcons()/editIcons()
 *      -> compare requested vs existing desktop references
 *      -> create/remove/update references in desktop/overlay containers
 *
 *   writeIconDB()
 *      -> canonicalize each reference to type + selector
 *      -> preserve the optional `fn` DNA attribute
 *      -> serialize the resulting ImdDNA
 *      -> write one serialized DNA per line
 *
 * The database is therefore fundamentally a collection of serialized ImdDNA
 * references. It is not a list of icon image filenames.
 */

/*
 * readNewIcons() @ 0x100039bc imports and uses:
 *
 *   istream::getline(char *, int, char)
 *   ImdString construction
 *   ImdDNA(ImdString)
 *   ImdBroker::normalizeDNA(ImdDNA &)
 *   ImdBroker::getModel(const ImdDNA &)
 *
 * This matches FLtoPopDB's stdout contract exactly: FLtoPopDB emits one
 * ImdDNA::serialize() result per valid DefaultIconList path, and bgicons
 * reconstructs ImdDNA objects from those lines.
 */

/*
 * addIcon() @ 0x10004edc - high-confidence body shape
 * ----------------------------------------------------
 *
 * GOT/import resolution and .srdata establish the following behavior:
 *
 *   ImdBroker *broker = ImdBroker::contextBroker();
 *   ImdString alt = broker->altNameFromDNA(dna);
 *
 *   if (alt.length() == 0 || alt == "/" || alt == "//")
 *       alt = ImdGetLocalizedText("uxsgiimd:227:UnixRoot");
 *
 *   ImdDNA ref = ImdRefModel::createReference(
 *       *this,              // BackgroundIcons is the container DNA
 *       dna,
 *       alt,
 *       0,
 *       0);
 *
 * The exact callback parameter types in createReference() remain inherited
 * from its imported signature and are not rewritten here.
 *
 * After reference creation the function gets the reference model through the
 * context broker, obtains an attribute/value from the model, and aborts if
 * that required value cannot be obtained. That tail still needs type recovery
 * before being expressed as source.
 *
 * Literal .srdata used by this body:
 *   10016e18: "/"
 *   10016e20: "//"
 *
 * Localized fallback string:
 *   "uxsgiimd:227:UnixRoot"
 */

/*
 * editIcons() @ 0x100045fc - recovered comparison behavior
 * --------------------------------------------------------
 *
 * This function is substantially recovered at the call/CFG level.
 * Important exact observations:
 *
 *   - `this` is passed as an ImdDNA to contextBroker()->getModel(*this).
 *   - fileNameFromDNA(*this) is used and its containing directory is opened.
 *   - directory entries are converted with dnaFromFullPath(..., true).
 *   - models are obtained/released through ImdBroker.
 *   - requested and existing collections are traversed with collection
 *     iterators (virtual iterator ABI still not named).
 *   - ImdDNA::type() and selector() are compared first.
 *   - when type/selector match, the DNA attribute named exactly "fn" is
 *     fetched from each DNA using getAttr().
 *   - `fn` values are compared against each other and against
 *     ImdString::kNull.
 *   - stale filesystem-backed entries can be unlinked; unlink failures go
 *     through perror().
 *   - unmatched requested DNAs eventually flow to addIcon().
 *
 * The `fn` attribute name is not inferred: it is the literal string at
 * .srdata 0x10016e00:
 *
 *     "fn"
 *
 * This is the first exact persistent-reference attribute recovered from the
 * background icon database path.
 */

/*
 * writeIconDB() @ 0x100050cc - high-confidence reconstruction
 * -----------------------------------------------------------
 *
 * The function opens the pathname in the ImdString at this + 0x1c using a
 * filebuf, wraps it in an ostream, and iterates the final DNA collection at
 * this + 0x68.
 *
 * For every final reference DNA, it DOES NOT blindly serialize the original
 * object. Instead it constructs a canonical DNA from only type + selector,
 * then conditionally copies the `fn` attribute before serialization:
 *
 *   filebuf fb;
 *   fb.open((const char *)(this->iconDBPath), /* output */ 2, filebuf::openprot);
 *   ostream out(&fb);
 *
 *   for (each const ImdDNA &src in finalCollection) {
 *       ImdString type = src.type();
 *       ImdString selector = src.selector();
 *       ImdDNA clean(type, selector);
 *
 *       ImdString attrName("fn");
 *       ImdString fn = src.getAttr(attrName);
 *       if (fn != ImdString::kNull)
 *           clean.setAttr(attrName, fn);
 *
 *       out << clean.serialize() << endl;
 *   }
 *
 *   fb.close();
 *
 * Notes:
 *   - The source-level iterator spelling above is descriptive; the binary
 *     uses the old SGI collection iterator virtual ABI.
 *   - `endl__GR7ostream` is explicitly imported and called after each
 *     serialized record.
 *   - `ImdString::kNull` is resolved through the local GOT to
 *     libimdUtil.so symbol kNull__9ImdString at 0x09852dc8.
 *
 * Consequence: the on-disk background icon DB format is deliberately a
 * reduced/canonical reference form:
 *
 *       type + selector + optional fn
 *
 * rather than a dump of every transient attribute on the live ImdDNA.
 */

/*
 * Shipped command-line interface strings:
 *
 *   bgicons -icondb <icondb> [ -inputfile <inputfile> ]
 *   -icondir
 *   -overlay
 *
 * Xsession.dt uses bgicons in several roles:
 *   - initialize overlay/background state
 *   - consume FLtoPopDB output from DefaultIconList
 *   - consume genDeviceIcons output from DefaultDeviceList
 *   - consume sabgicons output for sysadmin background objects
 */

/*
 * Linux reconstruction seam:
 *
 * We now have enough evidence to make the prototype persistence model match
 * the real bgicons contract without guessing icon filenames:
 *
 *   1. requested object source (DefaultIconList equivalent)
 *   2. canonical object identity (type + selector)
 *   3. optional `fn` persistent attribute
 *   4. persistent background database (one serialized DNA per line)
 *   5. FTR type/icon rendering
 *
 * The remaining blocker before implementing a compatible parser/writer is the
 * exact textual ImdDNA::serialize() grammar. That should be recovered from
 * libimdModel next rather than invented.
 */
