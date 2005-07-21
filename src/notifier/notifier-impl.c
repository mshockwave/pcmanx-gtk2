/**
 * notifier-impl.c
 * Lightweight Notifier implementation
 *
 * Copyright (c) 2005
 *	Jim Huang <jserv@kaffe.org>
 *
 * Licensed under the GNU GPL v2.  See COPYING.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <stdio.h>
#include <fcntl.h>

#include "notifier/api.h"

/* Customizable settings */
#define NHEIGHT 80
#define MAX_SLOTS 10
#define STEPS 20
#define SPEED 50

/* after you have moved the mouse over the window, it will disappear
   in this many microseconds. */
#define WAIT_PERIOD 3000
#define TIMEOUT     6000

static int width, height, max_slots;
static int slots[MAX_SLOTS] = {[0 ... (MAX_SLOTS - 1)] = -1 };
static int notifier_initialized = 0;
static GdkPixbuf *icon_pixbuf;

struct sWin {
	GtkWidget *win, *context;
	int slot;
	int size;
	gulong handlerid;
	GtkWidget* parent;
	gulong parent_handler_id;
	guint timeout_id;
	guint ani_timer_id;
};

typedef struct sWin Win;

static int get_slot(GtkWidget * win)
{
	int i;
	for (i = 0; i < max_slots; i++)
		if (slots[i] == -1) {
			slots[i] = 1;
			return i;
		}
	return 0;
}

static int slow_hide_win(gpointer data)
{
	Win *win = (Win *) data;
	int x_diff;
	int x, y;

	if (win->size == 0) {
		gtk_widget_destroy(win->win);
		return FALSE;
	}

	gtk_window_get_position(GTK_WINDOW(win->win), &x, &y);
	y += STEPS;
	win->size -= STEPS;
	x_diff = width - win->win->allocation.width;
	if (x_diff < 0)
		x_diff = 0;
	gtk_window_move(GTK_WINDOW(win->win), x_diff, y);
	return TRUE;
}

static int wait_win(gpointer data)
{
	Win *win = (Win *)data;
	win->ani_timer_id = gtk_timeout_add(SPEED, slow_hide_win, data);
	win->timeout_id = 0;
	return FALSE;
}

static int mouseout_win(GtkWidget *widget,
			GdkEventButton *event, gpointer data)
{
	Win *win = (Win *)data;
	g_signal_handler_disconnect(G_OBJECT(win->win), win->handlerid);
	gtk_container_set_border_width(GTK_CONTAINER(win->win), 5);
	if( win->timeout_id )
		g_source_remove(win->timeout_id);
	win->timeout_id = gtk_timeout_add(WAIT_PERIOD, wait_win, data);
	return TRUE;
}

static int mouseover_win(GtkWidget * widget,
			 GdkEventButton * event, gpointer data)
{
	Win *win = (Win *) data;
	g_signal_handler_disconnect(G_OBJECT(win->win), win->handlerid);
	win->handlerid =
		g_signal_connect(G_OBJECT(win->win),
				 "leave-notify-event",
				 G_CALLBACK(mouseout_win), data);
	if( win->timeout_id )
	{
		g_source_remove(win->timeout_id);
		win->timeout_id = 0;
	}
	return TRUE;
}

static int slow_show_win(gpointer data)
{
	Win *win = (Win *) data;
	int x_diff;
	int x, y;

	if (win->size == NHEIGHT) {
		win->handlerid =
		    g_signal_connect(G_OBJECT(win->win),
				     "enter-notify-event",
				     G_CALLBACK(mouseover_win), data);

		/* Added by Hong Jen Yee (PCMan)  */
		win->ani_timer_id = 0;
		win->timeout_id = gtk_timeout_add(TIMEOUT, wait_win, data);
		return FALSE;
	}

	gtk_window_get_position(GTK_WINDOW(win->win), &x, &y);
	y -= STEPS;
	win->size += STEPS;
	x_diff = width - win->win->allocation.width;
	if (x_diff < 0)
		x_diff = 0;
	gtk_window_move(GTK_WINDOW(win->win), x_diff, y);
	return TRUE;
}

static Win* begin_animation(GtkWidget * win, GtkWidget * context)
{
	int slot = get_slot(win);
	int begin = height - slot * NHEIGHT;
	Win *w = g_new0(Win, 1);

	w->win = win;
	w->context = context;
	w->slot = slot;
	w->size = 0;

	gtk_window_move(GTK_WINDOW(win), width - win->allocation.width, begin);
	gtk_widget_show_all(win);

	w->ani_timer_id = gtk_timeout_add(SPEED, slow_show_win, w);
	w->timeout_id = 0;

	return w;
}

/*   2005.07.22 Added by Hong Jen Yee (PCMan)
  Popup widgets should be removed when their parents are already destroyed.
*/
static void destroy_win(GtkWidget* win, Win* w)
{
	slots[w->slot] = -1;
	g_free(w);

	g_signal_handler_disconnect(w->parent, w->parent_handler_id);
	if(w->timeout_id)
		g_source_remove(w->timeout_id);
	if(w->ani_timer_id)
		g_source_remove(w->ani_timer_id);
}

/*   2005.07.22 Modified by Hong Jen Yee (PCMan)
  Return GtkWidget of the popup that the caller can get more control such as, 
  connecting some signal handlers to this widget.
*/
static GtkWidget* notify_new(const gchar *_caption_text, const gchar *body_text, 
							GtkWidget* parent, GCallback click_cb, 
							gpointer click_cb_data)
{
	GtkWidget *win = gtk_window_new(GTK_WINDOW_POPUP);
	GtkWidget *context, *frame;
	gchar *context_text = g_strdup(body_text);
	gchar *caption_text = g_strdup(_caption_text);
	GtkWidget *imageNotify;
	GtkWidget *labelNotify;
	GtkWidget *labelCaption;
	/* XXX:
	 * We should provide a better way to change pixmap */
	
	context = gtk_table_new(2, 2, TRUE);
	
	/*
	 * load the content
	 */
	labelNotify = gtk_label_new(NULL);
	labelCaption = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(labelNotify), context_text);
	gtk_label_set_markup(GTK_LABEL(labelCaption), caption_text);

	if (icon_pixbuf) {
		imageNotify = gtk_image_new_from_pixbuf(icon_pixbuf);

		gtk_table_attach(GTK_TABLE(context), imageNotify, 
			0,1,0,1,
			GTK_FILL, GTK_FILL,
			0, 0);
	}

	gtk_table_attach(GTK_TABLE(context), labelCaption,
		1,2,0,1,
		GTK_FILL, GTK_FILL,
		0, 0);
	gtk_table_attach(GTK_TABLE(context), labelNotify,
		0,2,1,2,
		GTK_FILL, GTK_FILL,
		0,0);

	GtkWidget* button = gtk_button_new();
	gtk_container_add(GTK_CONTAINER(win), button);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame),0);
	gtk_container_add(GTK_CONTAINER(button), frame);

	gtk_window_set_default_size(GTK_WINDOW(win), win->allocation.width, NHEIGHT);
	gtk_container_add(GTK_CONTAINER(frame), context);

	if( click_cb )
		g_signal_connect(G_OBJECT(button), "clicked", 
								click_cb, click_cb_data);

	Win* w = begin_animation(win, context);
	g_free(context_text);

	w->parent = parent;
	w->parent_handler_id = !parent ? 0 : g_signal_connect_swapped(G_OBJECT(parent), 
												"destroy", 
												 G_CALLBACK(gtk_widget_destroy),
												 win);
	g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(destroy_win), w);


	return win;
}

GtkWidget* popup_notifier_notify(const gchar *caption, const gchar *text, 
									GtkWidget* parent, GCallback click_cb, 
									gpointer click_cb_data)
{
	if (! notifier_initialized)
		return NULL;

	if (text && *text)
		return notify_new(caption, text, parent, click_cb, click_cb_data);
}

void popup_notifier_init(GdkPixbuf *pixbuf)
{
	/* Initialization has been done or not. */
	if (notifier_initialized)
		return;

	icon_pixbuf = pixbuf;

	height = gdk_screen_height();
	width = gdk_screen_width();
	max_slots = MAX((height - NHEIGHT) / NHEIGHT, MAX_SLOTS);

	notifier_initialized = 1;
}

