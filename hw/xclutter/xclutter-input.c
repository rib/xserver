#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "xclutter-xnest-client.h"
#include "xclutter-input.h"
#include "xclutter.h"

#include <clutter/x11/clutter-x11.h>

#include <X11/X.h>
#define NEED_EVENTS
#include "mi.h"
#include <X11/Xproto.h>
#include "scrnintstr.h"
#include "inputstr.h"
#include <X11/Xos.h>
#include "mibstore.h"
#include "mipointer.h"
//#include "lk201kbd.h"
#include <X11/keysym.h>
#include <dix.h>


/* FIXME - I'm not sure that XClutterMouse is needed; just
 * use DeviceIntPtr */
XClutterMouse xclutter_mouse;
DeviceIntPtr xclutter_keyboard;
EventListPtr xclutter_events = NULL;
KeySymsRec xclutter_key_syms;

/* FIXME: hack, it would be really helpfull */
extern void
_clutter_actor_apply_modelview_transform_recursive (ClutterActor *self,
						    ClutterActor *ancestor);

Bool
LegalModifier(unsigned int key, DeviceIntPtr pDev)
{
  return TRUE;
}

void
ProcessInputEvents()
{
  mieqProcessInputEvents();
}

void DDXRingBell (int volume, int pitch, int duration)
{
}

static int
xclutter_keyboard_proc (DeviceIntPtr pDevice, int onoff)
{
  /* KeySymsRec	keySyms; */
  CARD8 	modMap[MAP_LENGTH];
  DevicePtr	pDev = (DevicePtr)pDevice;
  int		i;

  switch (onoff)
    {
    case DEVICE_INIT:
      /* FIXME - we should be copying the modmap from the host xserver */
#if 0
      for (i = 0; i < MAP_LENGTH; i++)
	modMap[i] = NoSymbol;	/* make sure it is restored */
      modMap[KEY_LOCK] = LockMask;
      modMap[KEY_SHIFT] = ShiftMask;
      modMap[KEY_CTRL] = ControlMask;
      modMap[KEY_COMPOSE] = Mod1Mask;
#endif

      /* Use the keymap from the host xserver */
      xclutter_key_syms.map = (KeySym *)
	xclutter_xnest_load_keymap ((guint8 *)&xclutter_key_syms.minKeyCode,
				    (guint8 *)&xclutter_key_syms.maxKeyCode,
				    &xclutter_key_syms.mapWidth);
      InitKeyboardDeviceStruct (pDev, &xclutter_key_syms, modMap,
				(BellProcPtr)NoopDDA,
				(KbdCtrlProcPtr)NoopDDA);
      break;
    case DEVICE_ON:
      pDev->on = TRUE;
      break;
    case DEVICE_OFF:
      pDev->on = FALSE;
      break;
    case DEVICE_CLOSE:
      break;
    }
  return Success;
}

static int
xclutter_mouse_proc (DeviceIntPtr pDevice, int onoff)
{
  BYTE map[4];
  DevicePtr pDev = (DevicePtr)pDevice;

  switch (onoff)
    {
    case DEVICE_INIT:
      map[1] = 1;
      map[2] = 2;
      map[3] = 3;
      InitPointerDeviceStruct(pDev,
			      map,
			      3, /* num buttons */
			      (PtrCtrlProcPtr)NoopDDA,
			      GetMotionHistorySize(),
			      2); /* num axis */
      break;

    case DEVICE_ON:
      pDev->on = TRUE;
      break;

    case DEVICE_OFF:
      pDev->on = FALSE;
      break;

    case DEVICE_CLOSE:
      break;
    }
  return Success;
}

static void
enqueue_key_event (int type, unsigned int keycode)
{
  int i, n;

  GetEventList (&xclutter_events);
  /* XXX: I don't think this is needed */
  /* lastEventTime = GetTimeInMillis(); */
  n = GetKeyboardEvents (xclutter_events, xclutter_keyboard, type, keycode);
  for (i = 0; i < n; i++)
    mieqEnqueue (xclutter_keyboard, xclutter_events[i].event);
}

static void
update_modifier_state (unsigned int state)
{
  KeyClassPtr key_class = xclutter_keyboard->key;
  int i;
  CARD8 mask;

  state = state & 0xff;

  if (key_class->state == state)
    return;

  for (i = 0, mask = 1; i < 8; i++, mask <<= 1)
    {
      int key;

      /* Modifier is down, but shouldn't be
      */
      if ((key_class->state & mask) && !(state & mask))
	{
	  int count = key_class->modifierKeyCount[i];

	  for (key = 0; key < MAP_LENGTH; key++)
	    {
	      if (key_class->modifierMap[key] & mask)
		{
		  int bit;
		  BYTE *kptr;

		  kptr = &key_class->down[key >> 3];
		  bit = 1 << (key & 7);

		  if (*kptr & bit)
		    enqueue_key_event (KeyRelease, key);

		  if (--count == 0)
		    break;
		}
	    }
	}

      /* Modifier shoud be down, but isn't
       */
      if (!(key_class->state & mask) && (state & mask))
	for (key = 0; key < MAP_LENGTH; key++)
	  if (key_class->modifierMap[key] & mask) {
	      enqueue_key_event (KeyPress, key);
	      break;
	  }
    }
}

static ClutterX11FilterReturn
clutter_x11_event_filter (XEvent *xev, ClutterEvent *cev, gpointer data)
{
  gboolean need_to_enqueue_mouse_events = FALSE;
  int n_events, i;

  //g_printerr ("clutter_x11_event_filter\n");

  /* XXX: ask someone about this:
   * Note: some DDXs would update lastEventTime here, but I couldn't
   * see any users of the data. The function TimeSinceLastInputEvent
   * which lets you access the data, doesn't seem be called anywhere,
   * though lots of input drivers do call SetTimeSinceLastInputEvent.
   */

  switch (xev->type)
    {
    case MotionNotify:
	{
	  int valuators[2] = { xev->xmotion.x, xev->xmotion.y };
	  GetEventList(&xclutter_events);
	  n_events = GetPointerEvents (xclutter_events,
				       xclutter_mouse.dixdev,
				       MotionNotify,
				       0, /* buttons */
				       POINTER_ABSOLUTE,
				       0, /* first valuator */
				       2, /* n valuators */
				       valuators); /* valuators */
	  need_to_enqueue_mouse_events = TRUE;

	  break;
	}
    case ButtonPress:
    case ButtonRelease:
	{
	  GetEventList(&xclutter_events);
	  n_events = GetPointerEvents (xclutter_events,
				       xclutter_mouse.dixdev,
				       xev->type,
				       xev->xbutton.button,
				       POINTER_RELATIVE,
				       0, /* first valuator */
				       0, /* n valuators */
				       NULL); /* valuators */
	  need_to_enqueue_mouse_events = TRUE;
	}
    case KeyPress:
    case KeyRelease:
      update_modifier_state (xev->xkey.state);
      enqueue_key_event (xev->type, xev->xkey.keycode);
      break;
    }

  if (need_to_enqueue_mouse_events)
    for (i = 0; i < n_events; i++)
      mieqEnqueue (xclutter_mouse.dixdev, xclutter_events[i].event);

  return CLUTTER_X11_FILTER_CONTINUE;
}

/**
 * xclutter_xy_to_window:
 *
 * Replaces the DIX default XYToWindow implementation.
 *
 * Traverses from the root window to the window at the position x/y. While
 * traversing, it sets up the traversal history in the spriteTrace array.
 * After completing, the spriteTrace history is set in the following way:
 *   spriteTrace[0] ... root window
 *   spriteTrace[1] ... top level window that encloses x/y
 *       ...
 *   spriteTrace[spriteTraceGood - 1] ... window at x/y
 *
 * @returns the window at the given coordinates.
 */
static WindowPtr
xclutter_xy_to_window (DeviceIntPtr device, int x, int y)
{
  WindowPtr rootwin;
  WindowPtr window = NULL;
  BoxRec box;
  SpritePtr sprite;
  ScreenPtr screen;
  XClutterScreen *xclutter_screen;
  XClutterWindow *xclutter_window;
  ClutterActor *actor;

  sprite = device->spriteInfo->sprite;
  sprite->spriteTraceGood = 1;	/* root window still there */
  
  /* The root window is always the first entry... */
  rootwin = device->spriteInfo->sprite->spriteTrace[0];
  screen = rootwin->drawable.pScreen;
  xclutter_screen = GET_XCLUTTER_SCREEN (screen);

  actor = clutter_stage_get_actor_at_pos (
	    CLUTTER_STAGE (xclutter_screen->stage), x, y);
  xclutter_window = g_object_get_data (G_OBJECT (actor), "X_CLUTTER_WINDOW");
  if (xclutter_window)
    window = xclutter_window->window;

  while (window)
    {
      if ((window->mapped) &&
	  (x >= window->drawable.x - wBorderWidth (window)) &&
	  (x < window->drawable.x + (int)window->drawable.width +
	   wBorderWidth(window)) &&
	  (y >= window->drawable.y - wBorderWidth (window)) &&
	  (y < window->drawable.y + (int)window->drawable.height +
	   wBorderWidth (window))
#ifdef SHAPE
	  /* When a window is shaped, a further check
	   * is made to see if the point is inside
	   * borderSize
	   */
	  && (!wBoundingShape(window) || PointInBorderSize(window, x, y))
	  && (!wInputShape(window) ||
	      POINT_IN_REGION(window->drawable.pScreen,
			      wInputShape(window),
			      x - window->drawable.x,
			      y - window->drawable.y, &box))
#endif
	    )
	      {
		if (sprite->spriteTraceGood >= sprite->spriteTraceSize)
		  {
		    sprite->spriteTraceSize += 10;
		    sprite->spriteTrace =
		      (WindowPtr *)XNFrealloc (sprite->spriteTrace,
					       sprite->spriteTraceSize
					       * sizeof(WindowPtr));
		  }
		sprite->spriteTrace[sprite->spriteTraceGood++] = window;
		window = window->firstChild;
	      }
      else
	window = window->nextSib;
    }
  return sprite->spriteTrace[sprite->spriteTraceGood-1];
}


/* NB: Adapted from mtx_transform from clutter-actor.c ... */

#define MAT(m,r,c) (m)[(c)*4+(r)]

/* Transform point (x,y,z) by matrix */
static void
transform_matrix (GLfloat m[16],
		  int *x, int *y, int *z,
		  ClutterFixed *w)
{
    float _x, _y, _z, _w;
    _x = *x;
    _y = *y;
    _z = *z;
    _w = *w;

    _x = MAT (m, 0, 0) * _x + MAT (m, 0, 1) * _y +
	 MAT (m, 0, 2) * _z + MAT (m, 0, 3) * _w;

    _y = MAT (m, 1, 0) * _x + MAT (m, 1, 1) * _y +
	 MAT (m, 1, 2) * _z + MAT (m, 1, 3) * _w;

    _z = MAT (m, 2, 0) * _x + MAT (m, 2, 1) * _y +
	 MAT (m, 2, 2) * _z + MAT (m, 2, 3) * _w;

    _w = MAT (m, 3, 0) * _x + MAT (m, 3, 1) * _y +
	 MAT (m, 3, 2) * _z + MAT (m, 3, 3) * _w;

    *x = (int)_x;
    *y = (int)_y;
    *z = (int)_z;
    *w = (int)_w;
}

#undef MAT

/* NB: This function is copied from mesa/math/m_matrix.c */
static gboolean
invert_matrix_3d_general (float *in, float *out)
{
   //const GLfloat *in = mat->m;
   //GLfloat *out = mat->inv;
   float pos, neg, t;
   float det;

#define MAT(m,r,c) (m)[(c)*4+(r)]
#define SWAP_ROWS(a, b) { GLfloat *_tmp = a; (a)=(b); (b)=_tmp; }

   /* Calculate the determinant of upper left 3x3 submatrix and
    * determine if the matrix is singular.
    */
   pos = neg = 0.0;
   t =  MAT(in,0,0) * MAT(in,1,1) * MAT(in,2,2);
   if (t >= 0.0) pos += t; else neg += t;

   t =  MAT(in,1,0) * MAT(in,2,1) * MAT(in,0,2);
   if (t >= 0.0) pos += t; else neg += t;

   t =  MAT(in,2,0) * MAT(in,0,1) * MAT(in,1,2);
   if (t >= 0.0) pos += t; else neg += t;

   t = -MAT(in,2,0) * MAT(in,1,1) * MAT(in,0,2);
   if (t >= 0.0) pos += t; else neg += t;

   t = -MAT(in,1,0) * MAT(in,0,1) * MAT(in,2,2);
   if (t >= 0.0) pos += t; else neg += t;

   t = -MAT(in,0,0) * MAT(in,2,1) * MAT(in,1,2);
   if (t >= 0.0) pos += t; else neg += t;

   det = pos + neg;

   if (det*det < 1e-25)
      return FALSE;

   det = 1.0F / det;
   MAT(out,0,0) = (  (MAT(in,1,1)*MAT(in,2,2) - MAT(in,2,1)*MAT(in,1,2) )*det);
   MAT(out,0,1) = (- (MAT(in,0,1)*MAT(in,2,2) - MAT(in,2,1)*MAT(in,0,2) )*det);
   MAT(out,0,2) = (  (MAT(in,0,1)*MAT(in,1,2) - MAT(in,1,1)*MAT(in,0,2) )*det);
   MAT(out,1,0) = (- (MAT(in,1,0)*MAT(in,2,2) - MAT(in,2,0)*MAT(in,1,2) )*det);
   MAT(out,1,1) = (  (MAT(in,0,0)*MAT(in,2,2) - MAT(in,2,0)*MAT(in,0,2) )*det);
   MAT(out,1,2) = (- (MAT(in,0,0)*MAT(in,1,2) - MAT(in,1,0)*MAT(in,0,2) )*det);
   MAT(out,2,0) = (  (MAT(in,1,0)*MAT(in,2,1) - MAT(in,2,0)*MAT(in,1,1) )*det);
   MAT(out,2,1) = (- (MAT(in,0,0)*MAT(in,2,1) - MAT(in,2,0)*MAT(in,0,1) )*det);
   MAT(out,2,2) = (  (MAT(in,0,0)*MAT(in,1,1) - MAT(in,1,0)*MAT(in,0,1) )*det);

   /* Do the translation part */
   MAT(out,0,3) = - (MAT(in,0,3) * MAT(out,0,0) +
		     MAT(in,1,3) * MAT(out,0,1) +
		     MAT(in,2,3) * MAT(out,0,2) );
   MAT(out,1,3) = - (MAT(in,0,3) * MAT(out,1,0) +
		     MAT(in,1,3) * MAT(out,1,1) +
		     MAT(in,2,3) * MAT(out,1,2) );
   MAT(out,2,3) = - (MAT(in,0,3) * MAT(out,2,0) +
		     MAT(in,1,3) * MAT(out,2,1) +
		     MAT(in,2,3) * MAT(out,2,2) );


#undef MAT
#undef SWAP_ROWS

   return TRUE;
}


/**
 * Compute inverse of 4x4 transformation matrix.
 * 
 * \param mat pointer to a GLmatrix structure. The matrix inverse will be
 * stored in the GLmatrix::inv attribute.
 * 
 * \return GL_TRUE for success, GL_FALSE for failure (\p singular matrix).
 * 
 * \author
 * Code contributed by Jacques Leroy jle@star.be
 *
 * Calculates the inverse matrix by performing the gaussian matrix reduction
 * with partial pivoting followed by back/substitution with the loops manually
 * unrolled.
 */
#if 0
static gboolean
invert_matrix_general (const GLfloat *m, GLfloat *out)
{
   GLfloat wtmp[4][8];
   GLfloat m0, m1, m2, m3, s;
   GLfloat *r0, *r1, *r2, *r3;

   r0 = wtmp[0], r1 = wtmp[1], r2 = wtmp[2], r3 = wtmp[3];

#define MAT(m,r,c) (m)[(c)*4+(r)]
#define SWAP_ROWS(a, b) { GLfloat *_tmp = a; (a)=(b); (b)=_tmp; }

   r0[0] = MAT(m,0,0), r0[1] = MAT(m,0,1),
   r0[2] = MAT(m,0,2), r0[3] = MAT(m,0,3),
   r0[4] = 1.0, r0[5] = r0[6] = r0[7] = 0.0,

   r1[0] = MAT(m,1,0), r1[1] = MAT(m,1,1),
   r1[2] = MAT(m,1,2), r1[3] = MAT(m,1,3),
   r1[5] = 1.0, r1[4] = r1[6] = r1[7] = 0.0,

   r2[0] = MAT(m,2,0), r2[1] = MAT(m,2,1),
   r2[2] = MAT(m,2,2), r2[3] = MAT(m,2,3),
   r2[6] = 1.0, r2[4] = r2[5] = r2[7] = 0.0,

   r3[0] = MAT(m,3,0), r3[1] = MAT(m,3,1),
   r3[2] = MAT(m,3,2), r3[3] = MAT(m,3,3),
   r3[7] = 1.0, r3[4] = r3[5] = r3[6] = 0.0;

   /* choose pivot - or die */
   if (fabsf(r3[0])>fabsf(r2[0])) SWAP_ROWS(r3, r2);
   if (fabsf(r2[0])>fabsf(r1[0])) SWAP_ROWS(r2, r1);
   if (fabsf(r1[0])>fabsf(r0[0])) SWAP_ROWS(r1, r0);
   if (0.0 == r0[0])  return FALSE;

   /* eliminate first variable     */
   m1 = r1[0]/r0[0]; m2 = r2[0]/r0[0]; m3 = r3[0]/r0[0];
   s = r0[1]; r1[1] -= m1 * s; r2[1] -= m2 * s; r3[1] -= m3 * s;
   s = r0[2]; r1[2] -= m1 * s; r2[2] -= m2 * s; r3[2] -= m3 * s;
   s = r0[3]; r1[3] -= m1 * s; r2[3] -= m2 * s; r3[3] -= m3 * s;
   s = r0[4];
   if (s != 0.0) { r1[4] -= m1 * s; r2[4] -= m2 * s; r3[4] -= m3 * s; }
   s = r0[5];
   if (s != 0.0) { r1[5] -= m1 * s; r2[5] -= m2 * s; r3[5] -= m3 * s; }
   s = r0[6];
   if (s != 0.0) { r1[6] -= m1 * s; r2[6] -= m2 * s; r3[6] -= m3 * s; }
   s = r0[7];
   if (s != 0.0) { r1[7] -= m1 * s; r2[7] -= m2 * s; r3[7] -= m3 * s; }

   /* choose pivot - or die */
   if (fabsf(r3[1])>fabsf(r2[1])) SWAP_ROWS(r3, r2);
   if (fabsf(r2[1])>fabsf(r1[1])) SWAP_ROWS(r2, r1);
   if (0.0 == r1[1])  return FALSE;

   /* eliminate second variable */
   m2 = r2[1]/r1[1]; m3 = r3[1]/r1[1];
   r2[2] -= m2 * r1[2]; r3[2] -= m3 * r1[2];
   r2[3] -= m2 * r1[3]; r3[3] -= m3 * r1[3];
   s = r1[4]; if (0.0 != s) { r2[4] -= m2 * s; r3[4] -= m3 * s; }
   s = r1[5]; if (0.0 != s) { r2[5] -= m2 * s; r3[5] -= m3 * s; }
   s = r1[6]; if (0.0 != s) { r2[6] -= m2 * s; r3[6] -= m3 * s; }
   s = r1[7]; if (0.0 != s) { r2[7] -= m2 * s; r3[7] -= m3 * s; }

   /* choose pivot - or die */
   if (fabsf(r3[2])>fabsf(r2[2])) SWAP_ROWS(r3, r2);
   if (0.0 == r2[2])  return FALSE;

   /* eliminate third variable */
   m3 = r3[2]/r2[2];
   r3[3] -= m3 * r2[3], r3[4] -= m3 * r2[4],
   r3[5] -= m3 * r2[5], r3[6] -= m3 * r2[6],
   r3[7] -= m3 * r2[7];

   /* last check */
   if (0.0 == r3[3]) return FALSE;

   s = 1.0F/r3[3];             /* now back substitute row 3 */
   r3[4] *= s; r3[5] *= s; r3[6] *= s; r3[7] *= s;

   m2 = r2[3];                 /* now back substitute row 2 */
   s  = 1.0F/r2[2];
   r2[4] = s * (r2[4] - r3[4] * m2), r2[5] = s * (r2[5] - r3[5] * m2),
   r2[6] = s * (r2[6] - r3[6] * m2), r2[7] = s * (r2[7] - r3[7] * m2);
   m1 = r1[3];
   r1[4] -= r3[4] * m1, r1[5] -= r3[5] * m1,
   r1[6] -= r3[6] * m1, r1[7] -= r3[7] * m1;
   m0 = r0[3];
   r0[4] -= r3[4] * m0, r0[5] -= r3[5] * m0,
   r0[6] -= r3[6] * m0, r0[7] -= r3[7] * m0;

   m1 = r1[2];                 /* now back substitute row 1 */
   s  = 1.0F/r1[1];
   r1[4] = s * (r1[4] - r2[4] * m1), r1[5] = s * (r1[5] - r2[5] * m1),
   r1[6] = s * (r1[6] - r2[6] * m1), r1[7] = s * (r1[7] - r2[7] * m1);
   m0 = r0[2];
   r0[4] -= r2[4] * m0, r0[5] -= r2[5] * m0,
   r0[6] -= r2[6] * m0, r0[7] -= r2[7] * m0;

   m0 = r0[1];                 /* now back substitute row 0 */
   s  = 1.0F/r0[0];
   r0[4] = s * (r0[4] - r1[4] * m0), r0[5] = s * (r0[5] - r1[5] * m0),
   r0[6] = s * (r0[6] - r1[6] * m0), r0[7] = s * (r0[7] - r1[7] * m0);

   MAT(out,0,0) = r0[4]; MAT(out,0,1) = r0[5],
   MAT(out,0,2) = r0[6]; MAT(out,0,3) = r0[7],
   MAT(out,1,0) = r1[4]; MAT(out,1,1) = r1[5],
   MAT(out,1,2) = r1[6]; MAT(out,1,3) = r1[7],
   MAT(out,2,0) = r2[4]; MAT(out,2,1) = r2[5],
   MAT(out,2,2) = r2[6]; MAT(out,2,3) = r2[7],
   MAT(out,3,0) = r3[4]; MAT(out,3,1) = r3[5],
   MAT(out,3,2) = r3[6]; MAT(out,3,3) = r3[7];

#undef MAT
#undef SWAP_ROWS

   return TRUE;
}
#undef SWAP_ROWS
#endif

static void
print_matrix (const char *name, float *matrix)
{
  g_printerr ("%s\n", name);
  g_printerr ("%f %f %f %f\n",
	      matrix[0], matrix[1], matrix[2], matrix[3]);
  g_printerr ("%f %f %f %f\n",
	      matrix[4], matrix[5], matrix[6], matrix[7]);
  g_printerr ("%f %f %f %f\n",
	      matrix[8], matrix[9], matrix[10], matrix[11]);
  g_printerr ("%f %f %f %f\n",
	      matrix[12], matrix[13], matrix[14], matrix[15]);
  g_printerr ("\n");
}

#if 0
/* XXX: copied from clutter-actor.c */
static void
clutter_actor_apply_modelview_transform (ClutterActor *self)
{
  ClutterActorPrivate *priv = self->priv;
  gboolean             is_stage = CLUTTER_IS_STAGE (self);
  ClutterGeometry      allocation;
  gdouble	       scale_x, scale_y;
  gint		       anchor_x, anchor_y;
  gdouble	       angle;
  ClutterRotateAxis    axis;
  gint		       rx, ry, rz;
  
  clutter_actor_get_allocation_geometry (self, &allocation);
  clutter_actor_get_scale (self, &scale_x, &scale_y);
  clutter_actor_get_anchor_point (self, &anchor_x, &anchor_y);
  angle = clutter_actor_get_rotation (self,
				      &axis,
				      &rx,
				      &ry,
				      &rz);
  if (!is_stage)
    cogl_translatex (CLUTTER_UNITS_TO_FIXED (allocation.x1),
		     CLUTTER_UNITS_TO_FIXED (allocation.y1),
		     0);

  /*
   * because the rotation involves translations, we must scale before
   * applying the rotations (if we apply the scale after the rotations,
   * the translations included in the rotation are not scaled and so the
   * entire object will move on the screen as a result of rotating it).
   */
  if (scale_x != CFX_ONE || scale_y != CFX_ONE)
    cogl_scale (scale_x, scale_y);

   if (axis == CLUTTER_Z_AXIS)
    {
      cogl_translatex (CLUTTER_UNITS_TO_FIXED (x),
		       CLUTTER_UNITS_TO_FIXED (y),
		       0);

      cogl_rotatex (angle, 0, 0, CFX_ONE);

      cogl_translatex (CLUTTER_UNITS_TO_FIXED (-priv->rzx),
		       CLUTTER_UNITS_TO_FIXED (-priv->rzy),
		       0);
    }

  if (axis == CLUTTER_Y_AXIS)
    {
      cogl_translatex (CLUTTER_UNITS_TO_FIXED (x),
		       0,
		       CLUTTER_UNITS_TO_FIXED (priv->z/* FIXME */ + z));

      cogl_rotatex (angle, 0, CFX_ONE, 0);

      cogl_translatex (CLUTTER_UNITS_TO_FIXED (-z),
		       0,
		       CLUTTER_UNITS_TO_FIXED (-(priv->z/* FIXME */ + z)));
    }

  if (axis == CLUTTER_X_AXIS)
    {
      cogl_translatex (0,
		       CLUTTER_UNITS_TO_FIXED (y),
		       CLUTTER_UNITS_TO_FIXED (priv->z + z));

      cogl_rotatex (angle, CFX_ONE, 0, 0);

      cogl_translatex (0,
		       CLUTTER_UNITS_TO_FIXED (-y),
		       CLUTTER_UNITS_TO_FIXED (-(priv->z/* FIXME */ + z)));
    }

  if (!is_stage && (anchor_x || anchor_y))
    cogl_translatex (CLUTTER_UNITS_TO_FIXED (-anchor_x),
		     CLUTTER_UNITS_TO_FIXED (-anchor_y),
		     0);

  if (priv->z)
    cogl_translatex (0, 0, priv->z);
}
#endif


static void
xclutter_fixup_event_from_window (CallbackListPtr *list,
				  void *_null,
				  FixUpEventFromWindowInfoRec *fixupinfo)
{
#if 0
  WindowPtr rootwin;
  ScreenPtr screen;
  XClutterScreen *xclutter_screen;
#endif
  WindowPtr window;
  XClutterWindow *xclutter_window;
  ClutterVertex verts[4];
  ClutterFixed mtx[16];
  //int eventX, eventY, dummyZ, dummyW;
  int rootX, rootY, dummyZ, dummyW;
  int i;
  GLfloat float_matrix[16];
  GLfloat float_inverse[16];
  ClutterUnit actor_x, actor_y;
  gboolean success;

#if 0
  ClutterActor *actor;

  rootwin = pDev->spriteInfo->sprite->spriteTrace[0];
  screen = rootwin->drawable.pScreen;
  xclutter_screen = GET_XCLUTTER_SCREEN (screen);
#endif

  window = fixupinfo->pWin;
  xclutter_window = GET_XCLUTTER_WINDOW (window);
  if (!xclutter_window)
    return;

  rootX = fixupinfo->xE->u.keyButtonPointer.rootX;
  rootY = fixupinfo->xE->u.keyButtonPointer.rootY;
  success = 
    clutter_actor_transform_stage_point (xclutter_window->texture,
				       CLUTTER_UNITS_FROM_INT (rootX),
				       CLUTTER_UNITS_FROM_INT (rootY),
				       &actor_x,
				       &actor_y);
  if (!success)
    g_printerr ("clutter_actor_transform_stage_point FAIL!\n");
  fixupinfo->xE->u.keyButtonPointer.eventX = CLUTTER_UNITS_TO_INT (actor_x);
  fixupinfo->xE->u.keyButtonPointer.eventY = CLUTTER_UNITS_TO_INT (actor_y);
  g_printerr ("xclutter_fixup_event_from_window (root_x=%d, root_y=%d, x=%d, y=%d)\n",
	      fixupinfo->xE->u.keyButtonPointer.rootX,
	      fixupinfo->xE->u.keyButtonPointer.rootY,
	      CLUTTER_UNITS_TO_INT (actor_x),
	      CLUTTER_UNITS_TO_INT (actor_y));
#if 0
  /* NB:
   * v[0] contains (x1, y1)
   * v[1] contains (x2, y1)
   * v[2] contains (x1, y2)
   * v[3] contains (x2, y2)
   */
  clutter_actor_get_abs_allocation_vertices (xclutter_window->texture,
					     verts);
  /* Now that we have screen coordinates for the  */

  cogl_push_matrix();
  _clutter_actor_apply_modelview_transform_recursive (
      xclutter_window->texture, NULL);
  cogl_get_modelview_matrix (mtx);
  eventX = fixupinfo->xE->u.keyButtonPointer.rootX;
  eventY = fixupinfo->xE->u.keyButtonPointer.rootY;
  dummyZ = 0;
  dummyW = 1;
  for (i = 0; i < 16; i++)
    float_matrix[i] = CLUTTER_FIXED_TO_FLOAT (mtx[i]);
  if (invert_matrix_3d_general (float_matrix, float_inverse))
    {
      print_matrix ("float_matrix", float_matrix);
      print_matrix ("float_inverse", float_inverse);
      eventX=0;
      eventY=0;
      transform_matrix (float_matrix, &eventX, &eventY, &dummyZ, &dummyW);
      g_printerr ("DEBUG0: x=%d, y=%d\n", eventX, eventY);
      transform_matrix (float_inverse, &eventX, &eventY, &dummyZ, &dummyW);
      g_printerr ("DEBUG1: x=%d, y=%d\n", eventX, eventY);
     // transform_matrix (float_matrix, &eventX, &eventY, &dummyZ, &dummyW);
#if 0
      fixupinfo->xE->u.keyButtonPointer.eventX = eventX;
      fixupinfo->xE->u.keyButtonPointer.eventY = eventY;
#endif
      g_printerr ("xclutter_fixup_event_from_window (x=%d, y=%d)\n",
		  eventX, eventY);

    }

  cogl_pop_matrix();
#endif
}

void
InitInput (int argc, char *argv[])
{
  xclutter_mouse.dixdev =
    AddInputDevice(serverClient, xclutter_mouse_proc, TRUE);
  xclutter_keyboard =
    AddInputDevice(serverClient, xclutter_keyboard_proc, TRUE);
  RegisterPointerDevice(xclutter_mouse.dixdev);
  RegisterKeyboardDevice(xclutter_keyboard);
  (void)mieqInit();

  XYToWindowDDX = xclutter_xy_to_window;

  clutter_x11_add_filter (clutter_x11_event_filter, NULL);
  AddCallback (&FixUpEventFromWindowCallback,
	       xclutter_fixup_event_from_window,
	       NULL);
}

