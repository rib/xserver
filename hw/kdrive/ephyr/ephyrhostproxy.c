/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 *
 * Copyright © 2007 OpenedHand Ltd
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
 */

#include <X11/Xproto.h>
#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include <X11/Xlibint.h>
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"
#include "ephyrhostproxy.h"
#include "hostx.h"
#include "GL/glx.h"
#include <poll.h>
#include <unistd.h>
#include <unistd.h>
#include <fcntl.h>
#include <dixstruct.h>

/* byte swap a short */
#define swaps(x, n) { \
  n = ((char *) (x))[0];\
  ((char *) (x))[0] = ((char *) (x))[1];\
  ((char *) (x))[1] = n; }

#if 0
#define GetXReq(req) \
    WORD64ALIGN ;\
    if ((dpy->bufptr + SIZEOF(xReq)) > dpy->bufmax)\
            COPYING_XFlush(dpy);\
    req = (xReq *)(dpy->last_req = dpy->bufptr);\
    dpy->bufptr += SIZEOF(xReq);\
    dpy->request++
#endif

#define GetXReq(req, bytes) \
  WORD64ALIGN\
  if ((xdpy->bufptr + (bytes)) > xdpy->bufmax)\
    _XFlush(xdpy);\
  req = (xReq *)(xdpy->last_req = xdpy->bufptr);\
  xdpy->bufptr += (bytes);\
  xdpy->request++

#if 0
typedef struct {
    char type;                  /* X_Error */
    char errorCode;
    uint16_t sequenceNumber;/* the nth request from this client */
    uint32_t resourceID;
    uint16_t minorCode;
    uint8_t majorCode;
    uint8_t pad1;
    uint32_t pad3;
    uint32_t pad4;
    uint32_t pad5;
    uint32_t pad6;
    uint32_t pad7;
} XError;
#endif


/* NB: We need a dedicated connection since we do no inturpretation
 * of data being passed back to the client from the host x server.
 * This wouldn't work if we also needed to use the connection for
 * other misc host server queries. 
 *
 * In particular, it is desireable to not need to interpret the
 * the data we pass back since we aim to support proxying of
 * extensions that are not specified. */
int
ephyrHostProxyOpenDisplay (void **host_xdpy)
{
  int fd;
  Display *proxy_xdpy;
  long flags;

  proxy_xdpy = XOpenDisplay (NULL);
  if (!proxy_xdpy)
    ErrorF("Failed to open X connection for proxying requests\n");
  
  /* When proxying data we don't want to block when trying to read
   * replys/events from the host x server. */
  /* Note: It looks like xcb already uses non blocking IO, but since we
   * depend on that, we explicitly ensure it too */
  fd = XConnectionNumber (proxy_xdpy);

  flags = fcntl (fd, F_GETFD);
  if (flags != -1)
    fcntl (fd, F_SETFL, flags | O_NONBLOCK);
  else
    ErrorF ("Failed to read X connection fd flags\n");
    /* It's probably opened O_NONBLOCK anyway, so just continue */
  
  *host_xdpy = proxy_xdpy;
  return fd;
}

void
ephyrHostProxyCloseDisplay (void *xdpy)
{
  XCloseDisplay ((Display *)xdpy);
}

Bool
ephyrHostProxyGetGLXMajor (void *_xdpy, int *major)
{
  Display *xdpy = _xdpy;
  int first_event, first_error;

  return XQueryExtension (xdpy, "GLX", major, &first_event, &first_error);
}

void
ephyrHostProxyDoBackward (void *_xdpy,
			  ClientPtr client,
			  XProxyBuffer *do_backward_buf)
{
  Display	*xdpy = _xdpy;
  struct pollfd  fd;
  int		 ret;

  fd.fd = XConnectionNumber (xdpy);
  fd.events = POLLIN;
  
read_more: /* We can jump here to try reading the rest of a reply */

  /* NB: we are using non blocking IO */
  while ((ret = read (fd.fd, do_backward_buf->pos,
		      do_backward_buf->remaining)
	 ) == -1
	 && errno == EINTR)
    ; /* */
  if (ret == -1)
    {
      if (errno == EAGAIN)
	return;
      else
	{
	  perror ("ephyrHostProxyDoBackward read");
	  return;
	}
    }

  do_backward_buf->pos += ret;
  do_backward_buf->remaining -= ret;

  if (do_backward_buf->remaining != 0)
    return;

  if (do_backward_buf->buf[0] == 0)
    {
      /* Errors */
      xError *error = (xError *)do_backward_buf->buf;
      error->sequenceNumber = client->sequence;
      WriteToClient(client, 32, do_backward_buf->buf);
    }
  else if (do_backward_buf->buf[0] == 1)
    {
      /* Replys */
      xGenericReply *reply = (xGenericReply *)do_backward_buf->buf;
      /* NB: replys are defined as at least 32 bytes long and the
       * length is the number of additional 4 byte words. */
      size_t reply_len_bytes = 32 + (reply->length * 4);

      /* additional data to read */
      if  (reply->length)
	{
	  if (do_backward_buf->len < reply_len_bytes)
	    {
	      int pos = do_backward_buf->pos - do_backward_buf->buf;
	      do_backward_buf->buf =
		realloc (do_backward_buf->buf, reply_len_bytes);
	      reply = (xGenericReply *)do_backward_buf->buf;
	      do_backward_buf->pos = do_backward_buf->buf + pos;
	      do_backward_buf->len = reply_len_bytes;
	    }
	  if ((do_backward_buf->pos - do_backward_buf->buf)
	       < reply_len_bytes)
	    {
	      do_backward_buf->remaining = reply->length * 4;
	      goto read_more;
	    }
	}
      reply->sequenceNumber = client->sequence;
      WriteToClient (client, reply_len_bytes, do_backward_buf->buf);
    }
  else
    {
      /* Events */
      xEvent *event = (xEvent *)do_backward_buf->buf;
      event->u.u.sequenceNumber = client->sequence;
      WriteToClient (client, 32, do_backward_buf->buf);
    }

  /* reset - ready for the next data */
  do_backward_buf->remaining = 32;
  do_backward_buf->pos = do_backward_buf->buf;
}

Bool
ephyrHostProxyDoForward (void *_xdpy,
			 char *request_buffer,
			 u_int8_t major,
			 /* char *reply_buffer, */
			 Bool do_swap)
{
  Display *xdpy = _xdpy;
  Bool is_ok = FALSE ;
  int n=0 ;
  xReq *in_req = (xReq*) request_buffer ;
  xReq *forward_req = NULL ;
  int fd = XConnectionNumber (xdpy);
  /* struct XReply reply; */
  /* int n_longs = 0; */

  EPHYR_RETURN_VAL_IF_FAIL (in_req && xdpy, FALSE) ;

  EPHYR_LOG ("enter\n") ;

  if (do_swap) {
      swaps (&in_req->length, n) ;
  }
  EPHYR_LOG ("Req {type:%d, data:%d, length:%d}\n",
	     in_req->reqType, in_req->data, in_req->length) ;
#if 0//Foo
  LockDisplay (xdpy);
  GetXReq (forward_req, (in_req->length<<2));
  memcpy (forward_req, in_req, (in_req->length<<2));
  forward_req->reqType = major;
#else
  in_req->reqType = major;
  write (fd, in_req, (in_req->length<<2));
#endif
  
#if 0
  /* FIXME - not all requests have a reply and if they dont this
   * causes us to block indefinately! */
  if (!_XReply (xdpy, (xReply*) &reply, 0, FALSE)) {
      EPHYR_LOG_ERROR ("failed to get reply\n") ;
      UnlockDisplay (xdpy);
      SyncHandle ();
      goto out;
  }
  EPHYR_LOG ("XReply{type:%d, foo:%d, seqnum:%d, length:%d}\n",
	     reply.type, reply.foo, reply.sequence_number, reply.length) ;

  memcpy (reply_buffer, &reply, sizeof (xReply));

  if (reply.length)
    {
      int length = reply.length * 4;
      char *dst = (char *)(reply_buffer + sizeof (reply));

      _XRead(xdpy, dst, length);
    }
#endif

#if 0//Foo
  UnlockDisplay (xdpy);
#endif

  is_ok = TRUE ;

  //XFlush (xdpy);
  //XSync (xdpy, FALSE);

/* out: */
  EPHYR_LOG ("leave\n") ;
  return is_ok ;
}

/**
 * ephyrHostProxyChangeClientIndex:
 * _xdpy: A libX11 Display * pointer
 * client: A new client
 *
 * This function shuffles the client->index so that the associated
 * XIDs will fall into the resource range alotted to our proxy
 * connection to the host X server. This is so any XIDs generated
 * by a client of us for new resources may be passed on the host
 * X server un-touched.
 */
void
ephyrHostProxyChangeClientIndex (void *_xdpy, ClientPtr client)
{
  Display *xdpy = _xdpy;
  int client_index;
  
  /* This approach is not ideal. Currently this function is called
   * via a callback registerd via AddCallback for client state
   * changes. It's possible that other components get notified of
   * the new client before us, and it's possible they would
   * expect the client index to remain static. */

  client_index = xdpy->resource_base >> CLIENTOFFSET;
  
  if (!client->index)
    {
      ErrorF ("ephyrHostProxySetClientAsMask: "
	      "shouldn't be called for the server client");
      return;
    }

  if (client->index != client_index)
    {
      if (!clients[client_index])
	{
	  clients[client_index] = client;
	  clients[client->index] = NULL;

	  client->index = client_index;
	  client->clientAsMask = xdpy->resource_base;
	}
      else
	ErrorF ("Unexpect client index crash. "
		"This will probably break extension proxying\n");
    }
}

