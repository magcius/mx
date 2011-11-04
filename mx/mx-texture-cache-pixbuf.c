/* This file is part of mx-texture-cache.c */

/* A private structure for keeping width and height. */
typedef struct {
  int width;
  int height;
} Dimensions;

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

static GdkPixbuf *
load_pixbuf_async_finish (MxTextureCache *cache,
                          GAsyncResult   *result,
                          GError        **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);
  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;
  return g_simple_async_result_get_op_res_gpointer (simple);
}

static void
on_pixbuf_loaded (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  GSList *iter;
  MxTextureCache *cache;
  MxTextureCachePrivate *priv;
  MxTextureCacheAsyncLoadData *data;
  MxTextureCacheItem *item;
  GdkPixbuf *pixbuf;
  GError *error = NULL;
  CoglHandle texdata = NULL;

  data = user_data;
  cache = MX_TEXTURE_CACHE (source);

  priv = TEXTURE_CACHE_PRIVATE (cache);

  g_hash_table_remove (priv->outstanding_requests, data->key);

  pixbuf = load_pixbuf_async_finish (cache, result, &error);
  if (pixbuf == NULL)
    goto out;

  texdata = pixbuf_to_cogl_handle (pixbuf, data->enforced_square);

  g_object_unref (pixbuf);

  if (data->policy != MX_TEXTURE_CACHE_POLICY_NONE)
    {
      if (!g_hash_table_lookup (priv->cache, data->key))
        {
          cogl_handle_ref (texdata);
          item = mx_texture_cache_item_new ();
          item->ptr = texdata;
          add_texture_to_cache (cache, g_strdup (data->key), item);
        }
    }

  for (iter = data->textures; iter; iter = iter->next)
    {
      ClutterTexture *texture = iter->data;
      /* Reverse the opacity we added while loading */
      clutter_texture_set_cogl_texture (texture, texdata);
      g_object_set (texture, "opacity", 255, NULL);
    }

out:
  if (texdata)
    cogl_handle_unref (texdata);

  mx_texture_cache_async_load_data_free (data);
  g_free (data);

  g_clear_error (&error);
}
