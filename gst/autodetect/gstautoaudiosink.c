/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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
#include "config.h"
#endif

#include <string.h>

#include "gstautoaudiosink.h"
#include "gstautodetect.h"

static void gst_auto_audio_sink_base_init (GstAutoAudioSinkClass * klass);
static void gst_auto_audio_sink_class_init (GstAutoAudioSinkClass * klass);
static void gst_auto_audio_sink_init (GstAutoAudioSink * sink);
static void gst_auto_audio_sink_detect (GstAutoAudioSink * sink);

static GstBinClass *parent_class = NULL;

GType
gst_auto_audio_sink_get_type (void)
{
  static GType gst_auto_audio_sink_type = 0;

  if (!gst_auto_audio_sink_type) {
    static const GTypeInfo gst_auto_audio_sink_info = {
      sizeof (GstAutoAudioSinkClass),
      (GBaseInitFunc) gst_auto_audio_sink_base_init,
      NULL,
      (GClassInitFunc) gst_auto_audio_sink_class_init,
      NULL,
      NULL,
      sizeof (GstAutoAudioSink),
      0,
      (GInstanceInitFunc) gst_auto_audio_sink_init,
    };

    gst_auto_audio_sink_type = g_type_register_static (GST_TYPE_BIN,
        "GstAutoAudioSink", &gst_auto_audio_sink_info, 0);
  }

  return gst_auto_audio_sink_type;
}

static void
gst_auto_audio_sink_base_init (GstAutoAudioSinkClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails gst_auto_audio_sink_details = {
    "Auto audio sink",
    "Sink/Audio",
    "Audio sink embedding the Auto-settings for audio output",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };
  GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (eklass,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (eklass, &gst_auto_audio_sink_details);
}

static void
gst_auto_audio_sink_class_init (GstAutoAudioSinkClass * klass)
{
  parent_class = g_type_class_ref (GST_TYPE_BIN);
}

static void
gst_auto_audio_sink_init (GstAutoAudioSink * sink)
{
  gst_auto_audio_sink_detect (sink);
}

static gboolean
gst_auto_audio_sink_factory_filter (GstPluginFeature * feature, gpointer data)
{
  guint rank;
  const gchar *klass;

  /* we only care about element factories */
  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  /* audio sinks */
  klass = gst_element_factory_get_klass (GST_ELEMENT_FACTORY (feature));
  if (strcmp (klass, "Sink/Audio") != 0)
    return FALSE;

  /* only select elements with autoplugging rank */
  rank = gst_plugin_feature_get_rank (feature);
  if (rank < GST_RANK_MARGINAL)
    return FALSE;

  return TRUE;
}

static gint
gst_auto_audio_sink_compare_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  gint diff;

  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;
  return strcmp (gst_plugin_feature_get_name (f2),
      gst_plugin_feature_get_name (f1));
}

static GstElement *
gst_auto_audio_sink_find_best (GstAutoAudioSink * sink)
{
  GList *list;

  list = gst_registry_pool_feature_filter (
      (GstPluginFeatureFilter) gst_auto_audio_sink_factory_filter, FALSE, sink);
  list = g_list_sort (list, (GCompareFunc) gst_auto_audio_sink_compare_ranks);

  /* FIXME:
   * - soundservers have no priority yet.
   * - soundserversinks should only be chosen if already running, or if
   *    the user explicitely wants this to run... That is not easy.
   */

  for (; list != NULL; list = list->next) {
    GstElementFactory *f = GST_ELEMENT_FACTORY (list->data);
    GstElement *el;

    if ((el = gst_element_factory_create (f, "actual-sink"))) {
      if (gst_element_set_state (el, GST_STATE_READY) == GST_STATE_SUCCESS) {
        gst_element_set_state (el, GST_STATE_NULL);
        return el;
      }

      gst_object_unref (GST_OBJECT (el));
    }
  }

  return NULL;
}

static void
gst_auto_audio_sink_detect (GstAutoAudioSink * sink)
{
  GstElement *esink;

  /* find element */
  if (!(esink = gst_auto_audio_sink_find_best (sink))) {
    GST_ELEMENT_ERROR (sink, LIBRARY, INIT, (NULL),
        ("Failed to find a supported audio sink"));
    return;
  }

  /* attach ghost pad */
  gst_bin_add (GST_BIN (sink), esink);
  gst_element_add_ghost_pad (GST_ELEMENT (sink),
      gst_element_get_pad (esink, "sink"), "sink");
}
