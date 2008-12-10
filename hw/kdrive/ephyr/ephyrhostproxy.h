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

#ifndef __EPHYRHOSTPROXY_H__
#define __EPHYRHOSTPROXY_H__


struct XReply {
    int8_t type ;/*X_Reply*/
    int8_t foo;
    int16_t sequence_number ;
    int32_t length ;
    /*following is some data up to 32 bytes lenght*/
    int32_t pad0 ;
    int32_t pad1 ;
    int32_t pad2 ;
    int32_t pad3 ;
    int32_t pad4 ;
    int32_t pad5 ;
};

typedef struct _XProxyBuffer
{
  size_t   len;
  char	  *buf;
  char	  *pos;
  size_t   remaining;
} XProxyBuffer;

int
ephyrHostProxyOpenDisplay (void **host_xdpy);

void
ephyrHostProxyCloseDisplay (void *host_xdpy);

Bool
ephyrHostProxyGetGLXMajor (void *xdpy, int *major);

Bool
ephyrHostProxyDoForward (void *xdpy,
			 char *request_buffer,
			 u_int8_t major,
			 /* char *reply_buffer, */
			 Bool do_swap);

void ephyrHostProxyDoBackward (void *xdpy, ClientPtr client, XProxyBuffer *buf);

void ephyrHostProxyChangeClientIndex (void *_xdpy, ClientPtr client);

#endif /*__EPHYRHOSTPROXY_H__*/

