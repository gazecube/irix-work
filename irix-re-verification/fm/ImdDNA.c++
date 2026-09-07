/*
 * IRIX 6.5 libimdModel.so ImdDNA reconstruction notes.
 *
 * Source binary:
 *   /usr/lib32/libimdModel.so equivalent
 *   ELF32 MSB MIPS N32, dynamically linked, debug_info present
 *
 * Exact dynamic symbols recovered:
 *
 *   ImdDNA::ImdDNA()                         0x09740bfc size 108
 *   ImdDNA::ImdDNA(const ImdDNA &)           0x09741210 size 132
 *   ImdDNA::ImdDNA(const ImdString &,        0x09741294 size 124
 *                  const ImdString &)
 *   ImdDNA::ImdDNA(const ImdString &)        0x09747030 size 2340
 *   ImdDNA::type() const                     0x09740b68 size 8
 *   ImdDNA::selector() const                 0x09740dec size 8
 *   ImdDNA::getAttr(const ImdString &) const 0x097416f4 size 116
 *   ImdDNA::setAttr(...)                     0x09758f3c size 276
 *   ImdDNA::serialize() const                0x0974b208 size 812
 *   ImdDNA::_unescape(ImdString &)           0x0974b078 size 400
 *   ImdDNA::_escapeLength(...)               0x0974b534 size 228
 *   ImdDNA::_addStringWithEscape(...)        0x0974b618 size 292
 *
 * Layout observations (high confidence):
 *
 *   +0x00 ImdString field0      semantic name not yet proven
 *   +0x08 ImdString type        confirmed by type()
 *   +0x10 ImdString selector    confirmed by selector()
 *   +0x18 attribute-map pointer / container
 *
 * type() is literally:
 *
 *   return (ImdString *)((char *)this + 0x08);
 *
 * selector() is literally:
 *
 *   return (ImdString *)((char *)this + 0x10);
 *
 * The two-string constructor initializes field0 to ImdString::kNull,
 * copies argument 1 to +0x08, argument 2 to +0x10, and clears +0x18.
 * Do not assign a semantic name to field0 until another call site or
 * surviving type information proves it.
 */

/*
 * SERIALIZED ImdDNA GRAMMAR
 * =========================
 *
 * ImdDNA::serialize() gives us the concrete wire/storage grammar used by
 * FLtoPopDB, bgicons, and the persistent background databases.
 *
 * The first three ImdString fields are emitted in this exact order:
 *
 *   escape(field0) ':' escape(type) ':' escape(selector) '!'
 *
 * If attributes exist, each attribute is then emitted as:
 *
 *   escape(key) '=' escape(value) ':'
 *
 * Therefore the complete format is:
 *
 *   <field0>:<type>:<selector>!<key>=<value>:<key>=<value>:...
 *
 * A trailing ':' is present after each serialized attribute, including the
 * last one. With no attributes, the string terminates immediately after '!'.
 *
 * Example shape only - NOT an observed literal IRIX record:
 *
 *   :File:/tmp/foo!fn=foo:
 *
 * The example above is deliberately schematic. Actual field values and type
 * names must come from recovered IRIX output, not guesses.
 */

/*
 * serialize() @ 0x0974b208 - recovered control flow
 * --------------------------------------------------
 *
 * 1. Compute escaped-storage length for all three fixed strings.
 * 2. If the attribute map exists, iterate it and include escaped lengths of
 *    each key and value plus two delimiter bytes per pair.
 * 3. Allocate an ImdString/output buffer of the exact required size.
 * 4. For offsets 0x00, 0x08, 0x10:
 *      _addStringWithEscape(...)
 *      append ':'
 * 5. Replace the final ':' with '!'.
 * 6. Iterate attributes:
 *      _addStringWithEscape(key)
 *      append '='
 *      _addStringWithEscape(value)
 *      append ':'
 * 7. NUL terminate.
 *
 * The literal delimiter constants are visible directly in the MIPS body:
 *
 *   ':' = 0x3a
 *   '!' = 0x21
 *   '=' = 0x3d
 */

/*
 * Parser constructor @ 0x09747030
 * --------------------------------
 *
 * The single-ImdString constructor is the inverse parser for serialize().
 * Its MIPS body searches the input in this sequence:
 *
 *   first ':'
 *   second ':'
 *   '!'
 *
 * It copies/unescapes the three fixed fields into offsets 0x00, 0x08, 0x10.
 * After '!', it parses repeated attribute records by locating '=' and ':'.
 * Those substrings are unescaped and inserted into the DNA attribute map.
 *
 * This confirms that the grammar above is not merely an output convention;
 * it is the actual bidirectional ImdDNA serialization format.
 *
 * The constructor has several early-return/error paths for malformed input.
 * Exact error policy and all legacy compatibility branches are still being
 * reconstructed and should not yet be replaced with a simplified parser in
 * the source-level decomp.
 */

/*
 * Escaping - exact table recovered
 * --------------------------------
 *
 * _unescape() loads a four-entry table through the GOT. The table lives at
 * 0x09779848 and consists of four 12-byte records:
 *
 *   source-byte, escaped-string pointer, escaped-length
 *
 * Resolved table:
 *
 *   '!'  -> "%21"
 *   ':'  -> "%3a"
 *   '='  -> "%3d"
 *   '%'  -> "%25"
 *
 * The backing strings are in small read-only data at:
 *
 *   0x097834f0 "%21"
 *   0x097834f8 "%3a"
 *   0x09783500 "%3d"
 *   0x09783508 "%25"
 *
 * _unescape() recognizes '%' and compares the following two bytes against
 * bytes 1 and 2 of each escaped string. On match it replaces the three-byte
 * sequence with the table's source byte and compacts the string in place.
 *
 * Therefore the escape grammar is now fully recovered and byte-exact for all
 * delimiter characters used by serialized ImdDNA.
 */

/*
 * Consequence for the desktop reconstruction
 * ==========================================
 *
 * We can now state the persistent background pipeline much more precisely:
 *
 *   DefaultIconList path
 *      -> ImdFileModel::dnaFromFullPath()
 *      -> ImdDNA::serialize()
 *      -> FLtoPopDB stdout
 *      -> ImdDNA(serialized-line)
 *      -> bgicons normalization/reference handling
 *      -> reduced DNA containing type/selector/optional fn
 *      -> ImdDNA::serialize()
 *      -> background icon DB
 *      -> fm
 *
 * The fixed grammar and all four escapes are now recovered. The remaining
 * compatibility question is malformed-input behavior in the original parser;
 * valid-record serialization/deserialization can be implemented byte-for-byte.
 */
