/*
 * IRIX 6.5 libimdModel.so file-selector reconstruction notes.
 *
 * Source binary:
 *   /usr/lib32/libimdModel.so equivalent
 *   ELF32 MSB MIPS N32, dynamically linked, debug_info present
 *
 * This file captures the exact selector decomposition helpers around
 * ImdFileModel's proven "file://host/path" selector format.
 */

/*
 * hostFromDNA() @ 0x09748838 size 360 - RESOLVED
 * ------------------------------------------------
 *
 * Dynamic symbol:
 *
 *   hostFromDNA__12ImdFileModelSGRC6ImdDNA
 *
 * Resolved GOT calls in execution order:
 *
 *   ImdString::ImdString()
 *   ImdDNA::selector() const
 *   ImdString::getLength(false)              // _urlPrefix
 *   ImdString::getLength(false)              // selector
 *   ImdString::operator const char *()        // selector
 *   ImdString::operator const char *()        // _urlPrefix
 *   strncmp(selector, _urlPrefix, prefixLen)
 *   ImdString::find('/', prefixLen, false)
 *   ImdString::getSubString(prefixLen, slash - 1)
 *   ImdString::operator=(substring)
 *   ImdString::~ImdString()
 *
 * Fallback path:
 *
 *   ImdGlobalEnv::getLocalHostName()
 *   ImdString::operator=(localHost)
 */

/*
 * Exact recovered control flow
 * ----------------------------
 *
 * The function default-constructs its return ImdString and obtains
 * dna.selector(). It then validates the selector against
 * ImdFileModel::_urlPrefix, which has already been proven to be "file://".
 *
 * Validation sequence:
 *
 *   prefixLen   = _urlPrefix.getLength(false)
 *   selectorLen = selector.getLength(false)
 *
 *   if (prefixLen >= selectorLen)
 *       goto fallback_local_host
 *
 *   if (strncmp(selector, _urlPrefix, prefixLen) != 0)
 *       goto fallback_local_host
 *
 *   slash = selector.find('/', prefixLen, false)
 *
 *   if (slash == ImdString::NotFound)
 *       goto fallback_local_host
 *
 *   result = selector.getSubString(prefixLen, slash - 1)
 *   return result
 *
 * fallback_local_host:
 *   result = ImdGlobalEnv::getLocalHostName()
 *   return result
 */

/*
 * Pseudocode matching the MIPS body
 * ---------------------------------
 *
 *   ImdString ImdFileModel::hostFromDNA(const ImdDNA &dna)
 *   {
 *       ImdString result;
 *       const ImdString &selector = dna.selector();
 *       unsigned long prefixLen = _urlPrefix.getLength(false);
 *       unsigned long selectorLen = selector.getLength(false);
 *
 *       if (prefixLen < selectorLen &&
 *           strncmp((const char *)selector,
 *                   (const char *)_urlPrefix,
 *                   prefixLen) == 0) {
 *
 *           unsigned long slash = selector.find('/', prefixLen, false);
 *           if (slash != ImdString::NotFound) {
 *               result = selector.getSubString(prefixLen, slash - 1);
 *               return result;
 *           }
 *       }
 *
 *       result = ImdGlobalEnv::getLocalHostName();
 *       return result;
 *   }
 *
 * The local-host fallback is important. Malformed/non-file selectors do NOT
 * yield an empty host string here; they resolve to the local host name.
 */

/*
 * Proven selector codec
 * =====================
 *
 * We now have all three directions around the same selector representation:
 *
 *   dnaSelectorFromPath(path, host)
 *       -> "file://" + host + path
 *
 *   hostFromDNA(dna)
 *       -> substring between "file://" and the first following '/'
 *       -> local host name on malformed/non-file selector
 *
 *   fileNameFromDNA(dna)
 *       -> substring beginning at the first '/' after "file://<host>"
 *       -> empty ImdString on malformed/non-file selector
 *
 * Example using an illustrative host name:
 *
 *   selector = "file://indy/usr/people/me"
 *
 *   hostFromDNA()     -> "indy"
 *   fileNameFromDNA() -> "/usr/people/me"
 */

/*
 * Exact machine-code observations
 * -------------------------------
 *
 * Function range:
 *   0x09748838 - 0x0974899f
 *
 * Literal '/' comparison/search argument:
 *   0x2f
 *
 * Prefix comparison:
 *   strncmp(selector, _urlPrefix, prefixLen)
 *
 * Host substring call:
 *   selector.getSubString(prefixLen, slash - 1)
 *
 * NotFound test:
 *   compares result of find() against ImdString::NotFound
 *
 * The fallback to ImdGlobalEnv::getLocalHostName() occurs on all three
 * validation failures:
 *   - selector too short
 *   - wrong prefix
 *   - no slash after host
 */

/*
 * Consequence for normalizeDNA()
 * ==============================
 *
 * The previously recovered ImdFileModelFactory::normalize() begins with:
 *
 *   host = ImdFileModel::hostFromDNA(dna)
 *   if (ImdGlobalEnv::isLocalHostName(host)) {
 *       path = ImdFileModel::fileNameFromDNA(dna)
 *       ...
 *   }
 *
 * Because hostFromDNA() falls back to the local host on malformed selectors,
 * selector validity is not established by hostFromDNA() alone. The later
 * fileNameFromDNA() call remains significant because it independently checks
 * the "file://" grammar and can return an empty pathname.
 */

/*
 * Remaining nearby exact targets
 * ==============================
 *
 * The file:// selector codec itself is now reconstructed in both directions.
 * The highest-value remaining gaps in dnaFromFullPath() are:
 *
 *   1. ImdFileModel::_fsBasedClasses element semantics and virtual slots
 *   2. exact kTypeName values for ImdFileModel / ImdDirModel / ImdRefModel
 *   3. local/remote hostname equivalence details in
 *      ImdGlobalEnv::isLocalHostName()
 *
 * Of these, _fsBasedClasses is the largest remaining semantic gap inside
 * dnaFromFullPath() itself.
 */
