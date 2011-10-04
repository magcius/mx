/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-theme-node.h: style information for one node in a tree of themed objects
 *
 * Copyright 2008-2010 Red Hat, Inc.
 * Copyright 2009, 2010 Florian Müllner
 * Copyright 2010 Giovanni Campagna
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

#ifndef __MX_ST_THEME_NODE_H__
#define __MX_ST_THEME_NODE_H__

#include <clutter/clutter.h>
#include "st-border-image.h"
#include "st-icon-colors.h"
#include "st-shadow.h"

G_BEGIN_DECLS

/**
 * SECTION:MxStThemeNode
 * @short_description: style information for one node in a tree of themed objects
 *
 * A #MxStThemeNode represents the CSS style information (the set of CSS properties) for one
 * node in a tree of themed objects. In typical usage, it represents the style information
 * for a single #ClutterActor. A #MxStThemeNode is immutable: attributes such as the
 * CSS classes for the node are passed in at construction. If the attributes of the node
 * or any parent node change, the node should be discarded and a new node created.
 * #MxStThemeNode has generic accessors to look up properties by name and specific
 * accessors for standard CSS properties that add caching and handling of various
 * details of the CSS specification. #MxStThemeNode also has convenience functions to help
 * in implementing a #ClutterActor with borders and padding.
 */

typedef struct _StTheme          StTheme;
typedef struct _StThemeContext   StThemeContext;

typedef struct _MxStThemeNode      MxStThemeNode;
typedef struct _MxStThemeNodeClass MxStThemeNodeClass;

#define ST_TYPE_THEME_NODE              (mx_st_theme_node_get_type ())
#define MX_ST_THEME_NODE(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), ST_TYPE_THEME_NODE, MxStThemeNode))
#define MX_ST_THEME_NODE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass),     ST_TYPE_THEME_NODE, MxStThemeNodeClass))
#define ST_IS_THEME_NODE(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), ST_TYPE_THEME_NODE))
#define ST_IS_THEME_NODE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass),     ST_TYPE_THEME_NODE))
#define MX_ST_THEME_NODE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj),     ST_TYPE_THEME_NODE, MxStThemeNodeClass))

typedef enum {
    ST_SIDE_TOP,
    ST_SIDE_RIGHT,
    ST_SIDE_BOTTOM,
    ST_SIDE_LEFT
} StSide;

typedef enum {
    ST_CORNER_TOPLEFT,
    ST_CORNER_TOPRIGHT,
    ST_CORNER_BOTTOMRIGHT,
    ST_CORNER_BOTTOMLEFT
} StCorner;

/* These are the CSS values; that doesn't mean we have to implement blink... */
typedef enum {
    ST_TEXT_DECORATION_UNDERLINE    = 1 << 0,
    ST_TEXT_DECORATION_OVERLINE     = 1 << 1,
    ST_TEXT_DECORATION_LINE_THROUGH = 1 << 2,
    ST_TEXT_DECORATION_BLINK        = 1 << 3
} StTextDecoration;

typedef enum {
    ST_TEXT_ALIGN_LEFT = PANGO_ALIGN_LEFT,
    ST_TEXT_ALIGN_CENTER = PANGO_ALIGN_CENTER,
    ST_TEXT_ALIGN_RIGHT = PANGO_ALIGN_RIGHT,
    ST_TEXT_ALIGN_JUSTIFY
} StTextAlign;

typedef enum {
  ST_GRADIENT_NONE,
  ST_GRADIENT_VERTICAL,
  ST_GRADIENT_HORIZONTAL,
  ST_GRADIENT_RADIAL
} StGradientType;

GType mx_st_theme_node_get_type (void) G_GNUC_CONST;

MxStThemeNode *mx_st_theme_node_new (StThemeContext *context,
                                MxStThemeNode    *parent_node,   /* can be null */
                                StTheme        *theme,         /* can be null */
                                GType           element_type,
                                const char     *element_id,
                                const char     *element_class,
                                const char     *pseudo_class,
                                const char     *inline_style);

MxStThemeNode *mx_st_theme_node_get_parent (MxStThemeNode *node);

StTheme *mx_st_theme_node_get_theme (MxStThemeNode *node);

gboolean    mx_st_theme_node_equal (MxStThemeNode *node_a, MxStThemeNode *node_b);

GType       mx_st_theme_node_get_element_type  (MxStThemeNode *node);
const char *mx_st_theme_node_get_element_id    (MxStThemeNode *node);
const char *mx_st_theme_node_get_element_class (MxStThemeNode *node);
const char *mx_st_theme_node_get_pseudo_class  (MxStThemeNode *node);

/* Generic getters ... these are not cached so are less efficient. The other
 * reason for adding the more specific version is that we can handle the
 * details of the actual CSS rules, which can be complicated, especially
 * for fonts
 */
gboolean mx_st_theme_node_lookup_color  (MxStThemeNode  *node,
                                      const char   *property_name,
                                      gboolean      inherit,
                                      ClutterColor *color);
gboolean mx_st_theme_node_lookup_double (MxStThemeNode  *node,
                                      const char   *property_name,
                                      gboolean      inherit,
                                      double       *value);
gboolean mx_st_theme_node_lookup_length (MxStThemeNode *node,
                                      const char  *property_name,
                                      gboolean     inherit,
                                      gdouble     *length);
gboolean mx_st_theme_node_lookup_shadow (MxStThemeNode  *node,
                                      const char   *property_name,
                                      gboolean      inherit,
                                      StShadow    **shadow);

/* Easier-to-use variants of the above, for application-level use */
void          mx_st_theme_node_get_color  (MxStThemeNode  *node,
                                        const char   *property_name,
                                        ClutterColor *color);
gdouble       mx_st_theme_node_get_double (MxStThemeNode  *node,
                                        const char   *property_name);
gdouble       mx_st_theme_node_get_length (MxStThemeNode  *node,
                                        const char   *property_name);
StShadow     *mx_st_theme_node_get_shadow (MxStThemeNode  *node,
                                        const char   *property_name);

/* Specific getters for particular properties: cached
 */
void mx_st_theme_node_get_background_color (MxStThemeNode  *node,
                                         ClutterColor *color);
void mx_st_theme_node_get_foreground_color (MxStThemeNode  *node,
                                         ClutterColor *color);
void mx_st_theme_node_get_background_gradient (MxStThemeNode   *node,
                                            StGradientType *type,
                                            ClutterColor   *start,
                                            ClutterColor   *end);

const char *mx_st_theme_node_get_background_image (MxStThemeNode *node);

int    mx_st_theme_node_get_border_width  (MxStThemeNode  *node,
                                        StSide        side);
int    mx_st_theme_node_get_border_radius (MxStThemeNode  *node,
                                        StCorner      corner);
void   mx_st_theme_node_get_border_color  (MxStThemeNode  *node,
                                        StSide        side,
                                        ClutterColor *color);

int    mx_st_theme_node_get_outline_width (MxStThemeNode  *node);
void   mx_st_theme_node_get_outline_color (MxStThemeNode  *node,
                                        ClutterColor *color);

double mx_st_theme_node_get_padding       (MxStThemeNode  *node,
                                        StSide        side);

double mx_st_theme_node_get_horizontal_padding (MxStThemeNode *node);
double mx_st_theme_node_get_vertical_padding   (MxStThemeNode *node);

int    mx_st_theme_node_get_width         (MxStThemeNode  *node);
int    mx_st_theme_node_get_height        (MxStThemeNode  *node);
int    mx_st_theme_node_get_min_width     (MxStThemeNode  *node);
int    mx_st_theme_node_get_min_height    (MxStThemeNode  *node);
int    mx_st_theme_node_get_max_width     (MxStThemeNode  *node);
int    mx_st_theme_node_get_max_height    (MxStThemeNode  *node);

int    mx_st_theme_node_get_transition_duration (MxStThemeNode *node);

StTextDecoration mx_st_theme_node_get_text_decoration (MxStThemeNode *node);

StTextAlign mx_st_theme_node_get_text_align (MxStThemeNode *node);

/* Font rule processing is pretty complicated, so we just hardcode it
 * under the standard font/font-family/font-size/etc names. This means
 * you can't have multiple separate styled fonts for a single item,
 * but that should be OK.
 */
const PangoFontDescription *mx_st_theme_node_get_font (MxStThemeNode *node);

StBorderImage *mx_st_theme_node_get_border_image (MxStThemeNode *node);
StShadow      *mx_st_theme_node_get_box_shadow   (MxStThemeNode *node);
StShadow      *mx_st_theme_node_get_text_shadow  (MxStThemeNode *node);

StShadow      *mx_st_theme_node_get_background_image_shadow (MxStThemeNode *node);

StIconColors  *mx_st_theme_node_get_icon_colors  (MxStThemeNode *node);

/* Helpers for get_preferred_width()/get_preferred_height() ClutterActor vfuncs */
void mx_st_theme_node_adjust_for_height       (MxStThemeNode  *node,
                                            float        *for_height);
void mx_st_theme_node_adjust_preferred_width  (MxStThemeNode  *node,
                                            float        *min_width_p,
                                            float        *natural_width_p);
void mx_st_theme_node_adjust_for_width        (MxStThemeNode  *node,
                                            float        *for_width);
void mx_st_theme_node_adjust_preferred_height (MxStThemeNode  *node,
                                            float        *min_height_p,
                                            float        *natural_height_p);

/* Helper for allocate() ClutterActor vfunc */
void mx_st_theme_node_get_content_box         (MxStThemeNode        *node,
                                            const ClutterActorBox *allocation,
                                            ClutterActorBox       *content_box);
/* Helper for MxStThemeNodeTransition */
void mx_st_theme_node_get_paint_box           (MxStThemeNode           *node,
                                            const ClutterActorBox *allocation,
                                            ClutterActorBox       *paint_box);
/* Helper for background prerendering */
void mx_st_theme_node_get_background_paint_box (MxStThemeNode           *node,
                                             const ClutterActorBox *allocation,
                                             ClutterActorBox       *paint_box);

gboolean mx_st_theme_node_geometry_equal (MxStThemeNode *node,
                                       MxStThemeNode *other);
gboolean mx_st_theme_node_paint_equal    (MxStThemeNode *node,
                                       MxStThemeNode *other);

void mx_st_theme_node_paint (MxStThemeNode            *node,
                          const ClutterActorBox  *box,
                          guint8                  paint_opacity);

void mx_st_theme_node_copy_cached_paint_state (MxStThemeNode *node,
                                            MxStThemeNode *other);

G_END_DECLS

#endif /* __MX_ST_THEME_NODE_H__ */
