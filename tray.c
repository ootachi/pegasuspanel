/*
 * pegasuspanel
 *
 * tray.c - systray logic
 *
 * Much of this code is based on XFCE's xfce-tray-manager.c, which is in turn
 * based on the GNOME system tray panel applet.
 */

#include "panel.h"
#include "tray.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <err.h>
#include <stdio.h>

#define PP_TRAY_ATOM	"_NET_SYSTEM_TRAY_S%d"

static void pp_tray_dock(PPPanel *panel, XClientMessageEvent *xevent)
{
	GtkSocket *sock = GTK_SOCKET(gtk_socket_new());
	gtk_widget_show(GTK_WIDGET(sock));

	GdkWindow *window = GTK_WIDGET(panel->window)->window;
	GdkColormap *colormap = gdk_drawable_get_colormap(window);
	gtk_widget_set_colormap(GTK_WIDGET(sock), colormap);

	gtk_container_add(GTK_CONTAINER(panel->tray), GTK_WIDGET(sock));

	gtk_widget_realize(GTK_WIDGET(sock));
	gdk_window_set_composited(GTK_WIDGET(sock)->window, TRUE);
	gtk_socket_add_id(sock, xevent->data.l[2]);
}

static GdkFilterReturn pp_tray_on_msg_received(GdkXEvent *gdk_xevent,
	GdkEvent *event, gpointer userdata)
{
	PPPanel *panel = userdata;
	XEvent *xevent = (XEvent *)gdk_xevent;

	if (xevent->type != ClientMessage ||
			xevent->xclient.message_type != panel->opcode_atom)
		return GDK_FILTER_CONTINUE;

	pp_tray_dock(panel, &xevent->xclient);
	return GDK_FILTER_REMOVE;
}

static void pp_become_manager(GdkWindow *window, GdkAtom selection_atom,
	guint32 timestamp)
{
	GdkColormap *colormap = gdk_drawable_get_colormap(window);
	GdkScreen *screen = gdk_colormap_get_screen(colormap);
	GdkDisplay *display = gdk_screen_get_display(screen);

    Window root_window = RootWindowOfScreen(GDK_SCREEN_XSCREEN(screen));
	Atom manager_xatom = gdk_x11_get_xatom_by_name_for_display(display,
		"MANAGER");
	Atom selection_xatom = gdk_x11_atom_to_xatom_for_display(display,
		selection_atom);

    XClientMessageEvent xevent = {
		.type = ClientMessage,
		.window = root_window,
		.message_type = manager_xatom,
		.format = 32,
		.data.l =
			{ timestamp, selection_xatom, GDK_WINDOW_XWINDOW(window), 0, 0 }
	};

	XSendEvent(GDK_DISPLAY_XDISPLAY(display), root_window, False,
		StructureNotifyMask, (XEvent *)&xevent);
}

static void pp_set_visual_property(GdkWindow *window)
{
	GdkColormap *colormap = gdk_drawable_get_colormap(window);
	GdkScreen *screen = gdk_colormap_get_screen(colormap);
	GdkDisplay *display = gdk_screen_get_display(screen);

	Atom visual_atom = gdk_x11_get_xatom_by_name_for_display(display,
		"_NET_SYSTEM_TRAY_VISUAL");
	Visual *xvisual = GDK_VISUAL_XVISUAL(gdk_screen_get_rgba_visual(screen));
	gulong data = XVisualIDFromVisual(xvisual);

	XChangeProperty(GDK_DISPLAY_XDISPLAY(display), GDK_WINDOW_XWINDOW(window),
		visual_atom, XA_VISUALID, 32, PropModeReplace, (guchar *)&data, 1);
}

static void pp_tray_paint_icon(GtkWidget *widget, gpointer userdata)
{
	cairo_t *cairo = userdata;
	gdk_cairo_set_source_pixmap(cairo, widget->window, widget->allocation.x,
		widget->allocation.y);
    cairo_paint(cairo);
}

/* Since we've set icons to be composited with gdk_window_set_composited(),
 * we have to paint them ourselves. */
static void pp_on_tray_expose(GtkWidget *widget, GdkEventExpose *event,
    gpointer userdata)
{
    cairo_t *cairo = gdk_cairo_create(widget->window);
	gtk_container_foreach(GTK_CONTAINER(widget), pp_tray_paint_icon, cairo);
    cairo_destroy(cairo);
}

void pp_tray_create(PPPanel *panel)
{
    GtkBox *tray = panel->tray = GTK_BOX(gtk_hbox_new(FALSE, 9));

    g_signal_connect(G_OBJECT(tray), "expose-event",
        G_CALLBACK(pp_on_tray_expose), NULL);

    gtk_container_add(GTK_CONTAINER(panel->hbox), GTK_WIDGET(tray));
    gtk_widget_show(GTK_WIDGET(tray));
}

void pp_tray_register(PPPanel *panel)
{
	/* TODO: Is the invisible stuff really necessary? Can we just reuse the 
	 * parent window? */

	GdkWindow *window = GTK_WIDGET(panel->window)->window;

	GdkColormap *colormap = gdk_drawable_get_colormap(window);
	GdkScreen *screen = gdk_colormap_get_screen(colormap);

	GtkWidget *invis = gtk_invisible_new_for_screen(screen);
	gtk_widget_realize(invis);
	gtk_widget_add_events(invis, GDK_PROPERTY_CHANGE_MASK |
		GDK_STRUCTURE_MASK);

	char selection_str[sizeof(PP_TRAY_ATOM) + 16];
	snprintf(selection_str, sizeof(selection_str), PP_TRAY_ATOM, 
		gdk_screen_get_number(screen));
	fprintf(stderr, "selection str %s\n", selection_str);
	GdkAtom selection_atom = gdk_atom_intern(selection_str, FALSE);

	GdkDisplay *display = gdk_screen_get_display(screen);
	guint32 timestamp = gdk_x11_get_server_time(invis->window);

	if (!gdk_selection_owner_set_for_display(display, invis->window,
			selection_atom, timestamp, TRUE))
		err(1, "can't become systray owner; is another tray running?");

	pp_set_visual_property(invis->window);
	pp_become_manager(invis->window, selection_atom, timestamp);

	g_object_ref(G_OBJECT(invis));

	GdkAtom opcode_atom = gdk_atom_intern("_NET_SYSTEM_TRAY_OPCODE", FALSE);
	panel->opcode_atom = gdk_x11_atom_to_xatom_for_display(display,
		opcode_atom);

	gdk_window_add_filter(invis->window, pp_tray_on_msg_received, panel);
}

