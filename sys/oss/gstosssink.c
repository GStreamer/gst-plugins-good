/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosssink.c: 
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
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#ifdef HAVE_OSS_INCLUDE_IN_SYS
#include <sys/soundcard.h>
#else

#ifdef HAVE_OSS_INCLUDE_IN_ROOT
#include <soundcard.h>
#else

#include <machine/soundcard.h>

#endif /* HAVE_OSS_INCLUDE_IN_ROOT */

#endif /* HAVE_OSS_INCLUDE_IN_SYS */

#include "gstosssink.h"

/* elementfactory information */
static GstElementDetails gst_osssink_details =
GST_ELEMENT_DETAILS ("Audio Sink (OSS)",
    "Sink/Audio",
    "Output to a sound card via OSS",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Wim Taymans <wim.taymans@chello.be>");

static void gst_osssink_base_init (gpointer g_class);
static void gst_osssink_class_init (GstOssSinkClass * klass);
static void gst_osssink_init (GstOssSink * osssink);
static void gst_osssink_dispose (GObject * object);

static GstElementStateReturn gst_osssink_change_state (GstElement * element);
static void gst_osssink_set_clock (GstElement * element, GstClock * clock);
static GstClock *gst_osssink_get_clock (GstElement * element);
static GstClockTime gst_osssink_get_time (GstClock * clock, gpointer data);

static const GstFormat *gst_osssink_get_formats (GstPad * pad);
static gboolean gst_osssink_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static const GstQueryType *gst_osssink_get_query_types (GstPad * pad);
static gboolean gst_osssink_query (GstElement * element, GstQueryType type,
    GstFormat * format, gint64 * value);
static gboolean gst_osssink_sink_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value);

static GstCaps *gst_osssink_getcaps (GstPad * pad);
static gboolean gst_osssink_setcaps (GstPad * pad, GstCaps * caps);

static void gst_osssink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_osssink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_osssink_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_osssink_handle_event (GstPad * pad, GstEvent * event);

/* OssSink signals and args */
enum
{
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_MUTE,
  ARG_FRAGMENT,
  ARG_BUFFER_SIZE,
  ARG_SYNC,
  ARG_CHUNK_SIZE
      /* FILL ME */
};

static GstStaticPadTemplate osssink_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );

static GstElementClass *parent_class = NULL;
static guint gst_osssink_signals[LAST_SIGNAL] = { 0 };

GType
gst_osssink_get_type (void)
{
  static GType osssink_type = 0;

  if (!osssink_type) {
    static const GTypeInfo osssink_info = {
      sizeof (GstOssSinkClass),
      gst_osssink_base_init,
      NULL,
      (GClassInitFunc) gst_osssink_class_init,
      NULL,
      NULL,
      sizeof (GstOssSink),
      0,
      (GInstanceInitFunc) gst_osssink_init,
    };

    osssink_type =
        g_type_register_static (GST_TYPE_OSSELEMENT, "GstOssSink",
        &osssink_info, 0);
  }

  return osssink_type;
}

static void
gst_osssink_dispose (GObject * object)
{
  GstOssSink *osssink = (GstOssSink *) object;

  if (osssink->provided_clock) {
    gst_object_unparent (GST_OBJECT (osssink->provided_clock));
    osssink->provided_clock = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_osssink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_osssink_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&osssink_sink_factory));
}
static void
gst_osssink_class_init (GstOssSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OSSELEMENT);

  gobject_class->set_property = gst_osssink_set_property;
  gobject_class->get_property = gst_osssink_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute the audio",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC,
      g_param_spec_boolean ("sync", "Sync",
          "If syncing on timestamps should be enabled", TRUE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAGMENT,
      g_param_spec_int ("fragment", "Fragment",
          "The fragment as 0xMMMMSSSS (MMMM = total fragments, 2^SSSS = fragment size)",
          0, G_MAXINT, 6, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFFER_SIZE,
      g_param_spec_uint ("buffer_size", "Buffer size",
          "Size of buffers in osssink's bufferpool (bytes)", 0, G_MAXINT, 4096,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CHUNK_SIZE,
      g_param_spec_uint ("chunk_size", "Chunk size",
          "Write data in chunk sized buffers", 0, G_MAXUINT, 4096,
          G_PARAM_READWRITE));

  gst_osssink_signals[SIGNAL_HANDOFF] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstOssSinkClass, handoff), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->dispose = gst_osssink_dispose;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_osssink_change_state);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_osssink_query);
  gstelement_class->set_clock = gst_osssink_set_clock;
  gstelement_class->get_clock = gst_osssink_get_clock;

}

static void
gst_osssink_init (GstOssSink * osssink)
{
  osssink->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&osssink_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (osssink), osssink->sinkpad);
  gst_pad_set_getcaps_function (osssink->sinkpad, gst_osssink_getcaps);
  gst_pad_set_setcaps_function (osssink->sinkpad, gst_osssink_setcaps);
  //gst_pad_set_fixate_function (osssink->sinkpad, gst_osssink_sink_fixate);
  gst_pad_set_convert_function (osssink->sinkpad, gst_osssink_convert);
  gst_pad_set_query_function (osssink->sinkpad, gst_osssink_sink_query);
  gst_pad_set_query_type_function (osssink->sinkpad,
      gst_osssink_get_query_types);
  gst_pad_set_formats_function (osssink->sinkpad, gst_osssink_get_formats);

  gst_pad_set_event_function (osssink->sinkpad, gst_osssink_handle_event);
  gst_pad_set_chain_function (osssink->sinkpad, gst_osssink_chain);

  GST_DEBUG ("initializing osssink");
  osssink->bufsize = 4096;
  osssink->chunk_size = 4096;
  osssink->mute = FALSE;
  osssink->sync = TRUE;
  osssink->provided_clock =
      gst_audio_clock_new ("ossclock", gst_osssink_get_time, osssink);
  gst_object_set_parent (GST_OBJECT (osssink->provided_clock),
      GST_OBJECT (osssink));
  osssink->handled = 0;
}

static gboolean
gst_osssink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstOssSink *osssink = GST_OSSSINK (GST_PAD_PARENT (pad));

  if (!gst_osselement_parse_caps (GST_OSSELEMENT (osssink), caps)) {
    GST_ELEMENT_ERROR (osssink, CORE, NEGOTIATION, (NULL),
        ("received unkown format"));
    gst_element_abort_preroll (GST_ELEMENT (osssink));
    return FALSE;
  }
  if (!gst_osselement_sync_parms (GST_OSSELEMENT (osssink))) {
    GST_ELEMENT_ERROR (osssink, CORE, NEGOTIATION, (NULL),
        ("received unkown format"));
    gst_element_abort_preroll (GST_ELEMENT (osssink));
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_osssink_getcaps (GstPad * pad)
{
  GstOssSink *osssink = GST_OSSSINK (GST_PAD_PARENT (pad));
  GstCaps *caps;

  gst_osselement_probe_caps (GST_OSSELEMENT (osssink));

  if (GST_OSSELEMENT (osssink)->probed_caps == NULL) {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  } else {
    caps = gst_caps_copy (GST_OSSELEMENT (osssink)->probed_caps);
  }

  return caps;
}

static inline gint
gst_osssink_get_delay (GstOssSink * osssink)
{
  gint delay = 0;
  gint ret;

  if (GST_OSSELEMENT (osssink)->fd == -1)
    return 0;

#ifdef SNDCTL_DSP_GETODELAY
  ret = ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_GETODELAY, &delay);
#else
  ret = -1;
#endif
  if (ret < 0) {
    audio_buf_info info;

    if (ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_GETOSPACE, &info) < 0) {
      delay = 0;
    } else {
      delay = (info.fragstotal * info.fragsize) - info.bytes;
    }
  }

  return delay;
}

static GstClockTime
gst_osssink_get_time (GstClock * clock, gpointer data)
{
  GstOssSink *osssink = GST_OSSSINK (data);
  gint delay;
  GstClockTime res;

  if (!GST_OSSELEMENT (osssink)->bps)
    return 0;

  delay = gst_osssink_get_delay (osssink);

  /* sometimes delay is bigger than the number of bytes sent to the device,
   * which screws up this calculation, we assume that everything is still
   * in the device then
   * thomas: with proper handling of the return value, this doesn't seem to
   * happen anymore, so remove the second code path after april 2004 */
  if (delay > (gint64) osssink->handled) {
    /*g_warning ("Delay %d > osssink->handled %" G_GUINT64_FORMAT
       ", setting to osssink->handled",
       delay, osssink->handled); */
    delay = osssink->handled;
  }
  res =
      ((gint64) osssink->handled -
      delay) * GST_SECOND / GST_OSSELEMENT (osssink)->bps;
  if (res < 0)
    res = 0;

  return res;
}

static GstClock *
gst_osssink_get_clock (GstElement * element)
{
  GstOssSink *osssink;

  osssink = GST_OSSSINK (element);

  return GST_CLOCK (osssink->provided_clock);
}

static void
gst_osssink_set_clock (GstElement * element, GstClock * clock)
{
  GstOssSink *osssink;

  osssink = GST_OSSSINK (element);

  osssink->clock = clock;
}

static gboolean
gst_osssink_handle_event (GstPad * pad, GstEvent * event)
{
  GstOssSink *osssink;
  gboolean result = TRUE;

  osssink = GST_OSSSINK (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_SYNC, 0);
      gst_audio_clock_set_active (GST_AUDIO_CLOCK (osssink->provided_clock),
          FALSE);
      gst_element_finish_preroll (GST_ELEMENT (osssink),
          GST_STREAM_GET_LOCK (pad));
      gst_pipeline_post_message (GST_ELEMENT_MANAGER (osssink),
          gst_message_new_eos (GST_OBJECT (osssink)));
      break;
    default:
      break;
  }
  GST_STREAM_UNLOCK (pad);

  return result;
}

static GstFlowReturn
gst_osssink_chain (GstPad * pad, GstBuffer * buf)
{
  GstOssSink *osssink;
  GstClockTimeDiff buftime, soundtime, elementtime;
  guchar *data;
  guint to_write;
  gint delay;
  GstFlowReturn result = GST_FLOW_OK;

  /* this has to be an audio buffer */
  osssink = GST_OSSSINK (GST_PAD_PARENT (pad));

  if (!GST_OSSELEMENT (osssink)->bps) {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (osssink, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_STREAM_LOCK (pad);
  result =
      gst_element_finish_preroll (GST_ELEMENT (osssink),
      GST_STREAM_GET_LOCK (pad));
  if (result != GST_FLOW_OK) {
    goto done;
  }

  data = GST_BUFFER_DATA (buf);
  to_write = GST_BUFFER_SIZE (buf);
  /* sync audio with buffers timestamp. elementtime is the *current* time.
   * soundtime is the time if the soundcard has processed all queued data. */
  if (GST_ELEMENT (osssink)->clock) {
    elementtime = gst_clock_get_time (GST_ELEMENT (osssink)->clock) -
        GST_ELEMENT (osssink)->base_time;
  } else {
    elementtime = 0;
  }
  delay = gst_osssink_get_delay (osssink);
  if (delay < 0)
    delay = 0;
  soundtime = elementtime + delay * GST_SECOND / GST_OSSELEMENT (osssink)->bps;
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    buftime = GST_BUFFER_TIMESTAMP (buf);
  } else {
    buftime = soundtime;
  }
  GST_LOG_OBJECT (osssink,
      "time: real %" GST_TIME_FORMAT ", buffer: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (soundtime), GST_TIME_ARGS (buftime));
  if (MAX (buftime, soundtime) - MIN (buftime, soundtime) > (GST_SECOND / 10)) {
    /* we need to adjust to the buffers here */
    GST_INFO_OBJECT (osssink,
        "need sync: real %" GST_TIME_FORMAT ", buffer: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (soundtime), GST_TIME_ARGS (buftime));
    if (soundtime > buftime) {
      /* do *not* throw frames out. It's useless. The next frame will come in
       * too late. And the next one. And so on. We don't want to lose sound.
       * This is a placeholder for what - some day - should become QoS, i.e.
       * sending events upstream to drop buffers. */
    } else {
      guint64 to_handle =
          (((buftime -
                  soundtime) * GST_OSSELEMENT (osssink)->bps / GST_SECOND) /
          ((GST_OSSELEMENT (osssink)->width / 8) *
              GST_OSSELEMENT (osssink)->channels)) *
          (GST_OSSELEMENT (osssink)->width / 8) *
          GST_OSSELEMENT (osssink)->channels;
      guint8 *sbuf = g_new (guint8, to_handle);

      memset (sbuf, (GST_OSSELEMENT (osssink)->width == 8) ? 0 : 128,
          to_handle);
      while (to_handle > 0) {
        gint done = write (GST_OSSELEMENT (osssink)->fd, sbuf,
            MIN (to_handle, osssink->chunk_size));

        if (done == -1 && errno != EINTR) {
          break;
        } else {
          to_handle -= done;
          osssink->handled += done;
        }
      }
      g_free (sbuf);
    }
  }

  if (GST_OSSELEMENT (osssink)->fd >= 0 && to_write > 0) {
    if (!osssink->mute) {

      while (to_write > 0) {
        gint done = write (GST_OSSELEMENT (osssink)->fd, data,
            MIN (to_write, osssink->chunk_size));

        if (done == -1) {
          if (errno != EINTR)
            break;
        } else {
          to_write -= done;
          data += done;
          osssink->handled += done;
        }
      }
    } else {
      g_warning ("muting osssinks unimplemented wrt clocks!");
    }
  }

  gst_audio_clock_update_time ((GstAudioClock *) osssink->provided_clock,
      gst_osssink_get_time (osssink->provided_clock, osssink));

done:
  GST_STREAM_UNLOCK (pad);
  gst_buffer_unref (buf);

  return result;
}

static const GstFormat *
gst_osssink_get_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,
    GST_FORMAT_BYTES,
    0
  };

  return formats;
}

static gboolean
gst_osssink_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstOssSink *osssink;

  osssink = GST_OSSSINK (GST_PAD_PARENT (pad));

  return gst_osselement_convert (GST_OSSELEMENT (osssink),
      src_format, src_value, dest_format, dest_value);
}

static const GstQueryType *
gst_osssink_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_LATENCY,
    GST_QUERY_POSITION,
    0,
  };

  return query_types;
}

static gboolean
gst_osssink_sink_query (GstPad * pad, GstQueryType type, GstFormat * format,
    gint64 * value)
{
  gboolean res = TRUE;
  GstOssSink *osssink;

  osssink = GST_OSSSINK (GST_PAD_PARENT (pad));

  switch (type) {
    case GST_QUERY_LATENCY:
      if (!gst_osssink_convert (pad,
              GST_FORMAT_BYTES, gst_osssink_get_delay (osssink),
              format, value)) {
        res = FALSE;
      }
      break;
    case GST_QUERY_POSITION:
      if (!gst_osssink_convert (pad,
              GST_FORMAT_TIME, gst_element_get_time (GST_ELEMENT (osssink)),
              format, value)) {
        res = FALSE;
      }
      break;
    default:
      res =
          gst_pad_query (gst_pad_get_peer (osssink->sinkpad), type, format,
          value);
      break;
  }

  return res;
}

static gboolean
gst_osssink_query (GstElement * element, GstQueryType type, GstFormat * format,
    gint64 * value)
{
  GstOssSink *osssink = GST_OSSSINK (element);

  return gst_osssink_sink_query (osssink->sinkpad, type, format, value);
}

static void
gst_osssink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstOssSink *osssink;

  osssink = GST_OSSSINK (object);

  switch (prop_id) {
    case ARG_MUTE:
      osssink->mute = g_value_get_boolean (value);
      g_object_notify (G_OBJECT (osssink), "mute");
      break;
    case ARG_FRAGMENT:
      GST_OSSELEMENT (osssink)->fragment = g_value_get_int (value);
      gst_osselement_sync_parms (GST_OSSELEMENT (osssink));
      break;
    case ARG_BUFFER_SIZE:
      osssink->bufsize = g_value_get_uint (value);
      g_object_notify (object, "buffer_size");
      break;
    case ARG_SYNC:
      osssink->sync = g_value_get_boolean (value);
      g_object_notify (G_OBJECT (osssink), "sync");
      break;
    case ARG_CHUNK_SIZE:
      osssink->chunk_size = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_osssink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOssSink *osssink;

  osssink = GST_OSSSINK (object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, osssink->mute);
      break;
    case ARG_FRAGMENT:
      g_value_set_int (value, GST_OSSELEMENT (osssink)->fragment);
      break;
    case ARG_BUFFER_SIZE:
      g_value_set_uint (value, osssink->bufsize);
      break;
    case ARG_SYNC:
      g_value_set_boolean (value, osssink->sync);
      break;
    case ARG_CHUNK_SIZE:
      g_value_set_uint (value, osssink->chunk_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_osssink_change_state (GstElement * element)
{
  GstOssSink *osssink;
  GstElementStateReturn result = GST_STATE_SUCCESS;

  osssink = GST_OSSSINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      result = GST_STATE_ASYNC;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      gst_audio_clock_set_active (GST_AUDIO_CLOCK (osssink->provided_clock),
          TRUE);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      if (GST_FLAG_IS_SET (element, GST_OSSSINK_OPEN))
        ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_RESET, 0);
      gst_audio_clock_set_active (GST_AUDIO_CLOCK (osssink->provided_clock),
          FALSE);
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_OSSSINK_OPEN))
        ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_RESET, 0);
      gst_osselement_reset (GST_OSSELEMENT (osssink));
      osssink->handled = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return result;
}
