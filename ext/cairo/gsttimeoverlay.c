/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * This file was (probably) generated from gsttimeoverlay.c,
 * gsttimeoverlay.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*#define DEBUG_ENABLED */
#include <gsttimeoverlay.h>
#include <string.h>
#include <math.h>

#include <cairo.h>

static void gst_timeoverlay_base_init (gpointer g_class);
static void gst_timeoverlay_class_init (gpointer g_class, gpointer class_data);
static void gst_timeoverlay_init (GTypeInstance * instance, gpointer g_class);

static void gst_timeoverlay_planar411 (GstVideofilter * videofilter, void *dest,
    void *src);
static void gst_timeoverlay_setup (GstVideofilter * videofilter);

GType
gst_cairotimeoverlay_get_type (void)
{
  static GType timeoverlay_type = 0;

  if (!timeoverlay_type) {
    static const GTypeInfo timeoverlay_info = {
      sizeof (GstTimeoverlayClass),
      gst_timeoverlay_base_init,
      NULL,
      gst_timeoverlay_class_init,
      NULL,
      NULL,
      sizeof (GstTimeoverlay),
      0,
      gst_timeoverlay_init,
    };

    timeoverlay_type = g_type_register_static (GST_TYPE_VIDEOFILTER,
        "GstCairoTimeOverlay", &timeoverlay_info, 0);
  }
  return timeoverlay_type;
}

static GstVideofilterFormat gst_timeoverlay_formats[] = {
  {"I420", 12, gst_timeoverlay_planar411,},
};


static void
gst_timeoverlay_base_init (gpointer g_class)
{
  static GstElementDetails timeoverlay_details =
      GST_ELEMENT_DETAILS ("Time Overlay",
      "Filter/Editor/Video",
      "Overlays the time on a video stream",
      "David Schleef <ds@schleef.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;

  gst_element_class_set_details (element_class, &timeoverlay_details);

  for (i = 0; i < G_N_ELEMENTS (gst_timeoverlay_formats); i++) {
    gst_videofilter_class_add_format (videofilter_class,
        gst_timeoverlay_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_timeoverlay_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

  videofilter_class->setup = gst_timeoverlay_setup;
}

static void
gst_timeoverlay_init (GTypeInstance * instance, gpointer g_class)
{
  GstTimeoverlay *timeoverlay = GST_TIMEOVERLAY (instance);
  GstVideofilter *videofilter;

  GST_DEBUG ("gst_timeoverlay_init");

  videofilter = GST_VIDEOFILTER (timeoverlay);

  /* do stuff */
}

static void
gst_timeoverlay_setup (GstVideofilter * videofilter)
{
  GstTimeoverlay *timeoverlay;
  cairo_font_extents_t font_extents;
  cairo_surface_t *surface;
  cairo_t *cr;

  g_return_if_fail (GST_IS_TIMEOVERLAY (videofilter));
  timeoverlay = GST_TIMEOVERLAY (videofilter);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 64, 64);
  cr = cairo_create (surface);

  cairo_set_source_rgb (cr, 0, 0, 0);

  cairo_select_font_face (cr, "monospace", 0, 0);
  cairo_set_font_size (cr, 20);

  cairo_font_extents (cr, &font_extents);
  timeoverlay->text_height = font_extents.height;

  cairo_destroy (cr);
  cairo_surface_destroy (surface);
}

static char *
gst_timeoverlay_print_smpte_time (guint64 time)
{
  int hours;
  int minutes;
  int seconds;
  int ms;
  double x;

  x = rint ((time + 500000) * 1e-6);

  hours = floor (x / (60 * 60 * 1000));
  x -= hours * 60 * 60 * 1000;
  minutes = floor (x / (60 * 1000));
  x -= minutes * 60 * 1000;
  seconds = floor (x / (1000));
  x -= seconds * 1000;
  ms = rint (x);

  return g_strdup_printf ("%02d:%02d:%02d.%03d", hours, minutes, seconds, ms);
}

static void
gst_timeoverlay_planar411 (GstVideofilter * videofilter, void *dest, void *src)
{
  GstTimeoverlay *timeoverlay;
  int width;
  int height;
  int b_width;
  char *string;
  int i, j;
  unsigned char *image;
  cairo_text_extents_t extents;
  cairo_surface_t *surface;
  cairo_t *cr;

  g_return_if_fail (GST_IS_TIMEOVERLAY (videofilter));
  timeoverlay = GST_TIMEOVERLAY (videofilter);

  width = gst_videofilter_get_input_width (videofilter);
  height = gst_videofilter_get_input_height (videofilter);

  string =
      gst_timeoverlay_print_smpte_time (GST_BUFFER_TIMESTAMP (videofilter->
          in_buf));

  image = g_malloc (4 * width * timeoverlay->text_height);

  surface = cairo_image_surface_create_for_data (image, CAIRO_FORMAT_ARGB32,
      width, timeoverlay->text_height, width * 4);

  cr = cairo_create (surface);

  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_select_font_face (cr, "monospace", 0, 0);
  cairo_set_font_size (cr, 20);

  cairo_save (cr);

  cairo_rectangle (cr, 0, 0, width, timeoverlay->text_height);
  cairo_set_source_rgba (cr, 0, 0, 0, 0);

  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_fill (cr);
  cairo_restore (cr);

  cairo_save (cr);
  cairo_text_extents (cr, string, &extents);

  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_move_to (cr, 0, timeoverlay->text_height - 2);
  cairo_show_text (cr, string);
  g_free (string);

#if 0
  cairo_text_path (cr, string);
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);
#endif

  cairo_restore (cr);

  b_width = extents.width;
  if (b_width > width)
    b_width = width;

  memcpy (dest, src, videofilter->from_buf_size);
  for (i = 0; i < timeoverlay->text_height; i++) {
    for (j = 0; j < b_width; j++) {
      ((unsigned char *) dest)[i * width + j] = image[(i * width + j) * 4 + 0];
    }
  }
  for (i = 0; i < timeoverlay->text_height / 2; i++) {
    memset (dest + width * height + i * (width / 2), 128, b_width / 2);
    memset (dest + width * height + (width / 2) * (height / 2) +
        i * (width / 2), 128, b_width / 2);
  }

  g_free (image);

  cairo_destroy (cr);
  cairo_surface_destroy (surface);
}
