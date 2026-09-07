/*
 * IRIX 6.5 libimdModel.so ImdFileModel reconstruction notes.
 *
 * Source binary:
 *   /usr/lib32/libimdModel.so equivalent
 *   ELF32 MSB MIPS N32, dynamically linked, debug_info present
 *
 * Recovered dynamic symbols:
 *
 *   ImdFileModel::dnaSelectorFromPath(const ImdString &, const ImdString &)
 *       0x09741374 size 248
 *   ImdFileModel::fileNameFromDNA(const ImdDNA &)
 *       0x0974146c size 360
 *   ImdFileModel::dnaFromFullPath(const ImdString &, bool)
 *       0x09746420 size 1384
 *   ImdFileModel::dnaTypeFromPath(const ImdString &)
 *       0x09746d68 size 480
 *
 * The reconstruction below separates verified control flow from still-opaque
 * virtual calls and container ABI details. Do not replace those gaps with
 * invented semantic names until their vtable slots are independently proven.
 */

/*
 * _urlPrefix initialization - RESOLVED
 * ------------------------------------
 *
 * _urlPrefix__12ImdFileModel is an 8-byte ImdString object at 0x09783b00
 * in .bss, so its bytes are zero in the file and are populated at runtime.
 *
 * The library static-initialization code at 0x09757238-0x0975724c proves the
 * initializer:
 *
 *   0x09757238  load ImdString(const char *)
 *   0x0975723c  a1 = gp - 0x7480
 *   0x09757240  call ImdString(const char *)
 *   0x09757244  a0 = &_urlPrefix
 *
 * gp - 0x7480 = 0x097833a0, whose .srdata bytes are exactly:
 *
 *   "file://\0"
 *
 * Therefore:
 *
 *   ImdFileModel::_urlPrefix == ImdString("file://")
 *
 * This is now proven by the constructor call and backing literal, not merely
 * inferred from a nearby string.
 */

/*
 * dnaSelectorFromPath() @ 0x09741374
 * ----------------------------------
 *
 * High-confidence behavior from resolved GOT calls:
 *
 *   - start with ImdFileModel::_urlPrefix == "file://"
 *   - append a host component
 *       - if the second argument is ImdString::kNull, use
 *         ImdGlobalEnv::getLocalHostName()
 *       - otherwise append the supplied second argument
 *   - if the path argument is not kNull, append the path
 *   - construct the returned ImdString from the resulting C string
 *
 * Resolved calls:
 *   ImdString::operator const char *()
 *   strcpy()
 *   strcat()
 *   ImdString::operator==()
 *   ImdString::operator!=()
 *   ImdGlobalEnv::getLocalHostName()
 *   ImdString::ImdString(const char *)
 *
 * Pseudocode matching the observed branch structure:
 *
 *   ImdString ImdFileModel::dnaSelectorFromPath(const ImdString &path,
 *                                                const ImdString &host)
 *   {
 *       char tmp[large_stack_buffer];
 *       strcpy(tmp, "file://");
 *
 *       if (host == ImdString::kNull)
 *           strcat(tmp, ImdGlobalEnv::getLocalHostName());
 *       else
 *           strcat(tmp, (const char *)host);
 *
 *       if (path != ImdString::kNull)
 *           strcat(tmp, (const char *)path);
 *
 *       return ImdString(tmp);
 *   }
 *
 * For a local host named "indy" and path "/usr/people/me", the selector
 * shape is therefore:
 *
 *   file://indy/usr/people/me
 *
 * This example follows the proven concatenation grammar; "indy" itself is
 * illustrative rather than an observed machine name.
 */

/*
 * fileNameFromDNA() @ 0x0974146c - RESOLVED
 * -----------------------------------------
 *
 * Resolved GOT calls in exact execution order:
 *
 *   ImdString::ImdString()
 *   ImdDNA::selector()
 *   ImdString::getLength(false)
 *   ImdString::getLength(false)          // _urlPrefix
 *   ImdString::operator const char *()   // selector
 *   ImdString::operator const char *()   // _urlPrefix
 *   strncmp(selector, _urlPrefix, prefixLength)
 *   ImdString::find('/', prefixLength, false)
 *   ImdString::getLength(false)
 *   ImdString::getSubString(found, selectorLength - 1)
 *   ImdString::operator=(substring)
 *   ImdString::~ImdString()
 *
 * The function default-constructs the return string and only replaces it if
 * every validation step succeeds.
 *
 * Exact recovered algorithm:
 *
 *   ImdString ImdFileModel::fileNameFromDNA(const ImdDNA &dna)
 *   {
 *       ImdString result;
 *       const ImdString &selector = dna.selector();
 *       unsigned long prefixLength = _urlPrefix.getLength(false);
 *       unsigned long selectorLength = selector.getLength(false);
 *
 *       if (prefixLength >= selectorLength)
 *           return result;
 *
 *       if (strncmp((const char *)selector,
 *                   (const char *)_urlPrefix,
 *                   prefixLength) != 0)
 *           return result;
 *
 *       unsigned long slash = selector.find('/', prefixLength, false);
 *       if (slash == ImdString::NotFound)
 *           return result;
 *
 *       result = selector.getSubString(slash, selectorLength - 1);
 *       return result;
 *   }
 *
 * Since _urlPrefix is exactly "file://", this deliberately skips the two
 * slashes belonging to the URI prefix, searches for the next slash after the
 * host name, and returns from that slash through the final selector byte.
 *
 * Therefore the proven round trip is:
 *
 *   dnaSelectorFromPath("/usr/people/me", "indy")
 *       -> "file://indy/usr/people/me"
 *
 *   fileNameFromDNA(DNA(selector="file://indy/usr/people/me"))
 *       -> "/usr/people/me"
 *
 * fileNameFromDNA() does not validate whether the selector host is local. It
 * only validates the file:// prefix and extracts the pathname component.
 */

/*
 * dnaTypeFromPath() @ 0x09746d68
 * --------------------------------
 *
 * This helper starts with ImdFileModel::kTypeName, performs lstat(path), and
 * selects a model type from stat64.st_mode & S_IFMT.
 *
 * Proven mappings from branch constants and resolved kTypeName symbols:
 *
 *   S_IFDIR  (0x4000) -> ImdDirModel::kTypeName
 *   S_IFLNK  (0xa000) -> ImdRefModel::kTypeName
 *
 * The ordinary filesystem modes below resolve to ImdFileModel::kTypeName:
 *
 *   S_IFIFO  (0x1000)
 *   S_IFCHR  (0x2000)
 *   S_IFBLK  (0x6000)
 *   S_IFREG  (0x8000)
 *   S_IFSOCK (0xc000)
 *
 * The lstat failure / unusual-mode path includes monitor-thread and virtual
 * dispatch behavior which is not yet semantically named. Keep that path
 * separate from the proven normal-mode mapping.
 */

/*
 * dnaFromFullPath() @ 0x09746420
 * --------------------------------
 *
 * Signature recovered from the dynamic symbol:
 *
 *   ImdDNA ImdFileModel::dnaFromFullPath(const ImdString &fullPath,
 *                                         bool normalize)
 *
 * Entry validation:
 *
 *   - default-construct the output ImdDNA
 *   - reject an empty string
 *   - reject a string whose first byte is not '/'
 *
 * The absolute-path requirement is literal in the MIPS body:
 *
 *   getLength(fullPath, false)
 *   fullPath[0]
 *   compare with 0x2f ('/')
 *
 * Therefore relative paths return the default/null DNA immediately.
 */

/*
 * Core setup sequence
 * -------------------
 *
 * Once fullPath is accepted, the function performs this exact high-level
 * sequence using resolved imports:
 *
 *   selector = dnaSelectorFromPath(fullPath, ImdString::kNull)
 *   broker   = ImdBroker::contextBroker()
 *   dirName  = imdDirName(fullPath)
 *   dirSel   = dnaSelectorFromPath(dirName, ImdString::kNull)
 *   parentDNA = ImdDNA(ImdDirModel::kTypeName, dirSel)
 *   parentModel = broker->getExistingModel(parentDNA)
 *   baseName = imdBaseName(fullPath)
 *
 * The function then initializes/uses ImdFileModel::_fsBasedClasses and walks
 * that collection. Each collection entry participates through virtual calls;
 * the exact class/interface represented by those entries is still under
 * reconstruction.
 */

/*
 * Existing-model probe
 * --------------------
 *
 * For each filesystem-based class entry, the function constructs a candidate
 * ImdDNA using:
 *
 *   candidate = ImdDNA(<class-derived type>, selector)
 *   model = broker->getExistingModel(candidate)
 *
 * If a model exists, a virtual call on that model writes an ImdDNA to a stack
 * temporary. That DNA is assigned to the output, then the model is released
 * through ImdBroker::releaseModel().
 *
 * This loop exits after the collection is exhausted or a usable existing
 * model DNA has been recovered.
 *
 * The vtable slot used to obtain the model DNA is visible but its exact source
 * declaration/name is not yet proven, so it remains unnamed here.
 */

/*
 * Parent-directory child-info refinement
 * --------------------------------------
 *
 * If an output DNA has already been recovered and parentModel exists:
 *
 *   childInfo = parentModel->_getChildInfo(baseName, ...)
 *
 * If child info is present and its returned discriminator is not 1, the
 * function calls:
 *
 *   ImdDirModel::_dnaFromChildInfo(baseName, childInfo)
 *
 * and assigns the resulting DNA to the output.
 *
 * Proven dynamic helpers:
 *
 *   _getChildInfo__11ImdDirModelGRC9ImdStringPPC9ImdString
 *   _dnaFromChildInfo__11ImdDirModelCGRC9ImdStringPCv
 */

/*
 * Fallback construction when no existing DNA was found
 * ----------------------------------------------------
 *
 * If the output is still ImdDNA::kNull, the function falls back to building a
 * fresh filesystem DNA.
 *
 * First it copies fullPath into a working ImdString.
 *
 * If normalize == true:
 *
 *   tmp = ImdDNA(ImdFileModel::kTypeName, selector)
 *   broker->normalizeDNA(tmp)
 *   selector = tmp.selector()
 *
 * It then performs another _fsBasedClasses existing-model probe using the
 * normalized selector.
 *
 * After that probe it executes:
 *
 *   workingPath = ImdFileModel::fileNameFromDNA(tmp)
 *
 * We can now state exactly what this conversion does: it strips
 * "file://<host>" and returns the absolute pathname beginning with '/'.
 *
 * The final fallback is then:
 *
 *   type = ImdFileModel::dnaTypeFromPath(workingPath)
 *   output = ImdDNA(type, selector)
 *
 * If normalize == false, the normalization block is skipped and the original
 * fullPath remains the working path for dnaTypeFromPath().
 */

/*
 * Parent cache update and cleanup
 * -------------------------------
 *
 * Near function exit:
 *
 *   if (output != ImdDNA::kNull && parentModel != 0)
 *       parentModel->_updateChildInfo(baseName, output)
 *
 *   broker->releaseModel(parentModel)
 *
 * Then all temporary ImdString/ImdDNA objects are destroyed and the hidden
 * return object is left containing the recovered/fresh DNA.
 *
 * Proven helper:
 *
 *   _updateChildInfo__11ImdDirModelGRC9ImdStringRC6ImdDNA
 */

/*
 * Recovered algorithm, condensed
 * ==============================
 *
 * This is intentionally pseudocode rather than claimed buildable source.
 * It reflects proven calls and branch ordering while preserving the opaque
 * virtual operations as placeholders.
 *
 *   ImdDNA dnaFromFullPath(const ImdString &path, bool normalize)
 *   {
 *       ImdDNA result;
 *
 *       if (path.getLength(false) == 0 || path[0] != '/')
 *           return result;
 *
 *       ImdString selector = dnaSelectorFromPath(path, ImdString::kNull);
 *       ImdBroker *broker = ImdBroker::contextBroker();
 *
 *       ImdString dir = imdDirName(path);
 *       ImdString dirSelector = dnaSelectorFromPath(dir, ImdString::kNull);
 *       ImdDNA parentDNA(ImdDirModel::kTypeName, dirSelector);
 *       ImdModel *parent = broker->getExistingModel(parentDNA);
 *       ImdString base = imdBaseName(path);
 *
 *       initFsBasedClassesIfNeeded();
 *       result = probeExistingFsBasedModelDNA(broker, selector);
 *
 *       if (result != ImdDNA::kNull && parent) {
 *           const void *childInfo = 0;
 *           int kind = parent->_getChildInfo(base, &childInfo);
 *           if (childInfo && kind != 1)
 *               result = parent->_dnaFromChildInfo(base, childInfo);
 *       }
 *
 *       if (result == ImdDNA::kNull) {
 *           ImdString workingPath(path);
 *
 *           if (normalize) {
 *               ImdDNA tmp(ImdFileModel::kTypeName, selector);
 *               broker->normalizeDNA(tmp);
 *               selector = tmp.selector();
 *
 *               result = probeExistingFsBasedModelDNA(broker, selector);
 *               workingPath = ImdFileModel::fileNameFromDNA(tmp);
 *           }
 *
 *           if (result == ImdDNA::kNull) {
 *               ImdString type = dnaTypeFromPath(workingPath);
 *               result = ImdDNA(type, selector);
 *           }
 *       }
 *
 *       if (result != ImdDNA::kNull && parent)
 *           parent->_updateChildInfo(base, result);
 *
 *       broker->releaseModel(parent);
 *       return result;
 *   }
 *
 * Important: probeExistingFsBasedModelDNA() is a descriptive placeholder for
 * the duplicated iterator/vtable sequence in the machine code, not an
 * original recovered function name.
 */

/*
 * Why this matters for the desktop reconstruction
 * ===============================================
 *
 * We now have a proven path <-> selector conversion pair:
 *
 *   path -> "file://" + host + path
 *   selector -> pathname beginning at first '/' after "file://<host>"
 *
 * and the larger path -> ImdDNA side of the FLtoPopDB pipeline is mostly
 * mapped:
 *
 *   absolute path
 *      -> selector = "file://" + localHost + path
 *      -> inspect existing filesystem-backed models
 *      -> optionally normalize selector
 *      -> selector -> absolute pathname through fileNameFromDNA()
 *      -> classify filesystem object by lstat()
 *      -> ImdDNA(type, selector)
 *      -> parent directory child-info cache update
 *      -> ImdDNA::serialize()
 *
 * Remaining exact decomp targets:
 *
 *   1. resolve the _fsBasedClasses element type and its virtual slots
 *   2. recover ImdBroker::normalizeDNA() behavior relevant to file selectors
 *   3. resolve exact kTypeName string values used by file/dir/ref models
 *
 * With those proven, a Linux-side dnaFromFullPath compatibility routine can
 * be implemented against the already recovered ImdDNA codec with very little
 * semantic guesswork remaining.
 */
