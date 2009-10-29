/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 * 
 * Copyright © 2008 Red Hat, Inc.
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
 * XXX: This file is basically just a copy of
 * hw/xfree86/dri2/dri2ext.c...
 *
 * Authors:
 *    Kristian Høgsberg (krh@redhat.com)
 *    Robert Bragg <robert@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/dri2proto.h>
#include <X11/extensions/xfixeswire.h>
#include "dixstruct.h"
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "extnsionst.h"
#include "xf86drm.h"
#include "xfixes.h"
#include "ephyrdri2.h"
#include "protocol-versions.h"

/* NB: We don't forward version queries to the host X connection since this
 * code will need to be explicitly updated to support later versions!
 */
#define EPHYR_SERVER_DRI2_MAJOR_VERSION 1
#define EPHYR_SERVER_DRI2_MINOR_VERSION 1


/*************************************************************************
 * XXX: This file is basically just a copy of
 * hw/xfree86/dri2/dri2ext.c...
 */

static ExtensionEntry	*dri2Extension;
static RESTYPE		 dri2DrawableRes;


static Bool
validDrawable(ClientPtr client, XID drawable, Mask access_mode,
	      DrawablePtr *pDrawable, int *status)
{
    *status = dixLookupDrawable(pDrawable, drawable, client, 0, access_mode);
    if (*status != Success) {
	client->errorValue = drawable;
	return FALSE;
    }

    return TRUE;
}

static int
ProcDRI2QueryVersion(ClientPtr client)
{
    REQUEST(xDRI2QueryVersionReq);
    xDRI2QueryVersionReply rep;
    int n;

    if (client->swapped)
	swaps(&stuff->length, n);

    REQUEST_SIZE_MATCH(xDRI2QueryVersionReq);
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.majorVersion = EPHYR_SERVER_DRI2_MAJOR_VERSION;
    rep.minorVersion = EPHYR_SERVER_DRI2_MINOR_VERSION;

    if (client->swapped) {
    	swaps(&rep.sequenceNumber, n);
    	swapl(&rep.length, n);
	swapl(&rep.majorVersion, n);
	swapl(&rep.minorVersion, n);
    }

    WriteToClient(client, sizeof(xDRI2QueryVersionReply), &rep);

    return client->noClientException;
}

static int
ProcDRI2Connect(ClientPtr client)
{
    REQUEST(xDRI2ConnectReq);
    xDRI2ConnectReply rep;
    DrawablePtr pDraw;
    int fd, status;
    char *driverName;
    char *deviceName;

    REQUEST_SIZE_MATCH(xDRI2ConnectReq);
    if (!validDrawable(client, stuff->window, DixGetAttrAccess,
		       &pDraw, &status))
	return status;
    
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.driverNameLength = 0;
    rep.deviceNameLength = 0;

    if (!ephyrDRI2Connect(pDraw->pScreen,
			  stuff->driverType, &fd, &driverName, &deviceName))
	goto fail;

    rep.driverNameLength = strlen(driverName);
    rep.deviceNameLength = strlen(deviceName);
    rep.length = (rep.driverNameLength + 3) / 4 +
	    (rep.deviceNameLength + 3) / 4;

 fail:
    WriteToClient(client, sizeof(xDRI2ConnectReply), &rep);
    WriteToClient(client, rep.driverNameLength, driverName);
    WriteToClient(client, rep.deviceNameLength, deviceName);

    return client->noClientException;
}

static int
ProcDRI2Authenticate(ClientPtr client)
{
    REQUEST(xDRI2AuthenticateReq);
    xDRI2AuthenticateReply rep;
    DrawablePtr pDraw;
    int status;

    REQUEST_SIZE_MATCH(xDRI2AuthenticateReq);
    if (!validDrawable(client, stuff->window, DixGetAttrAccess,
		       &pDraw, &status))
	return status;

    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.length = 0;
    rep.authenticated = ephyrDRI2Authenticate(pDraw->pScreen, stuff->magic);
    WriteToClient(client, sizeof(xDRI2AuthenticateReply), &rep);

    return client->noClientException;
}

static int
ProcDRI2CreateDrawable(ClientPtr client)
{
    REQUEST(xDRI2CreateDrawableReq);
    DrawablePtr pDrawable;
    int status;

    REQUEST_SIZE_MATCH(xDRI2CreateDrawableReq);

    if (!validDrawable(client, stuff->drawable, DixAddAccess,
		       &pDrawable, &status))
	return status;

    status = ephyrDRI2CreateDrawable(pDrawable);
    if (status != Success)
	return status;

    if (!AddResource(stuff->drawable, dri2DrawableRes, pDrawable)) {
	ephyrDRI2DestroyDrawable(pDrawable);
	return BadAlloc;
    }

    return client->noClientException;
}

static int
ProcDRI2DestroyDrawable(ClientPtr client)
{
    REQUEST(xDRI2DestroyDrawableReq);
    DrawablePtr pDrawable;
    int status;

    REQUEST_SIZE_MATCH(xDRI2DestroyDrawableReq);
    if (!validDrawable(client, stuff->drawable, DixRemoveAccess,
		       &pDrawable, &status))
	return status;

    FreeResourceByType(stuff->drawable, dri2DrawableRes, FALSE);

    return client->noClientException;
}

/* XXX: Unlike the original from dri2ext.c we don't need to worry about filtering
 * out the real front left buffers, since that's already been done by the host
 * X server. */
static void
send_buffers_reply(ClientPtr client, DrawablePtr pDrawable,
		   xDRI2Buffer *buffers, int count, int width, int height)
{
    xDRI2GetBuffersReply rep;
    int i;

    rep.type = X_Reply;
    rep.length = count * sizeof(xDRI2Buffer) / 4;
    rep.sequenceNumber = client->sequence;
    rep.width = width;
    rep.height = height;
    rep.count = count;
    WriteToClient(client, sizeof(xDRI2GetBuffersReply), &rep);

    for (i = 0; i < count; i++)
	WriteToClient(client, sizeof(xDRI2Buffer), &buffers[i]);
}


static int
ProcDRI2GetBuffers(ClientPtr client)
{
    REQUEST(xDRI2GetBuffersReq);
    DrawablePtr pDrawable;
    xDRI2Buffer *buffers;
    int status, width, height, count;
    unsigned int *attachments;

    REQUEST_FIXED_SIZE(xDRI2GetBuffersReq, stuff->count * 4);
    if (!validDrawable(client, stuff->drawable, DixReadAccess | DixWriteAccess,
		       &pDrawable, &status))
	return status;

    attachments = (unsigned int *) &stuff[1];
    buffers = ephyrDRI2GetBuffers(pDrawable, &width, &height,
				  attachments, stuff->count, &count);


    send_buffers_reply(client, pDrawable, buffers, count, width, height);

    return client->noClientException;
}

static int
ProcDRI2GetBuffersWithFormat(ClientPtr client)
{
    REQUEST(xDRI2GetBuffersReq);
    DrawablePtr pDrawable;
    xDRI2Buffer *buffers;
    int status, width, height, count;
    unsigned int *attachments;

    REQUEST_FIXED_SIZE(xDRI2GetBuffersReq, stuff->count * (2 * 4));
    if (!validDrawable(client, stuff->drawable, DixReadAccess | DixWriteAccess,
		       &pDrawable, &status))
	return status;

    attachments = (unsigned int *) &stuff[1];
    buffers = ephyrDRI2GetBuffersWithFormat(pDrawable, &width, &height,
					    attachments, stuff->count, &count);

    send_buffers_reply(client, pDrawable, buffers, count, width, height);

    return client->noClientException;
}

static int
ProcDRI2CopyRegion(ClientPtr client)
{
    REQUEST(xDRI2CopyRegionReq);
    xDRI2CopyRegionReply rep;
    DrawablePtr pDrawable;
    int status;
    RegionPtr pRegion;

    REQUEST_SIZE_MATCH(xDRI2CopyRegionReq);

    if (!validDrawable(client, stuff->drawable, DixWriteAccess,
		       &pDrawable, &status))
	return status;

    VERIFY_REGION(pRegion, stuff->region, client, DixReadAccess);

    status = ephyrDRI2CopyRegion(pDrawable, pRegion, stuff->dest, stuff->src);
    if (status != Success)
	return status;

    /* CopyRegion needs to be a round trip to make sure the X server
     * queues the swap buffer rendering commands before the DRI client
     * continues rendering.  The reply has a bitmask to signal the
     * presense of optional return values as well, but we're not using
     * that yet.
     */

    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;

    WriteToClient(client, sizeof(xDRI2CopyRegionReply), &rep);

    return client->noClientException;
}

static int
ProcDRI2Dispatch (ClientPtr client)
{
    REQUEST(xReq);
    
    switch (stuff->data) {
    case X_DRI2QueryVersion:
	return ProcDRI2QueryVersion(client);
    }

    if (!LocalClient(client))
	return BadRequest;

    switch (stuff->data) {
    case X_DRI2Connect:
	return ProcDRI2Connect(client);
    case X_DRI2Authenticate:
	return ProcDRI2Authenticate(client);
    case X_DRI2CreateDrawable:
	return ProcDRI2CreateDrawable(client);
    case X_DRI2DestroyDrawable:
	return ProcDRI2DestroyDrawable(client);
    case X_DRI2GetBuffers:
	return ProcDRI2GetBuffers(client);
    case X_DRI2CopyRegion:
	return ProcDRI2CopyRegion(client);
    case X_DRI2GetBuffersWithFormat:
	return ProcDRI2GetBuffersWithFormat(client);
    default:
	return BadRequest;
    }
}

static int
SProcDRI2Connect(ClientPtr client)
{
    REQUEST(xDRI2ConnectReq);
    xDRI2ConnectReply rep;
    int n;

    /* If the client is swapped, it's not local.  Talk to the hand. */

    swaps(&stuff->length, n);
    if (sizeof(*stuff) / 4 != client->req_len)
	return BadLength;

    rep.sequenceNumber = client->sequence;
    swaps(&rep.sequenceNumber, n);
    rep.length = 0;
    rep.driverNameLength = 0;
    rep.deviceNameLength = 0;

    return client->noClientException;
}

static int
SProcDRI2Dispatch (ClientPtr client)
{
    REQUEST(xReq);

    /*
     * Only local clients are allowed DRI access, but remote clients
     * still need these requests to find out cleanly.
     */
    switch (stuff->data)
    {
    case X_DRI2QueryVersion:
	return ProcDRI2QueryVersion(client);
    case X_DRI2Connect:
	return SProcDRI2Connect(client);
    default:
	return BadRequest;
    }
}

static int DRI2DrawableGone(pointer p, XID id)
{
    DrawablePtr pDrawable = p;

    ephyrDRI2DestroyDrawable(pDrawable);

    return Success;
}

void
ephyrDRI2ExtensionInit (void)
{
    dri2Extension = AddExtension(DRI2_NAME,
				 DRI2NumberEvents,
				 DRI2NumberErrors,
				 ProcDRI2Dispatch,
				 SProcDRI2Dispatch,
				 NULL,
				 StandardMinorOpcode);

    dri2DrawableRes = CreateNewResourceType(DRI2DrawableGone);
}

