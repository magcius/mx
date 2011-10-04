/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-theme.h: A set of CSS stylesheets used for rule matching
 *
 * Copyright 2008, 2009 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ST_THEME_H__
#define __ST_THEME_H__

#include <glib-object.h>

#include "st-theme-node.h"

G_BEGIN_DECLS

/**
 * SECTION:MxStTheme
 * @short_description: a set of stylesheets
 *
 * #MxStTheme holds a set of stylesheets. (The "cascade" of the name
 * Cascading Stylesheets.) A #MxStTheme can be set to apply to all the actors
 * in a stage using mx_st_theme_context_set_theme() or applied to a subtree
 * of actors using st_widget_set_theme().
 */

typedef struct _MxStThemeClass MxStThemeClass;

#define MX_TYPE_ST_THEME              (mx_st_theme_get_type ())
#define MX_ST_THEME(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), MX_TYPE_ST_THEME, MxStTheme))
#define MX_ST_THEME_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), MX_TYPE_ST_THEME, MxStThemeClass))
#define MX_IS_ST_THEME(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), MX_TYPE_ST_THEME))
#define MX_IS_ST_THEME_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), MX_TYPE_ST_THEME))
#define MX_ST_THEME_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), MX_TYPE_ST_THEME, MxStThemeClass))

GType  mx_st_theme_get_type (void) G_GNUC_CONST;

MxStTheme *mx_st_theme_new (const char *application_stylesheet,
                       const char *theme_stylesheet,
                       const char *default_stylesheet);

gboolean  mx_st_theme_load_stylesheet        (MxStTheme *theme, const char *path, GError **error);
void      mx_st_theme_unload_stylesheet      (MxStTheme *theme, const char *path);
GSList   *mx_st_theme_get_custom_stylesheets (MxStTheme *theme);

G_END_DECLS

#endif /* __MX_ST_THEME_H__ */
