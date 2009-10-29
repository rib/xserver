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

#ifndef _EPHYR_DRI2_PROXY_H_
#define _EPHYR_DRI2_PROXY_H_

int
ephyrProxyDRI2CreateDrawable (DrawablePtr pDrawable);

xDRI2Buffer *
ephyrProxyDRI2GetBuffers (DrawablePtr pDraw,
			  int *width,
			  int *height,
			  unsigned int *attachments,
			  int count,
			  int *out_count);

xDRI2Buffer *
ephyrProxyDRI2GetBuffersWithFormat (DrawablePtr pDraw,
				    int *width,
				    int *height,
				    unsigned int *attachments,
				    int count,
				    int *out_count);

int
ephyrProxyDRI2CopyRegion (DrawablePtr pDraw,
			  RegionPtr pRegion,
			  unsigned int dest,
			  unsigned int src);

int
ephyrProxyDRI2DestroyDrawable(DrawablePtr pDrawable);

Bool
ephyrProxyDRI2Connect (ScreenPtr pScreen,
		       unsigned int driverType,
		       int *fd,
		       char **driverName,
		       char **deviceName);

Bool
ephyrProxyDRI2Authenticate (ScreenPtr pScreen, drm_magic_t magic);

#endif /* _EPHYR_DRI2_PROXY_H_ */

