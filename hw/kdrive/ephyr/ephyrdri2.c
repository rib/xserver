/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 * 
 * Copyright © 2007 OpenedHand Ltd 
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
 *    Dodji Seketeli <dodji@openedhand.com>
 *    Robert Bragg <robert@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <string.h>

#include "misc.h"
#include "privates.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "colormapst.h"
#include "cursorstr.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "pixmapstr.h"
#include "servermd.h"
#include "swaprep.h"

#include "ephyrdri2.h"
#include "ephyrdri2ext.h"
#include "ephyrdri2proxy.h"
#include "hostx.h"
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"

#ifndef TRUE
#define TRUE 1
#endif /*TRUE*/

#ifndef FALSE
#define FALSE 0
#endif /*FALSE*/


#include <X11/extensions/dri2proto.h>
#include <xf86drm.h>


typedef struct {
    int ref;
} EphyrDRI2WindowPrivRec;
typedef EphyrDRI2WindowPrivRec *EphyrDRI2WindowPrivPtr;

typedef struct {
    MoveWindowProcPtr MoveWindow;
    PositionWindowProcPtr PositionWindow;
    ClipNotifyProcPtr ClipNotify;
} EphyrDRI2ScreenPrivRec;
typedef EphyrDRI2ScreenPrivRec *EphyrDRIScreenPrivPtr;

static void ephyrDRI2MoveWindow (WindowPtr a_win,
                                 int a_x, int a_y,
                                 WindowPtr a_siblings,
                                 VTKind a_kind);
static Bool ephyrDRI2PositionWindow (WindowPtr a_win,
                                     int x, int y);
static void ephyrDRI2ClipNotify (WindowPtr a_win,
                                 int a_x, int a_y);

static Bool mirrorHostVisuals (ScreenPtr a_screen);
static Bool destroyHostPeerWindow (const WindowPtr a_win);

static int ephyrDRI2WindowKeyIndex;
static DevPrivateKey ephyrDRI2WindowKey = &ephyrDRI2WindowKeyIndex;
static int ephyrDRI2ScreenKeyIndex;
static DevPrivateKey ephyrDRI2ScreenKey = &ephyrDRI2ScreenKeyIndex;

#define GET_EPHYR_DRI2_WINDOW_PRIV(win) ((EphyrDRI2WindowPrivPtr) \
    dixLookupPrivate(&(win)->devPrivates, ephyrDRI2WindowKey))
#define GET_EPHYR_DRI2_SCREEN_PRIV(screen) ((EphyrDRIScreenPrivPtr) \
    dixLookupPrivate(&(screen)->devPrivates, ephyrDRI2ScreenKey))


static void
ephyrDRI2MoveWindow (WindowPtr a_win,
                     int a_x,
		     int a_y,
                     WindowPtr a_siblings,
                     VTKind a_kind)
{
    Bool is_ok = FALSE;
    ScreenPtr screen = NULL;
    EphyrDRIScreenPrivPtr screen_priv = NULL;
    EphyrDRI2WindowPrivPtr win_priv = NULL;
    EphyrDRI2WindowPair *pair = NULL;
    EphyrBox geo;
    int x = 0,y = 0; /*coords relative to parent window*/

    EPHYR_RETURN_IF_FAIL (a_win);

    EPHYR_LOG ("enter\n");
    screen = a_win->drawable.pScreen;
    EPHYR_RETURN_IF_FAIL (screen);
    screen_priv = GET_EPHYR_DRI2_SCREEN_PRIV (screen);
    EPHYR_RETURN_IF_FAIL (screen_priv && screen_priv->MoveWindow);

    screen->MoveWindow = screen_priv->MoveWindow;
    if (screen->MoveWindow) {
        (*screen->MoveWindow) (a_win, a_x, a_y, a_siblings, a_kind);
    }
    screen->MoveWindow = ephyrDRI2MoveWindow;

    EPHYR_LOG ("window: %p\n", a_win);
    if (!a_win->parent) {
        EPHYR_LOG ("cannot move root window\n");
        is_ok = TRUE;
        goto out;
    }
    win_priv = GET_EPHYR_DRI2_WINDOW_PRIV (a_win);
    if (!win_priv) {
        EPHYR_LOG ("not a DRI peered window\n");
        is_ok = TRUE;
        goto out;
    }
    if (!ephyrDRI2GetWindowPairFromLocalWindow (a_win, &pair)) {
        EPHYR_LOG_ERROR ("failed to get window pair\n");
        goto out;
    }
    /*compute position relative to parent window*/
    x = a_win->drawable.x - a_win->parent->drawable.x;
    y = a_win->drawable.y - a_win->parent->drawable.y;
    /*set the geometry to pass to hostx_set_window_geometry*/
    memset (&geo, 0, sizeof (geo));
    geo.x = x;
    geo.y = y;
    geo.width = a_win->drawable.width;
    geo.height = a_win->drawable.height;
    hostx_set_window_geometry (pair->remote, &geo);
    is_ok = TRUE;

out:
    EPHYR_LOG ("leave. is_ok:%d\n", is_ok);
    /*do cleanup here*/
}

static Bool
ephyrDRI2PositionWindow (WindowPtr a_win,
                         int a_x,
			 int a_y)
{
    Bool is_ok = FALSE;
    ScreenPtr screen = NULL;
    EphyrDRIScreenPrivPtr screen_priv = NULL;
    EphyrDRI2WindowPrivPtr win_priv = NULL;
    EphyrDRI2WindowPair *pair = NULL;
    EphyrBox geo;

    EPHYR_RETURN_VAL_IF_FAIL (a_win, FALSE);

    EPHYR_LOG ("enter\n");
    screen = a_win->drawable.pScreen;
    EPHYR_RETURN_VAL_IF_FAIL (screen, FALSE);
    screen_priv = GET_EPHYR_DRI2_SCREEN_PRIV (screen);
    EPHYR_RETURN_VAL_IF_FAIL (screen_priv
                              && screen_priv->PositionWindow,
                              FALSE);

    screen->PositionWindow = screen_priv->PositionWindow;
    if (screen->PositionWindow) {
        (*screen->PositionWindow) (a_win, a_x, a_y);
    }
    screen->PositionWindow = ephyrDRI2PositionWindow;

    EPHYR_LOG ("window: %p\n", a_win);
    win_priv = GET_EPHYR_DRI2_WINDOW_PRIV (a_win);
    if (!win_priv) {
        EPHYR_LOG ("not a DRI peered window\n");
        is_ok = TRUE;
        goto out;
    }
    if (!ephyrDRI2GetWindowPairFromLocalWindow (a_win, &pair)) {
        EPHYR_LOG_ERROR ("failed to get window pair\n");
        goto out;
    }
    /*set the geometry to pass to hostx_set_window_geometry*/
    memset (&geo, 0, sizeof (geo));
    geo.x = a_x;
    geo.y = a_y;
    geo.width = a_win->drawable.width;
    geo.height = a_win->drawable.height;
    hostx_set_window_geometry (pair->remote, &geo);
    is_ok = TRUE;

out:
    EPHYR_LOG ("leave. is_ok:%d\n", is_ok);
    /*do cleanup here*/
    return is_ok;
}

static void
ephyrDRI2ClipNotify (WindowPtr a_win, int a_x, int a_y)
{
    Bool is_ok = FALSE;
    ScreenPtr screen = NULL;
    EphyrDRIScreenPrivPtr screen_priv = NULL;
    EphyrDRI2WindowPrivPtr win_priv = NULL;
    EphyrDRI2WindowPair *pair = NULL;
    EphyrRect *rects = NULL;
    int i = 0;

    EPHYR_RETURN_IF_FAIL (a_win);

    EPHYR_LOG ("enter\n");
    screen = a_win->drawable.pScreen;
    EPHYR_RETURN_IF_FAIL (screen);
    screen_priv = GET_EPHYR_DRI2_SCREEN_PRIV (screen);
    EPHYR_RETURN_IF_FAIL (screen_priv && screen_priv->ClipNotify);

    screen->ClipNotify = screen_priv->ClipNotify;
    if (screen->ClipNotify) {
        (*screen->ClipNotify) (a_win, a_x, a_y);
    }
    screen->ClipNotify = ephyrDRI2ClipNotify;

    EPHYR_LOG ("window: %p\n", a_win);
    win_priv = GET_EPHYR_DRI2_WINDOW_PRIV (a_win);
    if (!win_priv) {
        EPHYR_LOG ("not a DRI peered window\n");
        is_ok = TRUE;
        goto out;
    }
    if (!ephyrDRI2GetWindowPairFromLocalWindow (a_win, &pair)) {
        EPHYR_LOG_ERROR ("failed to get window pair\n");
        goto out;
    }
    rects = xcalloc (REGION_NUM_RECTS (&a_win->clipList),
                     sizeof (EphyrRect));
    for (i = 0; i < REGION_NUM_RECTS (&a_win->clipList); i++) {
        memmove (&rects[i],
                 &REGION_RECTS (&a_win->clipList)[i],
                 sizeof (EphyrRect));
        rects[i].x1 -= a_win->drawable.x;
        rects[i].x2 -= a_win->drawable.x;
        rects[i].y1 -= a_win->drawable.y;
        rects[i].y2 -= a_win->drawable.y;
    }
    /*
     * push the clipping region of this window
     * to the peer window in the host
     */
    is_ok = hostx_set_window_bounding_rectangles
                                (pair->remote,
                                 rects,
                                 REGION_NUM_RECTS (&a_win->clipList));
    is_ok = TRUE;

out:
    if (rects) {
        xfree (rects);
        rects = NULL;
    }
    EPHYR_LOG ("leave. is_ok:%d\n", is_ok);
    /*do cleanup here*/
}

/**
 * Duplicates a visual of a_screen
 * In screen a_screen, for depth a_depth, find a visual which
 * bitsPerRGBValue and colormap size equal
 * a_bits_per_rgb_values and a_colormap_entries.
 * The ID of that duplicated visual is set to a_new_id.
 * That duplicated visual is then added to the list of visuals
 * of the screen.
 */
static Bool
duplicateVisual (unsigned int a_screen,
                 short a_depth,
		 short a_class,
		 short a_bits_per_rgb_values,
		 short a_colormap_entries,
		 unsigned int a_red_mask,
		 unsigned int a_green_mask,
		 unsigned int a_blue_mask,
		 unsigned int a_new_id)
{
    Bool is_ok = FALSE, found_visual=FALSE, found_depth=FALSE;
    ScreenPtr screen=NULL;
    VisualRec new_visual, *new_visuals=NULL;
    int i=0;

    EPHYR_LOG ("enter\n"); 
    if (a_screen > screenInfo.numScreens) {
        EPHYR_LOG_ERROR ("bad screen number\n");
        goto out;
    }
    memset (&new_visual, 0, sizeof (VisualRec));

    /*get the screen pointed to by a_screen*/
    screen = screenInfo.screens[a_screen];
    EPHYR_RETURN_VAL_IF_FAIL (screen, FALSE);

    /*
     * In that screen, first look for an existing visual that has the
     * same characteristics as those passed in parameter
     * to this function and copy it.
     */
    for (i=0; i < screen->numVisuals; i++) {
        if (screen->visuals[i].bitsPerRGBValue == a_bits_per_rgb_values &&
            screen->visuals[i].ColormapEntries == a_colormap_entries ) {
            /*copy the visual found*/
            memcpy (&new_visual, &screen->visuals[i], sizeof (new_visual));
            new_visual.vid = a_new_id;
            new_visual.class = a_class;
            new_visual.redMask = a_red_mask;
            new_visual.greenMask = a_green_mask;
            new_visual.blueMask = a_blue_mask;
            found_visual = TRUE;
            EPHYR_LOG ("found a visual that matches visual id: %d\n",
                       a_new_id);
            break;
        }
    }
    if (!found_visual) {
        EPHYR_LOG ("did not find any visual matching %d\n", a_new_id);
        goto out;
    }
    /*
     * be prepare to extend screen->visuals to add new_visual to it
     */
    new_visuals = xcalloc (screen->numVisuals + 1, sizeof (VisualRec));
    memmove (new_visuals,
             screen->visuals,
             screen->numVisuals*sizeof (VisualRec));
    memmove (&new_visuals[screen->numVisuals],
             &new_visual,
             sizeof (VisualRec));
    /*
     * Now, in that same screen, update the screen->allowedDepths member.
     * In that array, each element represents the visuals applicable to
     * a given depth. So we need to add an entry matching the new visual
     * that we are going to add to screen->visuals
     */
    for (i = 0; i < screen->numDepths; i++) {
        VisualID *vids = NULL;
        DepthPtr cur_depth = NULL;
        /*find the entry matching a_depth*/
        if (screen->allowedDepths[i].depth != a_depth)
            continue;
        cur_depth = &screen->allowedDepths[i];
        /*
         * extend the list of visual IDs in that entry,
         * so to add a_new_id in there.
         */
        vids = xrealloc (cur_depth->vids,
                         (cur_depth->numVids + 1) * sizeof (VisualID));
        if (!vids) {
            EPHYR_LOG_ERROR ("failed to realloc numids\n");
            goto out;
        }
        vids[cur_depth->numVids] = a_new_id;
        /*
         * Okay now commit our change.
         * Do really update screen->allowedDepths[i]
         */
        cur_depth->numVids++;
        cur_depth->vids = vids;
        found_depth = TRUE;
    }
    if (!found_depth) {
        EPHYR_LOG_ERROR ("failed to update screen[%d]->allowedDepth\n",
                         a_screen);
        goto out;
    }
    /*
     * Commit our change to screen->visuals
     */
    xfree (screen->visuals);
    screen->visuals = new_visuals;
    screen->numVisuals++;
    new_visuals = NULL;

    is_ok = TRUE;
out:
    if (new_visuals) {
        xfree (new_visuals);
        new_visuals = NULL;
    }
    EPHYR_LOG ("leave\n"); 
    return is_ok;
}

/**
 * Duplicates the visuals of the host X server.
 * This is necessary to have visuals that have the same
 * ID as those of the host X. It is important to have that for
 * GLX.
 */
static Bool
mirrorHostVisuals (ScreenPtr a_screen)
{
    Bool is_ok = FALSE;
    EphyrHostVisualInfo  *visuals = NULL;
    int nb_visuals = 0, i = 0;

    EPHYR_LOG ("enter\n");
    if (!hostx_get_visuals_info (&visuals, &nb_visuals)) {
        EPHYR_LOG_ERROR ("failed to get host visuals\n");
        goto out;
    }
    for (i = 0; i < nb_visuals; i++) {
        if (!duplicateVisual (a_screen->myNum,
                              visuals[i].depth,
			      visuals[i].class,
			      visuals[i].bits_per_rgb,
			      visuals[i].colormap_size,
			      visuals[i].red_mask,
			      visuals[i].green_mask,
			      visuals[i].blue_mask,
			      visuals[i].visualid)) {
            EPHYR_LOG_ERROR ("failed to duplicate host visual %d\n",
                             (int)visuals[i].visualid);
        }
    }

    is_ok = TRUE;
out:
    EPHYR_LOG ("leave\n");
    return is_ok;
}

static Bool
getWindowVisual (const WindowPtr a_win, VisualPtr *a_visual)
{
    int i = 0, visual_id = 0;
    EPHYR_RETURN_VAL_IF_FAIL (a_win
                              && a_win->drawable.pScreen
                              && a_win->drawable.pScreen->visuals,
                              FALSE);

    visual_id = wVisual (a_win);
    for (i = 0; i < a_win->drawable.pScreen->numVisuals; i++) {
        if (a_win->drawable.pScreen->visuals[i].vid == visual_id) {
            *a_visual = &a_win->drawable.pScreen->visuals[i];
            return TRUE;
        }
    }
    return FALSE;
}


#define NUM_WINDOW_PAIRS 256
static EphyrDRI2WindowPair window_pairs[NUM_WINDOW_PAIRS];

static Bool
appendWindowPairToList (WindowPtr a_local, int a_remote)
{
    int i = 0;

    EPHYR_RETURN_VAL_IF_FAIL (a_local, FALSE);

    EPHYR_LOG ("(local,remote):(%p, %d)\n", a_local, a_remote);

    for (i = 0; i < NUM_WINDOW_PAIRS; i++) {
        if (window_pairs[i].local == NULL) {
            window_pairs[i].local = a_local;
            window_pairs[i].remote = a_remote;
            return TRUE;
        }
    }
    return FALSE;
}

Bool
ephyrDRI2GetWindowPairFromLocalWindow (WindowPtr a_local,
				       EphyrDRI2WindowPair **a_pair)
{
    int i = 0;

    EPHYR_RETURN_VAL_IF_FAIL (a_pair && a_local, FALSE);

    for (i = 0; i < NUM_WINDOW_PAIRS; i++) {
        if (window_pairs[i].local == a_local) {
            *a_pair = &window_pairs[i];
            EPHYR_LOG ("found (%p, %d)\n",
                       (*a_pair)->local,
                       (*a_pair)->remote);
            return TRUE;
        }
    }
    return FALSE;
}

Bool
ephyrDRI2GetWindowPairFromHostWindow (int a_remote,
				      EphyrDRI2WindowPair **a_pair)
{
    int i = 0;

    EPHYR_RETURN_VAL_IF_FAIL (a_pair, FALSE);

    for (i = 0; i < NUM_WINDOW_PAIRS; i++) {
        if (window_pairs[i].remote == a_remote) {
            *a_pair = &window_pairs[i];
            EPHYR_LOG ("found (%p, %d)\n",
                       (*a_pair)->local,
                       (*a_pair)->remote);
            return TRUE;
        }
    }
    return FALSE;
}

static Bool
createHostPeerWindow (const WindowPtr a_win, int *a_peer_win)
{
    Bool is_ok = FALSE;
    VisualPtr visual = NULL;
    EphyrBox geo;

    EPHYR_RETURN_VAL_IF_FAIL (a_win && a_peer_win, FALSE);
    EPHYR_RETURN_VAL_IF_FAIL (a_win->drawable.pScreen,
                              FALSE);

    EPHYR_LOG ("enter. a_win '%p'\n", a_win);
    if (!getWindowVisual (a_win, &visual)) {
        EPHYR_LOG_ERROR ("failed to get window visual\n");
        goto out;
    }
    if (!visual) {
        EPHYR_LOG_ERROR ("failed to create visual\n");
        goto out;
    }
    memset (&geo, 0, sizeof (geo));
    geo.x = a_win->drawable.x;
    geo.y = a_win->drawable.y;
    geo.width = a_win->drawable.width;
    geo.height = a_win->drawable.height;
    if (!hostx_create_window (a_win->drawable.pScreen->myNum,
                              &geo, visual->vid, a_peer_win)) {
        EPHYR_LOG_ERROR ("failed to create host peer window\n");
        goto out;
    }
    if (!appendWindowPairToList (a_win, *a_peer_win)) {
        EPHYR_LOG_ERROR ("failed to append window to pair list\n");
        goto out;
    }
    is_ok = TRUE;
out:
    EPHYR_LOG ("leave:remote win%d\n", *a_peer_win);
    return is_ok;
}

static Bool
destroyHostPeerWindow (const WindowPtr a_win)
{
    Bool is_ok = FALSE;
    EphyrDRI2WindowPair *pair = NULL;
    EPHYR_RETURN_VAL_IF_FAIL (a_win, FALSE);

    EPHYR_LOG ("enter\n");

    if (!ephyrDRI2GetWindowPairFromLocalWindow (a_win, &pair)) {
        EPHYR_LOG_ERROR ("failed to find peer to local window\n");
        goto out;
    }
    hostx_destroy_window (pair->remote);
    is_ok = TRUE;

out:
    EPHYR_LOG ("leave\n");
    return is_ok;
}

static int
destroyEphyrDRI2WindowState (WindowPtr pWindow)
{
    EphyrDRI2WindowPrivPtr win_priv;
    EphyrDRI2WindowPair *pair = NULL;

    win_priv=GET_EPHYR_DRI2_WINDOW_PRIV (pWindow);
    if (!win_priv) {
        EPHYR_LOG_ERROR ("failed to get DRI2 window's private data\n");
        return BadImplementation;
    }

    destroyHostPeerWindow (pWindow);
    xfree (win_priv);
    dixSetPrivate(&pWindow->devPrivates, ephyrDRI2WindowKey, NULL);
    EPHYR_LOG ("destroyed the remote peer window\n");

    /* XXX: This must be done after destroyHostPeerWindow since it
     * queries the window pair. */
    if (!ephyrDRI2GetWindowPairFromLocalWindow (pWindow, &pair)) {
	EPHYR_LOG_ERROR ("failed to find pair window\n");
	return BadImplementation;
    }
    pair->local = NULL;
    pair->remote = 0;

    return Success;
}

static Bool
ephyrDRI2ScreenInit (ScreenPtr a_screen)
{
    Bool is_ok = FALSE;
    EphyrDRIScreenPrivPtr screen_priv = NULL;

    EPHYR_RETURN_VAL_IF_FAIL (a_screen, FALSE);

    screen_priv=GET_EPHYR_DRI2_SCREEN_PRIV (a_screen);
    EPHYR_RETURN_VAL_IF_FAIL (screen_priv, FALSE);

    screen_priv->MoveWindow = a_screen->MoveWindow;
    screen_priv->PositionWindow = a_screen->PositionWindow;
    screen_priv->ClipNotify = a_screen->ClipNotify;

    a_screen->MoveWindow = ephyrDRI2MoveWindow;
    a_screen->PositionWindow = ephyrDRI2PositionWindow;
    a_screen->ClipNotify = ephyrDRI2ClipNotify;

    is_ok = TRUE;

    return is_ok;
}

Bool
ephyrDRI2ExtensionSetup (ScreenPtr pScreen)
{
    Bool status = FALSE;
    EphyrDRIScreenPrivPtr screen_priv = NULL;

    if (!hostx_has_dri2 ()) {
        EPHYR_LOG ("host does not have DRI extension\n");
        goto out;
    }
    EPHYR_LOG ("host X does have DRI extension\n");
    if (!hostx_has_xshape ()) {
        EPHYR_LOG ("host does not have XShape extension\n");
        goto out;
    }
    EPHYR_LOG ("host X does have XShape extension\n");

    screen_priv = xcalloc (1, sizeof (EphyrDRI2ScreenPrivRec));
    if (!screen_priv) {
        EPHYR_LOG_ERROR ("failed to allocate screen_priv\n");
        goto out;
    }
    dixSetPrivate (&pScreen->devPrivates, ephyrDRI2ScreenKey, screen_priv);

    if (!ephyrDRI2ScreenInit (pScreen)) {
        EPHYR_LOG_ERROR ("ephyrDRI2ScreenInit() failed\n");
        goto out;
    }

    mirrorHostVisuals (pScreen);

    ephyrDRI2ExtensionInit ();

    status=TRUE;
out:
    return status;
}

int
ephyrDRI2CreateDrawable(DrawablePtr pDrawable)
{
    WindowPtr pWindow;
    int remote_win;
    EphyrDRI2WindowPair *pair = NULL;
    EphyrDRI2WindowPrivPtr win_priv = NULL;
    int status;

    if (pDrawable->type != DRAWABLE_WINDOW) {
        EPHYR_LOG_ERROR ("non Window Drawables are not yet supported\n");
        return BadImplementation;
    }

    pWindow = (WindowPtr)pDrawable;
    win_priv = GET_EPHYR_DRI2_WINDOW_PRIV (pWindow);
    if (win_priv) {
	win_priv->ref++;
    } else {
        win_priv = xcalloc (1, sizeof (EphyrDRI2WindowPrivRec));
	win_priv->ref = 1;
        if (!win_priv) {
            EPHYR_LOG_ERROR ("failed to allocate window private\n");
            return BadAlloc;
        }
	dixSetPrivate (&pWindow->devPrivates, ephyrDRI2WindowKey, win_priv);
        EPHYR_LOG ("paired window '%p' with remote '%d'\n",
                   pWindow, remote_win);
	if (ephyrDRI2GetWindowPairFromLocalWindow (pWindow, &pair)) {
            EPHYR_LOG_ERROR ("Found spurious peer window for newly created "
			     "DRI2 drawable\n");
	} else if (!createHostPeerWindow (pWindow, &remote_win)) {
	    EPHYR_LOG_ERROR ("failed to create host peer window\n");
	    return BadAlloc;
	}
    }

    status = ephyrProxyDRI2CreateDrawable (pDrawable);
    if (status != Success) {
	win_priv->ref--;
	if (win_priv->ref == 0)
	    destroyEphyrDRI2WindowState (pWindow);

	return status;
    }

    return Success;
}

xDRI2Buffer *
ephyrDRI2GetBuffers (DrawablePtr pDrawable,
		     int *width,
		     int *height,
		     unsigned int *attachments,
		     int count,
		     int *out_count)
{
    return ephyrProxyDRI2GetBuffers (pDrawable, width, height,
				     attachments, count, out_count);
}

xDRI2Buffer *
ephyrDRI2GetBuffersWithFormat (DrawablePtr pDrawable,
			       int *width,
			       int *height,
			       unsigned int *attachments,
			       int count,
			       int *out_count)
{
    return ephyrProxyDRI2GetBuffersWithFormat (pDrawable, width, height,
					       attachments, count, out_count);
}

int
ephyrDRI2CopyRegion (DrawablePtr pDrawable,
		     RegionPtr pRegion,
		     unsigned int dest,
		     unsigned int src)
{
    return ephyrProxyDRI2CopyRegion (pDrawable, pRegion, dest, src);
}

int
ephyrDRI2DestroyDrawable (DrawablePtr pDrawable)
{
    int status;
    WindowPtr pWindow;
    EphyrDRI2WindowPrivPtr win_priv;

    if (pDrawable->type != DRAWABLE_WINDOW) {
        EPHYR_LOG_ERROR ("non Window Drawables are not yet supported\n");
        return BadImplementation;
    }

    status = ephyrProxyDRI2DestroyDrawable (pDrawable);
    if (status != Success)
	return status;

    pWindow = (WindowPtr)pDrawable;

    win_priv=GET_EPHYR_DRI2_WINDOW_PRIV (pWindow);
    if (!win_priv) {
        EPHYR_LOG_ERROR ("failed to get DRI2 window's private data\n");
        return BadImplementation;
    }

    win_priv->ref--;
    if (win_priv->ref == 0)
	status = destroyEphyrDRI2WindowState (pWindow);

    return status;
}

Bool
ephyrDRI2Connect (ScreenPtr pScreen,
		  unsigned int driverType,
		  int *fd,
		  char **driverName,
		  char **deviceName)
{
    return ephyrProxyDRI2Connect (pScreen, driverType, fd,
				  driverName, deviceName);
}

Bool
ephyrDRI2Authenticate (ScreenPtr pScreen, drm_magic_t magic)
{
    return ephyrProxyDRI2Authenticate (pScreen, magic);
}

