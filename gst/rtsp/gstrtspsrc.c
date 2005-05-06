/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#include <unistd.h>
#include <string.h>

#include "gstrtspsrc.h"
#include "sdp.h"

/* elementfactory information */
static GstElementDetails gst_rtspsrc_details =
GST_ELEMENT_DETAILS ("RTSP packet receiver",
    "Source/Network",
    "Receive data over the network via RTSP",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION,
  /* FILL ME */
};

static void gst_rtspsrc_base_init (gpointer g_class);
static void gst_rtspsrc_class_init (GstRTSPSrc * klass);
static void gst_rtspsrc_init (GstRTSPSrc * rtspsrc);

static GstElementStateReturn gst_rtspsrc_change_state (GstElement * element);

static void gst_rtspsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtspsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstData *gst_rtspsrc_get (GstPad * pad);

static GstElementClass *parent_class = NULL;

/*static guint gst_rtspsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_rtspsrc_get_type (void)
{
  static GType rtspsrc_type = 0;

  if (!rtspsrc_type) {
    static const GTypeInfo rtspsrc_info = {
      sizeof (GstRTSPSrcClass),
      gst_rtspsrc_base_init,
      NULL,
      (GClassInitFunc) gst_rtspsrc_class_init,
      NULL,
      NULL,
      sizeof (GstRTSPSrc),
      0,
      (GInstanceInitFunc) gst_rtspsrc_init,
      NULL
    };

    rtspsrc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRTSPSrc", &rtspsrc_info,
        0);
  }
  return rtspsrc_type;
}

static void
gst_rtspsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_rtspsrc_details);
}

static void
gst_rtspsrc_class_init (GstRTSPSrc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtspsrc_set_property;
  gobject_class->get_property = gst_rtspsrc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOCATION,
      g_param_spec_string ("location", "RTSP Location",
          "Location of the RTSP url to read", NULL, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_rtspsrc_change_state;
}

static void
gst_rtspsrc_init (GstRTSPSrc * src)
{
  src->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&srctemplate),
      "src");
  gst_pad_set_get_function (src->srcpad, gst_rtspsrc_get);
  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);

  src->location = NULL;
}

static void
gst_rtspsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_free (rtspsrc->location);
      rtspsrc->location = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtspsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, rtspsrc->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstData *
gst_rtspsrc_get (GstPad * pad)
{
  RTSPMessage response = { 0 };
  RTSPResult res;
  GstBuffer *buf;
  GstRTSPSrc *src;
  guint8 *data;
  gint size;

  src = GST_RTSPSRC (GST_PAD_PARENT (pad));

retry:
  do {
    if ((res = rtsp_connection_receive (src->connection, &response)) < 0)
      goto receive_error;
  }
  while (response.type != RTSP_MESSAGE_DATA);

  rtsp_message_get_body (&response, &data, &size);

  /* no RTSP packets here */
  if (data[1] >= 200 && data[1] <= 204)
    goto retry;

  /* HACK only mpeg audio packets here */
  if (data[1] != 14)
    goto retry;

  /* HACK, strip off RTP 12-byte header and mpeg audio 4-byte header */
  size -= 16;
  data += 16;

  buf = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buf), data, size);

  return GST_DATA (buf);

receive_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not receive message."), (NULL));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }
}

static gboolean
gst_rtspsrc_send (GstRTSPSrc * src, RTSPMessage * request,
    RTSPMessage * response)
{
  RTSPResult res;

  if ((res = rtsp_connection_send (src->connection, request)) < 0)
    goto send_error;

  if ((res = rtsp_connection_receive (src->connection, response)) < 0)
    goto receive_error;
  if (response->type_data.response.code != RTSP_STS_OK)
    goto error_response;

  return TRUE;

send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
receive_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        ("Could not receive message."), (NULL));
    return FALSE;
  }
error_response:
  {
    rtsp_message_dump (request);
    rtsp_message_dump (response);
    GST_ELEMENT_ERROR (src, RESOURCE, READ, ("Got error response."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_open (GstRTSPSrc * src)
{
  RTSPUrl *url;
  RTSPResult res;
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  guint8 *data;
  guint size;
  SDPMessage sdp = { 0 };

  /* parse url */
  GST_DEBUG ("parsing url...");
  if ((res = rtsp_url_parse (src->location, &url)) < 0)
    goto invalid_url;

  /* open connection */
  GST_DEBUG ("opening connection...");
  if ((res = rtsp_connection_open (url, &src->connection)) < 0)
    goto could_not_open;

  /* create DESCRIBE */
  GST_DEBUG ("create describe...");
  if ((res =
          rtsp_message_init_request (RTSP_DESCRIBE, src->location,
              &request)) < 0)
    goto create_request_failed;
  /* we accept SDP for now */
  rtsp_message_add_header (&request, RTSP_HDR_ACCEPT, "application/sdp");

  /* send DESCRIBE */
  GST_DEBUG ("send describe...");
  if (!gst_rtspsrc_send (src, &request, &response))
    goto send_error;

  /* parse SDP */
  rtsp_message_get_body (&response, &data, &size);

  GST_DEBUG ("parse sdp...");
  sdp_message_init (&sdp);
  sdp_message_parse_buffer (data, size, &sdp);

  /* setup streams */
  {
    gint i;

    for (i = 0; i < sdp_message_medias_len (&sdp); i++) {
      SDPMedia *media;
      gchar *setup_url;
      gchar *control_url;

      media = sdp_message_get_media (&sdp, i);

      GST_DEBUG ("setup media %d", i);
      control_url = sdp_media_get_attribute_val (media, "control");
      if (control_url == NULL) {
        GST_DEBUG ("no control url found, skipping stream");
        continue;
      }

      /* FIXME, check absolute/relative URL */
      setup_url = g_strdup_printf ("%s/%s", src->location, control_url);

      GST_DEBUG ("setup %s", setup_url);
      /* create SETUP request */
      if ((res =
              rtsp_message_init_request (RTSP_SETUP, setup_url,
                  &request)) < 0) {
        g_free (setup_url);
        goto create_request_failed;
      }
      g_free (setup_url);

      /* select transport */
      rtsp_message_add_header (&request, RTSP_HDR_TRANSPORT,
          //"RTP/AVP/UDP;unicast;client_port=5000-5001,RTP/AVP/UDP;multicast,RTP/AVP/TCP");
          "RTP/AVP/TCP");

      if (!gst_rtspsrc_send (src, &request, &response))
        goto send_error;
    }
  }

  return TRUE;

invalid_url:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("Not a valid RTSP url."), (NULL));
    return FALSE;
  }
could_not_open:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE,
        ("Could not open connection."), (NULL));
    return FALSE;
  }
create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_close (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  /* do TEARDOWN */
  if ((res =
          rtsp_message_init_request (RTSP_TEARDOWN, src->location,
              &request)) < 0)
    goto create_request_failed;

  if (!gst_rtspsrc_send (src, &request, &response))
    goto send_error;

  /* close connection */
  GST_DEBUG ("closing connection...");
  if ((res = rtsp_connection_close (src->connection)) < 0)
    goto close_failed;

  return TRUE;

create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
close_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, CLOSE, ("Close failed."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_play (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  /* do play */
  if ((res =
          rtsp_message_init_request (RTSP_PLAY, src->location, &request)) < 0)
    goto create_request_failed;

  if (!gst_rtspsrc_send (src, &request, &response))
    goto send_error;

  return TRUE;

create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_pause (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  /* do play */
  if ((res =
          rtsp_message_init_request (RTSP_PAUSE, src->location, &request)) < 0)
    goto create_request_failed;

  if (!gst_rtspsrc_send (src, &request, &response))
    goto send_error;

  return TRUE;

create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
}

static GstElementStateReturn
gst_rtspsrc_change_state (GstElement * element)
{
  GstRTSPSrc *rtspsrc;
  GstElementState transition;
  GstElementStateReturn ret;

  rtspsrc = GST_RTSPSRC (element);

  transition = GST_STATE_TRANSITION (rtspsrc);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      gst_rtspsrc_open (rtspsrc);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      gst_rtspsrc_play (rtspsrc);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      gst_rtspsrc_pause (rtspsrc);
      break;
    case GST_STATE_READY_TO_NULL:
      gst_rtspsrc_close (rtspsrc);
      break;
    default:
      break;
  }

  return ret;
}
