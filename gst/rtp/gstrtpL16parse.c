/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more

      The RTP header has the following format:

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           timestamp                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           synchronization source (SSRC) identifier            |
   +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
   |            contributing source (CSRC) identifiers             |
   |                             ....                              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include "gstrtpL16parse.h"

/* elementfactory information */
static GstElementDetails gst_rtp_L16parse_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts raw audio from RTP packets",
  "Zeeshan Ali <zak147@yahoo.com>"
};

/* RtpL16Parse signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_CHANNELS,
  ARG_FREQUENCY,
  ARG_PAYLOAD_TYPE,
  ARG_RTPMAP
};

static GstStaticPadTemplate gst_rtpL16parse_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BIG_ENDIAN, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

static GstStaticPadTemplate gst_rtpL16parse_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static void gst_rtpL16parse_class_init (GstRtpL16ParseClass * klass);
static void gst_rtpL16parse_base_init (GstRtpL16ParseClass * klass);
static void gst_rtpL16parse_init (GstRtpL16Parse * rtpL16parse);

static void gst_rtpL16parse_chain (GstPad * pad, GstData * _data);

static void gst_rtpL16parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpL16parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_rtpL16parse_change_state (GstElement *
    element);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpL16parse_get_type (void)
{
  static GType rtpL16parse_type = 0;

  if (!rtpL16parse_type) {
    static const GTypeInfo rtpL16parse_info = {
      sizeof (GstRtpL16ParseClass),
      (GBaseInitFunc) gst_rtpL16parse_base_init,
      NULL,
      (GClassInitFunc) gst_rtpL16parse_class_init,
      NULL,
      NULL,
      sizeof (GstRtpL16Parse),
      0,
      (GInstanceInitFunc) gst_rtpL16parse_init,
    };

    rtpL16parse_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpL16Parse",
        &rtpL16parse_info, 0);
  }
  return rtpL16parse_type;
}

static void
gst_rtpL16parse_base_init (GstRtpL16ParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpL16parse_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpL16parse_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_L16parse_details);
}

static void
gst_rtpL16parse_class_init (GstRtpL16ParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_CHANNELS,
      g_param_spec_uint ("channels", "Channels", "Channels", 1, 2, 2,
          G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PAYLOAD_TYPE,
      g_param_spec_int ("payload_type", "payload_type", "payload type",
          G_MININT, G_MAXINT, PAYLOAD_L16_STEREO, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREQUENCY,
      g_param_spec_int ("frequency", "frequency", "frequency", G_MININT,
          G_MAXINT, 44100, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_RTPMAP,
      g_param_spec_string ("rtpmap", "rtpmap",
          "dynamic payload type definitions", NULL, G_PARAM_READWRITE));

  gobject_class->set_property = gst_rtpL16parse_set_property;
  gobject_class->get_property = gst_rtpL16parse_get_property;

  gstelement_class->change_state = gst_rtpL16parse_change_state;
}

static void
gst_rtpL16parse_init (GstRtpL16Parse * rtpL16parse)
{
  rtpL16parse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpL16parse_src_template), "src");
  rtpL16parse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpL16parse_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (rtpL16parse), rtpL16parse->srcpad);
  gst_element_add_pad (GST_ELEMENT (rtpL16parse), rtpL16parse->sinkpad);
  gst_pad_set_chain_function (rtpL16parse->sinkpad, gst_rtpL16parse_chain);

  rtpL16parse->frequency = 44100;
  rtpL16parse->channels = 2;

  rtpL16parse->payload_type = PAYLOAD_L16_STEREO;

  rtpL16parse->rtpmap = NULL;
  rtpL16parse->initial_timestamp = 0;
  rtpL16parse->initialised = 0;
}

void
gst_rtpL16parse_ntohs (GstBuffer * buf)
{
  gint16 *i, *len;

  /* FIXME: is this code correct or even sane at all? */
  i = (gint16 *) GST_BUFFER_DATA (buf);
  len = i + GST_BUFFER_SIZE (buf) / sizeof (gint16 *);

  for (; i < len; i++) {
    *i = g_ntohs (*i);
  }
}

void
gst_rtpL16_caps_nego (GstRtpL16Parse * rtpL16parse)
{
  GstCaps *caps;

  caps =
      gst_caps_copy (gst_static_caps_get (&gst_rtpL16parse_src_template.
          static_caps));

  gst_caps_set_simple (caps,
      "rate", G_TYPE_INT, rtpL16parse->frequency,
      "channels", G_TYPE_INT, rtpL16parse->channels, NULL);

  gst_pad_try_set_caps (rtpL16parse->srcpad, caps);
}

void
gst_rtpL16parse_payloadtype_change (GstRtpL16Parse * rtpL16parse,
    rtp_payload_t pt)
{
  rtpL16parse->payload_type = pt;

  switch (pt) {
    case PAYLOAD_L16_MONO:
      rtpL16parse->channels = 1;
      rtpL16parse->frequency = 44100;
      break;
    case PAYLOAD_L16_STEREO:
      rtpL16parse->channels = 2;
      rtpL16parse->frequency = 44100;
      break;
    default:
      if (rtpL16parse->rtpmap) {
        gchar m[32];

        sprintf (m, ":%u L16/%%u/%%u", pt);
        GST_DEBUG ("searching [%s] for [%s]", rtpL16parse->rtpmap, m);
        if (sscanf (rtpL16parse->rtpmap, m, &rtpL16parse->frequency,
                &rtpL16parse->channels) == 2) {
          GST_DEBUG ("pt %u mapped to L16/%u/%u", rtpL16parse->frequency,
              rtpL16parse->channels);
        } else
          g_warning ("unknown payload_t %d\n", pt);
      } else
        g_warning ("unknown payload_t %d\n", pt);
  }

  gst_rtpL16_caps_nego (rtpL16parse);
}


static void
gst_rtpL16parse_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstRtpL16Parse *rtpL16parse;
  GstBuffer *outbuf;
  rtp_payload_t pt;
  gint version, padding, extension, csrc_count, marker;
  guint16 seq;
  guint32 timestamp, ssrc, sample_bytes, sample_count;
  guchar *packet = GST_BUFFER_DATA (buf);
  gint16 *samples;
  guint64 temp;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  rtpL16parse = GST_RTP_L16_PARSE (GST_OBJECT_PARENT (pad));

  g_return_if_fail (rtpL16parse != NULL);
  g_return_if_fail (GST_IS_RTP_L16_PARSE (rtpL16parse));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    gst_pad_event_default (pad, event);

    return;
  }

  if (GST_PAD_CAPS (rtpL16parse->srcpad) == NULL) {
    gst_rtpL16_caps_nego (rtpL16parse);
  }

  version = (packet[0] & 0xC0) >> 6;
  padding = (packet[0] & 0x20) >> 5;
  extension = (packet[0] & 0x10) >> 4;
  csrc_count = packet[0] & 0x0F;
  marker = (packet[1] & 0x80) >> 7;
  pt = packet[1] & 0x7F;
  seq = ((guint16) packet[2] << 8) + packet[3];
  timestamp = (((((((guint16) packet[4]
                      ) << 8)
                  + (guint16) packet[5]
              ) << 8)
          + (guint16) packet[6]
      ) << 8) + packet[7];
  ssrc = (((((((guint16) packet[8]
                      ) << 8)
                  + (guint16) packet[9]
              ) << 8)
          + (guint16) packet[10]
      ) << 8) + packet[11];
  samples = (gint16 *) & packet[12];    /* starting guess */
  sample_bytes = GST_BUFFER_SIZE (buf) - 12;    /* starting guess */
  GST_DEBUG_OBJECT (rtpL16parse,
      "rtp version=%u pt=%u, seq=%u timestamp=%lu ssrc=%lx", version, pt, seq,
      timestamp, ssrc);
  if (csrc_count > 0) {
    /* FIXME we will ignore them for now - should be part of metadata for
     * making pure RTP mixers
     */
    samples += 2 * csrc_count;  /* csrcs are 32 bits, samples are 16 */
    sample_bytes -= 4 * csrc_count;
  }
  if (extension) {
    guint16 extension_length = (guint16) samples[1];

    samples += 2 + 2 * extension_length;
    sample_bytes -= 4 + 4 * extension_length;
  }
  if (padding) {
    guchar padding_count = packet[GST_BUFFER_SIZE (buf) - 1];

    sample_bytes -= padding_count;
  }
  sample_count = sample_bytes / 2;

  if (pt != rtpL16parse->payload_type) {
    gst_rtpL16parse_payloadtype_change (rtpL16parse, pt);
  }

  if (rtpL16parse->initialised == 0) {
    rtpL16parse->initialised = 1;
    rtpL16parse->initial_timestamp = timestamp;
  }

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = sample_bytes;
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));

  /* FIXME gst timestamp should be derived from RTCP */
  /* for now, or if RTCP is not sent, the RTP timestamp is a random
   * number to start with and then indicates the number of samples
   * since the start of the stream. 
   */

  temp = GST_SECOND * (timestamp - rtpL16parse->initial_timestamp);
  GST_DEBUG_OBJECT (rtpL16parse, "timestamp*samplerate=%llu", temp);
  GST_BUFFER_TIMESTAMP (outbuf) = temp / rtpL16parse->frequency;

  memcpy (GST_BUFFER_DATA (outbuf), samples, GST_BUFFER_SIZE (outbuf));

  GST_DEBUG ("gst_rtpL16parse_chain: pushing buffer of size %d",
      GST_BUFFER_SIZE (outbuf));

  gst_pad_push (rtpL16parse->srcpad, GST_DATA (outbuf));

  gst_buffer_unref (buf);
}

static void
gst_rtpL16parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpL16Parse *rtpL16parse;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_L16_PARSE (object));
  rtpL16parse = GST_RTP_L16_PARSE (object);

  switch (prop_id) {
    case ARG_RTPMAP:
      if (rtpL16parse->rtpmap != NULL)
        g_free (rtpL16parse->rtpmap);
      if (g_value_get_string (value) == NULL)
        rtpL16parse->rtpmap = NULL;
      else
        rtpL16parse->rtpmap = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void
gst_rtpL16parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpL16Parse *rtpL16parse;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_L16_PARSE (object));
  rtpL16parse = GST_RTP_L16_PARSE (object);

  switch (prop_id) {
    case ARG_PAYLOAD_TYPE:
      g_value_set_int (value, rtpL16parse->payload_type);
      break;
    case ARG_FREQUENCY:
      g_value_set_int (value, rtpL16parse->frequency);
      break;
    case ARG_CHANNELS:
      g_value_set_uint (value, rtpL16parse->channels);
      break;
    case ARG_RTPMAP:
      g_value_set_string (value, rtpL16parse->rtpmap);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_rtpL16parse_change_state (GstElement * element)
{
  GstRtpL16Parse *rtpL16parse;

  g_return_val_if_fail (GST_IS_RTP_L16_PARSE (element), GST_STATE_FAILURE);

  rtpL16parse = GST_RTP_L16_PARSE (element);

  GST_DEBUG ("state pending %d\n", GST_STATE_PENDING (element));

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean
gst_rtpL16parse_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpL16parse",
      GST_RANK_NONE, GST_TYPE_RTP_L16_PARSE);
}
