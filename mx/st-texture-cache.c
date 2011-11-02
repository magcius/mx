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

#include "config.h"

#include "st-icon-colors.h"
#include "st-texture-cache.h"
#include <gdk/gdk.h>
#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-desktop-thumbnail.h>
#include <string.h>
#include <glib.h>

#define CACHE_PREFIX_GICON "gicon:"
#define CACHE_PREFIX_URI "uri:"
#define CACHE_PREFIX_URI_FOR_CAIRO "uri-for-cairo:"
#define CACHE_PREFIX_THUMBNAIL_URI "thumbnail-uri:"
#define CACHE_PREFIX_RAW_CHECKSUM "raw-checksum:"
#define CACHE_PREFIX_COMPRESSED_CHECKSUM "compressed-checksum:"

struct _MxStTextureCachePrivate
{
  /* Things that were loaded with a cache policy != NONE */
  GHashTable *keyed_cache; /* char * -> CoglTexture* */
  /* Presently this is used to de-duplicate requests for GIcons,
   * it could in theory be extended to async URL loading and other
   * cases too.
   */
  GHashTable *outstanding_requests; /* char * -> AsyncTextureLoadData * */
  GnomeDesktopThumbnailFactory *thumbnails;
};

static void mx_st_texture_cache_dispose (GObject *object);
static void mx_st_texture_cache_finalize (GObject *object);

G_DEFINE_TYPE(MxStTextureCache, mx_st_texture_cache, G_TYPE_OBJECT);

/* We want to preserve the aspect ratio by default, also the default
 * material for an empty texture is full opacity white, which we
 * definitely don't want.  Skip that by setting 0 opacity.
 */
static ClutterTexture *
create_default_texture (MxStTextureCache *self)
{
  ClutterTexture * texture = CLUTTER_TEXTURE (clutter_texture_new ());
  g_object_set (texture, "keep-aspect-ratio", TRUE, "opacity", 0, NULL);
  return texture;
}

/* Reverse the opacity we added while loading */
static void
set_texture_cogl_texture (ClutterTexture *clutter_texture, CoglHandle cogl_texture)
{
  clutter_texture_set_cogl_texture (clutter_texture, cogl_texture);
  g_object_set (clutter_texture, "opacity", 255, NULL);
}

static void
mx_st_texture_cache_class_init (MxStTextureCacheClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *)klass;

  gobject_class->dispose = mx_st_texture_cache_dispose;
  gobject_class->finalize = mx_st_texture_cache_finalize;
}

static void
mx_st_texture_cache_init (MxStTextureCache *self)
{
  self->priv = g_new0 (MxStTextureCachePrivate, 1);

  self->priv->keyed_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, cogl_handle_unref);
  self->priv->outstanding_requests = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                            g_free, NULL);
  self->priv->thumbnails = gnome_desktop_thumbnail_factory_new (GNOME_DESKTOP_THUMBNAIL_SIZE_LARGE);
}

static void
mx_st_texture_cache_dispose (GObject *object)
{
  MxStTextureCache *self = (MxStTextureCache*)object;

  if (self->priv->keyed_cache)
    g_hash_table_destroy (self->priv->keyed_cache);
  self->priv->keyed_cache = NULL;

  if (self->priv->outstanding_requests)
    g_hash_table_destroy (self->priv->outstanding_requests);
  self->priv->outstanding_requests = NULL;

  if (self->priv->thumbnails)
    g_object_unref (self->priv->thumbnails);
  self->priv->thumbnails = NULL;

  G_OBJECT_CLASS (mx_st_texture_cache_parent_class)->dispose (object);
}

static void
mx_st_texture_cache_finalize (GObject *object)
{
  G_OBJECT_CLASS (mx_st_texture_cache_parent_class)->finalize (object);
}

static gboolean
compute_pixbuf_scale (gint      width,
                      gint      height,
                      gint      available_width,
                      gint      available_height,
                      gint     *new_width,
                      gint     *new_height)
{
  int scaled_width, scaled_height;

  if (width == 0 || height == 0)
    return FALSE;

  if (available_width >= 0 && available_height >= 0)
    {
      /* This should keep the aspect ratio of the image intact, because if
       * available_width < (available_height * width) / height
       * then
       * (available_width * height) / width < available_height
       * So we are guaranteed to either scale the image to have an available_width
       * for width and height scaled accordingly OR have the available_height
       * for height and width scaled accordingly, whichever scaling results
       * in the image that can fit both available dimensions.
       */
      scaled_width = MIN (available_width, (available_height * width) / height);
      scaled_height = MIN (available_height, (available_width * height) / width);
    }
  else if (available_width >= 0)
    {
      scaled_width = available_width;
      scaled_height = (available_width * height) / width;
    }
  else if (available_height >= 0)
    {
      scaled_width = (available_height * width) / height;
      scaled_height = available_height;
    }
  else
    {
      scaled_width = scaled_height = 0;
    }

  /* Scale the image only if that will not increase its original dimensions. */
  if (scaled_width > 0 && scaled_height > 0 && scaled_width < width && scaled_height < height)
    {
      *new_width = scaled_width;
      *new_height = scaled_height;
      return TRUE;
    }
  return FALSE;
}

/* A private structure for keeping width and height. */
typedef struct {
  int width;
  int height;
} Dimensions;

/* This struct corresponds to a request for an texture.
 * It's creasted when something needs a new texture,
 * and destroyed when the texture data is loaded. */
typedef struct {
  MxStTextureCache *cache;
  MxStTextureCachePolicy policy;
  char *key;
  char *checksum;

  gboolean thumbnail;
  gboolean enforced_square;

  guint width;
  guint height;
  GSList *textures;

  GIcon *icon;
  char *mimetype;
  StIconColors *colors;
  char *uri;
} AsyncTextureLoadData;

static void
texture_load_data_destroy (gpointer p)
{
  AsyncTextureLoadData *data = p;

  if (data->icon)
    {
      g_object_unref (data->icon);
      if (data->colors)
        st_icon_colors_unref (data->colors);
    }
  else if (data->uri)
    g_free (data->uri);

  if (data->key)
    g_free (data->key);
  if (data->checksum)
    g_free (data->checksum);
  if (data->mimetype)
    g_free (data->mimetype);

  if (data->textures)
    g_slist_free_full (data->textures, (GDestroyNotify) g_object_unref);
}

/**
 * on_image_size_prepared:
 *
 * @pixbuf_loader: #GdkPixbufLoader loading the image
 * @width: the original width of the image
 * @height: the original height of the image
 * @data: pointer to the #Dimensions sructure containing available width and height for the image,
 *        available width or height can be -1 if the dimension is not limited
 *
 * Private function.
 *
 * Sets the size of the image being loaded to fit the available width and height dimensions,
 * but never scales up the image beyond its actual size.
 * Intended to be used as a callback for #GdkPixbufLoader "size-prepared" signal.
 */
static void
on_image_size_prepared (GdkPixbufLoader *pixbuf_loader,
                        gint             width,
                        gint             height,
                        gpointer         data)
{
  Dimensions *available_dimensions = data;
  int available_width = available_dimensions->width;
  int available_height = available_dimensions->height;
  int scaled_width;
  int scaled_height;

  if (compute_pixbuf_scale (width, height, available_width, available_height,
                            &scaled_width, &scaled_height))
    gdk_pixbuf_loader_set_size (pixbuf_loader, scaled_width, scaled_height);
}

static GdkPixbuf *
impl_load_pixbuf_data (const guchar   *data,
                       gsize           size,
                       int             available_width,
                       int             available_height,
                       GError        **error)
{
  GdkPixbufLoader *pixbuf_loader = NULL;
  GdkPixbuf *rotated_pixbuf = NULL;
  GdkPixbuf *pixbuf;
  gboolean success;
  Dimensions available_dimensions;
  int width_before_rotation, width_after_rotation;

  pixbuf_loader = gdk_pixbuf_loader_new ();

  available_dimensions.width = available_width;
  available_dimensions.height = available_height;
  g_signal_connect (pixbuf_loader, "size-prepared",
                    G_CALLBACK (on_image_size_prepared), &available_dimensions);

  success = gdk_pixbuf_loader_write (pixbuf_loader, data, size, error);
  if (!success)
    goto out;
  success = gdk_pixbuf_loader_close (pixbuf_loader, error);
  if (!success)
    goto out;

  pixbuf = gdk_pixbuf_loader_get_pixbuf (pixbuf_loader);

  width_before_rotation = gdk_pixbuf_get_width (pixbuf);

  rotated_pixbuf = gdk_pixbuf_apply_embedded_orientation (pixbuf);
  width_after_rotation = gdk_pixbuf_get_width (rotated_pixbuf);

  /* There is currently no way to tell if the pixbuf will need to be rotated before it is loaded,
   * so we only check that once it is loaded, and reload it again if it needs to be rotated in order
   * to use the available width and height correctly.
   * See http://bugzilla.gnome.org/show_bug.cgi?id=579003
   */
  if (width_before_rotation != width_after_rotation)
    {
      g_object_unref (pixbuf_loader);
      g_object_unref (rotated_pixbuf);
      rotated_pixbuf = NULL;

      pixbuf_loader = gdk_pixbuf_loader_new ();

      /* We know that the image will later be rotated, so we reverse the available dimensions. */
      available_dimensions.width = available_height;
      available_dimensions.height = available_width;
      g_signal_connect (pixbuf_loader, "size-prepared",
                        G_CALLBACK (on_image_size_prepared), &available_dimensions);

      success = gdk_pixbuf_loader_write (pixbuf_loader, data, size, error);
      if (!success)
        goto out;

      success = gdk_pixbuf_loader_close (pixbuf_loader, error);
      if (!success)
        goto out;

      pixbuf = gdk_pixbuf_loader_get_pixbuf (pixbuf_loader);

      rotated_pixbuf = gdk_pixbuf_apply_embedded_orientation (pixbuf);
    }

out:
  if (pixbuf_loader)
    g_object_unref (pixbuf_loader);
  return rotated_pixbuf;
}

static GdkPixbuf*
decode_image (const char *val)
{
  int i;
  GError *error = NULL;
  GdkPixbuf *res = NULL;
  struct {
    const char *prefix;
    const char *mime_type;
  } formats[] = {
    { "data:image/x-icon;base64,", "image/x-icon" },
    { "data:image/png;base64,", "image/png" }
  };

  g_return_val_if_fail (val, NULL);

  for (i = 0; i < G_N_ELEMENTS (formats); i++)
    {
      if (g_str_has_prefix (val, formats[i].prefix))
        {
          gsize len;
          guchar *data = NULL;
          char *unescaped;

          unescaped = g_uri_unescape_string (val + strlen (formats[i].prefix), NULL);
          if (unescaped)
            {
              data = g_base64_decode (unescaped, &len);
              g_free (unescaped);
            }

          if (data)
            {
              GdkPixbufLoader *loader;

              loader = gdk_pixbuf_loader_new_with_mime_type (formats[i].mime_type, &error);
              if (loader &&
                  gdk_pixbuf_loader_write (loader, data, len, &error) &&
                  gdk_pixbuf_loader_close (loader, &error))
                {
                  res = gdk_pixbuf_loader_get_pixbuf (loader);
                  g_object_ref (res);
                }
              g_object_unref (loader);
              g_free (data);
            }
        }
    }
  if (!res)
    {
      if (error)
        {
          g_warning ("%s\n", error->message);
          g_error_free (error);
        }
      else
        g_warning ("incorrect data uri");
    }
  return res;
}

static GdkPixbuf *
impl_load_pixbuf_file (const char     *uri,
                       int             available_width,
                       int             available_height,
                       GError        **error)
{
  GdkPixbuf *pixbuf = NULL;
  GFile *file;
  char *contents = NULL;
  gsize size;

  if (g_str_has_prefix (uri, "data:"))
    return decode_image (uri);

  file = g_file_new_for_uri (uri);
  if (g_file_load_contents (file, NULL, &contents, &size, NULL, error))
    {
      pixbuf = impl_load_pixbuf_data ((const guchar *) contents, size,
                                      available_width, available_height,
                                      error);
    }

  g_object_unref (file);
  g_free (contents);

  return pixbuf;
}

static GdkPixbuf *
impl_load_thumbnail (MxStTextureCache    *cache,
                     const char        *uri,
                     const char        *mime_type,
                     guint              size,
                     GError           **error)
{
  GnomeDesktopThumbnailFactory *thumbnail_factory;
  GdkPixbuf *pixbuf = NULL;
  GFile *file;
  GFileInfo *file_info;
  GTimeVal mtime_g;
  time_t mtime = 0;
  char *existing_thumbnail;

  file = g_file_new_for_uri (uri);
  file_info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, NULL, NULL);
  g_object_unref (file);
  if (file_info)
    {
      g_file_info_get_modification_time (file_info, &mtime_g);
      g_object_unref (file_info);
      mtime = (time_t) mtime_g.tv_sec;
    }

  thumbnail_factory = cache->priv->thumbnails;

  existing_thumbnail = gnome_desktop_thumbnail_factory_lookup (thumbnail_factory, uri, mtime);

  if (existing_thumbnail != NULL)
    {
      pixbuf = gdk_pixbuf_new_from_file_at_size (existing_thumbnail, size, size, error);
      g_free (existing_thumbnail);
    }
  else if (gnome_desktop_thumbnail_factory_has_valid_failed_thumbnail (thumbnail_factory, uri, mtime))
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Has failed thumbnail");
  else if (gnome_desktop_thumbnail_factory_can_thumbnail (thumbnail_factory, uri, mime_type, mtime))
    {
      pixbuf = gnome_desktop_thumbnail_factory_generate_thumbnail (thumbnail_factory, uri, mime_type);
      if (pixbuf)
        {
          /* we need to save the thumbnail so that we don't need to generate it again in the future */
          gnome_desktop_thumbnail_factory_save_thumbnail (thumbnail_factory, pixbuf, uri, mtime);
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to generate thumbnail");
          gnome_desktop_thumbnail_factory_create_failed_thumbnail (thumbnail_factory, uri, mtime);
        }
     }
   return pixbuf;
}

static void
load_pixbuf_thread (GSimpleAsyncResult *result,
                    GObject *object,
                    GCancellable *cancellable)
{
  GdkPixbuf *pixbuf;
  AsyncTextureLoadData *data;
  GError *error = NULL;

  data = g_async_result_get_user_data (G_ASYNC_RESULT (result));
  g_assert (data != NULL);

  if (data->thumbnail)
    {
      const char *uri;
      const char *mimetype;

      uri = data->uri;
      mimetype = data->mimetype;
      pixbuf = impl_load_thumbnail (data->cache, uri, mimetype, data->width, &error);
    }
  else if (data->uri)
    pixbuf = impl_load_pixbuf_file (data->uri, data->width, data->height, &error);
  else
    g_assert_not_reached ();

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, error);
      return;
    }

  if (pixbuf)
    g_simple_async_result_set_op_res_gpointer (result, g_object_ref (pixbuf),
                                               g_object_unref);
}

static GdkPixbuf *
load_pixbuf_async_finish (MxStTextureCache *cache, GAsyncResult *result, GError **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);
  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;
  return g_simple_async_result_get_op_res_gpointer (simple);
}

static CoglHandle
pixbuf_to_cogl_handle (GdkPixbuf *pixbuf,
                       gboolean   add_padding)
{
  CoglHandle texture, offscreen;
  CoglColor clear_color;
  int width, height;
  guint size;

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  size = MAX (width, height);

  if (!add_padding || width == height)
    return cogl_texture_new_from_data (width,
                                       height,
                                       COGL_TEXTURE_NONE,
                                       gdk_pixbuf_get_has_alpha (pixbuf) ? COGL_PIXEL_FORMAT_RGBA_8888 : COGL_PIXEL_FORMAT_RGB_888,
                                       COGL_PIXEL_FORMAT_ANY,
                                       gdk_pixbuf_get_rowstride (pixbuf),
                                       gdk_pixbuf_get_pixels (pixbuf));

  texture = cogl_texture_new_with_size (size, size,
                                        COGL_TEXTURE_NO_SLICING,
                                        COGL_PIXEL_FORMAT_ANY);

  offscreen = cogl_offscreen_new_to_texture (texture);
  cogl_color_set_from_4ub (&clear_color, 0, 0, 0, 0);
  cogl_push_framebuffer (offscreen);
  cogl_clear (&clear_color, COGL_BUFFER_BIT_COLOR);
  cogl_pop_framebuffer ();
  cogl_handle_unref (offscreen);

  cogl_texture_set_region (texture,
                           0, 0,
                           (size - width) / 2, (size - height) / 2,
                           width, height,
                           width, height,
                           gdk_pixbuf_get_has_alpha (pixbuf) ? COGL_PIXEL_FORMAT_RGBA_8888 : COGL_PIXEL_FORMAT_RGB_888,
                           gdk_pixbuf_get_rowstride (pixbuf),
                           gdk_pixbuf_get_pixels (pixbuf));
  return texture;
}

static cairo_surface_t *
pixbuf_to_cairo_surface (GdkPixbuf *pixbuf)
{
  cairo_surface_t *dummy_surface;
  cairo_pattern_t *pattern;
  cairo_surface_t *surface;
  cairo_t *cr;

  dummy_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);

  cr = cairo_create (dummy_surface);
  gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
  pattern = cairo_get_source (cr);
  cairo_pattern_get_surface (pattern, &surface);
  cairo_surface_reference (surface);
  cairo_destroy (cr);
  cairo_surface_destroy (dummy_surface);

  return surface;
}

static void
on_pixbuf_loaded (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  GSList *iter;
  MxStTextureCache *cache;
  AsyncTextureLoadData *data;
  GdkPixbuf *pixbuf;
  GError *error = NULL;
  CoglHandle texdata = NULL;

  data = user_data;
  cache = MX_ST_TEXTURE_CACHE (source);

  g_hash_table_remove (cache->priv->outstanding_requests, data->key);

  pixbuf = load_pixbuf_async_finish (cache, result, &error);
  if (pixbuf == NULL)
    goto out;

  texdata = pixbuf_to_cogl_handle (pixbuf, data->enforced_square);

  g_object_unref (pixbuf);

  if (data->policy != MX_ST_TEXTURE_CACHE_POLICY_NONE)
    {
      gpointer orig_key, value;

      if (!g_hash_table_lookup_extended (cache->priv->keyed_cache, data->key,
                                         &orig_key, &value))
        {
          cogl_handle_ref (texdata);
          g_hash_table_insert (cache->priv->keyed_cache, g_strdup (data->key),
                               texdata);
        }
    }

  for (iter = data->textures; iter; iter = iter->next)
    {
      ClutterTexture *texture = iter->data;
      set_texture_cogl_texture (texture, texdata);
    }

out:
  if (texdata)
    cogl_handle_unref (texdata);

  texture_load_data_destroy (data);
  g_free (data);

  g_clear_error (&error);
}

static void
load_texture_async (MxStTextureCache       *cache,
                    AsyncTextureLoadData *data)
{
  GSimpleAsyncResult *result;
  result = g_simple_async_result_new (G_OBJECT (cache), on_pixbuf_loaded, data, load_texture_async);
  g_simple_async_result_run_in_thread (result, load_pixbuf_thread, G_PRIORITY_DEFAULT, NULL);
  g_object_unref (result);
}

typedef struct {
  MxStTextureCache *cache;
  ClutterTexture *texture;
  GObject *source;
  guint notify_signal_id;
  gboolean weakref_active;
} MxStTextureCachePropertyBind;

static void
mx_st_texture_cache_reset_texture (MxStTextureCachePropertyBind *bind,
                                const char                 *propname)
{
  GdkPixbuf *pixbuf;
  CoglHandle texdata;

  g_object_get (bind->source, propname, &pixbuf, NULL);

  g_return_if_fail (pixbuf == NULL || GDK_IS_PIXBUF (pixbuf));

  if (pixbuf != NULL)
    {
      texdata = pixbuf_to_cogl_handle (pixbuf, FALSE);
      g_object_unref (pixbuf);

      clutter_texture_set_cogl_texture (bind->texture, texdata);
      cogl_handle_unref (texdata);

      clutter_actor_set_opacity (CLUTTER_ACTOR (bind->texture), 255);
    }
  else
    clutter_actor_set_opacity (CLUTTER_ACTOR (bind->texture), 0);
}

static void
mx_st_texture_cache_on_pixbuf_notify (GObject           *object,
                                   GParamSpec        *paramspec,
                                   gpointer           data)
{
  MxStTextureCachePropertyBind *bind = data;
  mx_st_texture_cache_reset_texture (bind, paramspec->name);
}

static void
mx_st_texture_cache_bind_weak_notify (gpointer     data,
                                   GObject     *source_location)
{
  MxStTextureCachePropertyBind *bind = data;
  bind->weakref_active = FALSE;
  g_signal_handler_disconnect (bind->source, bind->notify_signal_id);
}

static void
mx_st_texture_cache_free_bind (gpointer data)
{
  MxStTextureCachePropertyBind *bind = data;
  if (bind->weakref_active)
    g_object_weak_unref (G_OBJECT(bind->texture), mx_st_texture_cache_bind_weak_notify, bind);
  g_free (bind);
}

/**
 * mx_st_texture_cache_bind_pixbuf_property:
 * @cache:
 * @object: A #GObject with a property @property_name of type #GdkPixbuf
 * @property_name: Name of a property
 *
 * Create a #ClutterTexture which tracks the #GdkPixbuf value of a GObject property
 * named by @property_name.  Unlike other methods in MxStTextureCache, the underlying
 * CoglHandle is not shared by default with other invocations to this method.
 *
 * If the source object is destroyed, the texture will continue to show the last
 * value of the property.
 *
 * Return value: (transfer none): A new #ClutterActor
 */
ClutterActor *
mx_st_texture_cache_bind_pixbuf_property (MxStTextureCache    *cache,
                                       GObject           *object,
                                       const char        *property_name)
{
  ClutterTexture *texture;
  gchar *notify_key;
  MxStTextureCachePropertyBind *bind;

  texture = CLUTTER_TEXTURE (clutter_texture_new ());

  bind = g_new0 (MxStTextureCachePropertyBind, 1);
  bind->cache = cache;
  bind->texture = texture;
  bind->source = object;
  g_object_weak_ref (G_OBJECT (texture), mx_st_texture_cache_bind_weak_notify, bind);
  bind->weakref_active = TRUE;

  mx_st_texture_cache_reset_texture (bind, property_name);

  notify_key = g_strdup_printf ("notify::%s", property_name);
  bind->notify_signal_id = g_signal_connect_data (object, notify_key, G_CALLBACK(mx_st_texture_cache_on_pixbuf_notify),
                                                  bind, (GClosureNotify)mx_st_texture_cache_free_bind, 0);
  g_free (notify_key);

  return CLUTTER_ACTOR(texture);
}

/**
 * mx_st_texture_cache_load: (skip)
 * @cache: A #MxStTextureCache
 * @key: Arbitrary string used to refer to item
 * @policy: Caching policy
 * @load: Function to create the texture, if not already cached
 * @data: User data passed to @load
 * @error: A #GError
 *
 * Load an arbitrary texture, caching it.  The string chosen for @key
 * should be of the form "type-prefix:type-uuid".  For example,
 * "url:file:///usr/share/icons/hicolor/48x48/apps/firefox.png", or
 * "stock-icon:gtk-ok".
 *
 * Returns: (transfer full): A newly-referenced handle to the texture
 */
CoglHandle
mx_st_texture_cache_load (MxStTextureCache       *cache,
                          const char           *key,
                          MxStTextureCachePolicy  policy,
                          MxStTextureCacheLoader  load,
                          void                 *data,
                          GError              **error)
{
  CoglHandle texture;

  texture = g_hash_table_lookup (cache->priv->keyed_cache, key);
  if (!texture)
    {
      texture = load (cache, key, data, error);
      if (texture)
        g_hash_table_insert (cache->priv->keyed_cache, g_strdup (key), texture);
      else
        return COGL_INVALID_HANDLE;
    }
  cogl_handle_ref (texture);
  return texture;
}

static ClutterActor *
load_from_pixbuf (GdkPixbuf *pixbuf)
{
  ClutterTexture *texture;
  CoglHandle texdata;
  int width = gdk_pixbuf_get_width (pixbuf);
  int height = gdk_pixbuf_get_height (pixbuf);

  texture = create_default_texture (mx_st_texture_cache_get_default ());

  clutter_actor_set_size (CLUTTER_ACTOR (texture), width, height);

  texdata = pixbuf_to_cogl_handle (pixbuf, FALSE);

  set_texture_cogl_texture (texture, texdata);

  cogl_handle_unref (texdata);
  return CLUTTER_ACTOR (texture);
}

typedef struct {
  gchar *path;
  gint   grid_width, grid_height;
  ClutterGroup *group;
} AsyncImageData;

static void
on_data_destroy (gpointer data)
{
  AsyncImageData *d = (AsyncImageData *)data;
  g_free (d->path);
  g_object_unref (d->group);
  g_free (d);
}

static void
on_sliced_image_loaded (GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data)
{
  AsyncImageData *data = (AsyncImageData *)user_data;
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  GList *list;

  if (g_simple_async_result_propagate_error (simple, NULL))
    return;

  for (list = g_simple_async_result_get_op_res_gpointer (simple); list; list = g_list_next (list))
    {
      ClutterActor *actor = load_from_pixbuf (GDK_PIXBUF (list->data));
      clutter_actor_hide (actor);
      clutter_container_add_actor (CLUTTER_CONTAINER (data->group), actor);
    }
}

static void
free_glist_unref_gobjects (gpointer p)
{
  GList *list = p;
  GList *iter;

  for (iter = list; iter; iter = iter->next)
    g_object_unref (iter->data);
  g_list_free (list);
}

static void
load_sliced_image (GSimpleAsyncResult *result,
                   GObject *object,
                   GCancellable *cancellable)
{
  AsyncImageData *data;
  GList *res = NULL;
  GdkPixbuf *pix;
  gint width, height, y, x;

  g_assert (!cancellable);

  data = g_object_get_data (G_OBJECT (result), "load_sliced_image");
  g_assert (data);

  if (!(pix = gdk_pixbuf_new_from_file (data->path, NULL)))
    return;

  width = gdk_pixbuf_get_width (pix);
  height = gdk_pixbuf_get_height (pix);
  for (y = 0; y < height; y += data->grid_width)
    {
      for (x = 0; x < width; x += data->grid_height)
        {
          GdkPixbuf *pixbuf = gdk_pixbuf_new_subpixbuf (pix, x, y, data->grid_width, data->grid_height);
          g_assert (pixbuf != NULL);
          res = g_list_append (res, pixbuf);
        }
    }
  /* We don't need the original pixbuf anymore, though the subpixbufs
     will hold a reference. */
  g_object_unref (pix);
  g_simple_async_result_set_op_res_gpointer (result, res, free_glist_unref_gobjects);
}

/**
 * mx_st_texture_cache_load_sliced_image:
 * @cache: A #MxStTextureCache
 * @path: Path to a filename
 * @grid_width: Width in pixels
 * @grid_height: Height in pixels
 *
 * This function reads a single image file which contains multiple images internally.
 * The image file will be divided using @grid_width and @grid_height;
 * note that the dimensions of the image loaded from @path 
 * should be a multiple of the specified grid dimensions.
 *
 * Returns: (transfer none): A new #ClutterGroup
 */
ClutterGroup *
mx_st_texture_cache_load_sliced_image (MxStTextureCache    *cache,
                                    const gchar       *path,
                                    gint               grid_width,
                                    gint               grid_height)
{
  AsyncImageData *data;
  GSimpleAsyncResult *result;
  ClutterGroup *group = CLUTTER_GROUP (clutter_group_new ());

  data = g_new0 (AsyncImageData, 1);
  data->grid_width = grid_width;
  data->grid_height = grid_height;
  data->path = g_strdup (path);
  data->group = group;
  g_object_ref (G_OBJECT (group));

  result = g_simple_async_result_new (G_OBJECT (cache), on_sliced_image_loaded, data, mx_st_texture_cache_load_sliced_image);

  g_object_set_data_full (G_OBJECT (result), "load_sliced_image", data, on_data_destroy);
  g_simple_async_result_run_in_thread (result, load_sliced_image, G_PRIORITY_DEFAULT, NULL);

  g_object_unref (result);

  return group;
}

/**
 * StIconType:
 * @ST_ICON_SYMBOLIC: a symbolic (ie, mostly monochrome) icon
 * @ST_ICON_FULLCOLOR: a full-color icon
 * @ST_ICON_APPLICATION: a full-color icon, which is expected
 *   to be an application icon
 * @ST_ICON_DOCUMENT: a full-color icon, which is expected
 *   to be a document (MIME type) icon
 *
 * Describes what style of icon is desired in a call to
 * mx_st_texture_cache_load_icon_name() or mx_st_texture_cache_load_gicon().
 * Use %ST_ICON_SYMBOLIC for symbolic icons (eg, for the panel and
 * much of the rest of the shell chrome) or %ST_ICON_FULLCOLOR for a
 * full-color icon.
 *
 * If you know that the requested icon is either an application icon
 * or a document type icon, you should use %ST_ICON_APPLICATION or
 * %ST_ICON_DOCUMENT, which may do a better job of selecting the
 * correct theme icon for those types. If you are unsure what kind of
 * icon you are loading, use %ST_ICON_FULLCOLOR.
 */

/**
 * mx_st_texture_cache_load_uri_async:
 *
 * @cache: The texture cache instance
 * @uri: uri of the image file from which to create a pixbuf
 * @available_width: available width for the image, can be -1 if not limited
 * @available_height: available height for the image, can be -1 if not limited
 *
 * Asynchronously load an image.   Initially, the returned texture will have a natural
 * size of zero.  At some later point, either the image will be loaded successfully
 * and at that point size will be negotiated, or upon an error, no image will be set.
 *
 * Return value: (transfer none): A new #ClutterActor with no image loaded initially.
 */
ClutterActor *
mx_st_texture_cache_load_uri_async (MxStTextureCache *cache,
                                 const gchar    *uri,
                                 int             available_width,
                                 int             available_height)
{
  ClutterTexture *texture;
  AsyncTextureLoadData *data;

  texture = create_default_texture (cache);

  data = g_new0 (AsyncTextureLoadData, 1);
  data->cache = cache;
  data->key = g_strconcat (CACHE_PREFIX_URI, uri, NULL);
  data->policy = MX_ST_TEXTURE_CACHE_POLICY_NONE;
  data->uri = g_strdup (uri);
  data->width = available_width;
  data->height = available_height;
  data->textures = g_slist_prepend (data->textures, g_object_ref (texture));
  load_texture_async (cache, data);

  return CLUTTER_ACTOR (texture);
}

static CoglHandle
mx_st_texture_cache_load_uri_sync_to_cogl_texture (MxStTextureCache *cache,
                                                MxStTextureCachePolicy policy,
                                                const gchar    *uri,
                                                int             available_width,
                                                int             available_height,
                                                GError         **error)
{
  CoglHandle texdata;
  GdkPixbuf *pixbuf;
  char *key;

  key = g_strconcat (CACHE_PREFIX_URI, uri, NULL);

  texdata = g_hash_table_lookup (cache->priv->keyed_cache, key);

  if (texdata == NULL)
    {
      pixbuf = impl_load_pixbuf_file (uri, available_width, available_height, error);
      if (!pixbuf)
        goto out;

      texdata = pixbuf_to_cogl_handle (pixbuf, FALSE);
      g_object_unref (pixbuf);

      if (policy == MX_ST_TEXTURE_CACHE_POLICY_FOREVER)
        {
          cogl_handle_ref (texdata);
          g_hash_table_insert (cache->priv->keyed_cache, g_strdup (key), texdata);
        }
    }
  else
    cogl_handle_ref (texdata);

out:
  g_free (key);
  return texdata;
}

static cairo_surface_t *
mx_st_texture_cache_load_uri_sync_to_cairo_surface (MxStTextureCache        *cache,
                                                 MxStTextureCachePolicy   policy,
                                                 const gchar           *uri,
                                                 int                    available_width,
                                                 int                    available_height,
                                                 GError               **error)
{
  cairo_surface_t *surface;
  GdkPixbuf *pixbuf;
  char *key;

  key = g_strconcat (CACHE_PREFIX_URI_FOR_CAIRO, uri, NULL);

  surface = g_hash_table_lookup (cache->priv->keyed_cache, key);

  if (surface == NULL)
    {
      pixbuf = impl_load_pixbuf_file (uri, available_width, available_height, error);
      if (!pixbuf)
        goto out;

      surface = pixbuf_to_cairo_surface (pixbuf);
      g_object_unref (pixbuf);

      if (policy == MX_ST_TEXTURE_CACHE_POLICY_FOREVER)
        {
          cairo_surface_reference (surface);
          g_hash_table_insert (cache->priv->keyed_cache, g_strdup (key), surface);
        }
    }
  else
    cairo_surface_reference (surface);

out:
  g_free (key);
  return surface;
}

/**
 * mx_st_texture_cache_load_uri_sync:
 *
 * @cache: The texture cache instance
 * @policy: Requested lifecycle of cached data
 * @uri: uri of the image file from which to create a pixbuf
 * @available_width: available width for the image, can be -1 if not limited
 * @available_height: available height for the image, can be -1 if not limited
 * @error: Return location for error
 *
 * Synchronously load an image from a uri.  The image is scaled down to fit the
 * available width and height imensions, but the image is never scaled up beyond
 * its actual size. The pixbuf is rotated according to the associated orientation
 * setting.
 *
 * Return value: (transfer none): A new #ClutterActor with the image file loaded if it was
 *               generated succesfully, %NULL otherwise
 */
ClutterActor *
mx_st_texture_cache_load_uri_sync (MxStTextureCache *cache,
                                MxStTextureCachePolicy policy,
                                const gchar       *uri,
                                int                available_width,
                                int                available_height,
                                GError            **error)
{
  CoglHandle texdata;
  ClutterTexture *texture;

  texdata = mx_st_texture_cache_load_uri_sync_to_cogl_texture (cache, policy, uri, available_width, available_height, error);

  if (texdata == COGL_INVALID_HANDLE)
    return NULL;

  texture = create_default_texture (cache);
  set_texture_cogl_texture (texture, texdata);
  cogl_handle_unref (texdata);

  return CLUTTER_ACTOR (texture);
}

/**
 * mx_st_texture_cache_load_file_to_cogl_texture:
 * @cache: A #MxStTextureCache
 * @file_path: Path to a file in supported image format
 *
 * This function synchronously loads the given file path
 * into a COGL texture.  On error, a warning is emitted
 * and %COGL_INVALID_HANDLE is returned.
 *
 * Returns: (transfer full): a new #CoglHandle
 */
CoglHandle
mx_st_texture_cache_load_file_to_cogl_texture (MxStTextureCache *cache,
                                            const gchar    *file_path)
{
  CoglHandle texture;
  GFile *file;
  char *uri;
  GError *error = NULL;

  file = g_file_new_for_path (file_path);
  uri = g_file_get_uri (file);

  texture = mx_st_texture_cache_load_uri_sync_to_cogl_texture (cache, MX_ST_TEXTURE_CACHE_POLICY_FOREVER,
                                                            uri, -1, -1, &error);
  g_object_unref (file);
  g_free (uri);

  if (texture == NULL)
    {
      g_warning ("Failed to load %s: %s", file_path, error->message);
      g_clear_error (&error);
      return COGL_INVALID_HANDLE;
    }
  return texture;
}

/**
 * mx_st_texture_cache_load_file_to_cairo_surface:
 * @cache: A #MxStTextureCache
 * @file_path: Path to a file in supported image format
 *
 * This function synchronously loads the given file path
 * into a cairo surface.  On error, a warning is emitted
 * and %NULL is returned.
 *
 * Returns: (transfer full): a new #cairo_surface_t
 */
cairo_surface_t *
mx_st_texture_cache_load_file_to_cairo_surface (MxStTextureCache *cache,
                                             const gchar    *file_path)
{
  cairo_surface_t *surface;
  GFile *file;
  char *uri;
  GError *error = NULL;

  file = g_file_new_for_path (file_path);
  uri = g_file_get_uri (file);

  surface = mx_st_texture_cache_load_uri_sync_to_cairo_surface (cache, MX_ST_TEXTURE_CACHE_POLICY_FOREVER,
                                                             uri, -1, -1, &error);
  g_object_unref (file);
  g_free (uri);

  if (surface == NULL)
    {
      g_warning ("Failed to load %s: %s", file_path, error->message);
      g_clear_error (&error);
      return NULL;
    }
  return surface;
}

/**
 * mx_st_texture_cache_load_file_simple:
 * @cache: A #MxStTextureCache
 * @file_path: Filesystem path
 *
 * Synchronously load an image into a texture.  The texture will be cached
 * indefinitely.  On error, this function returns an empty texture and prints a warning.
 *
 * Returns: (transfer none): A new #ClutterTexture
 */
ClutterActor *
mx_st_texture_cache_load_file_simple (MxStTextureCache *cache,
                                   const gchar    *file_path)
{
  GFile *file;
  char *uri;
  ClutterActor *texture;
  GError *error = NULL;

  file = g_file_new_for_path (file_path);
  uri = g_file_get_uri (file);

  texture = mx_st_texture_cache_load_uri_sync (cache, MX_ST_TEXTURE_CACHE_POLICY_FOREVER,
                                            uri, -1, -1, &error);
  if (texture == NULL)
    {
      g_warning ("Failed to load %s: %s", file_path, error->message);
      g_clear_error (&error);
      texture = clutter_texture_new ();
    }
  return texture;
}

/**
 * mx_st_texture_cache_load_from_data:
 * @cache: The texture cache instance
 * @data: Image data in PNG, GIF, etc format
 * @len: length of @data
 * @size: Size in pixels to use for the resulting texture
 * @error: Return location for error
 *
 * Synchronously creates an image from @data. The image is scaled down
 * to fit the available width and height dimensions, but the image is
 * never scaled up beyond its actual size. The pixbuf is rotated
 * according to the associated orientation setting.
 *
 * Return value: (transfer none): A new #ClutterActor with the image data loaded if it was
 *               generated succesfully, %NULL otherwise
 */
ClutterActor *
mx_st_texture_cache_load_from_data (MxStTextureCache    *cache,
                                 const guchar      *data,
                                 gsize              len,
                                 int                size,
                                 GError           **error)
{
  ClutterTexture *texture;
  CoglHandle texdata;
  GdkPixbuf *pixbuf;
  char *key;
  char *checksum;

  texture = create_default_texture (cache);
  clutter_actor_set_size (CLUTTER_ACTOR (texture), size, size);

  checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA1, data, len);
  key = g_strdup_printf (CACHE_PREFIX_COMPRESSED_CHECKSUM "checksum=%s,size=%d", checksum, size);
  g_free (checksum);

  texdata = g_hash_table_lookup (cache->priv->keyed_cache, key);
  if (texdata == NULL)
    {
      pixbuf = impl_load_pixbuf_data (data, len, size, size, error);
      if (!pixbuf)
        {
          g_object_unref (texture);
          g_free (key);
          return NULL;
        }

      texdata = pixbuf_to_cogl_handle (pixbuf, TRUE);
      g_object_unref (pixbuf);

      set_texture_cogl_texture (texture, texdata);

      g_hash_table_insert (cache->priv->keyed_cache, g_strdup (key), texdata);
    }

  g_free (key);

  set_texture_cogl_texture (texture, texdata);
  return CLUTTER_ACTOR (texture);
}

/**
 * mx_st_texture_cache_load_from_raw:
 * @cache: a #MxStTextureCache
 * @data: (array length=len): raw pixel data
 * @len: the length of @data
 * @has_alpha: whether @data includes an alpha channel
 * @width: width in pixels of @data
 * @height: width in pixels of @data
 * @rowstride: rowstride of @data
 *
 * Creates (or retrieves from cache) an icon based on raw pixel data.
 *
 * Return value: (transfer none): a new #ClutterActor displaying a
 * pixbuf created from @data and the other parameters.
 **/
ClutterActor *
mx_st_texture_cache_load_from_raw (MxStTextureCache    *cache,
                                   const guchar      *data,
                                   gsize              len,
                                   gboolean           has_alpha,
                                   int                width,
                                   int                height,
                                   int                rowstride,
                                   GError           **error)
{
  ClutterTexture *texture;
  CoglHandle texdata;
  char *key;
  char *checksum;

  texture = create_default_texture (cache);
  clutter_actor_set_size (CLUTTER_ACTOR (texture), width, height);

  /* In theory, two images of with different width and height could have the same
   * pixel data and thus hash the same. (Say, a 16x16 and a 8x32 blank image.)
   * We ignore this for now. If anybody hits this problem they should use
   * GChecksum directly to compute a checksum including the width and height.
   */
  checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA1, data, len);
  key = g_strdup_printf (CACHE_PREFIX_RAW_CHECKSUM "checksum=%s", checksum);
  g_free (checksum);

  texdata = g_hash_table_lookup (cache->priv->keyed_cache, key);
  if (texdata == NULL)
    {
      texdata = cogl_texture_new_from_data (width, height, COGL_TEXTURE_NONE,
                                            has_alpha ? COGL_PIXEL_FORMAT_RGBA_8888 : COGL_PIXEL_FORMAT_RGB_888,
                                            COGL_PIXEL_FORMAT_ANY,
                                            rowstride, data);
      g_hash_table_insert (cache->priv->keyed_cache, g_strdup (key), texdata);
    }

  g_free (key);

  set_texture_cogl_texture (texture, texdata);
  return CLUTTER_ACTOR (texture);
}

/**
 * mx_st_texture_cache_load_thumbnail:
 * @cache:
 * @size: Size in pixels to use for thumbnail
 * @uri: Source URI
 * @mimetype: Source mime type
 *
 * Asynchronously load a thumbnail image of a URI into a texture.  The
 * returned texture object will be a new instance; however, its texture data
 * may be shared with other objects.  This implies the texture data is cached.
 *
 * The current caching policy is permanent; to uncache, you must explicitly
 * call mx_st_texture_cache_unref_thumbnail().
 *
 * Returns: (transfer none): A new #ClutterActor
 */
ClutterActor *
mx_st_texture_cache_load_thumbnail (MxStTextureCache    *cache,
                                 int                size,
                                 const char        *uri,
                                 const char        *mimetype)
{
  ClutterTexture *texture;
  AsyncTextureLoadData *data;
  char *key;
  CoglHandle texdata;

  texture = create_default_texture (cache);
  clutter_actor_set_size (CLUTTER_ACTOR (texture), size, size);

  key = g_strdup_printf (CACHE_PREFIX_THUMBNAIL_URI "uri=%s,size=%d", uri, size);

  texdata = g_hash_table_lookup (cache->priv->keyed_cache, key);
  if (!texdata)
    {
      data = g_new0 (AsyncTextureLoadData, 1);
      data->cache = cache;
      data->key = g_strdup (key);
      data->policy = MX_ST_TEXTURE_CACHE_POLICY_FOREVER;
      data->uri = g_strdup (uri);
      data->mimetype = g_strdup (mimetype);
      data->thumbnail = TRUE;
      data->width = size;
      data->height = size;
      data->enforced_square = TRUE;
      data->textures = g_slist_prepend (data->textures, g_object_ref (texture));
      load_texture_async (cache, data);
    }
  else
    {
      set_texture_cogl_texture (texture, texdata);
    }

  g_free (key);
  return CLUTTER_ACTOR (texture);
}

static size_t
pixbuf_byte_size (GdkPixbuf *pixbuf)
{
  /* This bit translated from gtk+/gdk-pixbuf/gdk-pixbuf.c:gdk_pixbuf_copy.  The comment
   * there was:
   *
   * Calculate a semi-exact size.  Here we copy with full rowstrides;
   * maybe we should copy each row individually with the minimum
   * rowstride?
   */
  return (gdk_pixbuf_get_height (pixbuf) - 1) * gdk_pixbuf_get_rowstride (pixbuf) +
    + gdk_pixbuf_get_width (pixbuf) * ((gdk_pixbuf_get_n_channels (pixbuf)* gdk_pixbuf_get_bits_per_sample (pixbuf) + 7) / 8);
}

/**
 * mx_st_texture_cache_pixbuf_equal:
 *
 * Returns: %TRUE iff the given pixbufs are bytewise-equal
 */
gboolean
mx_st_texture_cache_pixbuf_equal (MxStTextureCache *cache, GdkPixbuf *a, GdkPixbuf *b)
{
  size_t size_a = pixbuf_byte_size (a);
  size_t size_b = pixbuf_byte_size (b);
  if (size_a != size_b)
    return FALSE;
  return memcmp (gdk_pixbuf_get_pixels (a), gdk_pixbuf_get_pixels (b), size_a) == 0;
}

static MxStTextureCache *instance = NULL;

/**
 * mx_st_texture_cache_get_default:
 *
 * Return value: (transfer none): The global texture cache
 */
MxStTextureCache*
mx_st_texture_cache_get_default (void)
{
  if (instance == NULL)
    instance = g_object_new (MX_TYPE_ST_TEXTURE_CACHE, NULL);
  return instance;
}
