#ifndef _XCLUTTER_H_
#define _XCLUTTER_H_

#include "inputstr.h"
//#include <screenint.h>
#include "scrnintstr.h"
#include "damage.h"
#include "windowstr.h"

#include <clutter/clutter.h>

typedef struct
{
  ClutterActor *stage;

  //WindowPtr rootwin;

  /* NB: They have to be named as in struct _Screen for
   * the above WRAP macro to work. */
  CreateWindowProcPtr CreateWindow;
  DestroyWindowProcPtr DestroyWindow;
  RealizeWindowProcPtr RealizeWindow;
  ResizeWindowProcPtr ResizeWindow;

  ClutterTimeline *timeline;

  ClutterActor *background;
  ClutterActor *group; /* group to put all children of the root window in */

} XClutterScreen;

typedef struct
{
  WindowPtr window;
  ClutterActor *texture;
  PixmapPtr pixmap;
  DamagePtr damage;

} XClutterWindow;


//Bool xclutter_create_window (WindowPtr window);

void xclutter_init_screen (ScreenPtr pScreen);

extern DevPrivateKey xclutter_screen_key;
extern DevPrivateKey xclutter_window_key;

#define UNWRAP(SCREEN, FUNC_NAME) \
  (SCREEN)->FUNC_NAME = GET_XCLUTTER_SCREEN(SCREEN)->FUNC_NAME

#define WRAP(SCREEN, FUNC_NAME, FUNCTION) \
  do{ \
      GET_XCLUTTER_SCREEN(SCREEN)->FUNC_NAME = (SCREEN)->FUNC_NAME; \
      (SCREEN)->FUNC_NAME = FUNCTION; \
  } while (0)

#define GET_XCLUTTER_SCREEN(SCREEN) \
  ((XClutterScreen *)dixLookupPrivate (&(SCREEN)->devPrivates, xclutter_screen_key))

#define GET_XCLUTTER_WINDOW(DRAWABLE) \
  ((XClutterWindow *)dixLookupPrivate (&(DRAWABLE)->devPrivates, xclutter_window_key))

#endif /* _XCLUTTER_H_ */

