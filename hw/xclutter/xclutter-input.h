#ifndef _XCLUTTER_INPUT_H_
#define _XCLUTTER_INPUT_H_

#include "inputstr.h"

typedef struct _XClutterMouse
{
  DeviceIntPtr dixdev;
} XClutterMouse;


extern XClutterMouse xclutter_mouse;
extern EventListPtr xclutter_events;
extern KeySymsRec xclutter_key_syms;

#endif /* _XCLUTTER_INPUT_H_ */

