/*
 * IRIX 6.5 libimdModel.so - ImdFileModel::_fsBasedClasses reconstruction.
 *
 * Source binary:
 *   /mnt/data/libimdModel.so
 *
 * Relevant symbols:
 *
 *   _fsBasedClasses__12ImdFileModel             0x09783b08 size 4
 *   _fsBasedClassesInited__12ImdFileModel       0x09783b10 size 1
 *   _initFsBasedClasses__12ImdFileModelSGv      0x0974d914 size 112
 *   addFileSystemBasedClass__12ImdFileModelSGRC9ImdString
 *                                               0x0974d984 size 380
 *
 * This closes a previously opaque part of dnaFromFullPath(). The collection
 * does NOT contain polymorphic filesystem-model class objects. It contains
 * pointers to ImdString type names.
 */

/*
 * _initFsBasedClasses() @ 0x0974d914 - RESOLVED
 * ------------------------------------------------
 *
 * The function checks _fsBasedClassesInited and returns immediately when it
 * is already nonzero. Otherwise it sets the byte to 1 and calls
 * addFileSystemBasedClass() four times in this exact order:
 *
 *   1. ImdFileModel::kUnknownType
 *   2. ImdFileModel::kTypeName
 *   3. ImdDirModel::kTypeName
 *   4. ImdRefModel::kTypeName
 *
 * Proven from GOT references in the body:
 *
 *   gp-0x7640 -> kUnknownType__12ImdFileModel @ 0x09783ad8
 *   gp-0x763c -> kTypeName__12ImdFileModel    @ 0x09783ae0
 *   gp-0x7620 -> kTypeName__11ImdDirModel     @ 0x09783b18
 *   gp-0x760c -> kTypeName__11ImdRefModel     @ 0x09783b38
 *
 * Pseudocode:
 *
 *   void ImdFileModel::_initFsBasedClasses()
 *   {
 *       if (_fsBasedClassesInited)
 *           return;
 *
 *       _fsBasedClassesInited = 1;
 *       addFileSystemBasedClass(kUnknownType);
 *       addFileSystemBasedClass(kTypeName);
 *       addFileSystemBasedClass(ImdDirModel::kTypeName);
 *       addFileSystemBasedClass(ImdRefModel::kTypeName);
 *   }
 */

/*
 * addFileSystemBasedClass() @ 0x0974d984
 * ------------------------------------------------
 *
 * This function proves the collection element type.
 *
 * It first ensures _initFsBasedClasses() has run. It then walks the collection
 * through its iterator ABI. Each iterator result is passed directly as the
 * right-hand operand to:
 *
 *   ImdString::operator==(const ImdString &) const
 *
 * Resolved external helper:
 *
 *   __eq__9ImdStringCGRC9ImdString @ 0x09837f50 in libimdUtil.so
 *
 * Therefore each collection element is an ImdString (stored by pointer), not
 * an ImdFactory or ImdModelClass object.
 *
 * If the type name is already present, the function destroys the iterator and
 * returns without modifying the collection.
 *
 * If it is absent, the function allocates 8 bytes, copy-constructs an
 * ImdString using:
 *
 *   __ct__9ImdStringGRC9ImdString @ 0x0983827c
 *
 * and inserts that new ImdString pointer into _fsBasedClasses through the
 * collection's virtual insertion method.
 *
 * Condensed pseudocode:
 *
 *   void ImdFileModel::addFileSystemBasedClass(const ImdString &type)
 *   {
 *       _initFsBasedClasses();
 *
 *       Iterator it(*_fsBasedClasses);
 *       for (ImdString *p = it.first(); p; p = it.next()) {
 *           if (type == *p)
 *               return;
 *       }
 *
 *       ImdString *copy = new ImdString(type);
 *       _fsBasedClasses->insert(copy);
 *   }
 *
 * Iterator/collection method names above are descriptive. Their exact C++
 * class names are not yet proven from surviving symbols, but their roles are
 * fixed by the call pattern.
 */

/*
 * Consequence for dnaFromFullPath()
 * =================================
 *
 * The duplicated loop at 0x0974655c and 0x09746748 is now substantially less
 * mysterious. It is effectively:
 *
 *   for each ImdString *typeName in _fsBasedClasses:
 *       candidate = ImdDNA(*typeName, selector)
 *       model = broker->getExistingModel(candidate)
 *       if (model):
 *           result = model-><DNA-returning virtual>()
 *           broker->releaseModel(model)
 *           break
 *
 * The collection's default search order is therefore exactly:
 *
 *   kUnknownType
 *   ImdFileModel::kTypeName
 *   ImdDirModel::kTypeName
 *   ImdRefModel::kTypeName
 *
 * Additional filesystem-backed type names can be registered later through
 * addFileSystemBasedClass(). That explains why dnaFromFullPath() probes the
 * collection instead of hardcoding only File/Dir/Ref candidates: the model
 * layer is extensible.
 */

/*
 * The remaining virtual at model+vtable[0xa8/0xb0]
 * ------------------------------------------------
 *
 * After getExistingModel(candidate) succeeds, dnaFromFullPath() calls a
 * virtual method on the returned ImdModel whose hidden-return destination is
 * a stack ImdDNA object. The result is then assigned into the function's
 * output DNA and the model is released.
 *
 * This is now the only semantically unnamed part of the existing-model probe.
 * It belongs to ImdModel (not to the _fsBasedClasses collection element).
 * The next exact step is to resolve the ImdModel vtable slot at this offset
 * against the exported virtual symbols/vtable so the call can be named rather
 * than represented as <DNA-returning virtual>().
 */
