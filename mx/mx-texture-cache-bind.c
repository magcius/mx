
/* This file is part of mx-texture-cache.c */

typedef struct {
  MxTextureCache *cache;
  ClutterTexture *texture;
  GObject *source;
  guint notify_signal_id;
  gboolean weakref_active;
} MxTextureCachePropertyBind;

static void
mx_texture_cache_reset_texture (MxTextureCachePropertyBind *bind,
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
mx_texture_cache_on_pixbuf_notify (GObject           *object,
                                   GParamSpec        *paramspec,
                                   gpointer           data)
{
  MxTextureCachePropertyBind *bind = data;
  mx_texture_cache_reset_texture (bind, paramspec->name);
}

static void
mx_texture_cache_bind_weak_notify (gpointer     data,
                                   GObject     *source_location)
{
  MxTextureCachePropertyBind *bind = data;
  bind->weakref_active = FALSE;
  g_signal_handler_disconnect (bind->source, bind->notify_signal_id);
}

static void
mx_texture_cache_free_bind (gpointer data)
{
  MxTextureCachePropertyBind *bind = data;
  if (bind->weakref_active)
    g_object_weak_unref (G_OBJECT(bind->texture), mx_texture_cache_bind_weak_notify, bind);
  g_slice_free (MxTextureCachePropertyBind, bind);
}

/**
 * mx_texture_cache_bind_pixbuf_property:
 * @cache:
 * @object: A #GObject with a property @property_name of type #GdkPixbuf
 * @property_name: Name of a property
 *
 * Create a #ClutterTexture which tracks the #GdkPixbuf value of a GObject property
 * named by @property_name.  Unlike other methods in MxTextureCache, the underlying
 * CoglHandle is not shared by default with other invocations to this method.
 *
 * If the source object is destroyed, the texture will continue to show the last
 * value of the property.
 *
 * Return value: (transfer none): A new #ClutterActor
 */
ClutterActor *
mx_texture_cache_bind_pixbuf_property (MxTextureCache    *cache,
                                       GObject           *object,
                                       const char        *property_name)
{
  ClutterTexture *texture;
  gchar *notify_key;
  MxTextureCachePropertyBind *bind;

  texture = CLUTTER_TEXTURE (clutter_texture_new ());

  bind = g_slice_new0 (MxTextureCachePropertyBind);
  bind->cache = cache;
  bind->texture = texture;
  bind->source = object;
  g_object_weak_ref (G_OBJECT (texture), mx_texture_cache_bind_weak_notify, bind);
  bind->weakref_active = TRUE;

  mx_texture_cache_reset_texture (bind, property_name);

  notify_key = g_strdup_printf ("notify::%s", property_name);
  bind->notify_signal_id = g_signal_connect_data (object,
                                                  notify_key,
                                                  G_CALLBACK (mx_texture_cache_on_pixbuf_notify),
                                                  bind,
                                                  (GClosureNotify) mx_texture_cache_free_bind,
                                                  0);
  g_free (notify_key);

  return CLUTTER_ACTOR(texture);
}
