/*  item_dialog.c
 *
 *  Copyright (C) Jasper Huijsmans (huysmans@users.sourceforge.net)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/
#define USE_XFFM_THEME_MAKER

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libxfce4util/i18n.h>

#include "xfce.h"
#include "groups.h"
#include "popup.h"
#include "item.h"
#include "item_dialog.h"
#include "settings.h"

/*  item_dialog.c
 *  -------------
 *  There are two types of configuration dialogs: for panel items and for 
 *  menu items.
 *
 *  1) Dialog for changing items on the panel. This is now defined in 
 *  controls_dialog.c. Only icon items use code from this file to 
 *  present their options. This code is partially shared with menu item
 *  dialogs.
 *  
 *  2) Dialogs for changing or adding menu items.
 *  Basically the same as the notebook page for icon panel items. Adds
 *  options for caption and position in menu.
*/

static void item_apply_options (void);
static GtkWidget *create_icon_option_menu (void);
static GtkWidget *create_icon_option (GtkSizeGroup *);
static GtkWidget *create_command_option (GtkSizeGroup *);
static GtkWidget *create_caption_option (GtkSizeGroup *);
static GtkWidget *create_tooltip_option (GtkSizeGroup *);
static GtkWidget *create_position_option (void);
static GtkWidget *create_item_options_box (void);
static GtkWidget *create_icon_preview_frame (void);
static GtkWidget *create_menu_item_dialog (Item *);


enum
{ RESPONSE_DONE, RESPONSE_REMOVE };

/* the item is a menu item or a panel control */
Control *config_control = NULL;
Item *config_item = NULL;
int num_items = 0;

static GtkWidget *menudialog = NULL;	/* keep track of this for signal 
					   handling */

static GtkWidget *dialog;

static int id_callback;

/* important widgets */
static GtkWidget *command_entry;
static GtkWidget *command_browse_button;
static GtkWidget *term_checkbutton;
static GtkWidget *sn_checkbutton;
static GtkWidget *icon_id_menu;
static GtkWidget *icon_entry;
static GtkWidget *icon_browse_button;
static GtkWidget *tip_entry;
static GtkWidget *preview_image;

/* for panel launchers */
static GtkWidget *popup_checkbutton;

/* for menu items */
static GtkWidget *caption_entry;
static GtkWidget *pos_spin;

/* controls on the parent dialog */
static GtkWidget *done_button;

/* usefull for (instant) apply */
int icon_id;
static char *icon_path = NULL;
int pos;

/* reindex menuitems */
static void
reindex_items (GList * items)
{
    Item *item;
    GList *li;
    int i;

    for (i = 0, li = items; li; i++, li = li->next)
    {
	item = li->data;

	item->pos = i;
    }
}

/*  Save options 
 *  ------------
*/
struct ItemBackup
{
    char *command;
    gboolean in_terminal;
    gboolean use_sn;

    char *caption;
    char *tooltip;

    int icon_id;
    char *icon_path;

    int pos;
    gboolean with_popup;
};

struct ItemBackup backup;

void
init_backup (void)
{
    backup.command = NULL;
    backup.in_terminal = FALSE;
    backup.use_sn = FALSE;

    backup.caption = NULL;
    backup.tooltip = NULL;

    backup.icon_id = 0;
    backup.icon_path = NULL;

    backup.pos = 0;
    backup.with_popup = TRUE;
}

void
clear_backup (void)
{
    g_free (backup.command);

    g_free (backup.caption);
    g_free (backup.tooltip);

    g_free (backup.icon_path);

    /* not really backup, but ... */
    g_free (icon_path);
    icon_path = NULL;

    init_backup ();
}

/*  useful in callbacks
*/
void
make_sensitive (GtkWidget * widget)
{
    gtk_widget_set_sensitive (widget, TRUE);
}

/*  entry callback
*/
gboolean
entry_lost_focus (GtkEntry * entry, GdkEventFocus * event, gpointer data)
{
    item_apply_options ();

    /* needed to prevent GTK+ crash :( */
    return FALSE;
}

/*  Changing the icon
 *  -----------------
 *  An icon is changed by changing the id or the path in case of an
 *  external icon.
 *  There are several mechamisms by which an external icon can be changed:
 *  - dragging an icon to the preview area
 *  - writing the path in the text entry
 *  - using the file dialog
*/
#define PREVIEW_SIZE 48

static GdkPixbuf *
scale_image (GdkPixbuf * pb)
{
    int w, h;
    GdkPixbuf *newpb;

    g_return_val_if_fail (pb != NULL, NULL);

    w = gdk_pixbuf_get_width (pb);
    h = gdk_pixbuf_get_height (pb);

    if (w > PREVIEW_SIZE || h > PREVIEW_SIZE)
    {
	if (w > h)
	{
	    h = (int) (((double) PREVIEW_SIZE / (double) w) * (double) h);
	    w = PREVIEW_SIZE;
	}
	else
	{
	    w = (int) (((double) PREVIEW_SIZE / (double) h) * (double) w);
	    h = PREVIEW_SIZE;
	}

	newpb = gdk_pixbuf_scale_simple (pb, w, h, GDK_INTERP_BILINEAR);
    }
    else
    {
	newpb = pb;
	g_object_ref (newpb);
    }

    return newpb;
}

static void
change_icon (int id, const char *path)
{
    GdkPixbuf *pb = NULL, *tmp;

    if (id == EXTERN_ICON && path)
    {
	if (g_file_test (path, G_FILE_TEST_IS_DIR) ||
	    !g_file_test (path, G_FILE_TEST_EXISTS))
	{
	    pb = get_pixbuf_by_id (UNKNOWN_ICON);
	}
	else
	{
	    pb = gdk_pixbuf_new_from_file (path, NULL);
	}
    }
    else
    {
	pb = get_pixbuf_by_id (id);
    }

    if (!pb || !GDK_IS_PIXBUF (pb))
    {
	g_warning ("%s: couldn't create pixbuf: id=%d, path=%s\n", PACKAGE,
		   id, path ? path : "");
	return;
    }

    tmp = pb;
    pb = scale_image (tmp);
    g_object_unref (tmp);

    gtk_image_set_from_pixbuf (GTK_IMAGE (preview_image), pb);
    g_object_unref (pb);

    icon_id = id;

    if (id == EXTERN_ICON || id == UNKNOWN_ICON)
    {
	if (path)
	{
	    if (!icon_path || !strequal (path, icon_path))
	    {
		g_free (icon_path);
		icon_path = g_strdup (path);
	    }

	    gtk_entry_set_text (GTK_ENTRY (icon_entry), path);
	}

	gtk_widget_set_sensitive (icon_entry, TRUE);
	gtk_widget_set_sensitive (icon_browse_button, TRUE);
    }
    else
    {
	gtk_entry_set_text (GTK_ENTRY (icon_entry), "");

	gtk_widget_set_sensitive (icon_entry, FALSE);
/*        gtk_widget_set_sensitive(icon_browse_button, FALSE);*/
    }

    g_signal_handler_block (icon_id_menu, id_callback);
    gtk_option_menu_set_history (GTK_OPTION_MENU (icon_id_menu),
				 (id == EXTERN_ICON) ? 0 : id);
    g_signal_handler_unblock (icon_id_menu, id_callback);

    item_apply_options ();
}

static void
icon_id_changed (void)
{
    int new_id = gtk_option_menu_get_history (GTK_OPTION_MENU (icon_id_menu));

    if (new_id == 0)
    {
	change_icon (EXTERN_ICON, icon_path);
    }
    else
    {
	change_icon (new_id, NULL);
    }
}

static GtkWidget *
create_icon_option_menu (void)
{
    GtkWidget *om;
    GtkWidget *menu = gtk_menu_new ();
    GtkWidget *mi;
    int i;

    mi = gtk_menu_item_new_with_label (_("Other Icon"));
    gtk_widget_show (mi);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);

    for (i = 1; i < NUM_ICONS; i++)
    {
	mi = gtk_menu_item_new_with_label (icon_names[i]);
	gtk_widget_show (mi);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    }

    om = gtk_option_menu_new ();
    gtk_option_menu_set_menu (GTK_OPTION_MENU (om), menu);

    id_callback =
	g_signal_connect_swapped (om, "changed", G_CALLBACK (icon_id_changed),
				  NULL);

    return om;
}

static void
icon_browse_cb (GtkWidget * b, GtkEntry * entry)
{
    char *file = select_file_with_preview (_("Select icon"),
					   gtk_entry_get_text (entry),
					   dialog);

    if (file)
    {
	change_icon (EXTERN_ICON, file);
	g_free (file);
    }
}

#ifdef USE_XFFM_THEME_MAKER
static void
xtm_cb (GtkWidget * b, GtkEntry * entry)
{
    GError *error = NULL;
    gchar *argv[2] = { "xffm_theme_maker", NULL };
    g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
		   &error);
    if (error)
	g_error_free (error);
}
#endif

gboolean
icon_entry_lost_focus (GtkEntry * entry, GdkEventFocus * event, gpointer data)
{
    const char *temp = gtk_entry_get_text (entry);

    if (temp)
	change_icon (EXTERN_ICON, temp);

    /* we must return FALSE or gtk will crash :-( */
    return FALSE;
}

static GtkWidget *
create_icon_option (GtkSizeGroup * sg)
{
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *image;

    vbox = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox);

    /* option menu */
    hbox = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

    label = gtk_label_new (_("Icon:"));
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_size_group_add_widget (sg, label);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    icon_id_menu = create_icon_option_menu ();
    gtk_widget_show (icon_id_menu);
    gtk_box_pack_start (GTK_BOX (hbox), icon_id_menu, TRUE, TRUE, 0);

    /* icon entry */
    hbox = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

    label = gtk_label_new ("");
    gtk_size_group_add_widget (sg, label);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    icon_entry = gtk_entry_new ();
    gtk_widget_show (icon_entry);
    gtk_box_pack_start (GTK_BOX (hbox), icon_entry, TRUE, TRUE, 0);

    g_signal_connect (icon_entry, "focus-out-event",
		      G_CALLBACK (icon_entry_lost_focus), NULL);


    icon_browse_button = gtk_button_new ();
    gtk_widget_show (icon_browse_button);
    gtk_box_pack_start (GTK_BOX (hbox), icon_browse_button, FALSE, FALSE, 0);

    image = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON);
    gtk_widget_show (image);
    gtk_container_add (GTK_CONTAINER (icon_browse_button), image);

    g_signal_connect (icon_browse_button, "clicked",
		      G_CALLBACK (icon_browse_cb), icon_entry);

#ifdef USE_XFFM_THEME_MAKER
    {
	gchar *g = g_find_program_in_path ("xffm_theme_maker");

	if (g)
	{
	    GtkWidget *xtm_button = gtk_button_new ();

	    gtk_box_pack_start (GTK_BOX (hbox), xtm_button, FALSE, FALSE, 0);
	    gtk_widget_show (xtm_button);

	    image =
		gtk_image_new_from_stock (GTK_STOCK_SELECT_COLOR,
					  GTK_ICON_SIZE_BUTTON);
	    gtk_widget_show (image);
	    gtk_container_add (GTK_CONTAINER (xtm_button), image);

	    g_signal_connect (xtm_button, "clicked",
			      G_CALLBACK (xtm_cb), icon_entry);
	    g_free (g);
	}
    }

#endif


    return vbox;
}

/*  Change the command 
 *  ------------------
*/
static void
command_browse_cb (GtkWidget * b, GtkEntry * entry)
{
    char *file =
	select_file_name (_("Select command"), gtk_entry_get_text (entry),
			  dialog);

    if (file)
    {
	gtk_entry_set_text (entry, file);
	g_free (file);
    }
}

static GtkWidget *
create_command_option (GtkSizeGroup * sg)
{
    GtkWidget *vbox;
    GtkWidget *vbox2;
    GtkWidget *hbox;
    GtkWidget *hbox2;
    GtkWidget *label;
    GtkWidget *image;

    vbox = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox);

    /* entry */
    hbox = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

    label = gtk_label_new (_("Command:"));
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_size_group_add_widget (sg, label);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    command_entry = gtk_entry_new ();
    gtk_widget_show (command_entry);
    gtk_box_pack_start (GTK_BOX (hbox), command_entry, TRUE, TRUE, 0);

    command_browse_button = gtk_button_new ();
    gtk_widget_show (command_browse_button);
    gtk_box_pack_start (GTK_BOX (hbox), command_browse_button, FALSE, FALSE,
			0);

    image = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON);
    gtk_widget_show (image);
    gtk_container_add (GTK_CONTAINER (command_browse_button), image);

    g_signal_connect (command_browse_button, "clicked",
		      G_CALLBACK (command_browse_cb), command_entry);

    /* terminal */
    hbox2 = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (hbox2);
    gtk_box_pack_start (GTK_BOX (vbox), hbox2, FALSE, TRUE, 0);

    label = gtk_label_new ("");
    gtk_size_group_add_widget (sg, label);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox2), label, FALSE, FALSE, 0);

    vbox2 = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (vbox2);
    gtk_box_pack_start (GTK_BOX (hbox2), vbox2, FALSE, TRUE, 0);

    term_checkbutton =
	gtk_check_button_new_with_mnemonic (_("Run in _terminal"));
    gtk_widget_show (term_checkbutton);
    gtk_box_pack_start (GTK_BOX (vbox2), term_checkbutton, FALSE, FALSE, 0);

    sn_checkbutton =
	gtk_check_button_new_with_mnemonic (_("Use startup _notification"));
    gtk_widget_show (sn_checkbutton);
    gtk_box_pack_start (GTK_BOX (vbox2), sn_checkbutton, FALSE, FALSE, 0);
#ifdef HAVE_LIBSTARTUP_NOTIFICATION
    gtk_widget_set_sensitive (sn_checkbutton, TRUE);
#else
    gtk_widget_set_sensitive (sn_checkbutton, FALSE);
#endif
    return vbox;
}

/*  Change caption
*/
static GtkWidget *
create_caption_option (GtkSizeGroup * sg)
{
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *label;

    vbox = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox);

    hbox = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

    label = gtk_label_new (_("Caption:"));
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_size_group_add_widget (sg, label);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    caption_entry = gtk_entry_new ();
    gtk_widget_show (caption_entry);
    gtk_box_pack_start (GTK_BOX (hbox), caption_entry, TRUE, TRUE, 0);

    /* only set label on focus out */
    g_signal_connect (caption_entry, "focus-out-event",
		      G_CALLBACK (entry_lost_focus), NULL);

    return vbox;
}

/*  Change tooltip
*/
static GtkWidget *
create_tooltip_option (GtkSizeGroup * sg)
{
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *label;

    vbox = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox);

    hbox = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

    label = gtk_label_new (_("Tooltip:"));
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_size_group_add_widget (sg, label);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    tip_entry = gtk_entry_new ();
    gtk_widget_show (tip_entry);
    gtk_box_pack_start (GTK_BOX (hbox), tip_entry, TRUE, TRUE, 0);

    return vbox;
}

/* show subpanel */
static void
popup_changed (GtkToggleButton * tb, gpointer data)
{
    config_item->with_popup = gtk_toggle_button_get_active (tb);

    groups_show_popup (config_control->index, config_item->with_popup);
}

static GtkWidget *
create_popup_option (GtkSizeGroup * sg)
{
    GtkWidget *hbox;
    GtkWidget *label;

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (hbox);

    label = gtk_label_new (_("Menu:"));
    gtk_size_group_add_widget (sg, label);
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    popup_checkbutton =
	gtk_check_button_new_with_label (_("Attach menu to launcher"));
    gtk_widget_show (popup_checkbutton);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (popup_checkbutton),
				  config_item->with_popup);
    gtk_box_pack_start (GTK_BOX (hbox), popup_checkbutton, FALSE, FALSE, 2);

    g_signal_connect (popup_checkbutton, "toggled",
		      G_CALLBACK (popup_changed), NULL);

    return hbox;
}

/* Change menu item position
 * -------------------------
*/
static void
pos_changed (GtkSpinButton * spin, gpointer data)
{
    int n = gtk_spin_button_get_value_as_int (spin);
    PanelPopup *pp = config_item->parent;

    if (n - 1 == config_item->pos)
	return;

    pp->items = g_list_remove (pp->items, config_item);
    config_item->pos = n - 1;
    pp->items = g_list_insert (pp->items, config_item, config_item->pos);
    reindex_items (pp->items);

    item_apply_options ();
}

static GtkWidget *
create_position_option (void)
{
    GtkWidget *hbox;
    GtkWidget *label;

    hbox = gtk_hbox_new (FALSE, 4);
    gtk_widget_show (hbox);

    label = gtk_label_new (_("Position:"));
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    pos_spin = gtk_spin_button_new_with_range
	(1, (num_items > 0) ? num_items : 1, 1);
    gtk_widget_show (pos_spin);
    gtk_box_pack_start (GTK_BOX (hbox), pos_spin, FALSE, FALSE, 0);

    g_signal_connect (pos_spin, "value-changed", G_CALLBACK (pos_changed),
		      NULL);

    return hbox;
}

/*  The main options box
 *  --------------------
*/
static GtkWidget *
create_item_options_box (void)
{
    GtkWidget *vbox;
    GtkWidget *box;
    GtkSizeGroup *sg = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

    vbox = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox);

    /* command */
    box = create_command_option (sg);
    gtk_box_pack_start (GTK_BOX (vbox), box, FALSE, TRUE, 0);

    /* icon */
    box = create_icon_option (sg);
    gtk_box_pack_start (GTK_BOX (vbox), box, FALSE, TRUE, 0);

    /* caption (menu item) */
    if (config_item->type == MENUITEM)
    {
	box = create_caption_option (sg);
	gtk_box_pack_start (GTK_BOX (vbox), box, FALSE, TRUE, 0);
    }

    /* tooltip */
    box = create_tooltip_option (sg);
    gtk_box_pack_start (GTK_BOX (vbox), box, FALSE, TRUE, 0);

    /* subpanel (panel item) */
    if (config_item->type == PANELITEM)
    {
	box = create_popup_option (sg);
	gtk_box_pack_start (GTK_BOX (vbox), box, FALSE, TRUE, 4);
    }

    return vbox;
}

/*  Icon preview area
 *  -----------------
*/
static void
icon_drop_cb (GtkWidget * widget, GdkDragContext * context,
	      gint x, gint y, GtkSelectionData * data,
	      guint info, guint time, gpointer user_data)
{
    GList *fnames;
    guint count;

    fnames = gnome_uri_list_extract_filenames ((char *) data->data);
    count = g_list_length (fnames);

    if (count > 0)
    {
	char *icon;

	icon = (char *) fnames->data;

	change_icon (EXTERN_ICON, icon);
    }

    gnome_uri_list_free_strings (fnames);
    gtk_drag_finish (context, (count > 0),
		     (context->action == GDK_ACTION_MOVE), time);
}

static GtkWidget *
create_icon_preview_frame ()
{
    GtkWidget *frame;
    GtkWidget *eventbox;

    frame = gtk_frame_new (_("Icon Preview"));
    gtk_widget_show (frame);

    eventbox = gtk_event_box_new ();
    add_tooltip (eventbox, _("Drag file onto this frame to change the icon"));
    gtk_widget_show (eventbox);
    gtk_container_add (GTK_CONTAINER (frame), eventbox);

    preview_image = gtk_image_new ();
    gtk_widget_show (preview_image);
    gtk_container_add (GTK_CONTAINER (eventbox), preview_image);

    /* signals */
    dnd_set_drag_dest (eventbox);

    g_signal_connect (eventbox, "drag_data_received",
		      G_CALLBACK (icon_drop_cb), NULL);

    return frame;
}

/*  Apply
 *  -----
*/
static void
item_apply_options (void)
{
    const char *temp;
    PanelPopup *pp = NULL;

    /* command */
    g_free (config_item->command);
    config_item->command = NULL;

    temp = gtk_entry_get_text (GTK_ENTRY (command_entry));

    if (temp && *temp)
	config_item->command = g_strdup (temp);

    config_item->in_terminal =
	gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (term_checkbutton));

    config_item->use_sn =
	gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sn_checkbutton));

    /* tooltip */
    g_free (config_item->tooltip);
    config_item->tooltip = NULL;
    temp = gtk_entry_get_text (GTK_ENTRY (tip_entry));

    if (temp && *temp)
	config_item->tooltip = g_strdup (temp);

    /* icon */
    config_item->icon_id = icon_id;

    g_free (config_item->icon_path);
    config_item->icon_path = NULL;

    if (icon_path && config_item->icon_id == EXTERN_ICON)
	config_item->icon_path = g_strdup (icon_path);

    if (config_item->type == MENUITEM)
    {
	pp = config_item->parent;

	/* caption */
	g_free (config_item->caption);
	temp = gtk_entry_get_text (GTK_ENTRY (caption_entry));
	config_item->caption = NULL;

	if (temp && *temp)
	    config_item->caption = g_strdup (temp);

	/* position */
	gtk_box_reorder_child (GTK_BOX (pp->item_vbox), config_item->button,
			       config_item->pos);
    }

    item_apply_config (config_item);
}

static void
item_create_options (GtkContainer * container)
{
    GtkWidget *vbox;
    GtkWidget *main_hbox;
    GtkWidget *options_box;
    GtkWidget *preview_frame;

    /* backup */
    init_backup ();

    if (config_item->command)
	backup.command = g_strdup (config_item->command);

    backup.in_terminal = config_item->in_terminal;
    backup.use_sn = config_item->use_sn;

    if (config_item->tooltip)
	backup.tooltip = g_strdup (config_item->tooltip);

    backup.icon_id = config_item->icon_id;

    if (config_item->icon_path)
	backup.icon_path = g_strdup (config_item->icon_path);

    if (config_item->type == MENUITEM)
    {
	if (config_item->caption)
	    backup.caption = g_strdup (config_item->caption);

	backup.pos = config_item->pos;
    }

    /* main vbox */
    vbox = gtk_vbox_new (FALSE, 8);
    gtk_widget_show (vbox);
    gtk_container_add (container, vbox);

    /* options box */
    main_hbox = gtk_hbox_new (FALSE, 6);
    gtk_widget_show (main_hbox);
    gtk_box_pack_start (GTK_BOX (vbox), main_hbox, FALSE, TRUE, 0);

    options_box = create_item_options_box ();
    gtk_box_pack_start (GTK_BOX (main_hbox), options_box, TRUE, TRUE, 0);

    preview_frame = create_icon_preview_frame ();
    gtk_box_pack_start (GTK_BOX (main_hbox), preview_frame, TRUE, FALSE, 0);

    /* fill in the structures use the backup values 
     * because the item values may have changed when connecting signals */
    if (backup.command)
	gtk_entry_set_text (GTK_ENTRY (command_entry), backup.command);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (term_checkbutton),
				  backup.in_terminal);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sn_checkbutton),
				  backup.use_sn);

    change_icon (backup.icon_id, backup.icon_path);

    if (backup.tooltip)
	gtk_entry_set_text (GTK_ENTRY (tip_entry), backup.tooltip);

    if (config_item->type == MENUITEM)
    {
	if (backup.caption)
	    gtk_entry_set_text (GTK_ENTRY (caption_entry), backup.caption);

	if (num_items > 1)
	    gtk_spin_button_set_value (GTK_SPIN_BUTTON (pos_spin),
				       (gfloat) backup.pos + 1);
    }

    item_apply_options ();
}

/*  Panel item dialog
 *  -----------------
*/
void
panel_item_create_options (Control * control, GtkContainer * container,
			   GtkWidget * done)
{
    config_control = control;
    config_item = control->data;

    dialog = gtk_widget_get_toplevel (done);

    g_signal_connect_swapped (done, "clicked",
			      G_CALLBACK (item_apply_options), NULL);

    item_create_options (container);
}

/*  Menu item dialogs
 *  -----------------
*/
static GtkWidget *
create_menu_item_dialog (Item * mi)
{
    GtkWidget *dlg;
    GtkWidget *main_vbox;
    GtkWidget *frame;
    GtkWidget *remove_button;
    GtkWidget *sep;

    /* create dialog */
    dlg = gtk_dialog_new ();
    gtk_window_set_title (GTK_WINDOW (dlg), _("Change menu item"));

    /* add buttons */
    remove_button = mixed_button_new (GTK_STOCK_REMOVE, _("Remove"));
    GTK_WIDGET_SET_FLAGS (remove_button, GTK_CAN_DEFAULT);
    gtk_widget_show (remove_button);
    gtk_dialog_add_action_widget (GTK_DIALOG (dlg), remove_button,
				  RESPONSE_REMOVE);

    done_button = gtk_button_new_from_stock (GTK_STOCK_CLOSE);
    GTK_WIDGET_SET_FLAGS (done_button, GTK_CAN_DEFAULT);
    gtk_widget_show (done_button);
    gtk_dialog_add_action_widget (GTK_DIALOG (dlg), done_button,
				  RESPONSE_DONE);

/*    gtk_widget_show(GTK_DIALOG(dlg)->action_area);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (GTK_DIALOG(dlg)->action_area),
                               GTK_BUTTONBOX_END);
*/
    gtk_button_box_set_child_secondary (GTK_BUTTON_BOX
					(GTK_DIALOG (dlg)->action_area),
					remove_button, TRUE);

    /* the options */
    main_vbox = GTK_DIALOG (dlg)->vbox;

    config_item = mi;

    /* position */
    if (num_items > 1)
    {
	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (frame), create_position_option ());

	sep = gtk_hseparator_new ();
	gtk_widget_show (sep);
	gtk_box_pack_start (GTK_BOX (main_vbox), sep, FALSE, TRUE, 0);
    }

    /* other options */
    frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
    gtk_widget_show (frame);
    gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);

    item_create_options (GTK_CONTAINER (frame));

    /* signals */
    g_signal_connect (done_button, "clicked",
		      G_CALLBACK (item_apply_options), NULL);

    gtk_widget_grab_default (done_button);
    gtk_widget_grab_focus (done_button);

    return dlg;
}

void
edit_menu_item_dialog (Item * mi)
{
    GtkWidget *dlg;
    GtkWidget **dlg_ptr;
    PanelPopup *pp = mi->parent;
    int response = GTK_RESPONSE_NONE;

    config_item = mi;

    num_items = g_list_length (pp->items);

    menudialog = dlg = create_menu_item_dialog (mi);
    dialog = dlg;

    dlg_ptr = &menudialog;
    g_object_add_weak_pointer (G_OBJECT (dlg), (gpointer *) dlg_ptr);

    gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_CENTER);
    response = gtk_dialog_run (GTK_DIALOG (dlg));

    /* the options are already applied, so we only have to deal
     * with removal */
    if (response == RESPONSE_REMOVE)
    {
	gtk_container_remove (GTK_CONTAINER (pp->item_vbox), mi->button);
	pp->items = g_list_remove (pp->items, mi);
	item_free (mi);
	reindex_items (pp->items);
    }

    gtk_widget_destroy (dlg);
    num_items = 0;

    clear_backup ();

    write_panel_config ();
}

void
add_menu_item_dialog (PanelPopup * pp)
{
    Item *mi = menu_item_new (pp);

    create_menu_item (mi);
    mi->pos = 0;

    panel_popup_add_item (pp, mi);

    edit_menu_item_dialog (mi);
}

void
destroy_menu_dialog (void)
{
    if (menudialog)
	gtk_dialog_response (GTK_DIALOG (menudialog), GTK_RESPONSE_OK);
}
