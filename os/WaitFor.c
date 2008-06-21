/***********************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.


Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/


/*****************************************************************
 * OS Dependent input routines:
 *
 *  WaitForSomething
 *  TimerForce, TimerSet, TimerCheck, TimerFree
 *
 *****************************************************************/
#define GLIB_DISPATCH
//#undef GLIB_DISPATCH

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#ifdef WIN32
#include <X11/Xwinsock.h>
#endif
#include <X11/Xos.h>			/* for strings, fcntl, time */
#include <errno.h>
#include <stdio.h>
#include <X11/X.h>
#include "misc.h"

#include "osdep.h"
#include <X11/Xpoll.h>
#include "dixstruct.h"
#include "opaque.h"
#ifdef DPMSExtension
#include "dpmsproc.h"
#endif
#ifdef GLIB_DISPATCH
#include <glib.h>
#endif

#ifdef WIN32
/* Error codes from windows sockets differ from fileio error codes  */
#undef EINTR
#define EINTR WSAEINTR
#undef EINVAL
#define EINVAL WSAEINVAL
#undef EBADF
#define EBADF WSAENOTSOCK
/* Windows select does not set errno. Use GetErrno as wrapper for 
   WSAGetLastError */
#define GetErrno WSAGetLastError
#else
/* This is just a fallback to errno to hide the differences between unix and
   Windows in the code */
#define GetErrno() errno
#endif

/* modifications by raphael */
int
mffs(fd_mask mask)
{
    int i;

    if (!mask) return 0;
    i = 1;
    while (!(mask & 1))
    {
	i++;
	mask >>= 1;
    }
    return i;
}

#ifdef DPMSExtension
#define DPMS_SERVER
#include <X11/extensions/dpms.h>
#endif

struct _OsTimerRec {
    OsTimerPtr		next;
    CARD32		expires;
    CARD32              delta;
    OsTimerCallback	callback;
    pointer		arg;
};

#ifdef GLIB_DISPATCH
/* For glib main loop integration, we use a simple GSource to help
 * us emulatate Select() semantics
 */
/* FIXME - this can be split up.
 *
 * The only member that is needed by our custom GSource is waitmsecs.
 *
 * The rest must just be globaly available to our poll() wrapper.
 */
typedef struct _GLibSelectSource
{
    GSource	    source;
    GList	    *poll_fds; /*!< A list of GPollFDs */

    /* select() like arguments
     */
    fd_set	    *clientsReadable;
    fd_set	    *clientsWritable;
    /* we don't care about exception file descriptors */
    /* struct timeval  waittime; TODO - check this can be removed */

    gint	     waitmsecs; /*!< timeval converted to milliseconds for poll() */

    int poll_status; /*!< The last return value of our poll() wrapper */
} GLibSelectSource;
#endif

static void DoTimer(OsTimerPtr timer, CARD32 now, OsTimerPtr *prev);
static void CheckAllTimers(void);
static OsTimerPtr timers = NULL;

#ifdef GLIB_DISPATCH
static GLibSelectSource *glib_select_source = NULL;
static guint glib_select_source_id;
static GPollFunc original_poll_func; /*!< the poll() to chain up too */


static gboolean
GLibSelectSourcePrepare (GSource *source,
			 gint    *timeout)
{
    GLibSelectSource *glib_select_source =
	(GLibSelectSource *)source;

    *timeout = glib_select_source->waitmsecs;

    return FALSE;
}

static gboolean
GLibSelectSourceCheck (GSource *source)
{
    /* We always return false, since GLibSelectSourceDispatch is
     * a NOP. The real dispatch code is in dispatch.c:Dispatch() */
    return FALSE;
}

static gboolean
GLibSelectSourceDispatch (GSource *source,
			  GSourceFunc callback,
			  gpointer data)
{
  /* Dispatch is a NOP here - since the real dispatch code
   * for this source is in Dispatch() */
  return TRUE;
}

static GSourceFuncs glib_select_source_funcs = {
  .prepare = GLibSelectSourcePrepare,
  .check = GLibSelectSourceCheck,
  .dispatch = GLibSelectSourceDispatch,
  .finalize = NULL,
};

/* Ideally glib would export a g_main_context_iterate function that would
 * let you iterate the sources of a context - including calling poll to block
 * for new events - but not immediatly dispatch new events.
 * - note: gmain.c: g_main_context_iterate has a dispatch boolean that
 *   gives this kind of control, but this function isn't exported.
 *
 * The X Dispatch mechanism is designed to call a number of "BlockHandlers"
 * and "WakeupHandlers" either side of your blocking select/poll calls
 * as well as stopping and starting the smart scheduler timer. The
 * WakeupHanders should be called, and the timer started _before_ any event
 * dispatching happens.
 *
 * The solution: we provide a poll() wrapper for the default main context
 * so the above handler calls can be tightly coupled with the poll, and
 * so everything will be reset during dispatch.
 */
static gint
GLibPollWrapper (GPollFD *ufds, guint nfds, gint timeout)
{
    struct timeval waittime, *wt;
    int msecs;
    fd_set clientsReadable_in;
    fd_set clientsWritable_in;
    int i;

    msecs = timeout % 1000;
    waittime.tv_usec = msecs * 1000;
    waittime.tv_sec = (timeout - msecs)/1000;
    wt = &waittime;

#ifdef SMART_SCHEDULE
    SmartScheduleStopTimer ();
#endif

    BlockHandler((pointer)&wt, (pointer)glib_select_source->clientsReadable);

    /* wt is an in-out argument to the BlockHandlers so we need to
     * now get the msecs back out of the timeval */
    msecs = waittime.tv_sec * 1000 + waittime.tv_usec / 1000;

    if (NewOutputPending)
	FlushAllOutput();

    /* keep this check close to select() call to minimize race */
    if (dispatchException)
	glib_select_source->poll_status = -1;
    else
	glib_select_source->poll_status =
	    original_poll_func (ufds, nfds, msecs);

    /* Just like select() we overwrite the list of input file descriptors
     * with new sets detailing which files can now be read from and written
     * too without blocking. */

    /* Since other glib sources can add additional file descriptors we
     * need to copy the input fd_sets as whitelists of descriptors we
     * care about here. */
    if (glib_select_source->clientsReadable)
    {
	XFD_COPYSET (glib_select_source->clientsReadable, &clientsReadable_in);
	FD_ZERO (glib_select_source->clientsReadable);
    }
    if (glib_select_source->clientsWritable)
    {
	XFD_COPYSET (glib_select_source->clientsWritable, &clientsWritable_in);
	FD_ZERO (glib_select_source->clientsWritable);
    }

    for (i = 0; i < nfds; i++)
    {
	GPollFD *poll_fd = ufds + i;
	/* FIXME - it's a bit yukky that we have this 0x3 constant, either use
	 * the appropriate POLL* define, or at leastcomment it. */
	if (glib_select_source->clientsReadable && poll_fd->revents & 0x3)
	{
	    if (FD_ISSET (poll_fd->fd, &clientsReadable_in))
		FD_SET (poll_fd->fd, glib_select_source->clientsReadable);
	}
	if (glib_select_source->clientsWritable && poll_fd->revents & G_IO_OUT)
	{
	    if (FD_ISSET (poll_fd->fd, &clientsWritable_in))
		FD_SET (poll_fd->fd, glib_select_source->clientsWritable);
	}
    }

    WakeupHandler(glib_select_source->poll_status,
		  (pointer)glib_select_source->clientsReadable);

#ifdef SMART_SCHEDULE
    SmartScheduleStartTimer ();
#endif

    return glib_select_source->poll_status;
}
#endif /* GLIB_DISPATCH */

void
WaitForSomethingInit (void)
{
#ifdef GLIB_DISPATCH
    GSource *source;

    if (glib_select_source)
	return;

    source =
	g_source_new (&glib_select_source_funcs,
		      sizeof (GLibSelectSource));
    /* g_source_set_priority (source, ); */

    glib_select_source = (GLibSelectSource *)source;
    glib_select_source->poll_fds = NULL;
    glib_select_source->clientsReadable = NULL;
    glib_select_source->clientsWritable = NULL;
    glib_select_source->waitmsecs = -1;

    g_source_set_can_recurse (source, TRUE);
    glib_select_source_id = g_source_attach (source, NULL);

    original_poll_func = g_main_context_get_poll_func (NULL);
    g_main_context_set_poll_func (NULL, GLibPollWrapper);
#endif /* GLIB_DISPATCH */
}
/* FIXME - need to hook in a de-init somewhere */

#ifdef GLIB_DISPATCH
static void
SetupGlibSelectSource (GLibSelectSource *glib_select_source,
		       fd_set *clientsReadable,
		       fd_set *clientsWritable,
		       struct timeval *waittime)
{
    GList *tmp;
    int i;

    /* FIXME - instead of banging the slice allocator this much I
     * should just use a growable GArray/allocation */

    for (tmp = glib_select_source->poll_fds; tmp != NULL; tmp = tmp->next)
    {
	g_source_remove_poll ((GSource *)glib_select_source, tmp->data);
	g_slice_free (GPollFD, tmp->data);
    }
    g_list_free (glib_select_source->poll_fds);
    glib_select_source->poll_fds = NULL;

    if (clientsReadable)
    {
	for (i = 0; i < howmany(FD_SETSIZE, NFDBITS); i++)
	{
	    GPollFD *poll_fd;
	    if (FD_ISSET(i, clientsReadable))
	    {
		poll_fd = g_slice_alloc (sizeof(GPollFD));
		poll_fd->fd = i;
		poll_fd->events = 0X3;//G_IO_IN;
		glib_select_source->poll_fds =
		    g_list_prepend (glib_select_source->poll_fds, poll_fd);
		g_source_add_poll ((GSource *)glib_select_source, poll_fd);
	    }
	}
    }
    if (clientsWritable)
    {
	for (i = 0; i < howmany(FD_SETSIZE, NFDBITS); i++)
	{
	    GPollFD *poll_fd;
	    if (FD_ISSET(i, clientsWritable))
	    {
		poll_fd = g_slice_alloc (sizeof(GPollFD));
		poll_fd->fd = i;
		poll_fd->events = G_IO_OUT;
		glib_select_source->poll_fds =
		    g_list_prepend (glib_select_source->poll_fds, poll_fd);
		g_source_add_poll ((GSource *)glib_select_source, poll_fd);
	    }
	}
    }

    glib_select_source->clientsReadable = clientsReadable;
    glib_select_source->clientsWritable = clientsWritable;

    /* Since poll() takes a timeout in milliseconds we need to convert
     * the timeval: */
    glib_select_source->waitmsecs =
	waittime->tv_sec * 1000 + waittime->tv_usec / 1000;
}
#endif /* GLIB_DISPATCH */

/*****************
 * WaitForSomething:
 *     Make the server suspend until there is
 *	1. data from clients or
 *	2. input events available or
 *	3. ddx notices something of interest (graphics
 *	   queue ready, etc.) or
 *	4. clients that have buffered replies/events are ready
 *
 *     If the time between INPUT events is
 *     greater than ScreenSaverTime, the display is turned off (or
 *     saved, depending on the hardware).  So, WaitForSomething()
 *     has to handle this also (that's why the select() has a timeout.
 *     For more info on ClientsWithInput, see ReadRequestFromClient().
 *     pClientsReady is an array to store ready client->index values into.
 *****************/

int
WaitForSomething(int *pClientsReady)
{
    int i;
    struct timeval waittime, *wt;
    INT32 timeout = 0;
    fd_set clientsReadable;
    fd_set clientsWritable;
    int curclient;
    int selecterr;
    int nready;
    fd_set devicesReadable;
    CARD32 now = 0;
#ifdef SMART_SCHEDULE
    Bool    someReady = FALSE;
#endif

    FD_ZERO(&clientsReadable);

    /* We need a while loop here to handle 
       crashed connections and the screen saver timeout */
    while (1)
    {
	/* deal with any blocked jobs */
	if (workQueue)
	    ProcessWorkQueue();
#ifdef GLIB_DISPATCH
	/* We don't currently support accessing the main loop from another
	 * thread. Given that the X server is single threaded a.t.m I guess
	 * that's ok. */
#ifdef G_THREADS_ENABLED
	if (!g_main_context_acquire (g_main_context_default()))
	    FatalError("WaitForSomething(): "
		       "g_main_context_acquire: failed\n");
#endif

	g_main_context_dispatch (g_main_context_default());
#endif /* GLIB_DISPATCH */

	if (XFD_ANYSET (&ClientsWithInput))
	{
#ifdef SMART_SCHEDULE
	    if (!SmartScheduleDisable)
	    {
		someReady = TRUE;
		waittime.tv_sec = 0;
		waittime.tv_usec = 0;
		wt = &waittime;
	    }
	    else
#endif
	    {
		XFD_COPYSET (&ClientsWithInput, &clientsReadable);
		break;
	    }
	}
#ifdef SMART_SCHEDULE
	if (someReady)
	{
	    XFD_COPYSET(&AllSockets, &LastSelectMask);
	    XFD_UNSET(&LastSelectMask, &ClientsWithInput);
	}
	else
	{
#endif
        wt = NULL;
	if (timers)
        {
            now = GetTimeInMillis();
	    timeout = timers->expires - now;
            if (timeout > 0 && timeout > timers->delta + 250) {
                /* time has rewound.  reset the timers. */
                CheckAllTimers();
            }

	    if (timers) {
		timeout = timers->expires - now;
		if (timeout < 0)
		    timeout = 0;
		waittime.tv_sec = timeout / MILLI_PER_SECOND;
		waittime.tv_usec = (timeout % MILLI_PER_SECOND) *
				   (1000000 / MILLI_PER_SECOND);
		wt = &waittime;
	    }
	}
	XFD_COPYSET(&AllSockets, &LastSelectMask);
#ifdef SMART_SCHEDULE
	}
#ifndef GLIB_DISPATCH
	SmartScheduleStopTimer ();
#endif /* GLIB_DISPATCH */
#endif /* SMART_SCHEDULE */

#ifndef GLIB_DISPATCH
	BlockHandler((pointer)&wt, (pointer)&LastSelectMask);
	if (NewOutputPending)
	    FlushAllOutput();
	/* keep this check close to select() call to minimize race */
	if (dispatchException)
	    i = -1;
	else 
#endif /* GLIB_DISPATCH */
	{
	    fd_set *write_fds = NULL;
	    if (AnyClientsWriteBlocked)
	    {
		XFD_COPYSET(&ClientsWriteBlocked, &clientsWritable);
		write_fds = &clientsWritable;
	    }

#ifdef GLIB_DISPATCH
	    /* We are emulating Select() semantics via the glib GSource
	     * mechanisms - as the least disruptive way to integrate
	     * the glib mainloop iterator. */
	    SetupGlibSelectSource (glib_select_source,
				   &LastSelectMask,
				   write_fds,
				   wt);
	    g_main_context_iteration (NULL, TRUE);
	    i = glib_select_source->poll_status;
#else
	    i = Select (MaxClients, &LastSelectMask, write_fds, NULL, wt);
	selecterr = GetErrno();
#endif /* GLIB_DISPATCH */
	}
#ifndef GLIB_DISPATCH
	WakeupHandler(i, (pointer)&LastSelectMask);
#ifdef SMART_SCHEDULE
	SmartScheduleStartTimer ();
#endif /* SMART_SCHEDULE */
#endif /* GLIB_DISPATCH */
	if (i <= 0) /* An error or timeout occurred */
	{
	    if (dispatchException)
		return 0;
	    if (i < 0) 
	    {
		if (selecterr == EBADF)    /* Some client disconnected */
		{
		    CheckConnections ();
		    if (! XFD_ANYSET (&AllClients))
			return 0;
		}
		else if (selecterr == EINVAL)
		{
		    FatalError("WaitForSomething(): select: %s\n",
			strerror(selecterr));
            }
		else if (selecterr != EINTR && selecterr != EAGAIN)
		{
		    ErrorF("WaitForSomething(): select: %s\n",
			strerror(selecterr));
		}
	    }
#ifdef SMART_SCHEDULE
	    else if (someReady)
	    {
		/*
		 * If no-one else is home, bail quickly
		 */
		XFD_COPYSET(&ClientsWithInput, &LastSelectMask);
		XFD_COPYSET(&ClientsWithInput, &clientsReadable);
		break;
	    }
#endif
	    if (*checkForInput[0] != *checkForInput[1])
		return 0;

	    if (timers)
	    {
                int expired = 0;
		now = GetTimeInMillis();
		if ((int) (timers->expires - now) <= 0)
		    expired = 1;

		while (timers && (int) (timers->expires - now) <= 0)
		    DoTimer(timers, now, &timers);

                if (expired)
                    return 0;
	    }
	}
	else
	{
	    fd_set tmp_set;

	    if (*checkForInput[0] == *checkForInput[1]) {
	        if (timers)
	        {
                    int expired = 0;
		    now = GetTimeInMillis();
		    if ((int) (timers->expires - now) <= 0)
		        expired = 1;

		    while (timers && (int) (timers->expires - now) <= 0)
		        DoTimer(timers, now, &timers);

                    if (expired)
                        return 0;
	        }
	    }
#ifdef SMART_SCHEDULE
	    if (someReady)
		XFD_ORSET(&LastSelectMask, &ClientsWithInput, &LastSelectMask);
#endif	    
	    if (AnyClientsWriteBlocked && XFD_ANYSET (&clientsWritable))
	    {
		NewOutputPending = TRUE;
		XFD_ORSET(&OutputPending, &clientsWritable, &OutputPending);
		XFD_UNSET(&ClientsWriteBlocked, &clientsWritable);
		if (! XFD_ANYSET(&ClientsWriteBlocked))
		    AnyClientsWriteBlocked = FALSE;
	    }

	    XFD_ANDSET(&devicesReadable, &LastSelectMask, &EnabledDevices);
	    XFD_ANDSET(&clientsReadable, &LastSelectMask, &AllClients); 
	    XFD_ANDSET(&tmp_set, &LastSelectMask, &WellKnownConnections);
	    if (XFD_ANYSET(&tmp_set))
		QueueWorkProc(EstablishNewConnections, NULL,
			      (pointer)&LastSelectMask);

	    if (XFD_ANYSET (&devicesReadable) || XFD_ANYSET (&clientsReadable))
		break;
	    /* check here for DDXes that queue events during Block/Wakeup */
	    if (*checkForInput[0] != *checkForInput[1])
		return 0;
	}
    }

    nready = 0;
    if (XFD_ANYSET (&clientsReadable))
    {
#ifndef WIN32
	for (i=0; i<howmany(XFD_SETSIZE, NFDBITS); i++)
	{
	    int highest_priority = 0;

	    while (clientsReadable.fds_bits[i])
	    {
	        int client_priority, client_index;

		curclient = ffs (clientsReadable.fds_bits[i]) - 1;
		client_index = /* raphael: modified */
			ConnectionTranslation[curclient + (i * (sizeof(fd_mask) * 8))];
#else
	int highest_priority = 0;
	fd_set savedClientsReadable;
	XFD_COPYSET(&clientsReadable, &savedClientsReadable);
	for (i = 0; i < XFD_SETCOUNT(&savedClientsReadable); i++)
	{
	    int client_priority, client_index;

	    curclient = XFD_FD(&savedClientsReadable, i);
	    client_index = GetConnectionTranslation(curclient);
#endif
#ifdef XSYNC
		/*  We implement "strict" priorities.
		 *  Only the highest priority client is returned to
		 *  dix.  If multiple clients at the same priority are
		 *  ready, they are all returned.  This means that an
		 *  aggressive client could take over the server.
		 *  This was not considered a big problem because
		 *  aggressive clients can hose the server in so many 
		 *  other ways :)
		 */
		client_priority = clients[client_index]->priority;
		if (nready == 0 || client_priority > highest_priority)
		{
		    /*  Either we found the first client, or we found
		     *  a client whose priority is greater than all others
		     *  that have been found so far.  Either way, we want 
		     *  to initialize the list of clients to contain just
		     *  this client.
		     */
		    pClientsReady[0] = client_index;
		    highest_priority = client_priority;
		    nready = 1;
		}
		/*  the following if makes sure that multiple same-priority 
		 *  clients get batched together
		 */
		else if (client_priority == highest_priority)
#endif
		{
		    pClientsReady[nready++] = client_index;
		}
#ifndef WIN32
		clientsReadable.fds_bits[i] &= ~(((fd_mask)1L) << curclient);
	    }
#else
	    FD_CLR(curclient, &clientsReadable);
#endif
	}
    }
    return nready;
}

/* If time has rewound, re-run every affected timer.
 * Timers might drop out of the list, so we have to restart every time. */
static void
CheckAllTimers(void)
{
    OsTimerPtr timer;
    CARD32 now;

start:
    now = GetTimeInMillis();

    for (timer = timers; timer; timer = timer->next) {
        if (timer->expires - now > timer->delta + 250) {
            TimerForce(timer);
            goto start;
        }
    }
}

static void
DoTimer(OsTimerPtr timer, CARD32 now, OsTimerPtr *prev)
{
    CARD32 newTime;

    *prev = timer->next;
    timer->next = NULL;
    newTime = (*timer->callback)(timer, now, timer->arg);
    if (newTime)
	TimerSet(timer, 0, newTime, timer->callback, timer->arg);
}

OsTimerPtr
TimerSet(OsTimerPtr timer, int flags, CARD32 millis, 
    OsTimerCallback func, pointer arg)
{
    register OsTimerPtr *prev;
    CARD32 now = GetTimeInMillis();

    if (!timer)
    {
	timer = xalloc(sizeof(struct _OsTimerRec));
	if (!timer)
	    return NULL;
    }
    else
    {
	for (prev = &timers; *prev; prev = &(*prev)->next)
	{
	    if (*prev == timer)
	    {
		*prev = timer->next;
		if (flags & TimerForceOld)
		    (void)(*timer->callback)(timer, now, timer->arg);
		break;
	    }
	}
    }
    if (!millis)
	return timer;
    if (flags & TimerAbsolute) {
        timer->delta = millis - now;
    }
    else {
        timer->delta = millis;
	millis += now;
    }
    timer->expires = millis;
    timer->callback = func;
    timer->arg = arg;
    if ((int) (millis - now) <= 0)
    {
	timer->next = NULL;
	millis = (*timer->callback)(timer, now, timer->arg);
	if (!millis)
	    return timer;
    }
    for (prev = &timers;
	 *prev && (int) ((*prev)->expires - millis) <= 0;
	 prev = &(*prev)->next)
        ;
    timer->next = *prev;
    *prev = timer;
    return timer;
}

Bool
TimerForce(OsTimerPtr timer)
{
    OsTimerPtr *prev;

    for (prev = &timers; *prev; prev = &(*prev)->next)
    {
	if (*prev == timer)
	{
	    DoTimer(timer, GetTimeInMillis(), prev);
	    return TRUE;
	}
    }
    return FALSE;
}


void
TimerCancel(OsTimerPtr timer)
{
    OsTimerPtr *prev;

    if (!timer)
	return;
    for (prev = &timers; *prev; prev = &(*prev)->next)
    {
	if (*prev == timer)
	{
	    *prev = timer->next;
	    break;
	}
    }
}

void
TimerFree(OsTimerPtr timer)
{
    if (!timer)
	return;
    TimerCancel(timer);
    xfree(timer);
}

void
TimerCheck(void)
{
    CARD32 now = GetTimeInMillis();

    while (timers && (int) (timers->expires - now) <= 0)
	DoTimer(timers, now, &timers);
}

void
TimerInit(void)
{
    OsTimerPtr timer;

    while ((timer = timers))
    {
	timers = timer->next;
	xfree(timer);
    }
}

#ifdef DPMSExtension

#define DPMS_CHECK_MODE(mode,time)\
    if (time > 0 && DPMSPowerLevel < mode && timeout >= time)\
	DPMSSet(serverClient, mode);

#define DPMS_CHECK_TIMEOUT(time)\
    if (time > 0 && (time - timeout) > 0)\
	return time - timeout;

static CARD32
NextDPMSTimeout(INT32 timeout)
{
    /*
     * Return the amount of time remaining until we should set
     * the next power level. Fallthroughs are intentional.
     */
    switch (DPMSPowerLevel)
    {
	case DPMSModeOn:
	    DPMS_CHECK_TIMEOUT(DPMSStandbyTime)

	case DPMSModeStandby:
	    DPMS_CHECK_TIMEOUT(DPMSSuspendTime)

	case DPMSModeSuspend:
	    DPMS_CHECK_TIMEOUT(DPMSOffTime)

	default: /* DPMSModeOff */
	    return 0;
    }
}
#endif /* DPMSExtension */

static CARD32
ScreenSaverTimeoutExpire(OsTimerPtr timer,CARD32 now,pointer arg)
{
    INT32 timeout      = now - lastDeviceEventTime.milliseconds;
    CARD32 nextTimeout = 0;

#ifdef DPMSExtension
    /*
     * Check each mode lowest to highest, since a lower mode can
     * have the same timeout as a higher one.
     */
    if (DPMSEnabled)
    {
	DPMS_CHECK_MODE(DPMSModeOff,     DPMSOffTime)
	DPMS_CHECK_MODE(DPMSModeSuspend, DPMSSuspendTime)
	DPMS_CHECK_MODE(DPMSModeStandby, DPMSStandbyTime)

	nextTimeout = NextDPMSTimeout(timeout);
    }

    /*
     * Only do the screensaver checks if we're not in a DPMS
     * power saving mode
     */
    if (DPMSPowerLevel != DPMSModeOn)
	return nextTimeout;
#endif /* DPMSExtension */

    if (!ScreenSaverTime)
	return nextTimeout;

    if (timeout < ScreenSaverTime)
    {
	return nextTimeout > 0 ? 
		min(ScreenSaverTime - timeout, nextTimeout) :
		ScreenSaverTime - timeout;
    }

    ResetOsBuffers(); /* not ideal, but better than nothing */
    dixSaveScreens(serverClient, SCREEN_SAVER_ON, ScreenSaverActive);

    if (ScreenSaverInterval > 0)
    {
	nextTimeout = nextTimeout > 0 ? 
		min(ScreenSaverInterval, nextTimeout) :
		ScreenSaverInterval;
    }

    return nextTimeout;
}

static OsTimerPtr ScreenSaverTimer = NULL;

void
FreeScreenSaverTimer(void)
{
    if (ScreenSaverTimer) {
	TimerFree(ScreenSaverTimer);
	ScreenSaverTimer = NULL;
    }
}

void
SetScreenSaverTimer(void)
{
    CARD32 timeout = 0;

#ifdef DPMSExtension
    if (DPMSEnabled)
    {
	/*
	 * A higher DPMS level has a timeout that's either less
	 * than or equal to that of a lower DPMS level.
	 */
	if (DPMSStandbyTime > 0)
	    timeout = DPMSStandbyTime;

	else if (DPMSSuspendTime > 0)
	    timeout = DPMSSuspendTime;

	else if (DPMSOffTime > 0)
	    timeout = DPMSOffTime;
    }
#endif

    if (ScreenSaverTime > 0)
    {
	timeout = timeout > 0 ?
		min(ScreenSaverTime, timeout) :
		ScreenSaverTime;
    }

#ifdef SCREENSAVER
    if (timeout && !screenSaverSuspended) {
#else
    if (timeout) {
#endif
	ScreenSaverTimer = TimerSet(ScreenSaverTimer, 0, timeout,
	                            ScreenSaverTimeoutExpire, NULL);
    }
    else if (ScreenSaverTimer) {
	FreeScreenSaverTimer();
    }
}

