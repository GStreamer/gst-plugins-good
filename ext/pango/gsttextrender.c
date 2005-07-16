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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <gst/gst.h>
#include "gsttextrender.h"

GST_DEBUG_CATEGORY_EXTERN (pango_debug);
#define GST_CAT_DEFAULT pango_debug

static GstElementDetails text_render_details = {
  "Text Render",
  "Filter/Editor/Video",
  "Renders a text string to a image bitmap",
  "David Schleef <ds@schleef.org>, "
      "Ronald S. Bultje <rbultje@ronald.bitfreak.net>"
};

enum
{
  ARG_0,
  ARG_FONT_DESC
};


static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) AYUV, "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ], framerate = (double) 1")
    );

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-pango-markup; text/plain")
    );

static void gst_text_render_base_init (gpointer g_class);
static void gst_text_render_class_init (GstTextRenderClass * klass);
static void gst_text_render_init (GstTextRender * overlay);
static void gst_text_render_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_text_render_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_text_render_finalize (GObject * object);

static GstElementClass *parent_class = NULL;

/*static guint gst_text_render_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_text_render_get_type (void)
{
  static GType text_render_type = 0;

  if (!text_render_type) {
    static const GTypeInfo text_render_info = {
      sizeof (GstTextRenderClass),
      gst_text_render_base_init,
      NULL,
      (GClassInitFunc) gst_text_render_class_init,
      NULL,
      NULL,
      sizeof (GstTextRender),
      0,
      (GInstanceInitFunc) gst_text_render_init,
    };

    text_render_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstTextRender",
        &text_render_info, 0);
  }
  return text_render_type;
}

static void
gst_text_render_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));

  gst_element_class_set_details (element_class, &text_render_details);
}

static void
gst_text_render_class_init (GstTextRenderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_text_render_finalize;
  gobject_class->set_property = gst_text_render_set_property;
  gobject_class->get_property = gst_text_render_get_property;

  klass->pango_context = pango_ft2_get_context (72, 72);
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font "
          "to be used for rendering. "
          "See documentation of "
          "pango_font_description_from_string"
          " for syntax.", "", G_PARAM_WRITABLE));
}


static void
resize_bitmap (GstTextRender * overlay, int width, int height)
{
  FT_Bitmap *bitmap = &overlay->bitmap;
  int pitch = (width | 3) + 1;
  int size = pitch * height;

  /* no need to keep reallocating; just keep the maximum size so far */
  if (size <= overlay->bitmap_buffer_size) {
    bitmap->rows = height;
    bitmap->width = width;
    bitmap->pitch = pitch;
    memset (bitmap->buffer, 0, overlay->bitmap_buffer_size);
    return;
  }
  if (!bitmap->buffer) {
    /* initialize */
    bitmap->pixel_mode = ft_pixel_mode_grays;
    bitmap->num_grays = 256;
  }
  if (bitmap->buffer)
    bitmap->buffer = g_realloc (bitmap->buffer, size);
  else
    bitmap->buffer = g_malloc (size);
  bitmap->rows = height;
  bitmap->width = width;
  bitmap->pitch = pitch;
  memset (bitmap->buffer, 0, size);
  overlay->bitmap_buffer_size = size;
}

static void
render_text (GstTextRender * overlay)
{
  PangoRectangle ink_rect, logical_rect;

  pango_layout_get_pixel_extents (overlay->layout, &ink_rect, &logical_rect);
  resize_bitmap (overlay, ink_rect.width, ink_rect.height + ink_rect.y);
  pango_ft2_render_layout (&overlay->bitmap, overlay->layout, -ink_rect.x, 0);
  overlay->baseline_y = ink_rect.y;
}

static GstPadLinkReturn
gst_text_render_link (GstPad * pad, const GstCaps * caps)
{
  GstTextRender *overlay = GST_TEXT_RENDER (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);
  overlay->width = overlay->height = 0;
  gst_structure_get_int (structure, "width", &overlay->width);
  gst_structure_get_int (structure, "height", &overlay->height);

  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_text_render_fixate (GstPad * pad, const GstCaps * caps)
{
  GstTextRender *overlay = GST_TEXT_RENDER (gst_pad_get_parent (pad));
  GstCaps *copy = gst_caps_copy (caps);
  GstStructure *s = gst_caps_get_structure (copy, 0);

  if (gst_caps_structure_fixate_field_nearest_int (s, "width",
          overlay->bitmap.width) ||
      gst_caps_structure_fixate_field_nearest_int (s, "height",
          overlay->bitmap.rows))
    return copy;

  gst_caps_free (copy);

  return NULL;
}

static void
gst_text_renderer_bitmap_to_ayuv (GstTextRender * overlay, FT_Bitmap * bitmap,
    guchar * pixbuf)
{
  int y;                        /* text bitmap coordinates */
  int rowinc, bit_rowinc;
  guchar *p, *bitp;
  guchar v;

  rowinc = overlay->width - bitmap->width;
  bit_rowinc = bitmap->pitch - bitmap->width;

  bitp = bitmap->buffer;
  p = pixbuf;

  for (y = 0; y < bitmap->rows; y++) {
    int n;

    for (n = bitmap->width; n > 0; --n) {
      v = *bitp;
      if (v) {
        p[0] = v;
        p[1] = 255;
        p[2] = p[3] = 0x80;
      }
      p += 4;
      bitp++;
    }
    p += rowinc * 4;
    bitp += bit_rowinc;
  }
}


static void
gst_text_render_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data), *out;
  GstTextRender *overlay = GST_TEXT_RENDER (gst_pad_get_parent (pad));
  guint size = GST_BUFFER_SIZE (buf);
  guint8 *data = GST_BUFFER_DATA (buf);

  /* somehow pango barfs over "\0" buffers... */
  while (size > 0 &&
      (data[size - 1] == '\r' ||
          data[size - 1] == '\n' || data[size - 1] == '\0')) {
    size--;
  }

  /* render text */
  GST_DEBUG ("rendering '%*s'", size, data);
  pango_layout_set_markup (overlay->layout, (gchar *) data, size);
  render_text (overlay);

  if (GST_PAD_LINK_FAILED (gst_pad_renegotiate (overlay->srcpad))) {
    GST_ELEMENT_ERROR (overlay, CORE, NEGOTIATION, (NULL), (NULL));
    return;
  }

  /* put in a buffer */
  out = gst_buffer_new_and_alloc (overlay->width * overlay->height * 4);
  gst_buffer_stamp (out, GST_BUFFER (buf));
//  gst_buffer_stamp (out, buf);
  data = GST_BUFFER_DATA (out);
  gint n;

  for (n = 0; n < overlay->width * overlay->height; n++) {
    data[n * 4] = 0;
    data[n * 4 + 1] = 0;
    data[n * 4 + 2] = data[n * 4 + 3] = 128;
  }
  if (overlay->bitmap.buffer) {
    gst_text_renderer_bitmap_to_ayuv (overlay, &overlay->bitmap, data);
  }
  gst_data_unref (_data);

  gst_pad_push (overlay->srcpad, GST_DATA (out));
}

static void
gst_text_render_finalize (GObject * object)
{
  GstTextRender *overlay = GST_TEXT_RENDER (object);

  if (overlay->layout) {
    g_object_unref (overlay->layout);
    overlay->layout = NULL;
  }
  if (overlay->bitmap.buffer) {
    g_free (overlay->bitmap.buffer);
    overlay->bitmap.buffer = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_text_render_init (GstTextRender * overlay)
{
  /* sink */
  overlay->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_pad_set_chain_function (overlay->sinkpad, gst_text_render_chain);
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->sinkpad);

  /* source */
  overlay->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&src_template_factory), "src");
  gst_pad_set_link_function (overlay->srcpad, gst_text_render_link);
  gst_pad_set_fixate_function (overlay->srcpad, gst_text_render_fixate);
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->srcpad);

  overlay->layout =
      pango_layout_new (GST_TEXT_RENDER_GET_CLASS (overlay)->pango_context);
  memset (&overlay->bitmap, 0, sizeof (overlay->bitmap));
}


static void
gst_text_render_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTextRender *overlay;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TEXT_RENDER (object));
  overlay = GST_TEXT_RENDER (object);

  switch (prop_id) {
    case ARG_FONT_DESC:
    {
      PangoFontDescription *desc;

      desc = pango_font_description_from_string (g_value_get_string (value));
      if (desc) {
        GST_LOG ("font description set: %s", g_value_get_string (value));
        pango_layout_set_font_description (overlay->layout, desc);
        pango_font_description_free (desc);
        render_text (overlay);
      } else
        GST_WARNING ("font description parse failed: %s",
            g_value_get_string (value));
      break;
    }

    default:
      break;
  }
}

static void
gst_text_render_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTextRender *overlay;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TEXT_RENDER (object));
  overlay = GST_TEXT_RENDER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
