/*
 * IRIX 6.5 /usr/sbin/bgicons reconstruction notes.
 *
 * Source binary:
 *   ELF32 MSB MIPS N32, stripped
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
 *   ImdDNA::ImdDNA(const ImdString &)
 *   ImdDNA::serialize() const
 *   ImdDNA::type() const
 *   ImdDNA::selector() const
 *   ImdDNA::getAttr(...)
 *   ImdDNA::setAttr(...)
 *   ImdBroker::normalizeDNA(ImdDNA &)
 *   ImdBroker::getModel(const ImdDNA &)
 *   ImdBroker::altNameFromDNA(const ImdDNA &)
 *   ImdFileModel::dnaFromFullPath(...)
 *   ImdFileModel::fileNameFromDNA(...)
 *   ImdRefModel::desktopContainerDNA()
 *   ImdRefModel::overlayContainerDNA()
 *   ImdRefModel::createReference(...)
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
 *      -> serialize the resulting ImdDNA references back to the icon DB
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
 * writeIconDB() @ 0x100050cc iterates the final DNA collection and uses
 * ImdDNA accessors plus ImdDNA::serialize() before writing each record.
 * Exact attribute edits performed before serialization are still under
 * reconstruction; do not replace them with guessed fields yet.
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
 * The next prototype data model should separate:
 *
 *   1. requested object source (DefaultIconList equivalent)
 *   2. canonical object identity (our ImdDNA-compatible representation)
 *   3. persistent background database
 *   4. FTR type/icon rendering
 *
 * This lets us replace the current synthetic Home/Dumpster scan without
 * prematurely guessing the full FTR renderer.
 */
