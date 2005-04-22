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
#include <stdlib.h>
#include "gstrtpL16enc.h"

/* elementfactory information */
static GstElementDetails gst_rtpL16enc_details = {
  "RTP RAW Audio Encoder",
  "Codec/Encoder/Network",
  "Encodes Raw Audio into a RTP packet",
  "Zeeshan Ali <zak147@yahoo.com>"
};

/* RtpL16Enc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  /* FILL ME */
  ARG_0,
  ARG_SAMPLE_RATE,
  ARG_PAYLOAD_TYPE,
  ARG_CHANNELS,
  ARG_MTU,
  ARG_RTPMAP
};

static GstStaticPadTemplate gst_rtpL16enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

static GstStaticPadTemplate gst_rtpL16enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static void gst_rtpL16enc_set_clock (GstElement * element, GstClock * clock);
static void gst_rtpL16enc_class_init (GstRtpL16EncClass * klass);
static void gst_rtpL16enc_base_init (GstRtpL16EncClass * klass);
static void gst_rtpL16enc_init (GstRtpL16Enc * rtpL16enc);
static void gst_rtpL16enc_chain (GstPad * pad, GstData * _data);
static void gst_rtpL16enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpL16enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstPadLinkReturn gst_rtpL16enc_sinkconnect (GstPad * pad,
    const GstCaps * caps);
static GstElementStateReturn gst_rtpL16enc_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpL16enc_get_type (void)
{
  static GType rtpL16enc_type = 0;

  if (!rtpL16enc_type) {
    static const GTypeInfo rtpL16enc_info = {
      sizeof (GstRtpL16EncClass),
      (GBaseInitFunc) gst_rtpL16enc_base_init,
      NULL,
      (GClassInitFunc) gst_rtpL16enc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpL16Enc),
      0,
      (GInstanceInitFunc) gst_rtpL16enc_init,
    };

    rtpL16enc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpL16Enc",
        &rtpL16enc_info, 0);
  }
  return rtpL16enc_type;
}

static void
gst_rtpL16enc_base_init (GstRtpL16EncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpL16enc_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpL16enc_src_template));
  gst_element_class_set_details (element_class, &gst_rtpL16enc_details);
}

static void
gst_rtpL16enc_class_init (GstRtpL16EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpL16enc_set_property;
  gobject_class->get_property = gst_rtpL16enc_get_property;

  gstelement_class->change_state = gst_rtpL16enc_change_state;
  gstelement_class->set_clock = gst_rtpL16enc_set_clock;

  g_object_class_install_property (gobject_class, ARG_SAMPLE_RATE,
      g_param_spec_uint ("sample_rate", "Sample Rate", "Sample Rate",
          1000, 48000, 44100, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_PAYLOAD_TYPE,
      g_param_spec_uint ("payload_type", "Payload Type", "Payload Type",
          0, 127, 96, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_CHANNELS,
      g_param_spec_uint ("channels", "Channels", "Channels", 1, 2, 2,
          G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_MTU,
      g_param_spec_uint ("mtu", "MTU", "max bytes in a packet", 0, G_MAXUINT,
          1460, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_RTPMAP,
      g_param_spec_string ("rtpmap", "rtpmap",
          "dynamic payload type definitions", NULL, G_PARAM_READWRITE));
}

static void
gst_rtpL16enc_init (GstRtpL16Enc * rtpL16enc)
{
  rtpL16enc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpL16enc_sink_template), "sink");
  rtpL16enc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpL16enc_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rtpL16enc), rtpL16enc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (rtpL16enc), rtpL16enc->srcpad);
  gst_pad_set_chain_function (rtpL16enc->sinkpad, gst_rtpL16enc_chain);
  gst_pad_set_link_function (rtpL16enc->sinkpad, gst_rtpL16enc_sinkconnect);

  rtpL16enc->sample_rate = 44100;
  rtpL16enc->channels = 2;
  rtpL16enc->timestamp = random ();
  rtpL16enc->seq = random ();
  rtpL16enc->ssrc = random ();
  rtpL16enc->rtpmap = NULL;
  rtpL16enc->clock = gst_system_clock_obtain ();
  rtpL16enc->payload_type = 10;
  rtpL16enc->mtu = 0;
}

static void
gst_rtpL16enc_set_clock (GstElement * element, GstClock * clock)
{
  GstRtpL16Enc *rtpL16enc = GST_RTP_L16_ENC (element);

  gst_object_replace ((GstObject **) & rtpL16enc->clock, (GstObject *) clock);
}

gboolean
get_payload_type (const gchar * rtpmap,
    guint sample_rate, guint channels, guchar * payload_type)
{
  GST_DEBUG ("r=%d c=%d", sample_rate, channels);
  if (sample_rate == 44100)
    switch (channels) {
      case 1:
        *payload_type = PAYLOAD_L16_MONO;
        GST_DEBUG ("selected payload type %d", *payload_type);
        return 1;
        break;
      case 2:
        *payload_type = PAYLOAD_L16_STEREO;
        GST_DEBUG ("selected payload type %d", *payload_type);
        return 1;
        break;
    }
  if (rtpmap) {
    gchar buf[16], *p;

    sprintf (buf, "%05d/%d", sample_rate, channels);
    p = (gchar *) strstr (rtpmap, buf);
    if (p) {
      while (*p != ':' && p > rtpmap)
        p--;
      if (*p == ':') {
        *payload_type = (guchar) strtoul (p + 1, NULL, 10);
        GST_DEBUG ("selected payload type %d", *payload_type);
        return 1;
      }
    }
  }
  return 0;
}

static GstPadLinkReturn
gst_rtpL16enc_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstRtpL16Enc *rtpL16enc;
  GstStructure *structure;
  gboolean ret;

  rtpL16enc = GST_RTP_L16_ENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "rate", &rtpL16enc->sample_rate);
  ret &= gst_structure_get_int (structure, "channels", &rtpL16enc->channels);
  ret &=
      gst_structure_get_int (structure, "endianness", &rtpL16enc->endianness);
  ret &=
      get_payload_type (rtpL16enc->rtpmap, rtpL16enc->sample_rate,
      rtpL16enc->channels, &rtpL16enc->payload_type);

  if (!ret)
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

static void
gst_rtpL16enc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstRtpL16Enc *rtpL16enc;
  GstBuffer *outbuf;
  guchar *samples;
  guint16 bytes_remaining, space_for_samples;
  guint16 rtp_fixed_header;
  GstClockTime timestamp;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  rtpL16enc = GST_RTP_L16_ENC (GST_OBJECT_PARENT (pad));

  g_return_if_fail (rtpL16enc != NULL);
  g_return_if_fail (GST_IS_RTP_L16_ENC (rtpL16enc));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
        GST_DEBUG ("discont");
        rtpL16enc->timestamp = 0;
        gst_pad_event_default (pad, event);
        return;
      default:
        gst_pad_event_default (pad, event);
        return;
    }
  }
  timestamp = GST_BUFFER_TIMESTAMP (buf);
  rtp_fixed_header = 0x8000 | rtpL16enc->payload_type;
  space_for_samples = rtpL16enc->mtu - 12;
  samples = GST_BUFFER_DATA (buf);
  bytes_remaining = GST_BUFFER_SIZE (buf);
  while (bytes_remaining > 0) {
    guchar *packet;
    guint32 this_packet_len;
    guint64 this_samples;

    outbuf = gst_buffer_new ();
    this_packet_len =
        (bytes_remaining >
        space_for_samples) ? space_for_samples : bytes_remaining;
    this_samples = this_packet_len / 2 / rtpL16enc->channels;
    /* if space_for_samples not right for a fixed number of samples, adjust! */
    this_packet_len = 2 * rtpL16enc->channels * this_samples;

    GST_BUFFER_SIZE (outbuf) = 12 + this_packet_len;
    GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
    packet = GST_BUFFER_DATA (outbuf);
    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

    ((guint16 *) packet)[0] = g_htons (rtp_fixed_header);
    ((guint16 *) packet)[1] = g_htons (rtpL16enc->seq);
    ((guint32 *) packet)[1] = g_htonl (rtpL16enc->timestamp);
    ((guint32 *) packet)[2] = g_htonl (rtpL16enc->ssrc);
    if (rtpL16enc->endianness == G_BIG_ENDIAN)
      memcpy (packet + 12, samples, this_packet_len);
    else {
      guint16 *in, *out;
      int i;

      in = (guint16 *) (samples);
      out = (guint16 *) (packet + 12);
      for (i = 0; i < this_packet_len / 2; i++)
        out[i] = g_htons (in[i]);
    }

    GST_DEBUG_OBJECT (rtpL16enc, "mtu=%ld space=%u pt=%u",
        rtpL16enc->mtu, space_for_samples, rtpL16enc->payload_type);
    GST_DEBUG_OBJECT (rtpL16enc, "seq=%u timestamp=%lu", rtpL16enc->seq,
        rtpL16enc->timestamp);

    /* wait on clock */
    gst_element_wait (GST_ELEMENT (rtpL16enc), timestamp);

    GST_DEBUG_OBJECT (rtpL16enc, "pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));
    gst_pad_push (rtpL16enc->srcpad, GST_DATA (outbuf));

    ++rtpL16enc->seq;
    rtpL16enc->timestamp += this_samples;
    bytes_remaining -= this_packet_len;
    samples += this_packet_len;
    timestamp += this_samples / rtpL16enc->sample_rate * GST_SECOND;

    /*gst_buffer_unref (outbuf); */
  }
  gst_buffer_unref (buf);
}

static void
gst_rtpL16enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpL16Enc *rtpL16enc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_L16_ENC (object));
  rtpL16enc = GST_RTP_L16_ENC (object);


  switch (prop_id) {
    case ARG_MTU:
      rtpL16enc->mtu = g_value_get_uint (value);
      break;
    case ARG_RTPMAP:
      if (rtpL16enc->rtpmap != NULL)
        g_free (rtpL16enc->rtpmap);
      if (g_value_get_string (value) == NULL)
        rtpL16enc->rtpmap = NULL;
      else
        rtpL16enc->rtpmap = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void
gst_rtpL16enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpL16Enc *rtpL16enc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_RTP_L16_ENC (object));
  rtpL16enc = GST_RTP_L16_ENC (object);

  switch (prop_id) {
    case ARG_SAMPLE_RATE:
      g_value_set_uint (value, rtpL16enc->sample_rate);
      break;
    case ARG_PAYLOAD_TYPE:
      g_value_set_uint (value, rtpL16enc->payload_type);
      break;
    case ARG_CHANNELS:
      g_value_set_uint (value, rtpL16enc->channels);
      break;
    case ARG_MTU:
      g_value_set_uint (value, rtpL16enc->mtu);
      break;
    case ARG_RTPMAP:
      g_value_set_string (value, rtpL16enc->rtpmap);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_rtpL16enc_change_state (GstElement * element)
{
  GstRtpL16Enc *rtpL16enc;

  g_return_val_if_fail (GST_IS_RTP_L16_ENC (element), GST_STATE_FAILURE);

  rtpL16enc = GST_RTP_L16_ENC (element);

  GST_DEBUG ("state pending %d\n", GST_STATE_PENDING (element));

  /* if going down into NULL state, close the file if it's open */
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
gst_rtpL16enc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpL16enc",
      GST_RANK_NONE, GST_TYPE_RTP_L16_ENC);
}
