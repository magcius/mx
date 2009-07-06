/*
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
#include <stdio.h>
#include <stdlib.h>

#include <clutter/clutter.h>
#include <nbtk/nbtk.h>

gint
sort_func (ClutterModel *model,
           const GValue *a,
           const GValue *b,
           gpointer user_data)
{
  const ClutterColor *ca, *cb;
  gfloat h1, h2;

  ca = clutter_value_get_color (a);
  cb = clutter_value_get_color (b);

  clutter_color_to_hls (ca, &h1, NULL, NULL);
  clutter_color_to_hls (cb, &h2, NULL, NULL);

  return h1 - h2;
}

gboolean
filter_func (ClutterModel *model,
             ClutterModelIter *iter,
             gpointer user_data)
{
  ClutterColor *color;
  gboolean show;
  gfloat h;

  clutter_model_iter_get (iter, 0, &color, -1);

  clutter_color_to_hls (color, &h, NULL, NULL);

  show = (h > 90 && h < 180);

  clutter_color_free (color);

  return show;
}

gboolean
key_release_cb (ClutterActor *actor,
                ClutterKeyEvent *event,
                ClutterModel *model)
{

  if (event->keyval == 's')
    {
      static gboolean sort_set = 0;

      if (!sort_set)
        clutter_model_set_sort (model, 0, sort_func, NULL, NULL);
      else
        clutter_model_set_sort (model, -1, NULL, NULL, NULL);

      sort_set = !sort_set;
    }

  if (event->keyval == 'f')
    {
      static gboolean filter_set = 0;

      if (!filter_set)
        clutter_model_set_filter (model, filter_func, NULL, NULL);
      else
        clutter_model_set_filter (model, NULL, NULL, NULL);

      filter_set = !filter_set;
    }

  return FALSE;
}

int
main (int argc, char *argv[])
{
  NbtkWidget *view, *scroll;
  ClutterActor *stage;
  ClutterModel *model;
  ClutterColor color = { 0x00, 0xff, 0xff, 0xff };
  gint i;

  clutter_init (&argc, &argv);

  stage = clutter_stage_get_default ();
  clutter_actor_set_size (stage, 320, 240);

  scroll = nbtk_scroll_view_new ();
  clutter_actor_set_size (CLUTTER_ACTOR (scroll), 320, 240);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage),
                               CLUTTER_ACTOR (scroll));

  view = nbtk_item_view_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (scroll),
                               CLUTTER_ACTOR (view));


  model = clutter_list_model_new (2, CLUTTER_TYPE_COLOR, "color",
                                  G_TYPE_FLOAT, "size");

  for (i = 0; i < 360; i++)
    {
      clutter_color_from_hls (&color,
                              g_random_double_range (0.0, 360.0), 0.6, 0.6);
      clutter_model_append (model, 0, &color, 1, 32.0, -1);
    }

  nbtk_item_view_set_model (NBTK_ITEM_VIEW (view), model);
  nbtk_item_view_set_item_type (NBTK_ITEM_VIEW (view), CLUTTER_TYPE_RECTANGLE);
  nbtk_item_view_add_attribute (NBTK_ITEM_VIEW (view), "color", 0);
  nbtk_item_view_add_attribute (NBTK_ITEM_VIEW (view), "width", 1);
  nbtk_item_view_add_attribute (NBTK_ITEM_VIEW (view), "height", 1);


  g_signal_connect (stage, "key-release-event", G_CALLBACK (key_release_cb), model);
  clutter_actor_show (stage);
  clutter_main ();

  return EXIT_SUCCESS;
}
