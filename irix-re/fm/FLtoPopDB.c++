/*
 * IRIX 6.5 FLtoPopDB reconstruction notes.
 *
 * Source binary:
 *   /usr/sbin/FLtoPopDB
 *   ELF32 MSB MIPS N32, stripped
 *
 * Exact symbols recovered from the dynamic symbol table:
 *   usage__Gv  0x10001824 size 84
 *   main       0x10001878 size 528
 *
 * Imported IMD calls used by main:
 *   ImdFileModel::dnaFromFullPath(const ImdString &, bool)
 *   ImdDNA::serialize() const
 *   ImdDNA::kNull
 *
 * This is intentionally written as reconstruction/pseudocode rather than
 * buildable replacement source until the exact helper call mapping and
 * ImdString/ImdDNA ABI are fully recovered.
 */

/*
 * High-confidence behavior from main() @ 0x10001878:
 *
 *   while (gets(input)) {
 *       expanded[0] = '\0';
 *
 *       // Copy input while expanding shell-style environment variables.
 *       // '$NAME' is terminated by '/' or end of string.
 *       // '${NAME}' is also accepted. The braces are not copied.
 *       // getenv(NAME) supplies the replacement. If the variable is not
 *       // present, conversion for this input line is abandoned.
 *       // A '/' is inserted between an expanded variable and the remaining
 *       // text when one is needed.
 *
 *       if (!realpath(expanded, canonical))
 *           continue;
 *
 *       ImdString path(canonical);
 *       ImdDNA dna = ImdFileModel::dnaFromFullPath(path, true);
 *
 *       if (dna != ImdDNA::kNull) {
 *           ImdString serialized = dna.serialize();
 *           puts((const char *)serialized);
 *       }
 *   }
 *
 * The exact emitted line format is therefore ImdDNA::serialize(), not a
 * bgicons-specific ad-hoc text record. This is the key interface between
 * DefaultIconList/FLtoPopDB and bgicons.
 */

/*
 * Evidence from the shipped DefaultIconList:
 *
 *   $HOME
 *   $HOME/dumpster
 *   /var/www/htdocs/WhatsNew/Welcome_to_SGI
 *   /var/www/htdocs/Register/Register_To_Win
 *   /usr/people/OutOfBox/StartSysTour
 *
 * The explicit '$' parsing in main() explains why DefaultIconList can contain
 * $HOME rather than requiring the shell to expand each input line.
 */

/*
 * Important implication for the Linux reconstruction:
 *
 *   DefaultIconList path
 *       -> environment expansion
 *       -> canonical filesystem path
 *       -> ImdFileModel::dnaFromFullPath(path, true)
 *       -> ImdDNA::serialize()
 *       -> one serialized DNA record per line on stdout
 *       -> bgicons::readNewIcons()
 *
 * So the prototype should not treat DefaultIconList as an icon database.
 * It is an input list of required filesystem objects. The persistent database
 * is produced later by bgicons after converting those paths to IMD DNA.
 */
