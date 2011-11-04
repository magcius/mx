/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * mx-widget.h: Base class for Mx actors
 *
 * Copyright 2007 OpenedHand
 * Copyright 2009 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * Boston, MA 02111-1307, USA.
 *
 */

/**
 * SECTION:mx-texture-cache
 * @short_description: A per-process store to cache textures
 *
 * #MxTextureCache allows an application to re-use an previously loaded
 * textures.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

#include "mx-texture-cache.h"
#include "mx-marshal.h"
#include "mx-private.h"

#define CACHE_PREFIX_URI "uri:"
#define CACHE_PREFIX_RAW_CHECKSUM "raw-checksum:"
#define CACHE_PREFIX_COMPRESSED_CHECKSUM "compressed-checksum:"

G_DEFINE_TYPE (MxTextureCache, mx_texture_cache, G_TYPE_OBJECT)

#define TEXTURE_CACHE_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MX_TYPE_TEXTURE_CACHE, MxTextureCachePrivate))

typedef struct _MxTextureCachePrivate MxTextureCachePrivate;

struct _MxTextureCachePrivate
{
  GHashTable *cache;
  GRegex     *is_uri;

  GHashTable *outstanding_requests;
};

static MxTextureCache* __cache_singleton = NULL;

/*
 * Convention: posX with a value of -1 indicates whole texture
 */
typedef struct MxTextureCacheItem {
  char          filename[256];
  int           width, height;
  int           posX, posY;
  CoglHandle    ptr;
  GHashTable   *meta;
} MxTextureCacheItem;

typedef struct
{
  gpointer        ident;
  CoglHandle     *texture;
  GDestroyNotify  destroy_func;
} MxTextureCacheMetaEntry;

static MxTextureCacheItem *
mx_texture_cache_item_new (void)
{
  return g_slice_new0 (MxTextureCacheItem);
}

static void
mx_texture_cache_item_free (MxTextureCacheItem *item)
{
  if (item->ptr)
    cogl_handle_unref (item->ptr);

  if (item->meta)
    g_hash_table_unref (item->meta);

  g_slice_free (MxTextureCacheItem, item);
}

typedef struct {
  MxTextureCache *cache;
  MxTextureCachePolicy policy;
  char *key;
  char *checksum;

  gboolean enforced_square;

  guint width;
  guint height;
  GSList *textures;

  GIcon *icon;
  char *mimetype;
  StIconColors *colors;
  char *uri;
} MxTextureCacheAsyncLoadData;

static void
mx_texture_cache_async_load_data_free (MxTextureCacheAsyncLoadData *data)
{
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

  g_slice_free (MxTextureCacheAsyncLoadData, data);
}

static void
mx_texture_cache_finalize (GObject *object)
{
  MxTextureCachePrivate *priv = TEXTURE_CACHE_PRIVATE(object);

  if (priv->cache)
    g_hash_table_unref (priv->cache);
  priv->cache = NULL;

  if (priv->is_uri)
    g_regex_unref (priv->is_uri);
  priv->is_uri = NULL;

  if (priv->outstanding_requests)
    g_hash_table_destroy (priv->outstanding_requests);
  priv->outstanding_requests = NULL;

  G_OBJECT_CLASS (mx_texture_cache_parent_class)->finalize (object);
}

static void
mx_texture_cache_class_init (MxTextureCacheClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (MxTextureCachePrivate));

  object_class->finalize = mx_texture_cache_finalize;

}

static void
mx_texture_cache_init (MxTextureCache *self)
{
  GError *error = NULL;
  MxTextureCachePrivate *priv = TEXTURE_CACHE_PRIVATE(self);

  priv->cache =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, (GDestroyNotify)mx_texture_cache_item_free);

  priv->is_uri = g_regex_new ("^([a-zA-Z0-9+.-]+)://.*",
                              G_REGEX_OPTIMIZE, 0, &error);

  priv->outstanding_requests =
    g_hash_table_new_full (g_str_hash, g_str_equal,
                           g_free, NULL);

  if (!priv->is_uri)
    g_error (G_STRLOC ": Unable to compile regex: %s", error->message);
}

/**
 * mx_texture_cache_get_default:
 *
 * Returns the default texture cache. This is owned by Mx and should not be
 * unreferenced or freed.
 *
 * Returns: (transfer none): a MxTextureCache
 */
MxTextureCache*
mx_texture_cache_get_default (void)
{
  if (G_UNLIKELY (__cache_singleton == NULL))
    __cache_singleton = g_object_new (MX_TYPE_TEXTURE_CACHE, NULL);

  return __cache_singleton;
}

/**
 * mx_texture_cache_get_size:
 * @self: A #MxTextureCache
 *
 * Returns the number of items in the texture cache
 *
 * Returns: the current size of the cache
 */
gint
mx_texture_cache_get_size (MxTextureCache *self)
{
  MxTextureCachePrivate *priv = TEXTURE_CACHE_PRIVATE(self);

  return g_hash_table_size (priv->cache);
}

static void
add_texture_to_cache (MxTextureCache     *self,
                      const gchar        *uri,
                      MxTextureCacheItem *item)
{
  MxTextureCachePrivate *priv = TEXTURE_CACHE_PRIVATE(self);

  g_hash_table_insert (priv->cache, g_strdup (uri), item);
}

/* NOTE: you should unref the returned texture when not needed */

static gchar *
mx_texture_cache_resolve_relative_path (const gchar *path)
{
  gchar *cwd, *new_path;

  if (g_path_is_absolute (path))
    return NULL;

  cwd = g_get_current_dir ();
  new_path = g_build_filename (cwd, path, NULL);
  g_free (cwd);

  return new_path;
}

static gchar *
mx_texture_cache_filename_to_uri (const gchar *file)
{
  gchar *uri;
  gchar *new_file;
  GError *error = NULL;

  new_file = mx_texture_cache_resolve_relative_path (file);
  if (new_file)
    {
      uri = g_filename_to_uri (new_file, NULL, &error);
      g_free (new_file);
    }
  else
    uri = g_filename_to_uri (file, NULL, &error);

  if (!uri)
    {
      g_warning ("Unable to transform filename to URI: %s",
                 error->message);
      g_error_free (error);
      return NULL;
    }

  return uri;
}

static gchar *
mx_texture_cache_uri_to_filename (const gchar *uri)
{
  GError *error = NULL;
  gchar *file = g_filename_from_uri (uri, NULL, &error);

  if (!file)
    g_warning (G_STRLOC ": Unable to transform URI to filename: %s",
               error->message);

  return file;
}

#include "mx-texture-cache-pixbuf.c"
#include "mx-texture-cache-bind.c"
#include "mx-texture-cache-cairo.c"
#include "mx-texture-cache-compat.c"

static void
load_pixbuf_thread (GSimpleAsyncResult *result,
                    GObject            *object,
                    GCancellable       *cancellable)
{
  GdkPixbuf *pixbuf;
  MxTextureCacheAsyncLoadData* data;
  GError *error = NULL;

  data = g_async_result_get_user_data (G_ASYNC_RESULT (result));
  g_assert (data != NULL);

  if (data->uri)
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

static void
load_texture_async (MxTextureCache              *cache,
                    MxTextureCacheAsyncLoadData *data)
{
  GSimpleAsyncResult *result;
  result = g_simple_async_result_new (G_OBJECT (cache), on_pixbuf_loaded, data, load_texture_async);
  g_simple_async_result_run_in_thread (result, load_pixbuf_thread, G_PRIORITY_DEFAULT, NULL);
  g_object_unref (result);
}

CoglHandle
mx_texture_cache_load (MxTextureCache       *cache,
                       const char           *key,
                       MxTextureCachePolicy  policy,
                       MxTextureCacheLoader  load,
                       void                 *data,
                       GError              **error)
{
  MxTextureCachePrivate *priv;
  CoglHandle texture;
  MxTextureCacheItem *item;

  priv = TEXTURE_CACHE_PRIVATE (cache);

  item = g_hash_table_lookup (priv->cache, key);
  if (item != NULL)
    return item->ptr;

  texture = load (cache, key, data, error);
  if (texture)
    {
      item = mx_texture_cache_item_new ();
      item->ptr = texture;
      add_texture_to_cache (cache, g_strdup (key), item);
      return texture;
    }
  else
    return COGL_INVALID_HANDLE;
}

/* We want to preserve the aspect ratio by default, also the default
 * material for an empty texture is full opacity white, which we
 * definitely don't want.  Skip that by setting 0 opacity.
 */
static ClutterTexture *
create_default_texture ()
{
  ClutterTexture * texture = CLUTTER_TEXTURE (clutter_texture_new ());
  g_object_set (texture, "keep-aspect-ratio", TRUE, "opacity", 0, NULL);
  return texture;
}

/**
 * mx_texture_cache_load_uri_async:
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
mx_texture_cache_load_uri_async (MxTextureCache *cache,
                                 const gchar    *uri,
                                 int             available_width,
                                 int             available_height)
{
  ClutterTexture *texture;
  MxTextureCacheAsyncLoadData *data;

  texture = create_default_texture ();

  data = g_new0 (MxTextureCacheAsyncLoadData, 1);
  data->cache = cache;
  data->key = g_strconcat (CACHE_PREFIX_URI, uri, NULL);
  data->policy = MX_TEXTURE_CACHE_POLICY_NONE;
  data->uri = g_strdup (uri);
  data->width = available_width;
  data->height = available_height;
  data->textures = g_slist_prepend (data->textures, g_object_ref (texture));
  load_texture_async (cache, data);

  return CLUTTER_ACTOR (texture);
}

static CoglHandle
mx_texture_cache_load_uri_sync_to_cogl_texture (MxTextureCache *cache,
                                                MxTextureCachePolicy policy,
                                                const gchar    *uri,
                                                int             available_width,
                                                int             available_height,
                                                GError         **error)
{
  CoglHandle texdata;
  GdkPixbuf *pixbuf;
  char *key;
  MxTextureCachePrivate *priv;
  MxTextureCacheItem *item;

  priv = TEXTURE_CACHE_PRIVATE (cache);

  texdata = COGL_INVALID_HANDLE;

  key = g_strconcat (CACHE_PREFIX_URI, uri, NULL);

  item = g_hash_table_lookup (priv->cache, key);

  if (item == NULL)
    {
      pixbuf = impl_load_pixbuf_file (uri, available_width, available_height, error);
      if (!pixbuf)
        goto out;

      texdata = pixbuf_to_cogl_handle (pixbuf, FALSE);
      g_object_unref (pixbuf);

      if (policy == MX_TEXTURE_CACHE_POLICY_FOREVER)
        {
          item = mx_texture_cache_item_new ();
          item->ptr = texdata;
          cogl_handle_ref (texdata);
          g_hash_table_insert (priv->cache, g_strdup (key), item);
        }
    }
  else
    cogl_handle_ref (texdata);

out:
  g_free (key);
  return texdata;
}

/**
 * mx_texture_cache_load_file_to_cogl_texture:
 * @cache: A #MxTextureCache
 * @file_path: Path to a file in supported image format
 *
 * This function synchronously loads the given file path
 * into a COGL texture.  On error, a warning is emitted
 * and %COGL_INVALID_HANDLE is returned.
 *
 * Returns: (transfer full): a new #CoglHandle
 */
CoglHandle
mx_texture_cache_load_file_to_cogl_texture (MxTextureCache *cache,
                                            const gchar    *file_path)
{
  CoglHandle texture;
  GFile *file;
  char *uri;
  GError *error = NULL;

  file = g_file_new_for_path (file_path);
  uri = g_file_get_uri (file);

  texture = mx_texture_cache_load_uri_sync_to_cogl_texture (cache, MX_TEXTURE_CACHE_POLICY_FOREVER,
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
 * mx_texture_cache_contains:
 * @self: A #MxTextureCache
 * @uri: A URI or path to an image file
 *
 * Checks whether the given URI/path is contained within the texture
 * cache.
 *
 * Returns: %TRUE if the image exists, %FALSE otherwise
 *
 * Since: 1.2
 */
gboolean
mx_texture_cache_contains (MxTextureCache *self,
                           const gchar    *uri)
{
  g_return_val_if_fail (MX_IS_TEXTURE_CACHE (self), FALSE);
  g_return_val_if_fail (uri != NULL, FALSE);

  return mx_texture_cache_get_item (self, uri, FALSE) ? TRUE : FALSE;
}

/**
 * mx_texture_cache_contains_meta:
 * @self: A #MxTextureCache
 * @uri: A URI or path to an image file
 * @ident: A unique identifier
 *
 * Checks whether there are any textures associated with the given URI by
 * the given identifier.
 *
 * Returns: %TRUE if the data exists, %FALSE otherwise
 *
 * Since: 1.2
 */
gboolean
mx_texture_cache_contains_meta (MxTextureCache *self,
                                const gchar    *uri,
                                gpointer        ident)
{
  MxTextureCacheItem *item;

  g_return_val_if_fail (MX_IS_TEXTURE_CACHE (self), FALSE);
  g_return_val_if_fail (uri != NULL, FALSE);

  item = mx_texture_cache_get_item (self, uri, FALSE);

  if (item && item->meta &&
      g_hash_table_lookup (item->meta, ident))
    return TRUE;
  else
    return FALSE;
}

/**
 * mx_texture_cache_insert:
 * @self: A #MxTextureCache
 * @uri: A URI or local file path
 * @texture: A #CoglHandle to a texture
 *
 * Inserts a texture into the texture cache. This can be useful if you
 * want to cache a texture from a custom or unhandled URI type, or you
 * want to override a particular texture.
 *
 * If the image is already in the cache, this texture will replace it. A
 * reference will be taken on the given texture.
 *
 * Since: 1.2
 */
void
mx_texture_cache_insert (MxTextureCache *self,
                         const gchar    *uri,
                         CoglHandle     *texture)
{
  gchar *new_uri = NULL;
  MxTextureCacheItem *item;
  MxTextureCachePrivate *priv;

  g_return_if_fail (MX_IS_TEXTURE_CACHE (self));
  g_return_if_fail (uri != NULL);
  g_return_if_fail (cogl_is_texture (texture));

  priv = TEXTURE_CACHE_PRIVATE (self);

  /* Transform path to URI, if necessary */
  if (!g_regex_match (priv->is_uri, uri, 0, NULL))
    {
      uri = new_uri = mx_texture_cache_filename_to_uri (uri);
      if (!new_uri)
        return;
    }

  item = mx_texture_cache_item_new ();
  item->ptr = cogl_handle_ref (texture);
  add_texture_to_cache (self, uri, item);

  g_free (new_uri);
}

static void
mx_texture_cache_destroy_meta_entry (gpointer data)
{
  MxTextureCacheMetaEntry *entry = data;

  if (entry->destroy_func)
    entry->destroy_func (entry->ident);

  if (entry->texture)
    cogl_handle_unref (entry->texture);

  g_slice_free (MxTextureCacheMetaEntry, entry);
}

/**
 * mx_texture_cache_insert_meta:
 * @self: A #MxTextureCache
 * @uri: A URI or local file path
 * @ident: A unique identifier
 * @texture: A #CoglHandle to a texture
 * @destroy_func: An optional destruction function for @ident
 *
 * Inserts a texture that's associated with a URI into the cache.
 * If the metadata already exists for this URI, it will be replaced.
 *
 * This is useful if you have a widely used modification of an image,
 * for example, an image with a border composited around it.
 *
 * Since: 1.2
 */
void
mx_texture_cache_insert_meta (MxTextureCache *self,
                              const gchar    *uri,
                              gpointer        ident,
                              CoglHandle     *texture,
                              GDestroyNotify  destroy_func)
{
  gchar *new_uri = NULL;
  MxTextureCacheItem *item;
  MxTextureCachePrivate *priv;
  MxTextureCacheMetaEntry *entry;

  g_return_if_fail (MX_IS_TEXTURE_CACHE (self));
  g_return_if_fail (uri != NULL);
  g_return_if_fail (cogl_is_texture (texture));

  priv = TEXTURE_CACHE_PRIVATE (self);

  /* Transform path to URI, if necessary */
  if (!g_regex_match (priv->is_uri, uri, 0, NULL))
    {
      uri = new_uri = mx_texture_cache_filename_to_uri (uri);
      if (!new_uri)
        return;
    }

  item = mx_texture_cache_get_item (self, uri, FALSE);
  if (!item)
    {
      item = mx_texture_cache_item_new ();
      add_texture_to_cache (self, uri, item);
    }

  g_free (new_uri);

  if (!item->meta)
    item->meta = g_hash_table_new_full (NULL, NULL, NULL,
                                        mx_texture_cache_destroy_meta_entry);

  entry = g_slice_new0 (MxTextureCacheMetaEntry);
  entry->ident = ident;
  entry->texture = cogl_handle_ref (texture);
  entry->destroy_func = destroy_func;

  g_hash_table_insert (item->meta, ident, entry);
}

void
mx_texture_cache_load_cache (MxTextureCache *self,
                             const gchar    *filename)
{
  FILE *file;
  MxTextureCacheItem *element, head;
  int ret;
  CoglHandle full_texture;
  MxTextureCachePrivate *priv;

  g_return_if_fail (MX_IS_TEXTURE_CACHE (self));
  g_return_if_fail (filename != NULL);

  priv = TEXTURE_CACHE_PRIVATE (self);

  file = fopen(filename, "rm");
  if (!file)
    return;

  ret = fread (&head, sizeof(MxTextureCacheItem), 1, file);
  if (ret < 0)
    {
      fclose (file);
      return;
    }

  /* check if we already if this texture in the cache */
  if (g_hash_table_lookup (priv->cache, head.filename))
    {
      /* skip it, we're done */
      fclose (file);
      return;
    }

  full_texture = mx_texture_cache_get_cogl_texture (self, head.filename);

  if (full_texture == COGL_INVALID_HANDLE)
    {
      g_critical (G_STRLOC ": Error opening cache image file");
      fclose (file);
      return;
    }

  while (!feof (file))
    {
      gchar *uri;

      element = mx_texture_cache_item_new ();
      ret = fread (element, sizeof (MxTextureCacheItem), 1, file);

      if (ret < 1)
        {
          /* end of file */
          mx_texture_cache_item_free (element);
          break;
        }

      uri = mx_texture_cache_filename_to_uri (element->filename);
      if (!uri)
        {
          /* Couldn't resolve path */
          mx_texture_cache_item_free (element);
          continue;
        }

      if (g_hash_table_lookup (priv->cache, uri))
        {
          /* URI is already in the cache.... */
          mx_texture_cache_item_free (element);
          g_free (uri);
        }
      else
        {
          element->ptr = cogl_texture_new_from_sub_texture (full_texture,
                                                            element->posX,
                                                            element->posY,
                                                            element->width,
                                                            element->height);
          g_hash_table_insert (priv->cache, uri, element);
        }
    }

  fclose (file);
}
