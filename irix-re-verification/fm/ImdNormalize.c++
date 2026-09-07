/*
 * IRIX 6.5 libimdModel.so normalization reconstruction.
 *
 * This file covers the broker dispatch and the file-model factory override
 * used by ImdFileModel::dnaFromFullPath(..., true).
 *
 * Source binary:
 *   /usr/lib32/libimdModel.so equivalent
 *   ELF32 MSB MIPS N32, debug_info present
 *
 * Exact dynamic symbols:
 *
 *   ImdBroker::normalizeDNA(ImdDNA &)                    0x09746f48 size 232
 *   ImdFactory::normalize(const ImdDNA &)                0x09758bc8 size 56
 *   ImdFileModelFactory::normalize(const ImdDNA &)       0x097489a0 size 672
 *
 * Related calls resolved from the GOT:
 *
 *   ImdBroker::_getFactoryEntry(const ImdString &)       0x0974093c
 *   ImdFileModel::hostFromDNA(const ImdDNA &)            0x09748838
 *   ImdGlobalEnv::isLocalHostName(const ImdString &)     0x09751900
 *   ImdFileModel::fileNameFromDNA(const ImdDNA &)        0x0974146c
 *   ImdFileModel::mountedPathFromDNA(const ImdDNA &)     0x09751f68
 *   ImdGlobalEnv::realPath(const ImdString &, ImdString &) 0x09752148
 *   ImdFileModel::dnaFromFullPath(const ImdString &, bool) 0x09746420
 */

/*
 * ImdBroker::normalizeDNA() @ 0x09746f48 - RESOLVED DISPATCH
 * ----------------------------------------------------------
 *
 * The function is a type-based factory dispatch. It does not itself contain
 * filesystem normalization policy.
 *
 * Exact high-level sequence:
 *
 *   1. copy dna.type() to a temporary ImdString
 *   2. entry = _getFactoryEntry(type)
 *   3. if entry == 0, destroy the temporary and return
 *   4. call the normalize virtual on the factory object stored in the entry
 *      into a temporary ImdDNA
 *   5. assign that temporary ImdDNA back into the caller's dna
 *   6. destroy temporaries and return
 *
 * The machine code at 0x09746fcc loads entry+0x08, then dispatches through
 * that object's vtable slot at +0x2c with the associated this-adjustment at
 * +0x24. That virtual call is the factory normalize operation.
 *
 * Condensed pseudocode:
 *
 *   void ImdBroker::normalizeDNA(ImdDNA &dna)
 *   {
 *       ImdString type(dna.type());
 *       FactoryEntry *entry = _getFactoryEntry(type);
 *       if (!entry)
 *           return;
 *
 *       ImdDNA normalized = entry->factory->normalize(dna);
 *       dna = normalized;
 *   }
 *
 * FactoryEntry's constructor at 0x09743d60 initializes:
 *
 *   +0x00 = 0
 *   +0x04 = constructor argument (factory registration metadata pointer)
 *   +0x08 = 0
 *   +0x0c = 0
 *
 * The exact semantic names of all four fields are not assigned here; only the
 * +0x08 use by normalizeDNA() is important to this reconstruction.
 */

/*
 * ImdFactory::normalize() @ 0x09758bc8 - RESOLVED BASE BEHAVIOR
 * ----------------------------------------------------------------
 *
 * The base factory implementation simply copy-constructs the input DNA into
 * the hidden return object and returns it unchanged.
 *
 * The only substantive call is:
 *
 *   ImdDNA::ImdDNA(const ImdDNA &)
 *
 * Therefore model classes without a normalize override are identity
 * normalizers.
 */

/*
 * ImdFileModelFactory::normalize() @ 0x097489a0
 * ------------------------------------------------
 *
 * This override contains the filesystem-specific normalization policy used by
 * ImdFileModel::dnaFromFullPath(path, true).
 *
 * The recovered path-selection stage is exact:
 *
 *   host = ImdFileModel::hostFromDNA(dna)
 *
 *   if (ImdGlobalEnv::isLocalHostName(host))
 *       path = ImdFileModel::fileNameFromDNA(dna)
 *   else
 *       path = ImdFileModel::mountedPathFromDNA(dna)
 *
 * For a non-local host, if mountedPathFromDNA() returns ImdString::kNull,
 * normalize() immediately returns an unchanged copy of the original DNA.
 *
 * This proves why fileNameFromDNA() itself does not need to enforce local-host
 * semantics: the file factory chooses between local pathname extraction and
 * remote mounted-path translation before canonicalization.
 */

/*
 * Root and directory canonicalization
 * -----------------------------------
 *
 * The small-data literal used by this function at gp-0x7468 is proven from
 * .srdata to be exactly:
 *
 *   "/"
 *
 * Once a pathname has been selected:
 *
 *   if (path != "/") {
 *       dir = imdDirName(path)
 *
 *       if (dir != "/") {
 *           ImdString realDir(1024);
 *
 *           if (ImdGlobalEnv::realPath(dir, realDir)) {
 *               if (realDir != "/")
 *                   realDir += "/";
 *
 *               realDir += imdBaseName(path);
 *               path = realDir;
 *           }
 *       } else {
 *           if (path[path.getLength(false) - 1] == '/')
 *               path[path.getLength(false) - 1] = '\0';
 *       }
 *   }
 *
 * Notes on the exact branch structure:
 *
 *   - imdDirName(path) is copied into a second working ImdString before the
 *     root comparison.
 *   - realPath() receives the directory, not the complete pathname.
 *   - the original basename is appended after the directory is resolved.
 *   - a slash is appended between realDir and basename only when realDir is
 *     not already the root string "/".
 *   - in the dir == "/" branch, the final character of path is inspected and
 *     explicitly overwritten with NUL when it is '/'.
 *
 * The root-special-case looks odd in clean pseudocode but is retained because
 * it is present literally in the original instruction sequence.
 */

/*
 * Final step - IMPORTANT
 * ----------------------
 *
 * After path selection/canonicalization, the factory does NOT manually edit
 * the input selector. It calls the already reconstructed path-to-DNA helper:
 *
 *   return ImdFileModel::dnaFromFullPath(path, false);
 *
 * The boolean argument is explicitly zero in the call at 0x09748c10.
 *
 * This avoids recursion because dnaFromFullPath(..., false) skips the broker
 * normalization block.
 *
 * The complete recovered shape is therefore:
 *
 *   ImdDNA ImdFileModelFactory::normalize(const ImdDNA &dna)
 *   {
 *       ImdString path;
 *       ImdString host = ImdFileModel::hostFromDNA(dna);
 *
 *       if (ImdGlobalEnv::isLocalHostName(host)) {
 *           path = ImdFileModel::fileNameFromDNA(dna);
 *       } else {
 *           path = ImdFileModel::mountedPathFromDNA(dna);
 *           if (path == ImdString::kNull)
 *               return ImdDNA(dna);
 *       }
 *
 *       if (path != "/") {
 *           ImdString dir = imdDirName(path);
 *
 *           if (dir != "/") {
 *               ImdString realDir(1024);
 *               if (ImdGlobalEnv::realPath(dir, realDir)) {
 *                   if (realDir != "/")
 *                       realDir += "/";
 *                   realDir += imdBaseName(path);
 *                   path = realDir;
 *               }
 *           } else if (path.getLength(false) != 0 &&
 *                      path[path.getLength(false) - 1] == '/') {
 *               path[path.getLength(false) - 1] = '\0';
 *           }
 *       }
 *
 *       return ImdFileModel::dnaFromFullPath(path, false);
 *   }
 *
 * The getLength(false) != 0 guard above is explanatory safety in this
 * pseudocode; the original branch reaches the last-character access only
 * through pathname states produced by the preceding logic. Do not claim that
 * extra guard as original source syntax.
 */

/*
 * Consequence for Linux compatibility
 * ===================================
 *
 * The normalization loop in dnaFromFullPath(path, true) is now substantially
 * explained:
 *
 *   path
 *     -> file://host/path selector
 *     -> ImdDNA(ImdFileModel::kTypeName, selector)
 *     -> ImdBroker::normalizeDNA()
 *     -> ImdFileModelFactory::normalize()
 *          local host  -> extract path
 *          remote host -> translate to mounted path or leave DNA unchanged
 *          canonicalize containing directory with realPath()
 *          preserve basename
 *          dnaFromFullPath(canonicalPath, false)
 *     -> normalized ImdDNA
 *
 * This means a first Linux-compatible local-filesystem implementation no
 * longer needs to guess what normalizeDNA means. For local paths, its core
 * observable behavior is directory canonicalization followed by a second,
 * non-normalizing dnaFromFullPath pass.
 *
 * Remaining exact targets that materially affect compatibility:
 *
 *   1. ImdFileModel::hostFromDNA()
 *   2. ImdFileModel::mountedPathFromDNA() and remote-host mount semantics
 *   3. ImdGlobalEnv::realPath() details
 *   4. _fsBasedClasses element/vtable semantics
 *   5. exact kTypeName string values
 */
