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


#ifndef __GST_UDPSRC_H__
#define __GST_UDPSRC_H__

#include <gst/gst.h>


#include <sys/socket.h>
#include <netinet/in.h>

#include <fcntl.h>
#include "address.h"

G_BEGIN_DECLS


#define GST_TYPE_UDPSRC \
  (gst_udpsrc_get_type())
#define GST_UDPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_UDPSRC,GstUDPSrc))
#define GST_UDPSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_UDPSRC,GstUDPSrc))
#define GST_IS_UDPSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_UDPSRC))
#define GST_IS_UDPSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_UDPSRC))

typedef struct _GstUDPSrc GstUDPSrc;
typedef struct _GstUDPSrcClass GstUDPSrcClass;


enum {
  ARG_0,
  ARG_LOCAL_ADDR,
  ARG_DATA_PORT,
  ARG_CONTROL_PORT,
  ARG_AUTHORIZED_SENDERS,
  ARG_REMOTE_ADDRESSES,
};

typedef enum {
  GST_UDPSRC_OPEN             = GST_ELEMENT_FLAG_LAST,

  GST_UDPSRC_FLAG_LAST        = GST_ELEMENT_FLAG_LAST + 2,
} GstUDPSrcFlags;

// FIXME: Release memory of stored data after setting them as properties.
//        Release memory of stored data in the destructor

struct _GstUDPSrc {
  GstElement element;

  /* pads */
  GstPad* srcpad;
  GstAddress local_address;
  gint data_port, control_port;
  /* raw members have the names resolved */
  GstNumAddress raw_local_address;
  gboolean local_address_uptodate;
  int data_socket, control_socket;
  GList* remote_addresses; /* Addresses that receive our control messages. GList of GstAddrPort */
  GstNumAddressesPorts raw_remote_addresses;
  gboolean remote_address_uptodate;
  GList* authorized_senders; /* Packets from other addresses are dropped. Glist of GstAddress */
  gboolean authorized_senders_uptodate;
  GstNumAddresses raw_authorized_senders;

  GstClock *clock;

  int pipe[2];

  gboolean first_buf;
  gboolean open;
  gboolean include_addr_in_data; // FIXME Initialize
};

struct _GstUDPSrcClass {
  GstElementClass parent_class;
};

GType gst_udpsrc_get_type(void);


#define GST_EVENT_CONTROL 100

struct control_event {
  guint size;
  gpointer data; /* Actually a (struct buffer_data_extended *) buffer */
};


struct buffer_data_extended {
  guint addr_size;
  struct sockaddr_in6 addr;
  char data[1];
};

G_END_DECLS

#endif /* __GST_UDPSRC_H__ */
