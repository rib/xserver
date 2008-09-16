#ifndef _XCLUTTER_XNEST_CLIENT_H_
#define _XCLUTTER_XNEST_CLIENT_H_

#include <glib.h>

void xclutter_xnest_init (void *screen);

guint32 *xclutter_xnest_load_keymap (guint8 *min_keycode,
				     guint8 *max_keycode,
				     int *width);

#endif /* _XCLUTTER_XNEST_CLIENT_H_ */

