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

#include "config.h"

#include "tilda-gtk-theme.h"
#include "debug.h"

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <string.h>

static gboolean initialized = FALSE;
static gchar *startup_default_theme = NULL;
static gboolean env_gtk_theme_set = FALSE;
static GtkCssProvider *override_provider = NULL;

static gboolean
has_gtk3_theme_css (const gchar *theme_dir)
{
    gchar *css_path = g_build_filename (theme_dir, "gtk-3.0", "gtk.css", NULL);
    gboolean found = g_file_test (css_path, G_FILE_TEST_IS_REGULAR);
    g_free (css_path);

    if (found) {
        return TRUE;
    }

    guint max_even_minor = gtk_get_minor_version () & ~1u;
    for (guint minor = max_even_minor; minor >= 14; minor -= 2) {
        gchar *subdir = g_strdup_printf ("gtk-3.%u", minor);
        css_path = g_build_filename (theme_dir, subdir, "gtk.css", NULL);
        found = g_file_test (css_path, G_FILE_TEST_IS_REGULAR);
        g_free (css_path);
        g_free (subdir);

        if (found) {
            return TRUE;
        }
    }

    return FALSE;
}

static void
add_theme_if_unseen (const gchar *theme_name,
                     GHashTable  *seen,
                     GPtrArray   *themes)
{
    if (theme_name == NULL || theme_name[0] == '\0') {
        return;
    }

    if (g_hash_table_contains (seen, theme_name)) {
        return;
    }

    gchar *owned_name = g_strdup (theme_name);
    g_hash_table_add (seen, owned_name);
    g_ptr_array_add (themes, owned_name);
}

static void
collect_themes_from_dir (const gchar *themes_dir,
                         GHashTable  *seen,
                         GPtrArray   *themes)
{
    if (themes_dir == NULL || !g_file_test (themes_dir, G_FILE_TEST_IS_DIR)) {
        return;
    }

    GDir *dir = g_dir_open (themes_dir, 0, NULL);
    if (dir == NULL) {
        return;
    }

    const gchar *entry_name;
    while ((entry_name = g_dir_read_name (dir)) != NULL) {
        if (entry_name[0] == '.' || g_hash_table_contains (seen, entry_name)) {
            continue;
        }

        gchar *theme_path = g_build_filename (themes_dir, entry_name, NULL);
        if (g_file_test (theme_path, G_FILE_TEST_IS_DIR) &&
            has_gtk3_theme_css (theme_path))
        {
            add_theme_if_unseen (entry_name, seen, themes);
        }
        g_free (theme_path);
    }

    g_dir_close (dir);
}

static void
collect_builtin_resource_themes (GHashTable *seen,
                                 GPtrArray  *themes)
{
    gchar **children = g_resources_enumerate_children ("/org/gtk/libgtk/theme",
                                                       G_RESOURCE_LOOKUP_FLAGS_NONE,
                                                       NULL);
    if (children == NULL) {
        return;
    }

    for (gsize i = 0; children[i] != NULL; i++) {
        gsize len = strlen (children[i]);
        if (len == 0) {
            continue;
        }

        gchar *name = (children[i][len - 1] == '/')
            ? g_strndup (children[i], len - 1)
            : g_strdup (children[i]);

#ifndef G_OS_WIN32
        if (g_strcmp0 (name, "win32") == 0) {
            g_free (name);
            continue;
        }
#endif

        gchar *resource_css = g_strdup_printf ("/org/gtk/libgtk/theme/%s/gtk.css", name);
        if (g_resources_get_info (resource_css, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL, NULL, NULL)) {
            add_theme_if_unseen (name, seen, themes);
        }

        g_free (resource_css);
        g_free (name);
    }

    g_strfreev (children);
}

static gint
compare_theme_names (gconstpointer a,
                     gconstpointer b)
{
    const gchar *name_a = *(const gchar * const *) a;
    const gchar *name_b = *(const gchar * const *) b;

    gint cmp = g_ascii_strcasecmp (name_a, name_b);
    if (cmp != 0) {
        return cmp;
    }

    return g_strcmp0 (name_a, name_b);
}

void
tilda_gtk_theme_init (void)
{
    DEBUG_FUNCTION ("tilda_gtk_theme_init");

    if (initialized) {
        return;
    }

    GtkSettings *settings = gtk_settings_get_default ();
    if (settings == NULL) {
        return;
    }

    g_object_get (settings, "gtk-theme-name", &startup_default_theme, NULL);

    const gchar *env_theme = g_getenv ("GTK_THEME");
    env_gtk_theme_set = (env_theme != NULL && env_theme[0] != '\0');

    initialized = TRUE;
}

void
tilda_gtk_theme_apply (const gchar *theme_name)
{
    DEBUG_FUNCTION ("tilda_gtk_theme_apply");

    tilda_gtk_theme_init ();

    GtkSettings *settings = gtk_settings_get_default ();
    if (settings == NULL) {
        return;
    }

    GdkScreen *screen = gdk_screen_get_default ();

    if (override_provider != NULL && screen != NULL) {
        gtk_style_context_remove_provider_for_screen (screen,
                                                      GTK_STYLE_PROVIDER (override_provider));
        g_clear_object (&override_provider);
    }

    if (theme_name == NULL || theme_name[0] == '\0') {
#if GTK_CHECK_VERSION(3, 20, 0)
        gtk_settings_reset_property (settings, "gtk-theme-name");
#else
        if (startup_default_theme != NULL) {
            g_object_set (settings, "gtk-theme-name", startup_default_theme, NULL);
        }
#endif
    } else {
        gchar *colon = strchr (theme_name, ':');
        if (colon != NULL) {
            gchar *base_name = g_strndup (theme_name, (gsize) (colon - theme_name));
            const gchar *variant = colon + 1;

            g_object_set (settings, "gtk-theme-name", base_name, NULL);

            if (env_gtk_theme_set && screen != NULL) {
                GtkCssProvider *named = gtk_css_provider_get_named (base_name,
                                                                    variant[0] != '\0' ? variant : NULL);
                if (named != NULL) {
                    override_provider = g_object_ref (named);
                    gtk_style_context_add_provider_for_screen (screen,
                                                               GTK_STYLE_PROVIDER (override_provider),
                                                               GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
                }
            }

            g_free (base_name);
        } else {
            g_object_set (settings, "gtk-theme-name", theme_name, NULL);

            if (env_gtk_theme_set && screen != NULL) {
                GtkCssProvider *named = gtk_css_provider_get_named (theme_name, NULL);
                if (named != NULL) {
                    override_provider = g_object_ref (named);
                    gtk_style_context_add_provider_for_screen (screen,
                                                               GTK_STYLE_PROVIDER (override_provider),
                                                               GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
                }
            }
        }
    }

    if (screen != NULL) {
        gtk_style_context_reset_widgets (screen);
    }
}

GPtrArray *
tilda_gtk_theme_get_installed_themes (void)
{
    DEBUG_FUNCTION ("tilda_gtk_theme_get_installed_themes");

    GPtrArray *themes = g_ptr_array_new_with_free_func (g_free);
    GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);

    /* 1. $XDG_DATA_HOME/themes */
    gchar *user_data_themes = g_build_filename (g_get_user_data_dir (), "themes", NULL);
    collect_themes_from_dir (user_data_themes, seen, themes);
    g_free (user_data_themes);

    /* 2. $HOME/.themes */
    gchar *home_themes = g_build_filename (g_get_home_dir (), ".themes", NULL);
    collect_themes_from_dir (home_themes, seen, themes);
    g_free (home_themes);

    /* 3. $XDG_DATA_DIRS/themes */
    const gchar * const *system_data_dirs = g_get_system_data_dirs ();
    for (gsize i = 0; system_data_dirs != NULL && system_data_dirs[i] != NULL; i++) {
        gchar *sys_themes = g_build_filename (system_data_dirs[i], "themes", NULL);
        collect_themes_from_dir (sys_themes, seen, themes);
        g_free (sys_themes);
    }

    /* 4. $GTK_DATA_PREFIX/share/themes or DATADIR/themes */
    const gchar *data_prefix = g_getenv ("GTK_DATA_PREFIX");
    gchar *prefix_themes = (data_prefix != NULL && data_prefix[0] != '\0')
        ? g_build_filename (data_prefix, "share", "themes", NULL)
        : g_build_filename (DATADIR, "themes", NULL);
    collect_themes_from_dir (prefix_themes, seen, themes);
    g_free (prefix_themes);

    /* 5. Built-in GTK 3 GResource themes */
    collect_builtin_resource_themes (seen, themes);

    g_hash_table_destroy (seen);

    g_ptr_array_sort (themes, compare_theme_names);

    return themes;
}
