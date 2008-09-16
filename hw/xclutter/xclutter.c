//#include <X11/X.h>
//#include "dix.h"

#include <X11/Xproto.h>

#include "scrnintstr.h"
#include "windowstr.h"
#include "servermd.h"
#include "compint.h"

#include "xclutter.h"
#include "xclutter-xnest-client.h"
#include "xclutter-input.h"

#include <clutter/clutter.h>
#include <cogl/cogl.h>

/*
 * FIXME - hacky <clutter/x11/clutter-x11.h> replacement
 */

/* Yet another macro, but hopfully more readable. */
#define SCREEN_GET_ROOT_WINDOW(SCREEN) WindowTable[SCREEN->myNum]

#if 0
typedef enum {
  CLUTTER_X11_FILTER_CONTINUE,
  CLUTTER_X11_FILTER_TRANSLATE,
  CLUTTER_X11_FILTER_REMOVE
} ClutterX11FilterReturn;

typedef ClutterX11FilterReturn (*ClutterX11FilterFunc) (XEvent        *xev,
                                                        ClutterEvent  *cev,
                                                        gpointer       data);
extern void clutter_x11_add_filter (ClutterX11FilterFunc func,
				    gpointer data);
#endif


DevPrivateKey xclutter_screen_key = &xclutter_screen_key;
DevPrivateKey xclutter_window_key = &xclutter_window_key;

static gboolean minicm_running = FALSE;

#if 0
/* XXX: Yuk! */
static GList *all_windows_list;
#endif


/* FIXME - hacky extern define */
extern KeySymsRec xclutter_key_syms;




/* FIXME - these declarations are a bit hacky! */
//extern XClutterMouse xclutter_mouse;
//extern EventListPtr xclutter_events;

/* These are used instead of CompositeNamePixmap */
static PixmapPtr
window_get_pixmap (WindowPtr window)
{
  PixmapPtr pixmap;

  pixmap = (*window->drawable.pScreen->GetWindowPixmap) (window);
  if (!pixmap)
    return NULL;

  ++pixmap->refcnt;

  return pixmap;
}

static void
pixmap_unref (PixmapPtr pixmap)
{
  /* NB: DestroyPixmap decrements pixmap->refcnt, and is a nop
   * until refcnt == 0 */
  (*pixmap->drawable.pScreen->DestroyPixmap) (pixmap);
}

static void
xclutter_window_update_pixmap (XClutterWindow *xclutter_window)
{
  WindowPtr window = xclutter_window->window;

  if (xclutter_window->pixmap)
    {
#if 0
      /* FIXME - no need to repeatedly re-register the damage
       * now that we are registering against the window and
       * not the pixmap! */
      DamageUnregister (&xclutter_window->window->drawable,
			xclutter_window->damage);
      DamageEmpty (xclutter_window->damage);
#endif
      pixmap_unref (xclutter_window->pixmap);
    }
  xclutter_window->pixmap = window_get_pixmap (window);
  //DamageRegister (&window->drawable, xclutter_window->damage);
}

static void
xclutter_window_update_texture (XClutterWindow *xclutter_window,
				int _x, int _y,
				unsigned int _width,
				unsigned int _height)
{
  PixmapPtr pixmap = xclutter_window->pixmap;
  ScreenPtr screen = xclutter_window->window->drawable.pScreen;
  //XClutterScreen *xclutter_screen = GET_XCLUTTER_SCREEN (screen);
  ClutterActor *texture = xclutter_window->texture;
  int length;

  pixmap = (*screen->GetWindowPixmap) (xclutter_window->window);

  length = PixmapBytePad (pixmap->drawable.width,
			  pixmap->drawable.depth)
    * pixmap->drawable.height;

  if (length)
    {
      /* FIXME - obviously this is a hacky approach for now of just
       * trying to *full* texture data each time we get a new
       * damage event */

      char *data = g_malloc (length);
      screen->GetImage (&pixmap->drawable, 0, 0,
			pixmap->drawable.width,
			pixmap->drawable.height,
			ZPixmap, ~0L,
			data);
      //g_printerr ("get image 0x%08lx w=%d h=%d\n",
	//	  pixmap, pixmap->drawable.width, pixmap->drawable.height);

      if (pixmap->drawable.depth == 24)
	{
	  guint xpos, ypos;

	  for (ypos=0; ypos<pixmap->drawable.height; ypos++)
	    for (xpos=0; xpos<pixmap->drawable.width; xpos++)
	      {
		char *p = data + pixmap->drawable.width * 4 * ypos
			  + xpos * 4;
		p[3] = 0xFF;
	      }
	}

      //memset (data, rand() % 0xff, length);

      if (CLUTTER_IS_TEXTURE (texture))
	{
	  //g_printerr ("uploading new texture\n");
	  if (!clutter_texture_set_from_rgb_data (
		    CLUTTER_TEXTURE (texture),
		    (unsigned char *)data,
		    TRUE,
		    pixmap->drawable.width,
		    pixmap->drawable.height,
		    4 * pixmap->drawable.width, /* FIXME - HACK */
		    4,
		    CLUTTER_TEXTURE_RGB_FLAG_BGR,
		    NULL))
	    {
	      g_printerr ("Failed to set rgb data from pixmap\n");
	    }
	}

      g_free (data);
    }
}

static Bool
xclutter_create_window (WindowPtr window)
{
  ScreenPtr screen = window->drawable.pScreen;
  XClutterScreen *xclutter_screen = GET_XCLUTTER_SCREEN(screen);

  if (window == SCREEN_GET_ROOT_WINDOW (screen))
    {
      ClutterActor *stage = xclutter_screen->stage;

      compRedirectSubwindows (serverClient,
			      window,
			      CompositeRedirectManual);

      //xclutter_screen->rootwin = window;
      xclutter_screen->background =
	clutter_texture_new_from_file (
	  "/home/bob/var/backgrounds/animorph_12x10.jpg",
	  NULL);
	  /* XXX: I dont see a hack.. nope, not here.. */
      if (xclutter_screen->background)
	clutter_actor_set_size (xclutter_screen->background,
				  clutter_actor_get_width (stage),
				  clutter_actor_get_height (stage));
      else
	xclutter_screen->background = clutter_rectangle_new ();

      xclutter_screen->group = clutter_group_new ();
      clutter_container_add_actor (CLUTTER_CONTAINER(xclutter_screen->stage),
				   xclutter_screen->background);
      clutter_container_add_actor (CLUTTER_CONTAINER(xclutter_screen->stage),
				   xclutter_screen->group);
    }

#if 0
  /* minicm creates a token window @ (-100,-100) with size 13x13
   * when this is created then we know minicm is running and all
   * children of the root window have been marked for manual
   * redirection with the composite extension. */
  /* NB: we currently need a dummy composite manager to redirect
   * windows because internally the composite code currently provides
   * no way to redirect window without an associated pClient.
   */
  if (window->drawable.x == -100
      && window->drawable.y == -100
      && window->drawable.width == 13
      && window->drawable.height == 13)
    {
      minicm_running = TRUE;
      g_printerr ("minicm is running\n");
    }
#endif

  xclutter_screen->CreateWindow (window);

#if 0
  xcm_window = g_new0 (XCMWindow, 1);
  xcm_window->pixmap =
    composite_name_pixmap (wClient (window));
  all_windows_list = g_list_prepend (all_windows_list, xcm_window);
#endif

  /* TODO: allocate an offscreen non-overlapping position for the window. */
  /* TODO: immediatly map the window */
  //MapWindow(window, serverClient);

  return TRUE;
}

static Bool
xclutter_destroy_window (WindowPtr window)
{
  ScreenPtr screen = window->drawable.pScreen;
  XClutterScreen *xclutter_screen = GET_XCLUTTER_SCREEN(screen);
  XClutterWindow *xclutter_window = GET_XCLUTTER_WINDOW(window);

  if (window->drawable.x == -100
      && window->drawable.y == -100
      && window->drawable.width == 13
      && window->drawable.height == 13)
    {
      minicm_running = FALSE;
      g_printerr ("minicm is no longer running\n");
    }

  xclutter_screen->DestroyWindow (window);

  if (xclutter_window)
    {
      clutter_actor_destroy (xclutter_window->texture);
      dixSetPrivate (&window->devPrivates, xclutter_window_key, NULL);
      g_free (xclutter_window);
    }

  return TRUE;
}

static void
xclutter_report_damage (DamagePtr damage,
			RegionPtr region,
			void *userdata)
{
  //PixmapPtr pixmap = userdata;
  //XClutterWindow *xclutter_window = GET_XCLUTTER_WINDOW (pixmap);
  XClutterWindow *xclutter_window = userdata;

  //g_printerr ("damage! pixmap=0x%08lx\n", (unsigned long)pixmap);

  /* FIXME - brute force hack */
  //xclutter_window_update_pixmap (xclutter_window);

  xclutter_window_update_texture (xclutter_window,
				  region->extents.x1,
				  region->extents.y1,
				  region->extents.x2 - region->extents.x1,
				  region->extents.y2 - region->extents.y1);
}

static ClutterActor *
xclutter_new_window_actor (XClutterWindow *xclutter_window)
{
  WindowPtr	   window = xclutter_window->window;
  ClutterActor	   *texture;
  ClutterTimeline  *timeline;
  ClutterAlpha     *alpha;
  ClutterBehaviour *r_behave;

  texture = clutter_texture_new ();

  //clutter_actor_set_opacity (texture, 0xff/2);
  clutter_actor_set_position (texture, window->drawable.x,
			      window->drawable.y);
  clutter_actor_set_size (texture, window->drawable.width,
			  window->drawable.height);

  timeline = clutter_timeline_new (200, 26); /* num frames, fps */
  g_object_set (timeline, "loop", TRUE, NULL);

  /* Set an alpha func to power behaviour - ramp is constant rise/fall */
  alpha = clutter_alpha_new_full (timeline,
                                  CLUTTER_ALPHA_RAMP_INC,
                                  NULL, NULL);

  /* Create a behaviour for that alpha */
  r_behave = clutter_behaviour_rotate_new (alpha,
                                           CLUTTER_Z_AXIS,
                                           CLUTTER_ROTATE_CW,
                                           0.0, 360.0);

  clutter_behaviour_rotate_set_center (CLUTTER_BEHAVIOUR_ROTATE (r_behave),
                                       window->drawable.width/2,
				       window->drawable.height/2, 0);

  /* Apply it to our actor */
  clutter_behaviour_apply (r_behave, texture);

  /* start the timeline and thus the animations */
  clutter_timeline_start (timeline);

#if 0
  clutter_actor_set_rotation (texture, CLUTTER_Z_AXIS, 45,
			      window->drawable.width/2,
			      window->drawable.height/2,
			      0);
#endif

  g_object_set_data (G_OBJECT (texture), "X_CLUTTER_WINDOW", xclutter_window);

  /* FIXME - lots of leaked stuff */
  return texture;
}

static Bool
xclutter_realize_window (WindowPtr window)
{
  ScreenPtr screen = window->drawable.pScreen;
  XClutterScreen *xclutter_screen = GET_XCLUTTER_SCREEN(screen);
  PixmapPtr pixmap;
  ClutterActor *window_actor;
  XClutterWindow *xclutter_window;
  ClutterColor clr;
  int random = rand();
  Bool status;
  DamagePtr damage;
  ClutterActor *texture;

  status = xclutter_screen->RealizeWindow (window);
  if (!status) /* TODO - just guessing this is the right thing todo? */
    return 0;

  if (window->parent != SCREEN_GET_ROOT_WINDOW (screen))
    return status;

  clr.red = (random % 0xff);
  clr.green = (random % 0xff);
  clr.blue = (random % 0xff);
  clr.alpha = 0xff;

#if 0
  pixmap = screen->GetWindowPixmap (window);
  g_printerr ("pixmap=0x%08lx\n", (unsigned long)pixmap);
#endif

  xclutter_window = g_new0 (XClutterWindow, 1);
  xclutter_window->window = window;

//  dixSetPrivate (&pixmap->devPrivates, xclutter_window_key, xclutter_window);

  dixSetPrivate (&window->devPrivates, xclutter_window_key, xclutter_window);

  damage = DamageCreate (xclutter_report_damage,
			 NULL,
			 DamageReportRawRegion,
			 TRUE,
			 screen,
			 xclutter_window);
  if (!damage)
    {
      g_printerr ("Failed to create damage!\n");
      /* FIXME handle properly!*/
      return FALSE;
    }
  DamageSetReportAfterOp (damage, TRUE);
  xclutter_window->damage = damage;

  DamageRegister (&window->drawable, xclutter_window->damage);

  //xclutter_window_update_pixmap (xclutter_window);

  texture = xclutter_new_window_actor (xclutter_window);
  //texture = clutter_texture_new ();
  //texture = clutter_rectangle_new_with_color (&clr);
  xclutter_window->texture = texture;
  clutter_container_add_actor (CLUTTER_CONTAINER(xclutter_screen->stage),
			       texture);

  xclutter_window_update_texture (xclutter_window,
				  0, 0,
				  window->drawable.width,
				  window->drawable.height
  );



  clutter_actor_show (window_actor);

  g_printerr ("realize window\n");

  return TRUE;
}

static void
xclutter_resize_window (WindowPtr window,
			int x,
			int y,
			unsigned int width,
			unsigned int height,
			WindowPtr sibling) /* XXX ? */
{
  ScreenPtr screen = window->drawable.pScreen;
  XClutterScreen *xclutter_screen = GET_XCLUTTER_SCREEN(screen);

  xclutter_screen->ResizeWindow (window, x, y, width, height, sibling);

  g_printerr ("resize window\n");
}

static void
new_frame_cb (ClutterTimeline *timeline,
	      gint frame_num,
	      gpointer data)
{
  //g_printerr ("new frame\n");
}

void
xclutter_init_screen (ScreenPtr screen)
{
  ClutterActor *stage;
  XClutterScreen *xclutter_screen;
  ClutterColor stage_clr = { 0xff, 0x00, 0x00, 0xff};
  static int initialised = 0;

  /* FIXME - hack */
  if (initialised)
    return;
  initialised = TRUE;

  g_printerr ("init screen\n");

  clutter_init (NULL, NULL);

  /* xclutter_xnest_init (screen); */

  xclutter_key_syms.map = (KeySym *)
    xclutter_xnest_load_keymap ((guint8 *)&xclutter_key_syms.minKeyCode,
				(guint8 *)&xclutter_key_syms.maxKeyCode,
				&xclutter_key_syms.mapWidth);

  stage = clutter_stage_get_default ();
  clutter_actor_set_size (stage, screen->width, screen->height);
  clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_clr);

  xclutter_screen = g_new0 (XClutterScreen, 1);
  xclutter_screen->stage = stage;
  dixSetPrivate (&screen->devPrivates, xclutter_screen_key, xclutter_screen);

  WRAP (screen, CreateWindow, xclutter_create_window);
  WRAP (screen, DestroyWindow, xclutter_destroy_window);
  WRAP (screen, RealizeWindow, xclutter_realize_window);
  WRAP (screen, ResizeWindow, xclutter_resize_window);

  /* FIXME - debug */
  clutter_actor_set_reactive (stage, TRUE);
  xclutter_screen->timeline =
    clutter_timeline_new (100, /* count */
			  10); /* fps */
  clutter_timeline_set_loop (xclutter_screen->timeline, TRUE);
  g_signal_connect (G_OBJECT(xclutter_screen->timeline),
		    "new-frame",
		    G_CALLBACK(new_frame_cb),
		    xclutter_screen);
  //clutter_timeline_advance (xclutter_screen->timeline, 50);
  //clutter_timeline_stop (xclutter_screen->timeline);
  clutter_timeline_start (xclutter_screen->timeline);



  clutter_actor_show (stage);
}


