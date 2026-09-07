/*
 * IconPipeline.c++ - recovered IID file type -> icon rendering seam
 *
 * Evidence binaries:
 *   libimdModel.so
 *   libimdFtrDB.so
 *   libimdFtrIcon.so
 *   libimdIconPanel.so
 *
 * These DSOs are IRIX 6.5 N32/MIPS-III, contain debug_info, and are not
 * stripped. This file records the verified cross-library call seam before
 * deeper body reconstruction. It intentionally does not invent FTR rule
 * syntax or class layouts that have not yet been recovered.
 */

/* Exact defined symbols/ranges observed in the specimens. */

/* libimdModel.so */
// 0x097591b4, size 2052
// imdCreateIconData__GRC6ImdDNAPCc

/* libimdFtrDB.so */
// 0x09dcf8c0, size 88
// imdFtrIconTypeFromTypeName
// 0x09dca7b4, size 56
// imdFtrIconTypeFromFileName
// 0x09dd750c, size 84
// imdFtrFileName
// 0x09dc5afc, size 276
// imdFtrTypeName

/* libimdFtrIcon.so */
// 0x0980f368, size 576
// imdFtrIconCreateIconFromFile
// 0x0980f5a8, size 576
// imdFtrIconCreateIconFromType
// 0x09810c5c, size 268
// drawIcon__11ImdFtrIconXGP10_WidgetRecP4_XGCQ2_10ImdFtrIcon13EColormapType
// 0x098085c4, size 252
// drawIcon__11ImdFtrIconXGP9_XDisplayUlT2P4_XGCT2Q2_10ImdFtrIcon13EColormapTypei
// 0x098086c0, size 212
// drawIcon__11ImdFtrIconXGP16ImdFtrIconParamsQ2_10ImdFtrIcon10ECacheType

/* libimdIconPanel.so */
// 0x0967d8f0, size 1568
// getType__21ImdModelBasedIconViewGv
// 0x0967f478, size 2456
// createContents__21ImdModelBasedIconViewGb
// 0x09679b54, size 436
// drawIcons__15ImdIconRendererSGR11ImdRendererR11ImdIconViewP4_XGCblT5f
// 0x096aefc4, size 628
// shape__15ImdIconRendererGR7ImdViewlT2P9_XDisplayUlP4_XGC

/*
 * Verified architecture from symbol/import relationships:
 *
 *   ImdModel / ImdDNA
 *       |
 *       +--> imdCreateIconData(ImdDNA, ...)
 *       |
 *       v
 *   ImdModelBasedIconView::getType()
 *       |
 *       +--> libimdFtrDB typing API
 *            imdFtrIconTypeFromFileName()
 *            imdFtrIconTypeFromTypeName()
 *            imdFtrTypeName()
 *            imdFtrFileName()
 *       |
 *       v
 *   ImdModelBasedIconView::createContents()
 *       |
 *       v
 *   ImdFtrIcon / ImdFtrIconX
 *       |
 *       +--> imdFtrIconCreateIconFromFile()
 *       +--> imdFtrIconCreateIconFromType()
 *       +--> ImdFtrInterpreter / ImdFtrXRenderer
 *       |
 *       v
 *   ImdIconRenderer::drawIcons()
 *       |
 *       +--> ImdFtrIconX::drawIcon()
 *       |
 *       v
 *   X drawable
 *
 *   ImdIconRenderer::shape() supplies the X Shape geometry used by fm's
 *   BackLayout::shapeView(). Therefore rendered icon geometry and desktop
 *   ShapeBounding geometry are two sides of the same renderer object.
 */

/*
 * Additional binary evidence:
 *
 * libimdFtrDB source units:
 *   ftrType.c++
 *   ftrTyper.c++
 *   ftrInterpreter.c++
 *
 * libimdFtrIcon source units:
 *   ftrIcon.c++
 *   ftrIconC.c++
 *   ftrIconX.c++
 *   ftrXRenderer.c++
 *   ftrGalleryX.c++
 *   ftrLibs.c++
 *
 * libimdIconPanel source units:
 *   ModelBasedIconView.c++
 *   ModelBasedIconPanel.c++
 *   IconRenderer.c++
 *   IconView.c++
 *   FileIconProtocol.c++
 *   FileTypeTarget.c++
 *
 * libimdFtrDB embeds the resource path "/filetype/desktop.otr" and the
 * strings "IconDB", "IconInstance", "Icon, BW", and "Icon, Color".
 * This confirms a feature/type database plus interpreted icon-rule layer;
 * arbitrary filename extensions are not themselves the desktop icon policy.
 */

/*
 * Native icon asset transport evidence:
 *
 * The extracted IRIX files Fm.icon and Fmprefs.icon identify as SGI RGB image
 * files. Fm.icon is RLE, 85x68, 3 channels. This is a concrete asset format we
 * can support in the Linux prototype immediately without pretending it is the
 * complete FTR type-selection mechanism.
 *
 * Prototype policy:
 *   1. Implement SGI RGB .icon/.rgb/.sgi decoding/rendering now.
 *   2. Keep type -> icon-name selection behind an explicit compatibility seam.
 *   3. Replace that seam with reconstructed FTR rule lookup once getType(),
 *      imdCreateIconData(), and the FTR interpreter bodies are recovered.
 */
