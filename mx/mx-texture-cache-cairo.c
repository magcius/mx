
/* This file is part of mx-texture-cache.c */
#include <gdk/gdk.h>

#define CACHE_PREFIX_URI_FOR_CAIRO "uri-for-cairo:"

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

static cairo_surface_t *
mx_texture_cache_load_uri_sync_to_cairo_surface (MxTextureCache        *cache,
                                                 MxTextureCachePolicy   policy,
                                                 const gchar           *uri,
                                                 int                    available_width,
                                                 int                    available_height,
                                                 GError               **error)
{
  cairo_surface_t *surface;
  GdkPixbuf *pixbuf;
  char *key;
  MxTextureCachePrivate *priv;

  key = g_strconcat (CACHE_PREFIX_URI_FOR_CAIRO, uri, NULL);

  priv = TEXTURE_CACHE_PRIVATE (cache);

  surface = g_hash_table_lookup (priv->cache, key);

  if (surface == NULL)
    {
      pixbuf = impl_load_pixbuf_file (uri, available_width, available_height, error);
      if (!pixbuf)
        goto out;

      surface = pixbuf_to_cairo_surface (pixbuf);
      g_object_unref (pixbuf);

      if (policy == MX_TEXTURE_CACHE_POLICY_FOREVER)
        {
          cairo_surface_reference (surface);
          g_hash_table_insert (priv->cache, g_strdup (key), surface);
        }
    }
  else
    cairo_surface_reference (surface);

out:
  g_free (key);
  return surface;
}

/**
 * mx_texture_cache_load_file_to_cairo_surface:
 * @cache: A #MxTextureCache
 * @file_path: Path to a file in supported image format
 *
 * This function synchronously loads the given file path
 * into a cairo surface.  On error, a warning is emitted
 * and %NULL is returned.
 *
 * Returns: (transfer full): a new #cairo_surface_t
 */
cairo_surface_t *
mx_texture_cache_load_file_to_cairo_surface (MxTextureCache *cache,
                                             const gchar    *file_path)
{
  cairo_surface_t *surface;
  GFile *file;
  char *uri;
  GError *error = NULL;

  file = g_file_new_for_path (file_path);
  uri = g_file_get_uri (file);

  surface = mx_texture_cache_load_uri_sync_to_cairo_surface (cache, MX_TEXTURE_CACHE_POLICY_FOREVER,
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
