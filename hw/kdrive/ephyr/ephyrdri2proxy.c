/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 * 
 * Copyright © 2009 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OpenedHand Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. OpenedHand Ltd makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * OpenedHand Ltd DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OpenedHand Ltd BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *    Robert Bragg <robert@linux.intel.com>
 */


#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <GL/glx.h>
#include <X11/extensions/dri2proto.h>
#include <xf86drm.h>

#define _HAVE_XALLOC_DECLS
#include "dixstruct.h"
#include "pixmapstr.h"
#include "scrnintstr.h"
#include "windowstr.h"

#include "ephyrlog.h"
#include "hostx.h"
#include "ephyrdri2.h"
#include "ephyrdri2proxy.h"
#include "dri2.h"

int
ephyrProxyDRI2CreateDrawable (DrawablePtr pDrawable)
{
    Display *dpy = hostx_get_display ();
    WindowPtr pWindow;
    EphyrDRI2WindowPair *pair;

    pWindow = (WindowPtr)pDrawable;
    if (!ephyrDRI2GetWindowPairFromLocalWindow (pWindow, &pair)) {
        EPHYR_LOG_ERROR ("Failed to find window pair for local drawable\n");
	return BadImplementation;
    }

    XSync (dpy, False);
    hostx_errors_trap ();
    DRI2CreateDrawable (dpy, pair->remote);
    XSync (dpy, False);
    return hostx_errors_untrap ();
}

xDRI2Buffer *
ephyrProxyDRI2GetBuffers (DrawablePtr pDrawable,
			  int *width,
			  int *height,
			  unsigned int *attachments,
			  int count,
			  int *out_count)
{
    Display *dpy = hostx_get_display ();
    WindowPtr pWindow;
    EphyrDRI2WindowPair *pair;

    pWindow = (WindowPtr)pDrawable;
    if (!ephyrDRI2GetWindowPairFromLocalWindow (pWindow, &pair)) {
        EPHYR_LOG_ERROR ("Failed to find window pair for local drawable\n");
	return NULL;
    }

    assert (sizeof (xDRI2Buffer) == sizeof (DRI2Buffer));
    return (xDRI2Buffer *)DRI2GetBuffers (dpy, pair->remote, width, height,
					  attachments, count, out_count);
}

xDRI2Buffer *
ephyrProxyDRI2GetBuffersWithFormat (DrawablePtr pDrawable,
				    int *width,
				    int *height,
				    unsigned int *attachments,
				    int count,
				    int *out_count)
{
    Display *dpy = hostx_get_display ();
    WindowPtr pWindow;
    EphyrDRI2WindowPair *pair;

    pWindow = (WindowPtr)pDrawable;
    if (!ephyrDRI2GetWindowPairFromLocalWindow (pWindow, &pair)) {
        EPHYR_LOG_ERROR ("Failed to find window pair for local drawable\n");
	return NULL;
    }

    assert (sizeof (xDRI2Buffer) == sizeof (DRI2Buffer));
    return (xDRI2Buffer *)DRI2GetBuffersWithFormat (dpy, pair->remote,
						    width, height,
						    attachments, count,
						    out_count);
}

int
ephyrProxyDRI2CopyRegion (DrawablePtr pDrawable, RegionPtr pRegion,
			  unsigned int dest, unsigned int src)
{
    Display *dpy = hostx_get_display ();
    WindowPtr pWindow;
    EphyrDRI2WindowPair *pair;
    XserverRegion tmp_host_region;
    int i;
    BoxPtr rects = REGION_RECTS (pRegion);
    int n_rects = REGION_NUM_RECTS (pRegion);
    XRectangle *xrectangles;

    pWindow = (WindowPtr)pDrawable;
    if (!ephyrDRI2GetWindowPairFromLocalWindow (pWindow, &pair)) {
        EPHYR_LOG_ERROR ("Failed to find window pair for local drawable\n");
	return BadDrawable;
    }

    XSync (dpy, False);
    hostx_errors_trap();

    xrectangles = calloc (n_rects, sizeof (XRectangle)) ;
    for (i = 0; i < n_rects; i++) {
	xrectangles[i].x = rects[i].x1 ;
	xrectangles[i].y = rects[i].y1 ;
	xrectangles[i].width = rects[i].x2 - rects[i].x1;
	xrectangles[i].height = rects[i].y2 - rects[i].y1;
    }
    tmp_host_region = XFixesCreateRegion (dpy, xrectangles, n_rects);

    DRI2CopyRegion (dpy, pair->remote, tmp_host_region, dest, src);

    XFixesDestroyRegion (dpy, tmp_host_region);

    XSync (dpy, False);
    return hostx_errors_untrap ();
}

int
ephyrProxyDRI2DestroyDrawable (DrawablePtr pDrawable)
{
    Display *dpy = hostx_get_display ();
    WindowPtr pWindow;
    EphyrDRI2WindowPair *pair;
    
    pWindow = (WindowPtr)pDrawable;
    if (!ephyrDRI2GetWindowPairFromLocalWindow (pWindow, &pair)) {
        EPHYR_LOG_ERROR ("Failed to find window pair for local drawable\n");
	return BadDrawable;
    }

    XSync (dpy, False);
    hostx_errors_trap ();
    DRI2DestroyDrawable (dpy, pair->remote);
    XSync (dpy, False);
    return hostx_errors_untrap ();
}

Bool
ephyrProxyDRI2Connect (ScreenPtr pScreen,
		       unsigned int driverType,
		       int *fd,
		       char **driverName,
		       char **deviceName)
{
    Display *dpy = hostx_get_display ();
    XID win = hostx_get_window (DefaultScreen (dpy));

    /* NB: The window passed to DRI2Connect is only used to reference
     * the screen it belongs too, so we simply use the Xephyr top level
     * window here... */

    /* XXX: The fd argument is ignored in ephyrdri2ext.c:ProcDRI2Connect so
     * we ignore it here. */

    return DRI2Connect (dpy, win, driverName, deviceName);
    /* XXX: We rely on ephyrdri2ext.c:ProcDRI2Connect to free driverName
     * and deviceName */
}

Bool
ephyrProxyDRI2Authenticate (ScreenPtr pScreen, drm_magic_t magic)
{
    Display *dpy = hostx_get_display ();
    XID win = hostx_get_window (DefaultScreen (dpy));

    /* NB: The window passed to DRI2Authenticate is only used to reference
     * the screen it belongs too, so we simply use the Xephyr top level
     * window here... */
    return DRI2Authenticate (dpy, win, magic);
}

