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

#include <config.h>

#include "st-texture-cache.h"
#include "st-theme.h"
#include "st-theme-context.h"

struct _MxStThemeContext {
  GObject parent;

  double resolution;
  PangoFontDescription *font;
  MxStThemeNode *root_node;
  MxStTheme *theme;
};

struct _MxStThemeContextClass {
  GObjectClass parent_class;
};

#define DEFAULT_RESOLUTION 96.
#define DEFAULT_FONT "sans-serif 10"

enum
{
  CHANGED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (MxStThemeContext, mx_st_theme_context, G_TYPE_OBJECT)

static void
mx_st_theme_context_finalize (GObject *object)
{
  MxStThemeContext *context = MX_ST_THEME_CONTEXT (object);

  if (context->root_node)
    g_object_unref (context->root_node);
  if (context->theme)
    g_object_unref (context->theme);

  pango_font_description_free (context->font);

  G_OBJECT_CLASS (mx_st_theme_context_parent_class)->finalize (object);
}

static void
mx_st_theme_context_class_init (MxStThemeContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mx_st_theme_context_finalize;

  signals[CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, /* no default handler slot */
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
mx_st_theme_context_init (MxStThemeContext *context)
{
  context->resolution = DEFAULT_RESOLUTION;
  context->font = pango_font_description_from_string (DEFAULT_FONT);
}

/**
 * mx_st_theme_context_new:
 *
 * Create a new theme context not associated with any #ClutterStage.
 * This can be useful in testing scenarios, or if using MxStThemeContext
 * with something other than #ClutterActor objects, but you generally
 * should use mx_st_theme_context_get_for_stage() instead.
 */
MxStThemeContext *
mx_st_theme_context_new (void)
{
  MxStThemeContext *context;

  context = g_object_new (MX_TYPE_ST_THEME_CONTEXT, NULL);

  return context;
}

static void
on_stage_destroy (ClutterStage *stage)
{
  MxStThemeContext *context = mx_st_theme_context_get_for_stage (stage);

  g_object_set_data (G_OBJECT (stage), "st-theme-context", NULL);
  g_object_unref (context);
}

static void
mx_st_theme_context_changed (MxStThemeContext *context)
{
  MxStThemeNode *old_root = context->root_node;
  context->root_node = NULL;

  g_signal_emit (context, signals[CHANGED], 0);

  if (old_root)
    g_object_unref (old_root);
}

/**
 * mx_st_theme_context_get_for_stage:
 * @stage: a #ClutterStage
 *
 * Gets a singleton theme context associated with the stage.
 *
 * Return value: (transfer none): the singleton theme context for the stage
 */
MxStThemeContext *
mx_st_theme_context_get_for_stage (ClutterStage *stage)
{
  MxStThemeContext *context;

  g_return_val_if_fail (CLUTTER_IS_STAGE (stage), NULL);

  context = g_object_get_data (G_OBJECT (stage), "st-theme-context");
  if (context)
    return context;

  context = mx_st_theme_context_new ();
  g_object_set_data (G_OBJECT (stage), "st-theme-context", context);
  g_signal_connect (stage, "destroy",
                    G_CALLBACK (on_stage_destroy), NULL);

  return context;
}

/**
 * mx_st_theme_context_set_theme:
 * @context: a #MxStThemeContext
 *
 * Sets the default set of theme stylesheets for the context. This theme will
 * be used for the root node and for nodes descending from it, unless some other
 * style is explicitely specified.
 */
void
mx_st_theme_context_set_theme (MxStThemeContext          *context,
                               MxStTheme                 *theme)
{
  g_return_if_fail (MX_IS_ST_THEME_CONTEXT (context));
  g_return_if_fail (theme == NULL || MX_IS_ST_THEME (theme));

  if (context->theme != theme)
    {
      if (context->theme)
        g_object_unref (context->theme);

      context->theme = theme;

      if (context->theme)
        g_object_ref (context->theme);

      mx_st_theme_context_changed (context);
    }
}

/**
 * mx_st_theme_context_get_theme:
 * @context: a #MxStThemeContext
 *
 * Gets the default theme for the context. See mx_st_theme_context_set_theme()
 *
 * Return value: (transfer none): the default theme for the context
 */
MxStTheme *
mx_st_theme_context_get_theme (MxStThemeContext *context)
{
  g_return_val_if_fail (MX_IS_ST_THEME_CONTEXT (context), NULL);

  return context->theme;
}

/**
 * mx_st_theme_context_set_resolution:
 * @context: a #MxStThemeContext
 * @resolution: resolution of the context (number of pixels in an "inch")
 *
 * Sets the resolution of the theme context. This is the scale factor
 * used to convert between points and the length units pt, in, and cm.
 * This does not necessarily need to correspond to the actual number
 * resolution of the device. A value of 72. means that points and
 * pixels are identical. The default value is 96.
 */
void
mx_st_theme_context_set_resolution (MxStThemeContext *context,
                                 double          resolution)
{
  g_return_if_fail (MX_IS_ST_THEME_CONTEXT (context));

  if (resolution == context->resolution)
    return;

  context->resolution = resolution;
  mx_st_theme_context_changed (context);
}

/**
 * mx_st_theme_context_set_default_resolution:
 * @context: a #MxStThemeContext
 *
 * Sets the resolution of the theme context to the default value of 96.
 * See mx_st_theme_context_set_resolution().
 */
void
mx_st_theme_context_set_default_resolution (MxStThemeContext *context)
{
  g_return_if_fail (MX_IS_ST_THEME_CONTEXT (context));

  if (context->resolution == DEFAULT_RESOLUTION)
    return;

  context->resolution = DEFAULT_RESOLUTION;
  mx_st_theme_context_changed (context);
}

/**
 * mx_st_theme_context_get_resolution:
 * @context: a #MxStThemeContext
 *
 * Gets the current resolution of the theme context.
 * See mx_st_theme_context_set_resolution().
 *
 * Return value: the resolution (in dots-per-"inch")
 */
double
mx_st_theme_context_get_resolution (MxStThemeContext *context)
{
  g_return_val_if_fail (MX_IS_ST_THEME_CONTEXT (context), DEFAULT_RESOLUTION);

  return context->resolution;
}

/**
 * mx_st_theme_context_set_font:
 * @context: a #MxStThemeContext
 * @font: the default font for theme context
 *
 * Sets the default font for the theme context. This is the font that
 * is inherited by the root node of the tree of theme nodes. If the
 * font is not overriden, then this font will be used. If the font is
 * partially modified (for example, with 'font-size: 110%', then that
 * modification is based on this font.
 */
void
mx_st_theme_context_set_font (MxStThemeContext             *context,
                           const PangoFontDescription *font)
{
  g_return_if_fail (MX_IS_ST_THEME_CONTEXT (context));
  g_return_if_fail (font != NULL);

  if (context->font == font ||
      pango_font_description_equal (context->font, font))
    return;

  pango_font_description_free (context->font);
  context->font = pango_font_description_copy (font);
  mx_st_theme_context_changed (context);
}

/**
 * mx_st_theme_context_get_font:
 * @context: a #MxStThemeContext
 *
 * Gets the default font for the theme context. See mx_st_theme_context_set_font().
 *
 * Return value: the default font for the theme context.
 */
const PangoFontDescription *
mx_st_theme_context_get_font (MxStThemeContext *context)
{
  g_return_val_if_fail (MX_IS_ST_THEME_CONTEXT (context), NULL);

  return context->font;
}

/**
 * mx_st_theme_context_get_root_node:
 * @context: a #MxStThemeContext
 *
 * Gets the root node of the tree of theme style nodes that associated with this
 * context. For the node tree associated with a stage, this node represents
 * styles applied to the stage itself.
 *
 * Return value: (transfer none): the root node of the context's style tree
 */
MxStThemeNode *
mx_st_theme_context_get_root_node (MxStThemeContext *context)
{
  if (context->root_node == NULL)
    context->root_node = mx_st_theme_node_new (context, NULL, context->theme,
                                               G_TYPE_NONE, NULL, NULL, NULL, NULL);

  return context->root_node;
}