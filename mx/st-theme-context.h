/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-theme-context.c: holds global information about a tree of styled objects
 *
 * Copyright 2009, 2010 Red Hat, Inc.
 * Copyright 2009 Florian Müllner
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

#ifndef __MX_ST_THEME_CONTEXT_H__
#define __MX_ST_THEME_CONTEXT_H__

#include <clutter/clutter.h>
#include <pango/pango.h>
#include "st-theme-node.h"

G_BEGIN_DECLS

/**
 * SECTION:MxStThemeContext
 * @short_description: holds global information about a tree of styled objects
 *
 * #MxStThemeContext is responsible for managing information global to a tree of styled objects,
 * such as the set of stylesheets or the default font. In normal usage, a #MxStThemeContext
 * is bound to a #ClutterStage; a singleton #MxStThemeContext can be obtained for a #ClutterStage
 * by using mx_st_theme_context_get_for_stage().
 */

typedef struct _MxStThemeContextClass MxStThemeContextClass;

#define MX_TYPE_ST_THEME_CONTEXT             (mx_st_theme_context_get_type ())
#define MX_ST_THEME_CONTEXT(object)          (G_TYPE_CHECK_INSTANCE_CAST ((object), MX_TYPE_ST_THEME_CONTEXT, MxStThemeContext))
#define MX_ST_THEME_CONTEXT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), MX_TYPE_ST_THEME_CONTEXT, MxStThemeContextClass))
#define MX_IS_ST_THEME_CONTEXT(object)       (G_TYPE_CHECK_INSTANCE_TYPE ((object), MX_TYPE_ST_THEME_CONTEXT))
#define MX_IS_ST_THEME_CONTEXT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), MX_TYPE_ST_THEME_CONTEXT))
#define MX_ST_THEME_CONTEXT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), MX_TYPE_ST_THEME_CONTEXT, MxStThemeContextClass))

GType mx_st_theme_context_get_type (void) G_GNUC_CONST;

MxStThemeContext *mx_st_theme_context_new           (void);
MxStThemeContext *mx_st_theme_context_get_for_stage (ClutterStage *stage);

void                        mx_st_theme_context_set_theme      (MxStThemeContext             *context,
                                                             MxStTheme                    *theme);
MxStTheme *                   mx_st_theme_context_get_theme      (MxStThemeContext             *context);

void                        mx_st_theme_context_set_resolution (MxStThemeContext             *context,
                                                             gdouble                     resolution);
void                        mx_st_theme_context_set_default_resolution (MxStThemeContext *context);
double                      mx_st_theme_context_get_resolution (MxStThemeContext             *context);
void                        mx_st_theme_context_set_font       (MxStThemeContext             *context,
                                                             const PangoFontDescription *font);
const PangoFontDescription *mx_st_theme_context_get_font       (MxStThemeContext             *context);

MxStThemeNode *               mx_st_theme_context_get_root_node  (MxStThemeContext             *context);

G_END_DECLS

#endif /* __MX_ST_THEME_CONTEXT_H__ */
