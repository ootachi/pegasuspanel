#ifndef PP_PANEL_H
#define PP_PANEL_H

#include <X11/Xlib.h>
#include <gtk/gtk.h>

struct PPPanel {
    GtkWindow *window;
	GtkBox *hbox;
    GtkMenuBar *menu;
	GtkBox *tray;
    Atom opcode_atom;
};
typedef struct PPPanel PPPanel;

#endif

