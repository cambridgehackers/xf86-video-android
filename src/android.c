/*
 * Authors:  Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *	     Michel Dänzer, <michel@tungstengraphics.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
/* all driver need this */
#include "xf86.h"
#include "xf86_OSproc.h"
#include "mipointer.h"
#include "micmap.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "dgaproc.h"
/* for visuals */
#include "fb.h"

#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#include "xf86RAC.h"
#endif
#include "xf86xv.h"
#include "compat-api.h"

static Bool debug = 1;

#define TRACE_ENTER(str) \
    do { if (debug) ErrorF("android: " str " %d\n",pScrn->scrnIndex); } while (0)
#define TRACE_EXIT(str) \
    do { if (debug) ErrorF("android: " str " done\n"); } while (0)
#define TRACE(str) \
    do { if (debug) ErrorF("android trace: " str "\n"); } while (0)

static const OptionInfoRec * AndroidAvailableOptions(int chipid, int busid);
static void AndroidIdentify(int flags);
static Bool AndroidProbe(DriverPtr drv, int flags);
static Bool AndroidPreInit(ScrnInfoPtr pScrn, int flags);
static Bool AndroidScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool AndroidCloseScreen(CLOSE_SCREEN_ARGS_DECL);
static Bool AndroidDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr);

/*
 * This is intentionally screen-independent.  It indicates the binding
 * choice made in the first PreInit.
 */
static int pix24bpp = 0;

#define ANDROID_VERSION		4000
#define ANDROID_NAME		"ANDROID"
#define ANDROID_DRIVER_NAME	"android"

_X_EXPORT DriverRec ANDROID = {
	ANDROID_VERSION,
	ANDROID_DRIVER_NAME,
	AndroidIdentify,
	AndroidProbe,
	AndroidAvailableOptions,
	NULL,
	0,
	AndroidDriverFunc, };

/* Supported "chipsets" */
static SymTabRec AndroidChipsets[] = {
    { 0, "android" },
    {-1, NULL }
};

/* Supported options */
typedef enum {
	OPTION_DEBUG
} AndroidOpts;

static const OptionInfoRec AndroidOptions[] = {
	{ OPTION_DEBUG,		"debug",	OPTV_BOOLEAN,	{0},	FALSE },
	{ -1,			NULL,		OPTV_NONE,	{0},	FALSE }
};

/* -------------------------------------------------------------------- */

#ifdef XFree86LOADER

MODULESETUPPROTO(AndroidSetup);

static XF86ModuleVersionInfo AndroidVersRec =
{
	"android",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
	{0,0,0,0}
};

_X_EXPORT XF86ModuleData androidModuleData = { &AndroidVersRec, AndroidSetup, NULL };

pointer
AndroidSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&ANDROID, module, HaveDriverFuncs);
		return (pointer)1;
	} else {
		if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
	}
}

#endif /* XFree86LOADER */

/* -------------------------------------------------------------------- */
/* our private data, and two functions to allocate/free this            */

typedef struct {
	unsigned char*			fbstart;
	unsigned char*			fbmem;
	int				lineLength;
	//Bool				shadowFB;
	//void				*shadow;
	CloseScreenProcPtr		CloseScreen;
	CreateScreenResourcesProcPtr	CreateScreenResources;
	EntityInfoPtr			pEnt;
	/* DGA info */
	DGAModePtr			pDGAMode;
	int				nDGAMode;
	OptionInfoPtr			Options;
} AndroidRec, *AndroidPtr;

#define ANDROIDPTR(p) ((AndroidPtr)((p)->driverPrivate))

static Bool
AndroidGetRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate != NULL)
		return TRUE;
	
	pScrn->driverPrivate = xnfcalloc(sizeof(AndroidRec), 1);
	return TRUE;
}

static void
AndroidFreeRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate == NULL)
		return;
	free(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

/* -------------------------------------------------------------------- */

static const OptionInfoRec *
AndroidAvailableOptions(int chipid, int busid)
{
	return AndroidOptions;
}

static void
AndroidIdentify(int flags)
{
	xf86PrintChipsets(ANDROID_NAME, "driver for framebuffer", AndroidChipsets);
}


static void AndroidAdjustFrame(ADJUST_FRAME_ARGS_DECL)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    SCRN_INFO_PTR(arg);
    int Base = (y * pScrn->displayWidth + x) >> 2;
    switch (pScrn->depth) {
    case  8 : break;
    case 15 : case 16 : Base *= 2; break;
    case 24 : Base *= 3; break;
    default : break;
    }
}

static Bool AndroidModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    //AndroidRestore(pScrn, FALSE);
    return(TRUE);
}

static Bool AndroidEnterVT(VT_FUNC_ARGS_DECL)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    SCRN_INFO_PTR(arg);
    
    /* Should we re-save the text mode on each VT enter? */
    if(!AndroidModeInit(pScrn, pScrn->currentMode))
      return FALSE;
    AndroidAdjustFrame(ADJUST_FRAME_ARGS(pScrn, pScrn->frameX0, pScrn->frameY0));
    return TRUE;
}

static void AndroidLeaveVT(VT_FUNC_ARGS_DECL)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    SCRN_INFO_PTR(arg);
    //AndroidRestore(pScrn, TRUE);
}

static Bool AndroidSwitchMode(SWITCH_MODE_ARGS_DECL)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    SCRN_INFO_PTR(arg);
    return AndroidModeInit(pScrn, mode);
}

static void AndroidFreeScreen(FREE_SCREEN_ARGS_DECL)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    SCRN_INFO_PTR(arg);
    AndroidFreeRec(pScrn);
}
static Bool AndroidSaveScreen(ScreenPtr pScreen, int mode)
{
    ScrnInfoPtr pScrn = NULL;
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    AndroidPtr dPtr;
    if (pScreen != NULL) {
        pScrn = xf86ScreenToScrn(pScreen);
        dPtr = ANDROIDPTR(pScrn);
        //dPtr->screenSaver = xf86IsUnblank(mode);
    }
    return TRUE;
}


static ModeStatus AndroidValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode, Bool verbose, int flags)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    return MODE_OK;
}
static void AndroidLoadPalette( ScrnInfoPtr pScrn, int numColors, int *indices, LOCO *colors, VisualPtr pVisual)
{
   int i, index, shift, Gshift;
   AndroidPtr dPtr = ANDROIDPTR(pScrn);

ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
   switch(pScrn->depth) {
   case 15:	
	shift = Gshift = 1;
	break;
   case 16:
	shift = 0; 
        Gshift = 0;
	break;
   default:
	shift = Gshift = 0;
	break;
   }
   for(i = 0; i < numColors; i++) {
       index = indices[i];
       //dPtr->colors[index].red = colors[index].red << shift;
       //dPtr->colors[index].green = colors[index].green << Gshift;
       //dPtr->colors[index].blue = colors[index].blue << shift;
   } 
}

static Bool
AndroidProbe(DriverPtr drv, int flags)
{
	int i;
	ScrnInfoPtr pScrn;
       	GDevPtr *devSections;
	int numDevSections;
	Bool foundScreen = FALSE;

	TRACE("probe start");
	/* For now, just bail out for PROBE_DETECT. */
	if (flags & PROBE_DETECT)
		return FALSE;
	if ((numDevSections = xf86MatchDevice(ANDROID_DRIVER_NAME, &devSections)) <= 0) 
	    return FALSE;
	for (i = 0; i < numDevSections; i++) {
	    int entity = xf86ClaimNoSlot(drv, 0, devSections[i], TRUE);
	    if ((pScrn = xf86AllocateScreen(drv,0 ))) {
                xf86AddEntityToScreen(pScrn,entity);
		foundScreen = TRUE;
		pScrn->driverVersion = ANDROID_VERSION;
		pScrn->driverName    = ANDROID_DRIVER_NAME;
		pScrn->name          = ANDROID_NAME;
		pScrn->Probe         = AndroidProbe;
		pScrn->PreInit       = AndroidPreInit;
		pScrn->ScreenInit    = AndroidScreenInit;
		pScrn->SwitchMode    = AndroidSwitchMode;
		pScrn->AdjustFrame   = AndroidAdjustFrame;
		pScrn->EnterVT       = AndroidEnterVT;
		pScrn->LeaveVT       = AndroidLeaveVT;
		pScrn->ValidMode     = AndroidValidMode;
	    }
	}
	free(devSections);
	TRACE("probe done");
	return foundScreen;
}

static Bool
AndroidPreInit(ScrnInfoPtr pScrn, int flags)
{
	AndroidPtr fPtr;
	const char *s;
	int type;

	if (flags & PROBE_DETECT) return FALSE;
	TRACE_ENTER("PreInit");
	/* Check the number of entities, and fail if it isn't one. */
	if (pScrn->numEntities != 1)
		return FALSE;
	pScrn->monitor = pScrn->confScreen->monitor;
	AndroidGetRec(pScrn);
	fPtr = ANDROIDPTR(pScrn);
	fPtr->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);
	if (!xf86SetDepthBpp(pScrn, 0, 0, 0, Support24bppFb | Support32bppFb))
		return FALSE;
	xf86PrintDepthBpp(pScrn);
	/* Get the depth24 pixmap format */
	if (pScrn->depth == 24 && pix24bpp == 0)
		pix24bpp = xf86GetBppFromDepth(pScrn, 24);
	/* color weight */
	if (pScrn->depth > 8) {
		rgb zeros = { 0, 0, 0 };
		if (!xf86SetWeight(pScrn, zeros, zeros))
			return FALSE;
	}
	/* visual init */
	if (!xf86SetDefaultVisual(pScrn, -1))
		return FALSE;
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	/* We don't currently support DirectColor at > 8bpp */
	if (pScrn->depth > 8 && pScrn->defaultVisual != TrueColor) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "requested default visual"
			   " (%s) is not supported at depth %d\n",
			   xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
		return FALSE;
	}

	{
		Gamma zeros = {0.0, 0.0, 0.0};

		if (!xf86SetGamma(pScrn,zeros)) {
			return FALSE;
		}
	}
	pScrn->progClock = TRUE;
	pScrn->rgbBits   = 8;
	pScrn->chipset   = "android";
	pScrn->videoRam  = 50000; //AndroidGetVidmem(pScrn);
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	/* handle options */
	xf86CollectOptions(pScrn, NULL);
	if (!(fPtr->Options = malloc(sizeof(AndroidOptions))))
		return FALSE;
	memcpy(fPtr->Options, AndroidOptions, sizeof(AndroidOptions));
	xf86ProcessOptions(pScrn->scrnIndex, fPtr->pEnt->device->options, fPtr->Options);
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	debug = xf86ReturnOptValBool(fPtr->Options, OPTION_DEBUG, FALSE);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "checking modes against framebuffer device...\n");
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "checking modes against monitor...\n");
	{
#if 0
		DisplayModePtr mode, first = mode = pScrn->modes;
		
		if (mode != NULL) do {
			mode->status = xf86CheckModeForMonitor(mode, pScrn->monitor);
			mode = mode->next;
		} while (mode != NULL && mode != first);
#else
       int apertureSize = (pScrn->videoRam * 1024);
#define MAX_WIDTH 400
#define MAX_HEIGHT 600
    ClockRangePtr clockRanges;

    clockRanges = (ClockRangePtr)xnfcalloc(sizeof(ClockRange), 1);
    clockRanges->next = NULL;
    clockRanges->ClockMulFactor = 1;
    clockRanges->minClock = 11000;   /* guessed §§§ */
    clockRanges->maxClock = 300000;
    clockRanges->clockIndex = -1;               /* programmable */
    clockRanges->interlaceAllowed = TRUE;
    clockRanges->doubleScanAllowed = TRUE;

        int i = xf86ValidateModes(pScrn, pScrn->monitor->Modes,
                              pScrn->display->modes, clockRanges,
                              NULL, 256, MAX_WIDTH,
                              (8 * pScrn->bitsPerPixel),
                              128, MAX_HEIGHT, pScrn->display->virtualX,
                              pScrn->display->virtualY, apertureSize,
                              LOOKUP_BEST_REFRESH);

       if (i == -1)
           return FALSE;
#endif
		xf86PruneDriverModes(pScrn);
	}
	pScrn->currentMode = pScrn->modes;
	/* First approximation, may be refined in ScreenInit */
	pScrn->displayWidth = pScrn->virtualX;
	xf86PrintModes(pScrn);
    xf86SetCrtcForModes(pScrn, 0);
	/* Set display resolution */
	xf86SetDpi(pScrn, 0, 0);
	if (xf86LoadSubModule(pScrn, "fb") == NULL) {
		AndroidFreeRec(pScrn);
		return FALSE;
	}
	TRACE_EXIT("PreInit");
	return TRUE;
}

static Bool
jjjAndroidCreateScreenResources(ScreenPtr pScreen)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    PixmapPtr pPixmap;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    AndroidPtr fPtr = ANDROIDPTR(pScrn);
    Bool ret;

    pScreen->CreateScreenResources = fPtr->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = jjjAndroidCreateScreenResources;
    if (!ret)
	return FALSE;
    pPixmap = pScreen->GetScreenPixmap(pScreen);
    return TRUE;
}

static Bool
AndroidScreenInit(SCREEN_INIT_ARGS_DECL)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	AndroidPtr fPtr = ANDROIDPTR(pScrn);
	VisualPtr visual;
	int init_picture = 0;
	int ret, flags;
	int type;

	TRACE_ENTER("AndroidScreenInit");

#if DEBUG
	ErrorF("\tbitsPerPixel=%d, depth=%d, defaultVisual=%s\n"
	       "\tmask: %x,%x,%x, offset: %d,%d,%d\n",
	       pScrn->bitsPerPixel,
	       pScrn->depth,
	       xf86GetVisualName(pScrn->defaultVisual),
	       pScrn->mask.red,pScrn->mask.green,pScrn->mask.blue,
	       pScrn->offset.red,pScrn->offset.green,pScrn->offset.blue);
#endif

int videoRam = 5000;
	if (NULL == (fPtr->fbmem = malloc(videoRam * 1024))) {//AndroidMapVidmem(pScrn))) {
	        xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"mapping of video memory"
			   " failed\n");
		return FALSE;
	}
	//fPtr->fboff = AndroidLinearOffset(pScrn);

	//AndroidSave(pScrn);
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);

	if (!AndroidModeInit(pScrn, pScrn->currentMode)) {
		xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"mode initialization failed\n");
		return FALSE;
	}
	AndroidSaveScreen(pScreen, SCREEN_SAVER_ON);
	AndroidAdjustFrame(ADJUST_FRAME_ARGS(pScrn, 0, 0));

	/* mi layer */
	miClearVisualTypes();
#if 0
	if (pScrn->bitsPerPixel > 8) {
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
		if (!miSetVisualTypes(pScrn->depth, TrueColorMask, pScrn->rgbBits, TrueColor)) {
			xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"visual type setup failed"
				   " for %d bits per pixel [1]\n",
				   pScrn->bitsPerPixel);
			return FALSE;
		}
	} else {
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
		if (!miSetVisualTypes(pScrn->depth,
				      miGetDefaultVisualMask(pScrn->depth),
				      pScrn->rgbBits, pScrn->defaultVisual)) {
			xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"visual type setup failed"
				   " for %d bits per pixel [2]\n",
				   pScrn->bitsPerPixel);
			return FALSE;
		}
	}
#else
   if (!miSetVisualTypes(pScrn->depth,
                      miGetDefaultVisualMask(pScrn->depth),
                      pScrn->rgbBits, pScrn->defaultVisual))
         return FALSE;
#endif
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	if (!miSetPixmapDepths()) {
	  xf86DrvMsg(pScrn->scrnIndex,X_ERROR,"pixmap depth setup failed\n");
	  return FALSE;
	}
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);

	fPtr->fbstart = fPtr->fbmem; // + fPtr->fboff;

ErrorF("[%s:%d] visuals %p num %x\n", __FUNCTION__, __LINE__, pScreen->visuals, pScreen->numVisuals);
			ret = fbScreenInit(pScreen, fPtr->fbstart, pScrn->virtualX,
					   pScrn->virtualY, pScrn->xDpi,
					   pScrn->yDpi, pScrn->displayWidth,
					   pScrn->bitsPerPixel);
			init_picture = 1;
	if (!ret)
		return FALSE;

ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	if (pScrn->bitsPerPixel > 8) {
		/* Fixup RGB ordering */
		visual = pScreen->visuals + pScreen->numVisuals;
ErrorF("[%s:%d] vis %p num %x\n", __FUNCTION__, __LINE__, pScreen->visuals, pScreen->numVisuals);
		while (--visual >= pScreen->visuals) {
ErrorF("[%s:%d] %p\n", __FUNCTION__, __LINE__, visual);
			if ((visual->class | DynamicClass) == DirectColor) {
				visual->offsetRed   = pScrn->offset.red;
				visual->offsetGreen = pScrn->offset.green;
				visual->offsetBlue  = pScrn->offset.blue;
				visual->redMask     = pScrn->mask.red;
				visual->greenMask   = pScrn->mask.green;
				visual->blueMask    = pScrn->mask.blue;
			}
		}
	}

ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	/* must be after RGB ordering fixed */
	if (init_picture && !fbPictureInit(pScreen, NULL, 0))
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "Render extension initialisation failed\n");

	  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "display rotated; disabling DGA\n");
	  xf86DrvMsg(pScrn->scrnIndex, X_INFO, "using driver rotation; disabling "
			                "XRandR\n");
	  xf86DisableRandR();
	  if (pScrn->bitsPerPixel == 24)
	    xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "rotation might be broken at 24 "
                                             "bits per pixel\n");

	xf86SetBlackWhitePixels(pScreen);
	xf86SetBackingStore(pScreen);
    xf86SetSilkenMouse(pScreen);

	/* software cursor */
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

		if (!miCreateDefColormap(pScreen)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                                   "internal error: miCreateDefColormap failed "
				   "in AndroidScreenInit()\n");
			return FALSE;
		}
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	if(!xf86HandleColormaps(pScreen, 256, 8, AndroidLoadPalette, NULL, flags))
		return FALSE;
	pScreen->SaveScreen = AndroidSaveScreen;
	/* Wrap the current CloseScreen function */
	fPtr->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = AndroidCloseScreen;
#if XV
	{
	    XF86VideoAdaptorPtr *ptr;
	    int n = xf86XVListGenericAdaptors(pScrn,&ptr);
	    if (n) {
		xf86XVScreenInit(pScreen,ptr,n);
	    }
	}
#endif
	TRACE_EXIT("AndroidScreenInit");
	return TRUE;
}

static Bool
AndroidCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	AndroidPtr fPtr = ANDROIDPTR(pScrn);
	
	//AndroidRestore(pScrn);
	//AndroidUnmapVidmem(pScrn);
	if (fPtr->pDGAMode) {
	  free(fPtr->pDGAMode);
	  fPtr->pDGAMode = NULL;
	  fPtr->nDGAMode = 0;
	}
	pScrn->vtSema = FALSE;
	pScreen->CreateScreenResources = fPtr->CreateScreenResources;
	pScreen->CloseScreen = fPtr->CloseScreen;
	return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

static Bool
AndroidDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
ErrorF("[%s:%d]\n", __FUNCTION__, __LINE__);
    xorgHWFlags *flag;
    
    switch (op) {
	case GET_REQUIRED_HW_INTERFACES:
	    flag = (CARD32*)ptr;
	    (*flag) = 0;
	    return TRUE;
	default:
	    return FALSE;
    }
}
