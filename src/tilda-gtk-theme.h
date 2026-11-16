/* vim: set ts=4 sts=4 sw=4 expandtab textwidth=112: */
/*
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TILDA_GTK_THEME_H
#define TILDA_GTK_THEME_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * Captures the initial system GTK theme before Tilda applies any configured
 * override. Safe to call multiple times; must be called after gtk_init().
 */
void tilda_gtk_theme_init (void);

/**
 * Applies @theme_name immediately to the running GTK application without
 * requiring a restart. Passing NULL or an empty string restores the system
 * default GTK theme.
 */
void tilda_gtk_theme_apply (const gchar *theme_name);

/**
 * Scans standard GTK 3 theme directories ($XDG_DATA_HOME/themes, ~/.themes,
 * $XDG_DATA_DIRS/themes, $GTK_DATA_PREFIX/share/themes or DATADIR/themes) as
 * well as built-in GTK 3 GResource themes, returning a sorted, deduplicated
 * GPtrArray of newly-allocated theme name strings (configured with g_free as
 * its element free function).
 */
GPtrArray *tilda_gtk_theme_get_installed_themes (void);

G_END_DECLS

#endif /* TILDA_GTK_THEME_H */
