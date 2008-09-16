
#include "xclutter-xnest-client.h"

#include <clutter/x11/clutter-x11.h>
#include <glib.h>

#include <string.h>

void
xclutter_xnest_init (void *screen)
{

}

guint32 *
xclutter_xnest_load_keymap (guint8 *min_keycode_return,
			    guint8 *max_keycode_return,
			    int *width)
{
  Display *xdpy;
  guint32 *xhost_keymap;
  guint32 *keymap;
  int	   keycode_count;
  gsize	   keymap_size;
  int	   min_keycode = 0, max_keycode = 0;

  g_return_val_if_fail (min_keycode_return != NULL, NULL);
  g_return_val_if_fail (max_keycode_return != NULL, NULL);
  g_return_val_if_fail (width != NULL, NULL);

  xdpy = clutter_x11_get_default_display ();

  XDisplayKeycodes (xdpy, &min_keycode, &max_keycode);
  *min_keycode_return = (guint8)min_keycode;
  *max_keycode_return = (guint8)max_keycode;

  keycode_count = max_keycode - min_keycode + 1;

  xhost_keymap =
    (guint32 *)XGetKeyboardMapping (xdpy,
				    min_keycode,
				    keycode_count,
				    width);

  keymap_size = keycode_count * (*width) * sizeof (guint32);
  keymap = g_malloc (keymap_size);
  memcpy (keymap, xhost_keymap, keymap_size);

  XFree (xhost_keymap);

  return keymap;
}


