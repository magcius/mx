
/* This file is part of mx-texture-cache.c */

#define CACHE_PREFIX_COMPAT "compat:"

static MxTextureCacheItem *
mx_texture_cache_get_item (MxTextureCache *cache,
                           const gchar    *filename,
                           gboolean        create_if_not_exists)
{
  MxTextureCachePrivate *priv;
  MxTextureCacheItem *item;
  gchar *key;
  gchar *uri;

  uri = g_filename_to_uri (filename, NULL, NULL);

  priv = TEXTURE_CACHE_PRIVATE (cache);

  key = g_strconcat (CACHE_PREFIX_COMPAT, uri, NULL);

  item = g_hash_table_lookup (priv->cache, key);

  if (item != NULL && item->ptr != NULL)
    goto out;
  else if (create_if_not_exists)
    {
      GdkPixbuf *pixbuf;

      pixbuf = impl_load_pixbuf_file (uri, -1, -1, NULL);
      if (pixbuf == NULL)
        goto out;

      item = mx_texture_cache_item_new ();
      item->ptr = cogl_handle_ref (pixbuf_to_cogl_handle (pixbuf, FALSE));

      add_texture_to_cache (cache, uri, item);
    }

 out:
  g_free (key);
  return item;
}

/**
 * mx_texture_cache_get_cogl_texture:
 * @self: A #MxTextureCache
 * @uri: A URI or path to an image file
 *
 * Create a #CoglHandle representing a texture of the specified image. Adds
 * the image to the cache if the image had not been previously loaded.
 * Subsequent calls with the same image URI/path will return the #CoglHandle of
 * the previously loaded image with an increased reference count.
 *
 * Returns: (transfer none): a #CoglHandle to the cached texture
 */
CoglHandle
mx_texture_cache_get_cogl_texture (MxTextureCache *self,
                                   const gchar    *uri)
{
  MxTextureCacheItem *item;

  g_return_val_if_fail (MX_IS_TEXTURE_CACHE (self), NULL);
  g_return_val_if_fail (uri != NULL, NULL);

  item = mx_texture_cache_get_item (self, uri, TRUE);

  if (item)
    return cogl_handle_ref (item->ptr);
  else
    return NULL;
}

/**
 * mx_texture_cache_get_texture:
 * @self: A #MxTextureCache
 * @uri: A URI or path to a image file
 *
 * Create a new ClutterTexture with the specified image. Adds the image to the
 * cache if the image had not been previously loaded. Subsequent calls with
 * the same image URI/path will return a new ClutterTexture with the previously
 * loaded image.
 *
 * Returns: (transfer none): a newly created ClutterTexture
 */
ClutterTexture*
mx_texture_cache_get_texture (MxTextureCache *self,
                              const gchar    *uri)
{
  MxTextureCacheItem *item;

  g_return_val_if_fail (MX_IS_TEXTURE_CACHE (self), NULL);
  g_return_val_if_fail (uri != NULL, NULL);

  item = mx_texture_cache_get_item (self, uri, TRUE);

  if (item)
    {
      ClutterActor *texture = clutter_texture_new ();
      clutter_texture_set_cogl_texture ((ClutterTexture*) texture, item->ptr);

      return (ClutterTexture *)texture;
    }
  else
    return NULL;
}


/**
 * mx_texture_cache_get_actor:
 * @self: A #MxTextureCache
 * @uri: A URI or path to a image file
 *
 * This is a wrapper around mx_texture_cache_get_texture() which returns
 * a ClutterActor.
 *
 * Returns: (transfer none): a newly created ClutterTexture
 */
ClutterActor*
mx_texture_cache_get_actor (MxTextureCache *self,
                            const gchar    *uri)
{
  ClutterTexture *tex;

  g_return_val_if_fail (MX_IS_TEXTURE_CACHE (self), NULL);
  g_return_val_if_fail (uri != NULL, NULL);

  if ((tex = mx_texture_cache_get_texture (self, uri)))
    return CLUTTER_ACTOR (tex);
  else
    return NULL;
}

/**
 * mx_texture_cache_get_meta_texture:
 * @self: A #MxTextureCache
 * @uri: A URI or path to an image file
 * @ident: A unique identifier
 *
 * Create a new ClutterTexture using the previously added image associated
 * with the given unique identifier.
 *
 * See mx_texture_cache_insert_meta()
 *
 * Returns: (transfer full): A newly allocated #ClutterTexture, or
 *   %NULL if no image was found
 *
 * Since: 1.2
 */
ClutterTexture *
mx_texture_cache_get_meta_texture (MxTextureCache *self,
                                   const gchar    *uri,
                                   gpointer        ident)
{
  MxTextureCacheItem *item;

  g_return_val_if_fail (MX_IS_TEXTURE_CACHE (self), NULL);
  g_return_val_if_fail (uri != NULL, NULL);

  item = mx_texture_cache_get_item (self, uri, TRUE);

  if (item && item->meta)
    {
      MxTextureCacheMetaEntry *entry = g_hash_table_lookup (item->meta, ident);

      if (entry->texture)
        {
          ClutterActor *texture = clutter_texture_new ();
          clutter_texture_set_cogl_texture ((ClutterTexture*) texture,
                                            entry->texture);
          return (ClutterTexture *)texture;
        }
    }

  return NULL;
}

/**
 * mx_texture_cache_get_meta_cogl_texture:
 * @self: A #MxTextureCache
 * @uri: A URI or path to an image file
 * @ident: A unique identifier
 *
 * Retrieves the #CoglHandle of the previously added image associated
 * with the given unique identifier.
 *
 * See mx_texture_cache_insert_meta()
 *
 * Returns: (transfer full): A #CoglHandle to a texture, with an added
 *   reference. %NULL if no image was found.
 *
 * Since: 1.2
 */
CoglHandle
mx_texture_cache_get_meta_cogl_texture (MxTextureCache *self,
                                        const gchar    *uri,
                                        gpointer        ident)
{
  MxTextureCacheItem *item;

  g_return_val_if_fail (MX_IS_TEXTURE_CACHE (self), NULL);
  g_return_val_if_fail (uri != NULL, NULL);

  item = mx_texture_cache_get_item (self, uri, TRUE);

  if (item && item->meta)
    {
      MxTextureCacheMetaEntry *entry = g_hash_table_lookup (item->meta, ident);

      if (entry->texture)
        return cogl_handle_ref (entry->texture);
    }

  return NULL;
}
