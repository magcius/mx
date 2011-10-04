/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-private.h: Private declarations and functions
 *
 * Copyright 2007 OpenedHand
 * Copyright 2009 Intel Corporation.
 * Copyright 2010 Red Hat, Inc.
 * Copyright 2010 Florian Müllner
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ST_PRIVATE_H__
#define __ST_PRIVATE_H__

#include <glib.h>
#include <cairo.h>
#include "mx-widget.h"
#include "st-shadow.h"

CoglHandle _st_create_texture_material (CoglHandle src_texture);

/* Helper for widgets which need to draw additional shadows */
CoglHandle _st_create_shadow_material (StShadow   *shadow_spec,
                                       CoglHandle  src_texture);
CoglHandle _st_create_shadow_material_from_actor (StShadow     *shadow_spec,
                                                  ClutterActor *actor);
cairo_pattern_t *_st_create_shadow_cairo_pattern (StShadow        *shadow_spec,
                                                  cairo_pattern_t *src_pattern);

void _st_paint_shadow_with_opacity (StShadow        *shadow_spec,
                                    CoglHandle       shadow_material,
                                    ClutterActorBox *box,
                                    guint8           paint_opacity);

#endif /* __ST_PRIVATE_H__ */
