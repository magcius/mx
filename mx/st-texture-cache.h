/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-texture-cache.h: Object for loading and caching images as textures
 *
 * Copyright 2009, 2010 Red Hat, Inc.
 * Copyright 2010, Maxim Ermilov
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

#ifndef __MX_ST_TEXTURE_CACHE_H__
#define __MX_ST_TEXTURE_CACHE_H__

#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <clutter/clutter.h>

#include <mx/mx-types.h>
#include <mx/st-theme-node.h>

#define MX_ST_TYPE_TEXTURE_CACHE                 (st_texture_cache_get_type ())
#define MX_ST_TEXTURE_CACHE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), MX_ST_TYPE_TEXTURE_CACHE, MxStTextureCache))
#define MX_ST_TEXTURE_CACHE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), MX_ST_TYPE_TEXTURE_CACHE, MxStTextureCacheClass))
#define MX_ST_IS_TEXTURE_CACHE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MX_ST_TYPE_TEXTURE_CACHE))
#define MX_ST_IS_TEXTURE_CACHE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), MX_ST_TYPE_TEXTURE_CACHE))
#define MX_ST_TEXTURE_CACHE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), MX_ST_TYPE_TEXTURE_CACHE, MxStTextureCacheClass))

typedef struct _MxStTextureCache MxStTextureCache;
typedef struct _MxStTextureCacheClass MxStTextureCacheClass;

typedef struct _MxStTextureCachePrivate MxStTextureCachePrivate;

struct _MxStTextureCache
{
  GObject parent;

  MxStTextureCachePrivate *priv;
};

struct _MxStTextureCacheClass
{
  GObjectClass parent_class;

};

typedef enum {
  MX_ST_TEXTURE_CACHE_POLICY_NONE,
  MX_ST_TEXTURE_CACHE_POLICY_FOREVER
} MxStTextureCachePolicy;

GType mx_st_texture_cache_get_type (void) G_GNUC_CONST;

MxStTextureCache* st_texture_cache_get_default (void);

ClutterGroup *
mx_st_texture_cache_load_sliced_image (MxStTextureCache    *cache,
                                    const gchar       *path,
                                    gint               grid_width,
                                    gint               grid_height);

ClutterActor *mx_st_texture_cache_bind_pixbuf_property (MxStTextureCache    *cache,
                                                     GObject           *object,
                                                     const char        *property_name);

ClutterActor *mx_st_texture_cache_load_icon_name (MxStTextureCache *cache,
                                               StThemeNode    *theme_node,
                                               const char     *name,
                                               MxStIconType      icon_type,
                                               gint            size);

ClutterActor *mx_st_texture_cache_load_gicon (MxStTextureCache *cache,
                                           StThemeNode    *theme_node,
                                           GIcon          *icon,
                                           gint            size);

ClutterActor *mx_st_texture_cache_load_thumbnail (MxStTextureCache *cache,
                                               int             size,
                                               const char     *uri,
                                               const char     *mimetype);

ClutterActor *mx_st_texture_cache_load_uri_async (MxStTextureCache    *cache,
                                               const gchar       *uri,
                                               int                available_width,
                                               int                available_height);

ClutterActor *mx_st_texture_cache_load_uri_sync (MxStTextureCache       *cache,
                                              MxStTextureCachePolicy  policy,
                                              const gchar          *uri,
                                              int                   available_width,
                                              int                   available_height,
                                              GError              **error);

CoglHandle    mx_st_texture_cache_load_file_to_cogl_texture (MxStTextureCache *cache,
                                                          const gchar    *file_path);

cairo_surface_t *mx_st_texture_cache_load_file_to_cairo_surface (MxStTextureCache *cache,
                                                              const gchar    *file_path);

ClutterActor *mx_st_texture_cache_load_file_simple (MxStTextureCache *cache,
                                                 const gchar    *file_path);

ClutterActor *mx_st_texture_cache_load_from_data (MxStTextureCache    *cache,
                                               const guchar      *data,
                                               gsize              len,
                                               int                size,
                                               GError           **error);
ClutterActor *mx_st_texture_cache_load_from_raw  (MxStTextureCache    *cache,
                                               const guchar      *data,
                                               gsize              len,
                                               gboolean           has_alpha,
                                               int                width,
                                               int                height,
                                               int                rowstride,
                                               int                size,
                                               GError           **error);

/**
 * MxStTextureCacheLoader: (skip)
 * @cache: a #MxStTextureCache
 * @key: Unique identifier for this texture
 * @data: Callback user data
 * @error: A #GError
 *
 * See mx_st_texture_cache_load().  Implementations should return a
 * texture handle for the given key, or set @error.
 *
 */
typedef CoglHandle (*MxStTextureCacheLoader) (MxStTextureCache *cache, const char *key, void *data, GError **error);

CoglHandle mx_st_texture_cache_load (MxStTextureCache       *cache,
                                  const char           *key,
                                  MxStTextureCachePolicy  policy,
                                  MxStTextureCacheLoader  load,
                                  void                 *data,
                                  GError              **error);

gboolean mx_st_texture_cache_pixbuf_equal (MxStTextureCache *cache, GdkPixbuf *a, GdkPixbuf *b);

#endif /* __MX_ST_TEXTURE_CACHE_H__ */
