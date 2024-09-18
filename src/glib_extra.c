/*
 * Things we wish were in glib.
 * $Log: glib_extra.c,v $
 * Revision 1.1  2002/02/25 04:53:35  sgt
 * initial checkin of autoconfiscated and renamed system
 *
 * Revision 1.1  1998/11/09 02:47:21  tell
 * Initial revision
 *
 */

#include <glib.h>

void *
g_list_shift(GList **gl)
{
	void *d;
	if(*gl) {
		d = (g_list_first(*gl)->data);
		*gl = g_list_remove(*gl, d);
		return d;
	} else
		return NULL;
}
