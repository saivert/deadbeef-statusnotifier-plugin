/*
 * deadbeef-statusnotifier-plugin - Copyright (C) 2015 Vladimir Perepechin
 *
 * menu.c
 * Copyright (C) 2014 Vladimir Perepechin <vovochka13@gmail.com>
 *
 * This file is part of deadbeef-statusnotifier-plugin.
 *
 * deadbeef-statusnotifier-plugin is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * deadbeef-statusnotifier-plugin is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * deadbeef-statusnotifier-plugin. If not, see http://www.gnu.org/licenses/
 */

#include "sni.h"

typedef struct {
    DbusmenuMenuitem *menu;        // root menu item
    DbusmenuMenuitem *item_play;   // play button
    DbusmenuMenuitem *item_stop;   // stop button
    DbusmenuMenuitem *item_next;   // next button
    DbusmenuMenuitem *item_prev;   // prev button
    DbusmenuMenuitem *item_pref;   // preference (settings) button
    DbusmenuMenuitem *item_random; // shuffle button
    DbusmenuMenuitem *item_show;   // show/hide button
    DbusmenuMenuitem *item_help;   // about button
    DbusmenuMenuitem *item_quit;   // quit button

    DbusmenuMenuitem *pb_menu; // playback menu root item
    /* Shuffle menu */
    struct {
        DbusmenuMenuitem *item_linear;         // DDB_SHUFFLE_OFF
        DbusmenuMenuitem *item_shuffle_tracks; // DDB_SHUFFLE_TRACKS
        DbusmenuMenuitem *item_random;         // DDB_SHUFFLE_RANDOM
        DbusmenuMenuitem *item_shuffle_albums; // DDB_SHUFFLE_ALBUMS

        // Private
        DbusmenuMenuitem *active_now; // now playback state
    } pb_order;
    /* Repeat menu */
    struct {
        DbusmenuMenuitem *item_all;    // DDB_REPEAT_ALL
        DbusmenuMenuitem *item_none;   // DDB_REPEAT_NONE
        DbusmenuMenuitem *item_single; // DDB_REPEAT_SINGLE

        // Private
        DbusmenuMenuitem *active_now; // now playback state
    } pb_loop;

    uintptr_t mlock;
} sni_menu_t;

static sni_menu_t *sm = NULL;
static DB_functions_t *deadbeef;

/* Generate actoion procedure name */
#define SNI_CALLBACK_NAME(item) on_##item##_activate
/* Menu item callback declaration */
#define SNI_MENU_ITEM_CALLBACK(item) void SNI_CALLBACK_NAME(item)(DbusmenuMenuitem * menuitem)

/* Menu item simple messaging function macro */
static inline void
sni_callback_message(unsigned msg) {
    if (deadbeef == NULL)
        return;
    deadbeef->sendmessage(msg, 0, 0, 0);
}
#define SNI_MENU_ITEM_MESSAGE(item, MSG)                                                           \
    SNI_MENU_ITEM_CALLBACK(item) {                                                                 \
        (void *)menuitem; /* unused messages cleanup */                                            \
        sni_callback_message(MSG);                                                                 \
    }

SNI_MENU_ITEM_MESSAGE(quit, DB_EV_TERMINATE);
SNI_MENU_ITEM_MESSAGE(play, DB_EV_TOGGLE_PAUSE);
SNI_MENU_ITEM_MESSAGE(stop, DB_EV_STOP);
SNI_MENU_ITEM_MESSAGE(next, DB_EV_NEXT);
SNI_MENU_ITEM_MESSAGE(prev, DB_EV_PREV);
SNI_MENU_ITEM_MESSAGE(random, DB_EV_PLAY_RANDOM);

SNI_MENU_ITEM_CALLBACK(show) { deadbeef_toogle_window(); }

SNI_MENU_ITEM_CALLBACK(pref) { deadbeef_preferences_activate(); }

SNI_MENU_ITEM_CALLBACK(help) { deadbeef_help_activate(); }

SNI_MENU_ITEM_CALLBACK(playback_order) {
    DB_functions_t *deadbeef = deadbeef_get_instance();
    if (deadbeef == NULL)
        return;
    guint32 val = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(menuitem), "pb_data"));
    deadbeef->conf_set_int("playback.order", val);
    deadbeef->sendmessage(DB_EV_CONFIGCHANGED, 0, 0, 0);
}

SNI_MENU_ITEM_CALLBACK(playback_loop) {
    DB_functions_t *deadbeef = deadbeef_get_instance();
    if (deadbeef == NULL)
        return;
    guint32 val = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(menuitem), "pb_data"));
    deadbeef->conf_set_int("playback.loop", val);
    deadbeef->sendmessage(DB_EV_CONFIGCHANGED, 0, 0, 0);
}

static inline void
change_toogle_items(DbusmenuMenuitem *old, DbusmenuMenuitem *new) {
    if (old)
        dbusmenu_menuitem_property_set_int(old, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE,
                                           DBUSMENU_MENUITEM_TOGGLE_STATE_UNCHECKED);
    dbusmenu_menuitem_property_set_int(new, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE,
                                       DBUSMENU_MENUITEM_TOGGLE_STATE_CHECKED);
}

void
update_window_controls(void) {
    if (deadbeef->conf_get_int(SNI_OPTION_MENU_TOGGLE, 1) == 0)
        return;
    if (sni_flag_get(SNI_FLAG_HIDDEN) && !deadbeef_window_is_visible())
        return;
    if ((!sni_flag_get(SNI_FLAG_HIDDEN)) && deadbeef_window_is_visible())
        return;

    if (sni_flag_get(SNI_FLAG_HIDDEN)) {
        g_debug("%s\n", "Update controls: SHOW");
        dbusmenu_menuitem_property_set(sm->item_show, DBUSMENU_MENUITEM_PROP_LABEL,
                                       _("Hide Player Window"));
        dbusmenu_menuitem_property_set(sm->item_show, DBUSMENU_MENUITEM_PROP_ICON_NAME, "go-down");
        sni_flag_unset(SNI_FLAG_HIDDEN);
    } else {
        g_debug("%s\n", "Update controls: HIDE");
        dbusmenu_menuitem_property_set(sm->item_show, DBUSMENU_MENUITEM_PROP_LABEL,
                                       _("Show Player Window"));
        dbusmenu_menuitem_property_set(sm->item_show, DBUSMENU_MENUITEM_PROP_ICON_NAME, "go-up");
        sni_flag_set(SNI_FLAG_HIDDEN);
    }
}

static inline void
update_playback_orders(const guint32 state) {
    DbusmenuMenuitem *item_changed = NULL;
    switch (state) {
    case DDB_SHUFFLE_OFF:
        item_changed = sm->pb_order.item_linear;
        break;
    case DDB_SHUFFLE_TRACKS:
        item_changed = sm->pb_order.item_shuffle_tracks;
        break;
    case DDB_SHUFFLE_RANDOM:
        item_changed = sm->pb_order.item_random;
        break;
    case DDB_SHUFFLE_ALBUMS:
        item_changed = sm->pb_order.item_shuffle_albums;
    }
    if (item_changed != sm->pb_order.active_now) {
        change_toogle_items(sm->pb_order.active_now, item_changed);
        sm->pb_order.active_now = item_changed;
    }
}

static inline void
update_playback_loops(const guint32 state) {
    DbusmenuMenuitem *item_changed = NULL;
    switch (state) {
    case DDB_REPEAT_ALL:
        item_changed = sm->pb_loop.item_all;
        break;
    case DDB_REPEAT_OFF:
        item_changed = sm->pb_loop.item_none;
        break;
    case DDB_REPEAT_SINGLE:
        item_changed = sm->pb_loop.item_single;
        break;
    }
    if (item_changed != sm->pb_loop.active_now) {
        change_toogle_items(sm->pb_loop.active_now, item_changed);
        sm->pb_loop.active_now = item_changed;
    }
}

void
update_playback_controls(void) {
    if (deadbeef->conf_get_int(SNI_OPTION_MENU_PLAYBACK, 1) == 0)
        return;
    if ((sm == NULL) || (sm->pb_menu == NULL))
        return;

    deadbeef->mutex_lock(sm->mlock);

    update_playback_orders(deadbeef->conf_get_int("playback.order", 0));
    update_playback_loops(deadbeef->conf_get_int("playback.loop", 0));

    deadbeef->mutex_unlock(sm->mlock);
}

void
update_play_controls(int play) {

    static int play_state = SNI_STATE_TOOGLE_PAUSE;

    if (play_state == play)
        return;

    deadbeef->mutex_lock(sm->mlock);

    play_state = play;
    switch (play) {
    case SNI_STATE_TOOGLE_PLAY:
        dbusmenu_menuitem_property_set(sm->item_play, DBUSMENU_MENUITEM_PROP_LABEL, _("Pause"));
        dbusmenu_menuitem_property_set(sm->item_play, DBUSMENU_MENUITEM_PROP_ICON_NAME,
                                       "media-playback-pause");
        dbusmenu_menuitem_property_set_bool(sm->item_stop, DBUSMENU_MENUITEM_PROP_ENABLED, TRUE);
        break;
    case SNI_STATE_TOOGLE_STOP:
        dbusmenu_menuitem_property_set(sm->item_play, DBUSMENU_MENUITEM_PROP_LABEL, _("Play"));
        dbusmenu_menuitem_property_set(sm->item_play, DBUSMENU_MENUITEM_PROP_ICON_NAME,
                                       "media-playback-start");
        dbusmenu_menuitem_property_set_bool(sm->item_stop, DBUSMENU_MENUITEM_PROP_ENABLED, FALSE);
        break;
    case SNI_STATE_TOOGLE_PAUSE:
        dbusmenu_menuitem_property_set(sm->item_play, DBUSMENU_MENUITEM_PROP_LABEL, _("Play"));
        dbusmenu_menuitem_property_set(sm->item_play, DBUSMENU_MENUITEM_PROP_ICON_NAME,
                                       "media-playback-start");
        dbusmenu_menuitem_property_set_bool(sm->item_stop, DBUSMENU_MENUITEM_PROP_ENABLED, TRUE);
        break;
    }

    deadbeef->mutex_unlock(sm->mlock);
}

static DbusmenuMenuitem *
create_menu_item(gchar *label, gchar *icon_name, SNIContextMenuItemType item_type) {
    DbusmenuMenuitem *item;

    item = dbusmenu_menuitem_new();
    dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_LABEL, label);

    switch (item_type) {
    case SNI_MENU_ITEM_TYPE_SEPARATOR:
        dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_TYPE, "separator");
        break;
    case SNI_MENU_ITEM_TYPE_RADIO:
        dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE,
                                       DBUSMENU_MENUITEM_TOGGLE_RADIO);
        break;
    case SNI_MENU_ITEM_TYPE_CHECKBOX:
        dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE,
                                       DBUSMENU_MENUITEM_TOGGLE_CHECK);
        break;
    case SNI_MENU_ITEM_TYPE_COMMON:
    default:
        break;
    }

    if (icon_name)
        dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_ICON_NAME, icon_name);

    dbusmenu_menuitem_property_set_bool(item, DBUSMENU_MENUITEM_PROP_ENABLED, TRUE);
    dbusmenu_menuitem_property_set_bool(item, DBUSMENU_MENUITEM_PROP_VISIBLE, TRUE);

    return item;
}

#define CREATE_SEPARATOR_ITEM(menu)                                                                \
    dbusmenu_menuitem_child_append(menu,                                                           \
                                   create_menu_item(NULL, NULL, SNI_MENU_ITEM_TYPE_SEPARATOR));

#define CREATE_PLAYBACK_ITEM(menu, name, label, mode, callback)                                    \
    do {                                                                                           \
        sm->pb_##menu.item_##name = create_menu_item(label, NULL, SNI_MENU_ITEM_TYPE_RADIO);       \
        g_object_set_data(G_OBJECT(sm->pb_##menu.item_##name), "pb_data", GUINT_TO_POINTER(mode)); \
        g_signal_connect(sm->pb_##menu.item_##name, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,       \
                         G_CALLBACK(callback), NULL);                                              \
                                                                                                   \
        dbusmenu_menuitem_child_append(sm->pb_menu, sm->pb_##menu.item_##name);                    \
    } while (0)

static inline DbusmenuMenuitem *
create_menu_playback(void) {
    sm->pb_menu = create_menu_item(_("Playback"), NULL, SNI_MENU_ITEM_TYPE_COMMON);

    CREATE_PLAYBACK_ITEM(order, linear, _("Shuffle - Off"), PLAYBACK_ORDER_LINEAR,
                         SNI_CALLBACK_NAME(playback_order));
    CREATE_PLAYBACK_ITEM(order, shuffle_tracks, _("Shuffle - Tracks"),
                         PLAYBACK_ORDER_SHUFFLE_TRACKS, SNI_CALLBACK_NAME(playback_order));
    CREATE_PLAYBACK_ITEM(order, shuffle_albums, _("Shuffle - Albums"),
                         PLAYBACK_ORDER_SHUFFLE_ALBUMS, SNI_CALLBACK_NAME(playback_order));
    CREATE_PLAYBACK_ITEM(order, random, _("Shuffle - Random Tracks"), PLAYBACK_ORDER_RANDOM,
                         SNI_CALLBACK_NAME(playback_order));

    CREATE_SEPARATOR_ITEM(sm->pb_menu);

    CREATE_PLAYBACK_ITEM(loop, all, _("Repeat - All"), PLAYBACK_MODE_LOOP_ALL,
                         SNI_CALLBACK_NAME(playback_loop));
    CREATE_PLAYBACK_ITEM(loop, single, _("Repeat - Single Track"), PLAYBACK_MODE_LOOP_SINGLE,
                         SNI_CALLBACK_NAME(playback_loop));
    CREATE_PLAYBACK_ITEM(loop, none, _("Repeat - Off"), PLAYBACK_MODE_NOLOOP,
                         SNI_CALLBACK_NAME(playback_loop));

    update_playback_controls();

    return sm->pb_menu;
}

#undef CREATE_PLAYBACK_ITEM

#define CREATE_CONTEXT_ITEM(name, label, icon, callback)                                           \
    do {                                                                                           \
        sm->item_##name = create_menu_item(label, icon, SNI_MENU_ITEM_TYPE_COMMON);                \
        g_signal_connect(sm->item_##name, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,                 \
                         G_CALLBACK(callback), NULL);                                              \
        dbusmenu_menuitem_child_append(sm->menu, sm->item_##name);                                 \
    } while (0)

static inline DbusmenuMenuitem *
create_context_menu(void) {

    sm->menu = create_menu_item(_("Deadbeef"), NULL, SNI_MENU_ITEM_TYPE_COMMON);
    dbusmenu_menuitem_set_root(sm->menu, TRUE);

    /** Common media controls **/

    CREATE_CONTEXT_ITEM(play, _("Play"), "media-playback-start", SNI_CALLBACK_NAME(play));
    CREATE_CONTEXT_ITEM(stop, _("Stop"), "media-playback-stop", SNI_CALLBACK_NAME(stop));
    CREATE_CONTEXT_ITEM(prev, _("Previous"), "media-skip-backward", SNI_CALLBACK_NAME(prev));
    CREATE_CONTEXT_ITEM(next, _("Next"), "media-skip-forward", SNI_CALLBACK_NAME(next));
    CREATE_CONTEXT_ITEM(random, _("Play Random"), NULL, SNI_CALLBACK_NAME(random));

    CREATE_SEPARATOR_ITEM(sm->menu);

    /** Playback settings controls **/
    if (deadbeef->conf_get_int(SNI_OPTION_MENU_PLAYBACK, 1)) {
        dbusmenu_menuitem_child_append(sm->menu, create_menu_playback());
        CREATE_SEPARATOR_ITEM(sm->menu);
    }
    if (deadbeef->conf_get_int(SNI_OPTION_MENU_TOGGLE, 1)) {
        if (deadbeef->conf_get_int("gtkui.hide_tray_icon", 0) == 0) {
            CREATE_CONTEXT_ITEM(show, _("Show Player Window"), "go-up", SNI_CALLBACK_NAME(show));
        } else {
            CREATE_CONTEXT_ITEM(show, _("Hide Player Window"), "go-down", SNI_CALLBACK_NAME(show));
        }
    }
    if (deadbeef_preferences_available())
        CREATE_CONTEXT_ITEM(pref, _("Preferences"), "preferences-system", SNI_CALLBACK_NAME(pref));

    if (deadbeef_help_available())
        CREATE_CONTEXT_ITEM(help, _("Help"), "help-contents", SNI_CALLBACK_NAME(help));

    CREATE_SEPARATOR_ITEM(sm->menu);

    CREATE_CONTEXT_ITEM(quit, _("Quit"), "application-exit", SNI_CALLBACK_NAME(quit));

    return sm->menu;
}

#undef CREATE_CONTEXT_ITEM
#undef CREATE_SEPARATOR_ITEM

DbusmenuMenuitem *
get_context_menu_item(SNIContextMenuItem item) {
    if (sm) {
        switch (item) {
        case SNI_MENU_ITEM_PLAY:
            return sm->item_play;
        case SNI_MENU_ITEM_STOP:
            return sm->item_stop;
        case SNI_MENU_ITEM_NEXT:
            return sm->item_next;
        case SNI_MENU_ITEM_PREV:
            return sm->item_prev;
        case SNI_MENU_ITEM_RANDOM:
            return sm->item_random;
        case SNI_MENU_ITEM_QUIT:
            return sm->item_quit;
        }
    }
    return NULL;
}

DbusmenuMenuitem *
get_context_menu(void) {
    return (sm) ? sm->menu : NULL;
}

int
sni_context_menu_create(void) {
    deadbeef = deadbeef_get_instance();
    if (deadbeef == NULL)
        return -1;

    sm = calloc(1, sizeof(sni_menu_t));
    if (sm == NULL)
        return -1;

    sm->mlock = deadbeef->mutex_create();
    if (sm->mlock) {
        create_context_menu();
    } else {
        sni_free_null(sm);
        return -1;
    }

    return 0;
}

void
sni_context_menu_release(void) {
    if (sm) {
        deadbeef->mutex_free(sm->mlock);
        sni_free_null(sm);
    }
}
