/*
 * Original IRIX 6.5.0 libSgm exported data symbols.
 *
 * These storage objects preserve symbol availability during the broad ABI
 * bring-up. Each widget class record is replaced by its reconstructed native
 * class-record type as that widget is implemented. Sizes below are the
 * original n32 object sizes and are retained as archaeology evidence, not as
 * a claim that n32 structure sizes equal the host ABI.
 */
unsigned char glwMDrawingAreaClassRec[152];
unsigned char sgColorChooserClassRec[224];
unsigned char glxDrawClassRec[120];
unsigned char glxMDrawClassRec[152];
unsigned char sgDropPocketClassRec[156];
/* sgDynaMenuClassRec is now a real native class record. */
/* sgFinderClassRec is now a real native class record. */
/* sgZbTextClassRec is now a real native class record. */
/* sgZoomBarClassRec is now a real native class record. */
unsigned char sgColumnClassRec[224];
unsigned char sgOglColorChooserClassRec[224];
unsigned char sgArcClassRec[124];
unsigned char SgdialClassRec[152];
unsigned char sgGraphClassRec[204];
unsigned char sgHorzPanedWindowClassRec[204];
/* sgGridClassRec is now a real native class record owned by Grid.c. */
unsigned char sgIconGCacheObjClassRec[140];
unsigned char sgIconGadgetClassRec[172];
unsigned char sgListClassRec[156];
unsigned char sgRubberBoardClassRec[224];
unsigned char sgScrollBarClassRec[156];
unsigned char sgSpringBoxClassRec[224];
/* sgThumbWheelClassRec is now a real native class record. */
/* sgVisualDrawingAreaClassRec is now a real native class record. */
unsigned char sgOscClassRec[152];
unsigned char SgFileSelectionBoxClassRec[236];

unsigned char _SgDropPocketPrimClassExtRec[28];
unsigned char _SgZoomBPrimClassExtRec[28];
unsigned char _SgIconGadClassExtRec[24];
/* _SgDynaMenu_pushBTranslations is now owned by DynaMenu.c. */
/* _SgZoomBar_defaultTranslations is now owned by ZoomBar.c. */

unsigned char __vtbl__19GLColorSliderGadget[48];
unsigned char __T_19GLColorSliderGadget[20];
unsigned char __vtbl__20GLColorSliderManager[48];
unsigned char __T_20GLColorSliderManager[20];
unsigned char __vtbl__23_SgOglColorSliderGadget[48];
unsigned char __T_23_SgOglColorSliderGadget[20];
unsigned char __vtbl__24_SgOglColorSliderManager[60];
unsigned char __T_24_SgOglColorSliderManager[20];
unsigned char __vtbl__7SgColor[24];
unsigned char __T_7SgColor[20];

unsigned char __TID_19GLColorSliderGadget;
unsigned char __TID_20GLColorSliderManager;
unsigned char __TID_23_SgOglColorSliderGadget;
unsigned char __TID_24_SgOglColorSliderManager;
unsigned char __TID_7SgColor;

void *__lptrfiXSetDrawingParameters;
void *__lptrfiXOffset;
void *__lptrfiXScale;
void *__lptrfiXColormapInfo;
void *__lptrfiTypeName;
void *__lptrfiIsSubtype;
void *__lptrfiFreeIconType;
void *__lptrfiIconTypeFromTypeName;
void *__lptrfiStdCommand;
void *__lptrfiGetVariableValue;
void *__lptrfiUseDefaultIconColors;
void *__lptrfiDrawIcon;
void *__lptrfiIconTypeFromFileName;
void *__lptrfiIconState;
void *__lptrfiLoadDB;
void *__lptrfiRenderX;
void *__lptrfiFreeDB;

int _sg_redraw_mode;
/* first__7SgColor is now a real cache head owned by SgColor.c. */
