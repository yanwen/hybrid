#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib.h>
#include "util.h"
#include "blist.h"
#include "conv.h"
#include "info.h"
#include "gtkutils.h"
#include "gtkcellrendererexpander.h"

HybridBlist *blist = NULL;

static void hybrid_blist_buddy_icon_save(HybridBuddy *buddy);
static void hybrid_blist_buddy_to_cache(HybridBuddy *buddy,
							HybridBlistCacheType type);
static void hybrid_blist_group_to_cache(HybridGroup *group);
static HybridGroup *hybrid_blist_find_group_by_name(
							HybridAccount *account, const gchar *name);

HybridBlist*
hybrid_blist_create()
{
	HybridBlist *imb = g_new0(HybridBlist, 1);
	return imb;
}

static void
render_column(HybridBlist *blist)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	/* expander columns */
	column = gtk_tree_view_column_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(blist->treeview), column);
	gtk_tree_view_column_set_visible(column, FALSE);
	gtk_tree_view_set_expander_column(GTK_TREE_VIEW(blist->treeview), column);

	/* main column */
	blist->column = gtk_tree_view_column_new ();
	gtk_tree_view_append_column(GTK_TREE_VIEW(blist->treeview), blist->column);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(blist->treeview));
	//gtk_tree_view_column_set_sizing(blist->column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	/* group expander */
	renderer = pidgin_cell_renderer_expander_new();
	g_object_set(renderer, "expander-visible", TRUE, NULL);
	gtk_tree_view_column_pack_start(blist->column, renderer, FALSE);
	gtk_tree_view_column_set_attributes(blist->column, renderer,
					    "visible", HYBRID_BLIST_GROUP_EXPANDER_COLUMN_VISIBLE,
					    NULL);

	/* contact expander */
	renderer = pidgin_cell_renderer_expander_new();
	gtk_tree_view_column_pack_start(blist->column, renderer, FALSE);
	gtk_tree_view_column_set_attributes(blist->column, renderer,
					    "visible", HYBRID_BLIST_CONTACT_EXPANDER_COLUMN_VISIBLE,
					    NULL);

	/* portrait */
	renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(blist->column, renderer, FALSE);
	gtk_tree_view_column_set_attributes(blist->column, renderer,
						"pixbuf", HYBRID_BLIST_BUDDY_ICON,
						"visible", HYBRID_BLIST_BUDDY_ICON_COLUMN_VISIBLE,
						NULL);
	g_object_set(renderer, "xalign", 1.0, "xpad", 3, "ypad", 0, NULL);

	/* name */
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(blist->column, renderer, TRUE);
	gtk_tree_view_column_set_attributes(blist->column, renderer,
						"markup", HYBRID_BLIST_BUDDY_NAME,
						NULL);
	g_object_set(renderer, "xalign", 0.0, "xpad", 3, "ypad", 0, NULL);
	g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);

	/* protocol icon */
	renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(blist->column, renderer, FALSE);
	gtk_tree_view_column_set_attributes(blist->column, renderer,
						"pixbuf", HYBRID_BLIST_PROTO_ICON,
						"visible", HYBRID_BLIST_PROTO_ICON_COLUMN_VISIBLE,
						NULL);
	g_object_set(renderer, "xalign", 0.0, "xpad", 6, "ypad", 0, NULL);

	/* status icon */
	renderer = gtk_cell_renderer_pixbuf_new();
	g_object_set(renderer, "xalign", 0.0, "xpad", 6, "ypad", 0, NULL);
	gtk_tree_view_column_pack_start(blist->column, renderer, FALSE);
	gtk_tree_view_column_set_attributes(blist->column, renderer,
						"pixbuf", HYBRID_BLIST_STATUS_ICON,
						"visible", HYBRID_BLIST_STATUS_ICON_COLUMN_VISIBLE,
						NULL);

}

static void
instant_message_menu_cb(GtkWidget *widget, gpointer user_data)
{
	hybrid_chat_panel_create(user_data);
}

static void
buddy_information_menu_cb(GtkWidget *widget, gpointer user_data)
{
	HybridBuddy *buddy = (HybridBuddy*)user_data;
	HybridAccount *account = buddy->account;
	HybridModule *proto = account->proto;
	HybridInfo *info;

	/* Call the get information callback in module space. */
	if (proto->info->get_info) {
		
		info = hybrid_info_create(buddy);

		proto->info->get_info(account, buddy);
	}
}

/**
 * Callback function of the buddy-move menu's activate event.
 */
static void
buddy_move_cb(GtkWidget *widget, gpointer user_data)
{
	gchar *group_name;
	const gchar *new_group_name;
	HybridBuddy *buddy;
	HybridGroup *group;
	HybridGroup *orig_group;
	HybridAccount *account;
	HybridModule *module;
		
	GdkPixbuf *status_icon;
	GdkPixbuf *proto_icon;
	GdkPixbuf *buddy_icon;

	GtkTreeView  *tree;
	GtkTreeModel *model;

	buddy = (HybridBuddy*)user_data;
	account = buddy->account;
	module = account->proto;

	new_group_name = gtk_menu_item_get_label(GTK_MENU_ITEM(widget));

	if (!(group = hybrid_blist_find_group_by_name(account, new_group_name))) {
		hybrid_debug_error("blist", "find group by name:%s", new_group_name);

		return;
	}

	if (module->info->buddy_move) {
		if (!module->info->buddy_move(account, buddy, group)) {
			hybrid_debug_error("blist", "move buddy protocol error");
			return;
		}

		/* 
		 * Move the buddy to the dst group. My method is to remove the buddy in 
		 * the old group, and then create a new one in the new group, and set it
		 * with the same attribute. I don't know how to do in other way, if you 
		 * know, tell me :)
		 */
		tree = GTK_TREE_VIEW(blist->treeview);
		model = gtk_tree_view_get_model(tree);

		gtk_tree_store_remove(GTK_TREE_STORE(model), &buddy->iter);
		gtk_tree_store_append(GTK_TREE_STORE(model), &buddy->iter, &group->iter);

		status_icon = hybrid_create_presence_pixbuf(buddy->state, 16);
		proto_icon = hybrid_create_proto_icon(module->info->name, 16);

		buddy_icon = hybrid_create_round_pixbuf(buddy->icon_data,
				buddy->icon_data_length, 32);

		if (BUDDY_IS_INVISIBLE(buddy) || BUDDY_IS_OFFLINE(buddy)) {
			gdk_pixbuf_saturate_and_pixelate(buddy_icon, buddy_icon, 0.0, FALSE);
		}

		gtk_tree_store_set(GTK_TREE_STORE(model), &buddy->iter,
				HYBRID_BLIST_BUDDY_ID, buddy->id,
				HYBRID_BLIST_BUDDY_ICON, buddy_icon,
				HYBRID_BLIST_STATUS_ICON, status_icon,
				HYBRID_BLIST_BUDDY_NAME, buddy->name,
				HYBRID_BLIST_OBJECT_COLUMN, buddy,
				HYBRID_BLIST_BUDDY_STATE, buddy->state,
				HYBRID_BLIST_GROUP_EXPANDER_COLUMN_VISIBLE, FALSE,
				HYBRID_BLIST_CONTACT_EXPANDER_COLUMN_VISIBLE, FALSE,
				HYBRID_BLIST_STATUS_ICON_COLUMN_VISIBLE, TRUE,
				HYBRID_BLIST_PROTO_ICON_COLUMN_VISIBLE, TRUE,
				HYBRID_BLIST_BUDDY_ICON_COLUMN_VISIBLE, TRUE,
				-1);
		/* 
		 * Change the number of total buddies and online buddies
		 * of the source group and the destination group. 
		 */
		orig_group = buddy->parent;

		group->buddy_count ++;
		orig_group->buddy_count --;

		if (!BUDDY_IS_INVISIBLE(buddy) && !BUDDY_IS_OFFLINE(buddy)) {
			group->online_count ++;
			orig_group->online_count --;
		}


		group_name = g_strdup_printf("<b>%s</b> (%d/%d)",
				group->name, group->online_count, group->buddy_count);

		gtk_tree_store_set(GTK_TREE_STORE(model), &group->iter,
				HYBRID_BLIST_BUDDY_NAME, group_name, -1);

		g_free(group_name);

		group_name = g_strdup_printf("<b>%s</b> (%d/%d)",
				orig_group->name, orig_group->online_count,
				orig_group->buddy_count);

		gtk_tree_store_set(GTK_TREE_STORE(model), &orig_group->iter,
				HYBRID_BLIST_BUDDY_NAME, group_name, -1);

		g_free(group_name);

		/*
		 * Now we need to process the local cache file blist.xml,
		 * move the xml node to the new group. My method is also
		 * remove the old one and create a new one in the new group node.
		 */
		if (!buddy->cache_node) {
			hybrid_debug_error("blist",
					"the buddy to move has no xml cache node.");
			return;
		}

		xmlnode_remove_node(buddy->cache_node);

		buddy->cache_node = xmlnode_new_child(group->cache_node, "buddy");
		xmlnode_new_prop(buddy->cache_node, "id", buddy->id);
		xmlnode_new_prop(buddy->cache_node, "mood", buddy->mood);
		xmlnode_new_prop(buddy->cache_node, "name", buddy->name);
		xmlnode_new_prop(buddy->cache_node, "icon", buddy->icon_name);
		xmlnode_new_prop(buddy->cache_node, "crc", buddy->icon_crc);

		hybrid_blist_cache_flush();

	}
}

static void
hybrid_buddy_remove(HybridBuddy *buddy)
{
	HybridAccount *account;
	HybridModule *module;
	GtkTreeView *tree;
	GtkTreeModel *model;

	g_return_if_fail(buddy != NULL);

	tree    = GTK_TREE_VIEW(blist->treeview);
	model   = gtk_tree_view_get_model(tree);
	account = buddy->account;
	module  = account->proto;

	/*
	 * If the buddy_remove hook function not defined, or if the
	 * buddy_remove defined but returned FALSE, then we should
	 * cancel the remove action in GUI, yes, we just return.
	 */
	if (!module->info->buddy_remove ||
		!module->info->buddy_remove(account, buddy)) {
		return;
	}

	/* Remove the buddy from account's buddy_list hashtable. */
	g_hash_table_remove(account->buddy_list, buddy);

	/* Remove the buddy from the blist's TreeView. */
	gtk_tree_store_remove(GTK_TREE_STORE(model), &buddy->iter);

	/* Remove the buddy from the xml cache context. */
	xmlnode_remove_node(buddy->cache_node);

	/* Synchronize the xml cache with the local xml file. */
	hybrid_blist_cache_flush();

	hybrid_blist_buddy_destroy(buddy);
}

static void
remove_buddy_menu_cb(GtkWidget *widget, gpointer user_data)
{
	HybridBuddy *buddy;
	gchar *confirm_text;

	buddy = (HybridBuddy*)user_data;

	confirm_text = g_strdup_printf(_("Are you sure to delete"
				" the buddy <b>%s</b> (%s)"), buddy->name, buddy->id);
	hybrid_confirm_show(_("Confirm"), confirm_text, _("Delete"),
			(confirm_cb)hybrid_buddy_remove, buddy);
	g_free(confirm_text);

}

static GtkWidget*
create_buddy_menu(GtkWidget *treeview, GtkTreePath *path)
{
	GtkWidget *menu;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkWidget *group_menu;
	GtkWidget *child_menu;
	HybridAccount *account;
	HybridBuddy *buddy;
	HybridGroup *group;

	GHashTableIter hash_iter;
	gpointer key;

	g_return_val_if_fail(treeview != NULL, NULL);
	g_return_val_if_fail(path != NULL, NULL);

	menu = gtk_menu_new();

	/* Select a tree row when right click on it. */
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));

	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, path);
	gtk_tree_model_get(model, &iter, HYBRID_BLIST_OBJECT_COLUMN, &buddy, -1);

	account = buddy->account;
	
	hybrid_create_menu(menu, _("Instant Message"), "instants", TRUE,
			instant_message_menu_cb, buddy);
	hybrid_create_menu(menu, _("Buddy Information"), "profile", TRUE,
			buddy_information_menu_cb, buddy);
	hybrid_create_menu_seperator(menu);
	child_menu = hybrid_create_menu(menu, _("Move To"), "move", TRUE, NULL, NULL);

	group_menu = gtk_menu_new();
	
	g_hash_table_iter_init(&hash_iter, account->group_list);
	while (g_hash_table_iter_next(&hash_iter, &key, (gpointer*)&group)) {

		hybrid_create_menu(group_menu, group->name, NULL, TRUE,
				buddy_move_cb, buddy);
	}

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(child_menu), group_menu);

	hybrid_create_menu(menu, _("Remove Buddy"), "remove", TRUE,
					remove_buddy_menu_cb, buddy);
	hybrid_create_menu(menu, _("Rename Buddy"), "rename", TRUE, NULL, NULL);
	hybrid_create_menu(menu, _("View Chat Logs"), "logs", TRUE, NULL, NULL);

	return menu;
}

static gboolean
button_press_cb(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	GtkWidget *menu;
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeSelection *selection;
	gint depth;

	if (event->button == 3) {

		model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
				(gint)event->x, (gint)event->y, &path, NULL, NULL, NULL);

		/* Select a tree row when right click on it. */
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
		gtk_tree_selection_select_path(selection, path);

		depth = gtk_tree_path_get_depth(path);

		if (depth > 1) {
			menu = create_buddy_menu(widget, path);

			gtk_widget_show_all(menu);

			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
					(event != NULL) ? event->button : 0,
					gdk_event_get_time((GdkEvent*)event));
		}

		return TRUE;
	}

	return FALSE;
}

static void 
row_activated_cb(GtkTreeView *treeview, GtkTreePath *path,
		GtkTreeViewColumn *col,	gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	HybridBuddy *buddy;
	gint depth;

	model = gtk_tree_view_get_model(treeview);
	gtk_tree_model_get_iter(model, &iter, path);

	depth = gtk_tree_path_get_depth(path);

	if (depth > 1) {

		gtk_tree_model_get(model, &iter,
				HYBRID_BLIST_OBJECT_COLUMN, &buddy, -1);

		hybrid_chat_panel_create(buddy);
	}
}

void 
hybrid_blist_init()
{
	blist = hybrid_blist_create();

	blist->treemodel = gtk_tree_store_new(HYBRID_BLIST_COLUMNS,
			G_TYPE_STRING,
			GDK_TYPE_PIXBUF,
			G_TYPE_STRING,
			GDK_TYPE_PIXBUF,
			GDK_TYPE_PIXBUF,
			G_TYPE_INT,
			G_TYPE_POINTER,
			G_TYPE_BOOLEAN,
			G_TYPE_BOOLEAN,
			G_TYPE_BOOLEAN,
			G_TYPE_BOOLEAN,
			G_TYPE_BOOLEAN);

	blist->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(
				blist->treemodel));
	g_object_unref(blist->treemodel);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(blist->treeview), FALSE);

	render_column(blist);

	gtk_tree_sortable_set_sort_column_id(
			GTK_TREE_SORTABLE(blist->treemodel),
			HYBRID_BLIST_BUDDY_STATE, GTK_SORT_DESCENDING);

	g_signal_connect(blist->treeview, "button-press-event",
			G_CALLBACK(button_press_cb), NULL);

	g_signal_connect(blist->treeview, "row-activated",
			G_CALLBACK(row_activated_cb), NULL);

}

HybridGroup*
hybrid_blist_add_group(HybridAccount *ac, const gchar *id, const gchar *name)
{
	g_return_val_if_fail(name != NULL && blist != NULL, NULL);

	gchar *temp;
	HybridGroup *group;
	GdkPixbuf *proto_icon;

	g_return_val_if_fail(ac != NULL, NULL);
	g_return_val_if_fail(id != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	/*
	 * Make sure we will add a group to an account only
	 * when the account's connection status is CONNECTED 
	 */
	if (!HYBRID_IS_CONNECTED(ac)) {
		return NULL;
	}

	/*
	 * If current account has a group exists with the specified ID,
	 * then just return the existing group.
	 */
	if ((group = hybrid_blist_find_group(ac, id))) {
		return group;
	}

	group = g_new0(HybridGroup, 1);
	proto_icon  = hybrid_create_proto_icon(ac->proto->info->name, 16);
	gtk_tree_store_append(blist->treemodel, &group->iter, NULL);

	temp = g_strdup_printf("<b>%s</b>", name);

	gtk_tree_store_set(blist->treemodel, &group->iter,
			HYBRID_BLIST_BUDDY_NAME, temp,
			HYBRID_BLIST_PROTO_ICON, proto_icon,
			HYBRID_BLIST_OBJECT_COLUMN, group,
			HYBRID_BLIST_GROUP_EXPANDER_COLUMN_VISIBLE, TRUE,
			HYBRID_BLIST_CONTACT_EXPANDER_COLUMN_VISIBLE, FALSE,
			HYBRID_BLIST_STATUS_ICON_COLUMN_VISIBLE, FALSE,
			HYBRID_BLIST_PROTO_ICON_COLUMN_VISIBLE, TRUE,
			HYBRID_BLIST_BUDDY_ICON_COLUMN_VISIBLE, FALSE,
			-1);

	g_free(temp);

	group->name = g_strdup(name);
	group->id = g_strdup(id);
	group->account = ac;

	g_hash_table_insert(ac->group_list, group->id, group);

	g_object_unref(proto_icon);

	hybrid_blist_group_to_cache(group);

	return group;
}

void
hybrid_blist_group_destroy(HybridGroup *group)
{
	if (group) {
		g_free(group->id);
		g_free(group->name);

		g_free(group);
	}
}

HybridBuddy*
hybrid_blist_add_buddy(HybridAccount *ac, HybridGroup *parent, const gchar *id,
		const gchar *name)
{
	GdkPixbuf *status_icon;
	GdkPixbuf *proto_icon;
	GdkPixbuf *buddy_icon;
	HybridBuddy *buddy;
	GtkTreeModel *treemodel;
	gchar *group_name;

	g_return_val_if_fail(blist != NULL, NULL);
	g_return_val_if_fail(parent != NULL, NULL);
	g_return_val_if_fail(id != NULL, NULL);

	/*
	 * Make sure we will add a buddy to an account only
	 * when the account's connection status is CONNECTED 
	 */
	if (!HYBRID_IS_CONNECTED(ac)) {
		return NULL;
	}

	/*
	 * If current account has a buddy exists with the specified ID,
	 * then just return the existing buddy.
	 */
	if ((buddy = hybrid_blist_find_buddy(ac, id))) {
		return buddy;
	}

	status_icon = hybrid_create_presence_pixbuf(HYBRID_STATE_OFFLINE, 16);
	proto_icon = hybrid_create_proto_icon(ac->proto->info->name, 16);

	/*
	 * Here we need to set a default icon for the buddy, never use 
	 * hybrid_blist_add_buddy_icon(), for it will modify the cache
	 * of "crc" attribute, but we only allow this function to modify
	 * the "id" and "name" attribute, so we use the original method
	 * to set a default icon.
	 */
	buddy_icon = hybrid_create_default_icon(32);
	gdk_pixbuf_saturate_and_pixelate(buddy_icon, buddy_icon, 0.0, FALSE);

	buddy = g_new0(HybridBuddy, 1);
	
	treemodel = gtk_tree_view_get_model(GTK_TREE_VIEW(blist->treeview));

	gtk_tree_store_append(GTK_TREE_STORE(treemodel), &buddy->iter, &parent->iter);

	gtk_tree_store_set(GTK_TREE_STORE(treemodel), &buddy->iter,
			HYBRID_BLIST_BUDDY_ID, id,
			HYBRID_BLIST_BUDDY_ICON, buddy_icon,
			HYBRID_BLIST_STATUS_ICON, status_icon,
			//HYBRID_BLIST_PROTO_ICON, proto_icon,
			HYBRID_BLIST_BUDDY_NAME, name,
			HYBRID_BLIST_OBJECT_COLUMN, buddy,
			HYBRID_BLIST_BUDDY_STATE, HYBRID_STATE_OFFLINE,
			HYBRID_BLIST_GROUP_EXPANDER_COLUMN_VISIBLE, FALSE,
			HYBRID_BLIST_CONTACT_EXPANDER_COLUMN_VISIBLE, FALSE,
			HYBRID_BLIST_STATUS_ICON_COLUMN_VISIBLE, TRUE,
			HYBRID_BLIST_PROTO_ICON_COLUMN_VISIBLE, TRUE,
			HYBRID_BLIST_BUDDY_ICON_COLUMN_VISIBLE, TRUE,
			-1);

	buddy->id = g_strdup(id);
	buddy->name = g_strdup(name);
	buddy->account = ac;
	buddy->parent = parent;

	g_hash_table_insert(ac->buddy_list, buddy->id, buddy);

	g_object_unref(status_icon);
	g_object_unref(proto_icon);
	g_object_unref(buddy_icon);

	/*
	 * Now we should update the buddy count displayed besides the gruop name.
	 * We did add a new buddy, so we show add the buddy_count by one.
	 */
	parent->buddy_count ++;
	group_name = g_strdup_printf("<b>%s</b> (%d/%d)",
			parent->name, parent->online_count, parent->buddy_count);

	gtk_tree_store_set(GTK_TREE_STORE(treemodel), &parent->iter,
			HYBRID_BLIST_BUDDY_NAME, group_name, -1);

	g_free(group_name);

	hybrid_blist_buddy_to_cache(buddy, HYBRID_BLIST_CACHE_ADD);

	return buddy;
}

const gchar*
hybrid_blist_get_buddy_checksum(HybridBuddy *buddy)
{
	g_return_val_if_fail(buddy != NULL, NULL);

	return buddy->icon_crc;
}

void
hybrid_blist_buddy_destroy(HybridBuddy *buddy)
{
	if (buddy) {
		g_free(buddy->id);
		g_free(buddy->name);
		g_free(buddy->mood);
		g_free(buddy->icon_name);
		g_free(buddy->icon_data);
		g_free(buddy->icon_crc);
		g_free(buddy);
	}
}

/**
 * Set the name field.
 */
static void
hybrid_blist_set_name_field(HybridBuddy *buddy)
{
	gchar *text;
	gchar *mood;
	gchar *tmp;
	const gchar *name;

	g_return_if_fail(buddy != NULL);

	if (buddy->name && *(buddy->name) != '\0') {
		name = buddy->name;

	} else {
		name = buddy->id;
	}

	if (buddy->mood && *(buddy->mood) != '\0') {

		tmp = g_markup_escape_text(buddy->mood, -1);

		mood = g_strdup_printf(
				"\n<small><span color=\"#8f8f8f\">%s</span></small>", 
				tmp);
		g_free(tmp);

	} else {
		mood = g_strdup("");
	}

	tmp = g_markup_escape_text(name, -1);

	text = g_strdup_printf("%s%s", tmp, mood);

	gtk_tree_store_set(blist->treemodel, &buddy->iter,
			HYBRID_BLIST_BUDDY_NAME, text, -1);

	g_free(mood);
	g_free(text);
	g_free(tmp);
}

/**
 * Set the buddy state.
 */
static void
hybrid_blist_set_state_field(HybridBuddy *buddy)
{
	GdkPixbuf *pixbuf;
	GError *err = NULL;
	guchar *icon_data;
	gsize icon_data_length;

	g_return_if_fail(buddy != NULL);

	pixbuf = hybrid_create_presence_pixbuf(buddy->state, 16);
	
	gtk_tree_store_set(blist->treemodel, &buddy->iter,
			HYBRID_BLIST_BUDDY_STATE, buddy->state,
			HYBRID_BLIST_STATUS_ICON, pixbuf, -1);

	g_object_unref(pixbuf);
	pixbuf = NULL;

	/* set the portrait */
	gint scale_size = 32;

	if (buddy->icon_data == NULL || buddy->icon_data_length == 0) {
		/* 
		 * Load the default icon. Note that we don't set the buddy's
		 * icon_data attribute to the data we got from the default icon,
		 * because if the icon_data attribute is not NULL, it will try
		 * to localize the icon, but we don't want to localize the 
		 * default icon, so if buddy doesn't have a self-defined icon,
		 * keep its icon_data attribute NULL :) .
		 */
		if (!g_file_get_contents(PIXMAPS_DIR"icons/icon.png", 
					(gchar**)&icon_data, &icon_data_length, &err)) {

			hybrid_debug_error("blist", "load the default icon:%s", err->message);
			g_error_free(err);
			return;
		}

		pixbuf = hybrid_create_round_pixbuf(icon_data, icon_data_length,
				scale_size);

	} else {
		pixbuf = hybrid_create_round_pixbuf(buddy->icon_data, 
				buddy->icon_data_length, scale_size);
	}

	/* If buddy is not online, show a grey icon. */
	if (BUDDY_IS_INVISIBLE(buddy) || BUDDY_IS_OFFLINE(buddy)) {
		gdk_pixbuf_saturate_and_pixelate(pixbuf, pixbuf, 0.0, FALSE);
	}

	gtk_tree_store_set(blist->treemodel, &buddy->iter,
			HYBRID_BLIST_BUDDY_ICON, pixbuf, -1);

	g_object_unref(pixbuf);

}

void
hybrid_blist_set_buddy_name(HybridBuddy *buddy, const gchar *name)
{
	g_return_if_fail(buddy != NULL);

	g_free(buddy->name);
	buddy->name = g_strdup(name);

	hybrid_blist_set_name_field(buddy);
	hybrid_blist_buddy_to_cache(buddy, HYBRID_BLIST_CACHE_UPDATE_NAME);
}

void
hybrid_blist_set_buddy_mood(HybridBuddy *buddy, const gchar *mood)
{
	g_return_if_fail(buddy != NULL);

	g_free(buddy->mood);
	buddy->mood = g_strdup(mood);

	hybrid_blist_set_name_field(buddy);
	hybrid_blist_buddy_to_cache(buddy, HYBRID_BLIST_CACHE_UPDATE_MOOD);
}

void
hybrid_blist_set_buddy_icon(HybridBuddy *buddy, const guchar *icon_data,
		gsize len, const gchar *crc)
{
	g_return_if_fail(buddy != NULL);

	g_free(buddy->icon_crc);
	g_free(buddy->icon_data);
	buddy->icon_data = NULL;
	buddy->icon_data_length = len;
	buddy->icon_crc = g_strdup(crc);

	if (icon_data != NULL) {
		buddy->icon_data = g_memdup(icon_data, len);
	}

	hybrid_blist_set_state_field(buddy);
	hybrid_blist_buddy_icon_save(buddy);
	hybrid_blist_buddy_to_cache(buddy, HYBRID_BLIST_CACHE_UPDATE_ICON);
}

void
hybrid_blist_set_buddy_state(HybridBuddy *buddy, gint state)
{
	HybridGroup *group;
	GtkTreeModel *model;
	gchar *group_name;
	gint online;

	g_return_if_fail(buddy != NULL);

	/*
	 * Here we update the group's online count first by checking
	 * whether state has changed
	 */
	group = buddy->parent;

	online = group->online_count;

	if (buddy->state > 0 && state == 0) {
		online = group->online_count - 1;

	} else if (buddy->state == 0 && state > 0) {
		online = group->online_count + 1;
	}

	if (online != group->online_count) {
		group->online_count = online;

		group_name = g_strdup_printf("<b>%s</b> (%d/%d)",
				group->name, group->online_count, group->buddy_count);


		model = gtk_tree_view_get_model(GTK_TREE_VIEW(blist->treeview));

		gtk_tree_store_set(GTK_TREE_STORE(model),&group->iter,
				HYBRID_BLIST_BUDDY_NAME, group_name, -1);

		g_free(group_name);
	}

	if (buddy->state != state) {
		buddy->state = state;
		hybrid_blist_set_state_field(buddy);
	}
}

HybridGroup*
hybrid_blist_find_group(HybridAccount *account, const gchar *id)
{
	HybridGroup *group;

	g_return_val_if_fail(account != NULL, NULL);
	g_return_val_if_fail(id != NULL, NULL);

	if ((group = g_hash_table_lookup(account->group_list, id))) {
		return group;
	}

	return NULL;
}

static HybridGroup*
hybrid_blist_find_group_by_name(HybridAccount *account, const gchar *name)
{
	GHashTableIter hash_iter;
	gpointer key;
	HybridGroup *group;

	g_return_val_if_fail(account != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	g_hash_table_iter_init(&hash_iter, account->group_list);

	while (g_hash_table_iter_next(&hash_iter, &key, (gpointer*)&group)) {

		if (g_strcmp0(group->name, name) == 0) {
			return group;
		}
	}

	return NULL;
}

HybridBuddy*
hybrid_blist_find_buddy(HybridAccount *account, const gchar *id)
{
	HybridBuddy *buddy;

	g_return_val_if_fail(account != NULL, NULL);
	g_return_val_if_fail(id != NULL, NULL);

	if ((buddy = g_hash_table_lookup(account->buddy_list, id))) {
		return buddy;
	}

	return NULL;
}

static void 
hybrid_blist_group_to_cache(HybridGroup *group)
{
	HybridAccount *ac;
	HybridConfig *config;
	HybridBlistCache *cache;
	xmlnode *root;
	xmlnode *node;
	xmlnode *temp;
	gchar *username;
	gchar *protoname;
	gchar *id;
	gchar *name;

	g_return_if_fail(group != NULL);

	if (group->cache_node) {
		node = group->cache_node;
		goto group_exist;
	}

	ac = group->account;
	config = ac->config;
	cache = config->blist_cache;
	root = cache->root;

	if (!(node = xmlnode_find(root, "accounts"))) {
		hybrid_debug_error("blist", "can't find accounts node in blist cache");
		return;
	}

	/* find node named 'account' */
	if ((temp = xmlnode_child(node))) {

		while (temp) {
			if (!xmlnode_has_prop(temp, "username") ||
				!xmlnode_has_prop(temp, "proto")) {
				hybrid_debug_error("blist", "invalid blist cache node found");
				temp = xmlnode_next(temp);
				continue;
			}

			username = xmlnode_prop(temp, "username");
			protoname = xmlnode_prop(temp, "proto");

			if (g_strcmp0(ac->username, username) == 0 &&
				g_strcmp0(ac->proto->info->name, protoname) == 0) {
				g_free(username);
				g_free(protoname);
				node = temp;
				goto account_exist;
			}

			temp = xmlnode_next(temp);

			g_free(username);
			g_free(protoname);
		}
	}

	node = xmlnode_new_child(node, "account");
	xmlnode_new_prop(node, "username", ac->username);
	xmlnode_new_prop(node, "proto", ac->proto->info->name);

account_exist:
	if (!(temp = xmlnode_find(node, "buddies"))) {
		node = xmlnode_new_child(node, "buddies");

	} else {
		node = temp;	
	}

	/* Now node point to node named 'buddies'', make it
	 * point to group node.
	 *
	 * check whether there was a group node that we need.
	 */
	if ((temp = xmlnode_child(node))) {

		while (temp) {

			if (!xmlnode_has_prop(temp, "id") ||
				!xmlnode_has_prop(temp, "name")) {
				hybrid_debug_error("blist", "invalid blist cache group node found");
				temp = xmlnode_next(temp);
				continue;
			}
			
			id = xmlnode_prop(temp, "id");
			name = xmlnode_prop(temp, "name");

			if (g_strcmp0(id, group->id) == 0 &&
				g_strcmp0(name, group->name) == 0) {
				g_free(id);
				g_free(name);
				node = temp;
				goto group_exist;
			}

			temp = xmlnode_next(temp);

			g_free(id);
			g_free(name);
		}
	}

	node = xmlnode_new_child(node, "group");

group_exist:
	if (xmlnode_has_prop(node, "id")) {
		xmlnode_set_prop(node, "id", group->id);

	} else {
		xmlnode_new_prop(node, "id", group->id);
	}

	if (xmlnode_has_prop(node, "name")) {
		xmlnode_set_prop(node, "name", group->name);

	} else {
		xmlnode_new_prop(node, "name", group->name);
	}

	group->cache_node = node;

	hybrid_blist_cache_flush();
}


/**
 * Write the buddy information to the cache which in fact is 
 * a XML tree in the memory, if you want to synchronize the cache
 * with the cache file, use hybrid_blist_cache_flush().
 *
 * @param buddy The buddy to write to cache.
 * @param type  The action of writing to cache.
 */
static void
hybrid_blist_buddy_to_cache(HybridBuddy *buddy, HybridBlistCacheType type)
{
	HybridAccount *ac;
	HybridConfig *config;
	HybridBlistCache *cache;
	xmlnode *root;
	xmlnode *node;
	xmlnode *temp;
	gchar *id;

	g_return_if_fail(buddy != NULL);

	if (buddy->cache_node) {
		node = buddy->cache_node;
		goto buddy_exist;
	}

	ac = buddy->account;
	config = ac->config;
	cache = config->blist_cache;
	root = cache->root;

	if (!(node = buddy->parent->cache_node)) {
		hybrid_debug_error("blist", 
				"group node isn't cached, cache buddy failed");
		return;
	}

	/* whether there's a buddy node */
	if ((temp = xmlnode_child(node))) {
		
		while (temp) {

			if (!xmlnode_has_prop(temp, "id")) {
				hybrid_debug_error("blist",
						"invalid blist cache buddy node found");
				temp = xmlnode_next(temp);
				continue;
			}
			
			id = xmlnode_prop(temp, "id");

			if (g_strcmp0(id, buddy->id) == 0) {
				g_free(id);
				node = temp;
				goto buddy_exist;
			}

			temp = xmlnode_next(temp);

			g_free(id);
		}
	}

	node = xmlnode_new_child(node, "buddy");

buddy_exist:

	if (type == HYBRID_BLIST_CACHE_ADD) {

		if (xmlnode_has_prop(node, "id")) {
			xmlnode_set_prop(node, "id", buddy->id);

		} else {
			xmlnode_new_prop(node, "id", buddy->id);
		}
	}

	if (type == HYBRID_BLIST_CACHE_ADD ||
		type == HYBRID_BLIST_CACHE_UPDATE_NAME) {

		if (xmlnode_has_prop(node, "name")) {
			xmlnode_set_prop(node, "name", buddy->name);

		} else {
			xmlnode_new_prop(node, "name", buddy->name);
		}
	}

	if (type == HYBRID_BLIST_CACHE_UPDATE_MOOD) {

		if (xmlnode_has_prop(node, "mood")) {
			xmlnode_set_prop(node, "mood", buddy->mood);

		} else {
			xmlnode_new_prop(node, "mood", buddy->mood);
		}
	}

	if (type == HYBRID_BLIST_CACHE_UPDATE_ICON) {

		if (xmlnode_has_prop(node, "crc")) {
			xmlnode_set_prop(node, "crc", buddy->icon_crc);

		} else {
			xmlnode_new_prop(node, "crc", buddy->icon_crc);
		}

		if (xmlnode_has_prop(node, "icon")) {
			xmlnode_set_prop(node, "icon", buddy->icon_name);

		} else {
			xmlnode_new_prop(node, "icon", buddy->icon_name);
		}
	}

	/* Set the buddy's xml cache node property. */
	buddy->cache_node = node;

	hybrid_blist_cache_flush();
}


/**
 * Save the buddy's icon to the local file.The naming method is:
 * SHA1(buddy_id + '_' + proto_name).type
 */
static void
hybrid_blist_buddy_icon_save(HybridBuddy *buddy)
{
	gchar *name;
	gchar *hashed_name;
	HybridAccount *account;
	HybridModule *module;
	HybridConfig *config;

	g_return_if_fail(buddy != NULL);

	if (buddy->icon_data == NULL || buddy->icon_data_length == 0) {
		return;
	}

	account = buddy->account;
	module = account->proto;
	config = account->config;


	if (!buddy->icon_name) {
		name = g_strdup_printf("%s_%s", module->info->name, buddy->id);
		hashed_name = hybrid_sha1(name, strlen(name));
		g_free(name);

		buddy->icon_name = g_strdup_printf("%s.jpg", hashed_name);

		name = g_strdup_printf("%s/%s.jpg", config->icon_path, hashed_name);
		g_free(hashed_name);

	} else {
		name = g_strdup_printf("%s/%s", config->icon_path, buddy->icon_name);
	}

	g_file_set_contents(name, (gchar*)buddy->icon_data,
			buddy->icon_data_length, NULL);

	g_free(name);
}
