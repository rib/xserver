/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 * 
 * Copyright © 2009 Intel Corporation.
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

#ifndef __EPHYRDRI2_H__
#define __EPHYRDRI2_H__

#include <X11/extensions/dri2proto.h>
#include <xf86drm.h>

typedef struct {
    WindowPtr local;
    int remote;
} EphyrDRI2WindowPair;

Bool
ephyrDRI2GetWindowPairFromLocalWindow (WindowPtr a_local,
				       EphyrDRI2WindowPair **a_pair);
Bool
ephyrDRI2GetWindowPairFromHostWindow (int a_remote,
				      EphyrDRI2WindowPair **a_pair);

int
ephyrDRI2CreateDrawable (DrawablePtr pDraw);

xDRI2Buffer *
ephyrDRI2GetBuffers (DrawablePtr pDraw,
		     int *width,
		     int *height,
		     unsigned int *attachments,
		     int count,
		     int *out_count);

xDRI2Buffer *
ephyrDRI2GetBuffersWithFormat (DrawablePtr pDraw,
			       int *width,
			       int *height,
			       unsigned int *attachments,
			       int count,
			       int *out_count);

int
ephyrDRI2CopyRegion (DrawablePtr pDraw,
		     RegionPtr pRegion,
		     unsigned int dest,
		     unsigned int src);

int
ephyrDRI2DestroyDrawable (DrawablePtr pDraw);

Bool
ephyrDRI2Connect (ScreenPtr pScreen,
		  unsigned int driverType,
		  int *fd,
		  char **driverName,
		  char **deviceName);

Bool
ephyrDRI2Authenticate (ScreenPtr pScreen,
		       drm_magic_t magic);

void
ephyrDRI2Version (int *major, int *minor);

Bool
ephyrDRI2ExtensionSetup (ScreenPtr pScreen);

#endif /*__EPHYRDRI2_H__*/

