/* GStreamer
 * UDPSRC element. Retreives data from the network by UDP protocol
 * Copyright (C) 2004 Ramón García Fernández
 * Copyright (C) 1999 Eric Walthinsen
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

#include "gstudpsrc.h"
#include <unistd.h>
#include <sys/poll.h>
#include <sys/ioctl.h>


/* elementfactory information */
static GstElementDetails gst_udpsrc_details = {
  "UDP packet receiver",
  "Source/Network",
  "LGPL",
  "Receive data over the network via UDP",
  0,
  "Ramon Garcia <ramon_garcia_f@yahoo.com>, Wim Taymans <wim.taymans@chello.be>"
  "(c) 2000-2004",
};

/* UDPSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};


static void		gst_udpsrc_base_init		(gpointer g_class);
static void		gst_udpsrc_class_init		(GstUDPSrc *klass);
static void		gst_udpsrc_init			(GstUDPSrc *udpsrc);

static GstData*		gst_udpsrc_get			(GstPad *pad);

static gboolean		gst_udpsrc_event_handler	(GstPad* element, GstEvent* event);

static GstElementStateReturn  gst_udpsrc_change_state(GstElement *element);

static void 		gst_udpsrc_set_property 	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void 		gst_udpsrc_get_property 	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);
static void 		gst_udpsrc_set_clock 		(GstElement *element, GstClock *clock);

static gboolean		gst_udpsrc_release_locks	(GstElement* element);

typedef enum {control, data, interrupt} source_type;

static void		wait_for_data			(const GstUDPSrc* udpsrc, guint* data_size, char** data, source_type* source_type, struct sockaddr_in6* peer);

static gboolean		check_authorized_peer		(const GstUDPSrc* udpsrc, const struct sockaddr_in6* peer);

static GstBuffer*
data_buffer(const GstUDPSrc* udpsrc, guint data_size, const char* data_received, struct sockaddr_in6* addr);

static GstEvent*
control_event(const GstUDPSrc* udpsrc, guint data_size, const char* data_received, const struct sockaddr_in6* addr);

static void update_raw_addresses(GstUDPSrc* element);

static void put_data_addr(const GstUDPSrc* udpsrc, char** result_data, guint* result_size, guint data_size, char* data);


static void
send_control_data(guint size, gpointer data);


static GstElementClass *parent_class = NULL;
/*static guint gst_udpsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_udpsrc_get_type (void)
{
  static GType udpsrc_type = 0;

  if (!udpsrc_type) {
    static const GTypeInfo udpsrc_info = {
      sizeof(GstUDPSrcClass),
      gst_udpsrc_base_init,
      NULL,
      (GClassInitFunc)gst_udpsrc_class_init,
      NULL,
      NULL,
      sizeof(GstUDPSrc),
      0,
      (GInstanceInitFunc)gst_udpsrc_init,
      NULL
    };
    udpsrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstUDPSrc", &udpsrc_info, 0);
  }
  return udpsrc_type;
}

static void
gst_udpsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_udpsrc_details);
}

static void
gst_udpsrc_class_init (GstUDPSrc *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOCAL_ADDR,
    g_param_spec_boxed ("local-addr", "local-addr", 
      "Address that this element will use for listenting", 
      GST_TYPE_ADDRESS, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DATA_PORT,
    g_param_spec_int ("data-port", "data-port", "The port to receive media content",
      0, 65535, -1, G_PARAM_READWRITE) );
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CONTROL_PORT,
    g_param_spec_int ("control-port", "control-port", 
      "The port to send and receive control messages",
      0, 65535, -1, G_PARAM_READWRITE) );
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_AUTHORIZED_SENDERS,
    g_param_spec_boxed ("authorized-senders", "authorized-senders", 
      "List of addresses of hosts, such that packets from these hosts are not rejected",
      GST_TYPE_ADDRESS_LIST, G_PARAM_READWRITE) );
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_REMOTE_ADDRESSES,
    g_param_spec_boxed ("remote-receivers", "remote-receivers",
      "List of addresses of hosts that we will send control messages to",
      GST_TYPE_ADDR_PORT_LIST, G_PARAM_READWRITE) );
  gobject_class->set_property = gst_udpsrc_set_property;
  gobject_class->get_property = gst_udpsrc_get_property;
  gstelement_class->change_state = gst_udpsrc_change_state;
  gstelement_class->set_clock = gst_udpsrc_set_clock;
  gstelement_class->release_locks = gst_udpsrc_release_locks;
}

static void
gst_udpsrc_set_clock (GstElement *element, GstClock *clock)
{
  GstUDPSrc *udpsrc;
	      
  udpsrc = GST_UDPSRC (element);

  udpsrc->clock = clock;
}

static void
gst_udpsrc_init (GstUDPSrc *udpsrc)
{
  /* create the src and src pads */
  udpsrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  GstRealPad* realsrcpad = (GstRealPad*) udpsrc->srcpad;
  realsrcpad->eventfunc = gst_udpsrc_event_handler;
  gst_element_add_pad (GST_ELEMENT (udpsrc), udpsrc->srcpad);
  gst_pad_set_get_function (udpsrc->srcpad, gst_udpsrc_get);

  udpsrc->local_address = (GstAddress) 
      {.type = IPV4, .addr.ipv4_address.s_addr = 0};
  udpsrc->data_port = -1;
  udpsrc->control_port = -1;
  udpsrc->remote_addresses = NULL;
  udpsrc->authorized_senders = NULL;
  udpsrc->clock = NULL;
  udpsrc->data_socket = -1;
  udpsrc->control_socket = -1;
  udpsrc->first_buf = TRUE;
}

static GstData*
gst_udpsrc_get(GstPad *pad)
{
  GstUDPSrc* udpsrc;
  GstBuffer* output;
  source_type source_type;
  char* data_received;
  guint data_size;
  struct sockaddr_in6 peer;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  udpsrc = GST_UDPSRC (GST_OBJECT_PARENT (pad));

  g_return_val_if_fail (udpsrc->data_socket != 0 && 
			udpsrc->control_socket != 0, NULL);

  while (1) {
    wait_for_data(udpsrc, &data_size, &data_received, &source_type, &peer);
    if (source_type == interrupt) {
      return NULL;
    }
    if (!check_authorized_peer(udpsrc, &peer)) {
      g_free(data_received);
      continue;
    }
  }

  if (udpsrc->first_buf) {
    deliver_timer_discont_event(udpsrc);
  }

  if (source_type == control) {
    return (GstData*) control_event(udpsrc, data_size, data_received, &peer);
  } else {
    return (GstData*) data_buffer(udpsrc, data_size, data_received, &peer);
  }
  
}

static gboolean
gst_udpsrc_event_handler(GstPad* pad, GstEvent* event)
{
  GstUDPSrc* udpsrc = GST_UDPSRC(gst_pad_get_parent(pad));
  struct control_event* data;
  g_assert(udpsrc != NULL);
  if (GST_EVENT_TYPE(event) != GST_EVENT_CONTROL) {
    return gst_pad_event_default(pad, event);
  }

  data = (struct control_event*) &event->event_data;
  send_control_data(data->size, data->data);
}


static void
gst_udpsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstUDPSrc *udpsrc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_UDPSRC(object));
  udpsrc = GST_UDPSRC(object);
  if (GST_STATE(udpsrc) != GST_STATE_NULL && 
      (prop_id == ARG_LOCAL_ADDR) || prop_id == ARG_DATA_PORT ) {
    return;
  }

  switch (prop_id) {
  case ARG_LOCAL_ADDR:
    udpsrc->local_address = *(GstAddress*) g_value_get_boxed(value);
    udpsrc->local_address_uptodate = FALSE;
    break;
  case ARG_DATA_PORT:
    udpsrc->data_port = g_value_get_int(value);
    break;
  case ARG_CONTROL_PORT:
    udpsrc->control_port = g_value_get_int(value);
    break;
  case ARG_AUTHORIZED_SENDERS:
    udpsrc->authorized_senders = (GList*) g_value_get_boxed(value);
    udpsrc->authorized_senders_uptodate = FALSE;
    break;
  case ARG_REMOTE_ADDRESSES:
    udpsrc->remote_addresses = (GList*) g_value_get_boxed(value);
    udpsrc->remote_address_uptodate = FALSE;
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_udpsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstUDPSrc *udpsrc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_UDPSRC(object));
  udpsrc = GST_UDPSRC(object);
  switch (prop_id) {
  case ARG_LOCAL_ADDR:
    g_value_set_boxed(value, &udpsrc->local_address);
    break;
  case ARG_DATA_PORT:
    g_value_set_int(value, udpsrc->data_port);
    break;
  case ARG_CONTROL_PORT:
    g_value_set_int(value, udpsrc->control_port);
    break;
  case ARG_AUTHORIZED_SENDERS:
    g_value_set_boxed(value, udpsrc->authorized_senders);
    break;
  case ARG_REMOTE_ADDRESSES:
    g_value_set_boxed(value, udpsrc->remote_addresses);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

/* Initialize network connections */

static gboolean
gst_udpsrc_init_connections(GstUDPSrc* element) 
{
  struct sockaddr_in6 data_local_address, control_local_address;
  gboolean error;
  update_raw_addresses(element);
  element->data_socket = socket(AF_INET6, SOCK_DGRAM, 0);
  if (element->data_socket < 0)
    goto err;
  error = try_bind(element->data_socket, &element->raw_local_address, element->data_port);
  if (error != 0) 
    goto err;
  element->control_socket = socket(AF_INET6, SOCK_DGRAM, 0);
  if (element->control_socket < 0)
    goto err;
  error = try_bind(element->control_socket, &element->raw_local_address, element->control_port);
  if (error != 0)
    goto err;
  error = pipe(element->pipe);
  if (error != 0) {
    goto err;
  }
  element->open = TRUE;
  return TRUE;
 err:
  if (element->data_socket > 0) {
    close(element->data_socket);
    element->data_socket = -1;
  }
  if (element->control_socket > 0) {
    close(element->control_socket);
    element->control_socket = -1;
  }
  return FALSE;
}

static void
update_raw_addresses(GstUDPSrc* element)
{
  if (!element->local_address_uptodate) {
    resolve_address(&element->local_address, &element->raw_local_address);
  }
  if (!element->remote_address_uptodate) {
    resolve_addresses_ports(element->remote_addresses, &element->raw_remote_addresses);
  }
  if (!element->authorized_senders_uptodate) {
    resolve_addresses(element->authorized_senders, &element->raw_authorized_senders);
  }
}


static void
gst_udpsrc_close(GstUDPSrc *element)
{
  if (element->control_socket > 0) {
    close(element->control_socket);
  }
  if (element->data_socket > 0) {
    close(element->data_socket);
  }
  if (element->pipe[0] != 0) {
    close(element->pipe[0]);
    close(element->pipe[1]);
  }
  element->open = FALSE;
}

static GstElementStateReturn
gst_udpsrc_change_state(GstElement* element)
{
  GstUDPSrc* udpsrc = GST_UDPSRC(element);
  g_assert(udpsrc != NULL);
  if (GST_STATE_PENDING(element) == GST_STATE_NULL) {
    if (udpsrc->open) {
      gst_udpsrc_close(udpsrc);
    }
  } else {
    if (!udpsrc->open) {
      gst_udpsrc_init_connections(udpsrc);
    }
  }
  
}

static gboolean
gst_udpsrc_release_locks(GstElement* element)
{
  GstUDPSrc* udpsrc;
  char foo = '\0';
  udpsrc = GST_UDPSRC(element);
  g_assert(udpsrc != 0);
  write(udpsrc->pipe[0], &foo, 1);
  return TRUE;
}

/* socket functions */

static void wait_for_data(const GstUDPSrc* udpsrc, guint* data_size, char** data, source_type* source_type, struct sockaddr_in6* peer)
{
  int error;
  struct pollfd pollfds[3] = 
    {
      {.fd = udpsrc->pipe[1],
      .events = POLLIN,
      .revents = 0
      },
      {.fd = udpsrc->control_socket,
       .events = POLLIN,
       .revents = 0
      },
      {.fd = udpsrc->data_socket,
       .events = POLLIN,
       .revents = 0
      }
    };
  int active_fd;
  unsigned int num_bytes;
  poll(pollfds, 3, 0);
  g_assert(error == 0);
  if (pollfds[0].revents & POLLIN) {
    char foo;
    *data = NULL;
    *source_type = interrupt;
    read(udpsrc->pipe[1], &foo, 1);
    return;
  } else {
    struct sockaddr_in6 from;
    int foo;
    if (pollfds[1].revents & POLLIN) {
      *source_type = control;
      active_fd = udpsrc->control_socket;
    } else if (pollfds[1].revents & POLLIN) {
      *source_type = data;
      active_fd = udpsrc->data_socket;
    }
    error = ioctl(active_fd, FIONREAD, &num_bytes);
    g_assert(error == 0);
    *data = g_malloc(num_bytes);
    *data_size = num_bytes;
    recvfrom(active_fd, *data, num_bytes, 0,
	     (struct sockaddr*) peer, &foo);
  }
}
 

gboolean check_authorized_peer(const GstUDPSrc* udpsrc, const struct sockaddr_in6* peer)
{
  guint i;
  GList* slab;
  update_raw_addresses((GstUDPSrc*)udpsrc);
  for (slab = udpsrc->raw_authorized_senders.list; slab != NULL; slab = slab->next) {
    GstNumAddress* site = (GstNumAddress*) slab->data;
    for (i = 0; i < site->num_alternatives; i++) {
      if (memcmp(&peer->sin6_addr, &site->alternatives[i], sizeof(struct in6_addr)) == 0){
	return TRUE;
      }
    }
  }
  return FALSE;
}


static GstBuffer*
data_buffer(const GstUDPSrc* udpsrc, guint data_size, const char* data_received, struct sockaddr_in6* addr)
{
  GstBuffer* buffer;
  buffer = gst_buffer_new();
  put_data_addr(udpsrc, &GST_BUFFER_DATA(buffer), &GST_BUFFER_SIZE(buffer), data_size, data_received);
}

static GstEvent*
control_event(const GstUDPSrc* udpsrc, guint data_size, const char* data_received, const struct sockaddr_in6* addr)
{
  GstEvent* event;
  struct control_event* ctl;
  guint event_data_size;
  event = gst_event_new(GST_EVENT_UNKNOWN);
  ctl = (struct control_event*) &event->event_data;
  put_data_addr(udpsrc, &ctl->data, &ctl->size, data_size, data_received);
}

static void put_data_addr(const GstUDPSrc* udpsrc, char** result_data, guint* result_size, guint data_size, char* data, const struct sockaddr_in6* addr)
{
  if (udpsrc->include_addr_in_data) {
    struct buffer_data_extended* data_ex =
      g_malloc(data_size + sizeof(guint) + sizeof(struct sockaddr_in6));
    data_ex->addr_size = sizeof(struct sockaddr_in6);
    data_ex->addr = *addr;
    memcpy((char*) data_ex->data, data_received, data_size);
    g_free(data_received);
    *result_data = (char*) data_ex;
    *result_size = data_size;
  } else {
    *result_data = data_received;
    *result_size = data_size

  }
}

static void
send_control_data(GstUDPSrc* udpsrc, guint size, gpointer data)
{
  update_raw_addresses();
  GList* slab;
  for (slab = udpsrc->raw_remote_senders.list; slab != NULL; slab = slab->next) {
    GstNumAddressPort* remote = (GstNumAddressPort*) slab->data;
    try_send(udpsrc->control_socket, remote, data, size);
  }
}

