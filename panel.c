/* pegasuspanel */

#include "panel.h"
#include "tray.h"

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <libindicator/indicator-object.h>

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PP_INDICATOR_DIR    "/usr/lib/indicators/4"

#define PP_ARC_RADIUS       6.0f
#define PP_PANEL_HEIGHT     24
#define PP_OFF_SCREEN       -5.5f

const char *pp_sticky_indicators[] =
    { "libsession.so", "libdatetime.so", NULL };

static void pp_build_curve_path(cairo_t *cairo, float width, float height,
    float radius)
{
    width -= 0.5f; height -= 0.5f;  /* Hit the pixel centers. */

    cairo_new_path(cairo);
    cairo_move_to(cairo, PP_OFF_SCREEN, PP_OFF_SCREEN);
    cairo_line_to(cairo, (float)width, PP_OFF_SCREEN);
    cairo_arc(cairo, (float)width - radius, (float)height - radius,
        radius, 0.0f, M_PI / 2.0f);
    cairo_line_to(cairo, PP_OFF_SCREEN, (float)height);
    cairo_line_to(cairo, PP_OFF_SCREEN, PP_OFF_SCREEN);
}

static gboolean pp_on_window_expose(GtkWidget *widget, GdkEventExpose *event,
    gpointer userdata)
{
    gint width, height;
    gdk_drawable_get_size(widget->window, &width, &height);

    cairo_t *cairo = gdk_cairo_create(widget->window);

    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cairo, 1.0, 1.0, 1.0, 0.0);
    cairo_paint(cairo);

    pp_build_curve_path(cairo, (float)width, (float)height,
        PP_ARC_RADIUS - 1.0f);
    cairo_set_source_rgba(cairo, 0.0f, 0.0f, 0.0f, 0.6f);
    cairo_fill(cairo);

    pp_build_curve_path(cairo, (float)width, (float)height, PP_ARC_RADIUS);
    cairo_set_source_rgba(cairo, 0.5f, 0.5f, 0.5f, 0.6f);
    cairo_set_line_width(cairo, 1.0);
    cairo_stroke(cairo);

    cairo_destroy(cairo);
    return FALSE;
}

/* Black magic! */
static void pp_monkey_patch_gtk_menu_bar(GtkMenuBar *bar)
{
    GtkMenuBarClass *klass = GTK_MENU_BAR_GET_CLASS(bar);
    GtkMenuShellClass *menu_shell_class = g_type_class_peek_parent(klass);
    GtkContainerClass *container_class =
        g_type_class_peek_parent(menu_shell_class);
    GtkWidgetClass *widget_class = g_type_class_peek_parent(container_class);
    GTK_WIDGET_CLASS(klass)->realize = widget_class->realize;
}

static void pp_create_spacer(GtkContainer *container, gint width, gint height)
{
    GtkWidget *spacer = gtk_alignment_new(0.0f, 0.0f, 0.0f, 0.0f);
    gtk_widget_set_size_request(spacer, width, height);
    gtk_container_add(container, spacer);
    gtk_widget_show(spacer);
}

static void pp_make_panel(PPPanel *panel)
{
    /* TODO: Use gtk_widget_show_all(); it's more efficient and concise. */

    GtkWindow *window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    panel->window = window;
    gtk_window_set_skip_taskbar_hint(window, TRUE);
    gtk_window_set_decorated(window, FALSE);
    gtk_window_set_type_hint(window, GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_widget_set_app_paintable(GTK_WIDGET(window), TRUE);

    GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(window));
    GdkColormap *colormap = gdk_screen_get_rgba_colormap(screen);
    gtk_widget_set_colormap(GTK_WIDGET(window), colormap);

    g_signal_connect(G_OBJECT(window), "expose-event",
        G_CALLBACK(pp_on_window_expose), NULL);

    GtkBox *vbox = GTK_BOX(gtk_vbox_new(FALSE, 0));
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(vbox));

    GtkBox *hbox = panel->hbox = GTK_BOX(gtk_hbox_new(FALSE, 3));
    gtk_container_add(GTK_CONTAINER(vbox), GTK_WIDGET(hbox));
    gtk_widget_set_size_request(GTK_WIDGET(hbox), -1, PP_PANEL_HEIGHT);
    gtk_widget_show(GTK_WIDGET(hbox));

    pp_create_spacer(GTK_CONTAINER(vbox), -1, 1);

    gtk_widget_show(GTK_WIDGET(vbox));

    /* Create the indicator menu. */
    GtkMenuBar *indicator_menu = (GtkMenuBar *)(gtk_menu_bar_new());
    panel->menu = indicator_menu;
    pp_monkey_patch_gtk_menu_bar(indicator_menu);
    gtk_widget_set_name(GTK_WIDGET(indicator_menu), "fast-user-switch");
    gtk_container_add(GTK_CONTAINER(hbox), GTK_WIDGET(indicator_menu));
    gtk_widget_show(GTK_WIDGET(indicator_menu));

    /* Magic! */
    GTK_WIDGET_SET_FLAGS(GTK_WIDGET(indicator_menu), GTK_NO_WINDOW);

    /* Create the tray. */
    pp_tray_create(panel);

    pp_create_spacer(GTK_CONTAINER(hbox), 3, -1);
}

static void pp_on_entry_added(IndicatorObject *obj, IndicatorObjectEntry *
    entry, PPPanel *panel)
{
    GtkMenuItem *item = GTK_MENU_ITEM(gtk_menu_item_new());
    GtkBox *hbox = GTK_BOX(gtk_hbox_new(FALSE, 3));

    if (entry->image)
        gtk_container_add(GTK_CONTAINER(hbox), GTK_WIDGET(entry->image));
    if (entry->label)
        gtk_container_add(GTK_CONTAINER(hbox), GTK_WIDGET(entry->label));
    if (entry->menu)
        gtk_menu_item_set_submenu(item, GTK_WIDGET(entry->menu));

    gtk_container_add(GTK_CONTAINER(item), GTK_WIDGET(hbox));
    gtk_widget_show(GTK_WIDGET(hbox));

    gtk_menu_shell_append(GTK_MENU_SHELL(panel->menu), GTK_WIDGET(item));
    gtk_widget_show(GTK_WIDGET(item));
}

static void pp_load_indicator_module(PPPanel *panel, const gchar *name)
{
    fprintf(stderr, "loading %s\n", name);

    gchar *path = g_build_filename(PP_INDICATOR_DIR, name, NULL);
    IndicatorObject *obj = indicator_object_new_from_file(path);
    g_free(path);

    /* TODO: Removal handler */
	g_signal_connect(G_OBJECT(obj), INDICATOR_OBJECT_SIGNAL_ENTRY_ADDED,
        G_CALLBACK(pp_on_entry_added), panel);

    GList *entries = indicator_object_get_entries(obj);
    GList *entry = entries;
    while (entry != NULL) {
        IndicatorObjectEntry *entry_data = (IndicatorObjectEntry *)
            entry->data;
        pp_on_entry_added(obj, entry_data, panel);
        entry = g_list_next(entry);
    }
    g_list_free(entries);
}

static _Bool pp_module_is_sticky(const char *name)
{
    int i = 0;
    while (pp_sticky_indicators[i]) {
        if (!strcmp(name, pp_sticky_indicators[i]))
            return true;
        i++;
    }
    return false;
}

static void pp_load_indicator_modules(PPPanel *panel)
{
    assert(g_file_test(PP_INDICATOR_DIR, G_FILE_TEST_EXISTS |
        G_FILE_TEST_IS_DIR));

    int i = 0;
    while (pp_sticky_indicators[i]) {
        pp_load_indicator_module(panel, pp_sticky_indicators[i]);
        i++;
    }

    GDir *dir = g_dir_open(PP_INDICATOR_DIR, 0, NULL);

    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!pp_module_is_sticky(name))
            pp_load_indicator_module(panel, name);
    }

    g_dir_close(dir);
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    PPPanel panel;
    pp_make_panel(&panel);

    pp_load_indicator_modules(&panel);

    gtk_widget_realize(GTK_WIDGET(panel.window));
    pp_tray_register(&panel);

    gtk_widget_show(GTK_WIDGET(panel.window));
    gtk_main();
    return 0;
}

