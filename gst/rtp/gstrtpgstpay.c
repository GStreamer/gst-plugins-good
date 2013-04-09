/* GStreamer
 * Copyright (C) <2010> Wim Taymans <wim.taymans@gmail.com>
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
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpgstpay.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_pay_debug);
#define GST_CAT_DEFAULT gst_rtp_pay_debug

/*
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |C| CV  |D|X|Y|Z|     ETYPE     |  MBZ                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Frag_offset                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * C: caps inlined flag
 *   When C set, first part of payload contains caps definition. Caps definition
 *   starts with variable-length length prefix and then a string of that length.
 *   the length is encoded in big endian 7 bit chunks, the top 1 bit of a byte
 *   is the continuation marker and the 7 next bits the data. A continuation
 *   marker of 1 means that the next byte contains more data.
 *
 * CV: caps version, 0 = caps from SDP, 1 - 7 inlined caps
 * D: delta unit buffer
 * X: media 1 flag
 * Y: media 2 flag
 * Z: media 3 flag
 * ETYPE: type of event. Payload contains the event, prefixed with a
 *        variable length field.
 *   0 = NO event
 *   1 = GST_EVENT_TAG
 *   2 = GST_EVENT_CUSTOM_DOWNSTREAM
 *   3 = GST_EVENT_CUSTOM_BOTH
 */

#define DEFAULT_BUFFER_LIST     FALSE
enum
{
  PROP_0,
  PROP_BUFFER_LIST,
  PROP_LAST
};

static GstStaticPadTemplate gst_rtp_gst_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rtp_gst_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"application\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"X-GST\"")
    );

static void gst_rtp_gst_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_gst_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_rtp_gst_pay_finalize (GObject * obj);

static GstStateChangeReturn
gst_rtp_gst_pay_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_rtp_gst_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_gst_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);
static gboolean gst_rtp_gst_pay_handle_event (GstPad * pad, GstEvent * event);

GST_BOILERPLATE (GstRtpGSTPay, gst_rtp_gst_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD)

     static void gst_rtp_gst_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_gst_pay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_gst_pay_sink_template);

  gst_element_class_set_details_simple (element_class,
      "RTP GStreamer payloader", "Codec/Payloader/Network/RTP",
      "Payload GStreamer buffers as RTP packets",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_gst_pay_class_init (GstRtpGSTPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->set_property = gst_rtp_gst_pay_set_property;
  gobject_class->get_property = gst_rtp_gst_pay_get_property;
  gobject_class->finalize = gst_rtp_gst_pay_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_LIST,
      g_param_spec_boolean ("buffer-list", "Buffer List",
          "Use Buffer Lists",
          DEFAULT_BUFFER_LIST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_rtp_gst_pay_change_state;

  gstbasertppayload_class->set_caps = gst_rtp_gst_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_gst_pay_handle_buffer;
  gstbasertppayload_class->handle_event = gst_rtp_gst_pay_handle_event;

  GST_DEBUG_CATEGORY_INIT (gst_rtp_pay_debug, "rtpgstpay", 0,
      "rtpgstpay element");
}

static void
gst_rtp_gst_pay_init (GstRtpGSTPay * rtpgstpay, GstRtpGSTPayClass * klass)
{
  rtpgstpay->buffer_list = DEFAULT_BUFFER_LIST;
  rtpgstpay->adapter = gst_adapter_new ();
  gst_basertppayload_set_options (GST_BASE_RTP_PAYLOAD (rtpgstpay),
      "application", TRUE, "X-GST", 90000);
}

static void
gst_rtp_gst_pay_reset (GstRtpGSTPay * rtpgstpay)
{
  g_list_foreach (rtpgstpay->events, (GFunc) gst_event_unref, NULL);
  g_list_free (rtpgstpay->events);
  rtpgstpay->events = NULL;
  rtpgstpay->have_caps = FALSE;
}

static void
gst_rtp_gst_pay_finalize (GObject * obj)
{
  GstRtpGSTPay *rtpgstpay;

  rtpgstpay = GST_RTP_GST_PAY (obj);

  gst_rtp_gst_pay_reset (rtpgstpay);
  g_object_unref (rtpgstpay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_rtp_gst_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpGSTPay *rtpgstpay;

  rtpgstpay = GST_RTP_GST_PAY (object);

  switch (prop_id) {
    case PROP_BUFFER_LIST:
      rtpgstpay->buffer_list = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_gst_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpGSTPay *rtpgstpay;

  rtpgstpay = GST_RTP_GST_PAY (object);

  switch (prop_id) {
    case PROP_BUFFER_LIST:
      g_value_set_boolean (value, rtpgstpay->buffer_list);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_rtp_gst_pay_flush (GstRtpGSTPay * rtpgstpay, GstClockTime timestamp)
{
  GstFlowReturn ret;
  guint avail;
  guint frag_offset;
  GstBufferList *list = NULL;
  GstBufferListIterator *it = NULL;

  frag_offset = 0;
  avail = gst_adapter_available (rtpgstpay->adapter);

  if (rtpgstpay->buffer_list) {
    list = gst_buffer_list_new ();
    it = gst_buffer_list_iterate (list);
  }

  while (avail) {
    guint towrite;
    guint8 *payload;
    guint payload_len;
    guint packet_len;
    GstBuffer *outbuf;

    /* this will be the total lenght of the packet */
    packet_len = gst_rtp_buffer_calc_packet_len (8 + avail, 0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, GST_BASE_RTP_PAYLOAD_MTU (rtpgstpay));

    /* this is the payload length */
    payload_len = gst_rtp_buffer_calc_payload_len (towrite, 0, 0);

    if (rtpgstpay->buffer_list) {
      outbuf = gst_rtp_buffer_new_allocate (8, 0, 0);
    } else {
      /* create buffer to hold the payload */
      outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);
    }
    payload = gst_rtp_buffer_get_payload (outbuf);

    GST_DEBUG_OBJECT (rtpgstpay, "new packet len %u, frag %u", packet_len,
        frag_offset);

    /*
     *  0                   1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |C| CV  |D|X|Y|Z|     ETYPE     |  MBZ                          |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |                          Frag_offset                          |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    payload[0] = rtpgstpay->flags;
    payload[1] = rtpgstpay->etype;
    payload[2] = payload[3] = 0;
    payload[4] = frag_offset >> 24;
    payload[5] = frag_offset >> 16;
    payload[6] = frag_offset >> 8;
    payload[7] = frag_offset & 0xff;

    payload += 8;
    payload_len -= 8;

    frag_offset += payload_len;
    avail -= payload_len;

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

    if (avail == 0)
      gst_rtp_buffer_set_marker (outbuf, TRUE);

    if (rtpgstpay->buffer_list) {
      GstBuffer *paybuf;

      /* create a new buf to hold the payload */
      GST_DEBUG_OBJECT (rtpgstpay, "take %u bytes from adapter", payload_len);
      paybuf = gst_adapter_take_buffer (rtpgstpay->adapter, payload_len);

      /* create a new group to hold the rtp header and the payload */
      gst_buffer_list_iterator_add_group (it);
      gst_buffer_list_iterator_add (it, outbuf);
      gst_buffer_list_iterator_add (it, paybuf);
    } else {
      GST_DEBUG_OBJECT (rtpgstpay, "copy %u bytes from adapter", payload_len);
      gst_adapter_copy (rtpgstpay->adapter, payload, 0, payload_len);
      gst_adapter_flush (rtpgstpay->adapter, payload_len);

      ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpgstpay), outbuf);
      if (ret != GST_FLOW_OK)
        goto push_failed;
    }
  }
  if (rtpgstpay->buffer_list) {
    gst_buffer_list_iterator_free (it);
    /* push the whole buffer list at once */
    ret = gst_basertppayload_push_list (GST_BASE_RTP_PAYLOAD (rtpgstpay), list);
  }

  rtpgstpay->flags &= 0x70;
  rtpgstpay->etype = 0;

  return GST_FLOW_OK;

  /* ERRORS */
push_failed:
  {
    GST_DEBUG_OBJECT (rtpgstpay, "push failed %d (%s)", ret,
        gst_flow_get_name (ret));
    gst_adapter_clear (rtpgstpay->adapter);
    rtpgstpay->flags &= 0x70;
    rtpgstpay->etype = 0;
    return ret;
  }
}

static GstBuffer *
make_data_buffer (GstRtpGSTPay * rtpgstpay, gchar * data, guint size)
{
  guint plen;
  guint8 *ptr;
  GstBuffer *outbuf;

  /* calculate length */
  plen = 1;
  while (size >> (7 * plen))
    plen++;

  outbuf = gst_buffer_new_and_alloc (plen + size);
  ptr = GST_BUFFER_DATA (outbuf);

  /* write length */
  while (plen) {
    plen--;
    *ptr++ = ((plen > 0) ? 0x80 : 0) | ((size >> (7 * plen)) & 0x7f);
  }
  /* copy data */
  memcpy (ptr, data, size);

  return outbuf;
}

static void
process_event (GstRtpGSTPay * rtpgstpay, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      rtpgstpay->etype = 1;
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      rtpgstpay->etype = 2;
      break;
    case GST_EVENT_CUSTOM_BOTH:
      rtpgstpay->etype = 2;
      break;
    default:
      break;
  }
  if (rtpgstpay->etype) {
    const GstStructure *s;
    gchar *estr;
    guint elen;
    GstBuffer *outbuf;

    GST_DEBUG_OBJECT (rtpgstpay, "make event type %d", rtpgstpay->etype);
    s = gst_event_get_structure (event);

    estr = gst_structure_to_string (s);
    elen = strlen (estr);
    outbuf = make_data_buffer (rtpgstpay, estr, elen);
    g_free (estr);

    gst_adapter_push (rtpgstpay->adapter, outbuf);
    /* flush the adapter immediately */
    gst_rtp_gst_pay_flush (rtpgstpay, GST_CLOCK_TIME_NONE);
  }
}


static gboolean
gst_rtp_gst_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  GstRtpGSTPay *rtpgstpay;
  gboolean res;
  gchar *capsstr, *capsenc, *capsver;
  guint capslen;
  GstBuffer *outbuf;
  GList *walk;

  rtpgstpay = GST_RTP_GST_PAY (payload);

  capsstr = gst_caps_to_string (caps);
  capslen = strlen (capsstr);

  rtpgstpay->current_CV = rtpgstpay->next_CV;

  /* encode without 0 byte */
  capsenc = g_base64_encode ((guchar *) capsstr, capslen);
  GST_DEBUG_OBJECT (payload, "caps=%s, caps(base64)=%s", capsstr, capsenc);
  /* for 0 byte */
  capslen++;

  /* make caps for SDP */
  capsver = g_strdup_printf ("%d", rtpgstpay->current_CV);
  res =
      gst_basertppayload_set_outcaps (payload, "caps", G_TYPE_STRING, capsenc,
      "capsversion", G_TYPE_STRING, capsver, NULL);

  rtpgstpay->have_caps = TRUE;

  for (walk = rtpgstpay->events; walk; walk = g_list_next (walk)) {
    GstEvent *event = walk->data;

    process_event (rtpgstpay, event);
    gst_event_unref (event);
  }
  g_list_free (rtpgstpay->events);
  rtpgstpay->events = NULL;

  /* make a data buffer of it */
  outbuf = make_data_buffer (rtpgstpay, capsstr, capslen);
  g_free (capsstr);

  /* store in adapter, we don't flush yet, buffer will follow */
  rtpgstpay->flags = (1 << 7) | (rtpgstpay->current_CV << 4);
  rtpgstpay->next_CV = (rtpgstpay->next_CV + 1) & 0x7;
  gst_adapter_push (rtpgstpay->adapter, outbuf);

  g_free (capsenc);
  g_free (capsver);

  return res;
}

static gboolean
gst_rtp_gst_pay_handle_event (GstPad * pad, GstEvent * event)
{
  GstRtpGSTPay *rtpgstpay;

  rtpgstpay = GST_RTP_GST_PAY (GST_PAD_PARENT (pad));

  if (!rtpgstpay->have_caps) {
    rtpgstpay->events =
        g_list_append (rtpgstpay->events, gst_event_ref (event));
  } else {
    process_event (rtpgstpay, event);
  }
  /* FALSE to let base class handle it as well */
  return FALSE;
}

static GstFlowReturn
gst_rtp_gst_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstFlowReturn ret;
  GstRtpGSTPay *rtpgstpay;
  GstClockTime timestamp;

  rtpgstpay = GST_RTP_GST_PAY (basepayload);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* caps always from SDP for now */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT))
    rtpgstpay->flags |= (1 << 3);
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_MEDIA1))
    rtpgstpay->flags |= (1 << 2);
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_MEDIA2))
    rtpgstpay->flags |= (1 << 1);
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_MEDIA3))
    rtpgstpay->flags |= (1 << 0);

  gst_adapter_push (rtpgstpay->adapter, buffer);
  ret = gst_rtp_gst_pay_flush (rtpgstpay, timestamp);

  return ret;
}

static GstStateChangeReturn
gst_rtp_gst_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpGSTPay *rtpgstpay;
  GstStateChangeReturn ret;

  rtpgstpay = GST_RTP_GST_PAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtp_gst_pay_reset (rtpgstpay);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_gst_pay_reset (rtpgstpay);
      break;
    default:
      break;
  }
  return ret;
}


gboolean
gst_rtp_gst_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgstpay",
      GST_RANK_NONE, GST_TYPE_RTP_GST_PAY);
}
