#include "address.h"
#include <netdb.h>

static gpointer gst_address_copy(gpointer);

static void gst_address_free(gpointer);

static gpointer gst_address_list_copy(gpointer);

static void gst_address_list_free(gpointer);

static gpointer gst_addr_port_copy(gpointer);

static void gst_addr_port_free(gpointer);

static gpointer gst_addr_port_list_copy(gpointer);

static void gst_addr_port_list_free(gpointer);

static void resolve_name(const gchar* name, GstNumAddress* result);

GType
gst_address_get_type(void) {
  static GType address_type = 0;
  if (address_type == 0) {
    address_type = g_boxed_type_register_static("GstAddress", 
						gst_address_copy,
						gst_address_free);
  }
  return address_type;
}

static gpointer 
gst_address_copy(gpointer src)
{
  GstAddress* addr_src = (GstAddress*) src;
  GstAddress* addr_result;
  addr_result = g_new0(GstAddress, 1);
  *addr_result = *addr_src;
  if (addr_src->type == NAME) {
    addr_result->addr.name = g_strdup(addr_src->addr.name);
  }
  return (gpointer) addr_result;
}

static void 
gst_address_free(gpointer ptr)
{
  GstAddress* addr_ptr = (GstAddress*) ptr;
  if (addr_ptr->type ==  NAME) {
    g_free(addr_ptr->addr.name);
  }
  g_free(ptr);
}

GType
gst_address_list_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    type = g_boxed_type_register_static("GstAddressList", 
					gst_address_list_copy,
					gst_address_list_free);
  }
  return type;
}

static gpointer 
gst_address_list_copy(gpointer src) 
{
  GList* src_list = (GList*) src;
  GList* dest_list = NULL;
  GList* list_node = src_list;
  while (list_node != NULL) {
    dest_list = g_list_append(dest_list, gst_address_copy(list_node->data));
    list_node = g_list_next(list_node);
  }
  return (gpointer) dest_list;
}

static void 
gst_address_list_free(gpointer ptr)
{
  GList* ptr_list = (GList*) ptr;
  GList* list_node = ptr_list;
  while (list_node != NULL) {
    gst_address_free(list_node->data);
    list_node = g_list_remove_link(list_node, list_node);
  }
}

static gpointer
gst_addr_port_copy(gpointer src)
{
  GstAddrPort* addr_src = (GstAddrPort*) src;
  GstAddrPort* addr_result;
  addr_result = g_new0(GstAddrPort, 1);
  *addr_result = *addr_src;
  if (addr_src->addr.type == NAME) {
    addr_result->addr.addr.name = g_strdup(addr_src->addr.addr.name);
  }
  return (gpointer) addr_result;
}

static void
gst_addr_port_free(gpointer ptr)
{
  GstAddrPort* addr_ptr = (GstAddrPort*) ptr;
  if (addr_ptr->addr.type ==  NAME) {
    g_free(addr_ptr->addr.addr.name);
  }
  g_free(ptr);
}

GType
gst_addr_port_get_type(void)
{
  static GType type = 0;
  if (type == 0) {
    type = g_boxed_type_register_static("GstAddrPort",
					gst_addr_port_copy,
					gst_addr_port_free);
  }
  return type;
}

GType 
gst_addr_port_list_get_type(void) 
{
  static GType type = 0;
  if (type == 0) {
    type = g_boxed_type_register_static("GstAddrPortList",
					gst_addr_port_list_copy,
					gst_addr_port_list_free);
  }
  return type;
}



static gpointer
gst_addr_port_list_copy(gpointer src)
{
  GList* src_list = (GList*) src;
  GList* dest_list = NULL;
  GList* list_node = src_list;
  while (list_node != NULL) {
    dest_list = g_list_append(dest_list, gst_addr_port_copy(list_node->data));
    list_node = g_list_next(list_node);
  }
  return (gpointer) dest_list;
}

static void
gst_addr_port_list_free(gpointer ptr)
{
  GList* ptr_list = (GList*) ptr;
  GList* list_node = ptr_list;
  while (list_node != NULL) {
    gst_addr_port_free(list_node->data);
    list_node = g_list_remove_link(list_node, list_node);
  }
}


void 
resolve_address(const GstAddress* address, GstNumAddress* result)
{
  if (result->alternatives != NULL) {
    g_free(result->alternatives);
  }
  result->num_alternatives = 0
  result->alternatives = NULL;
  switch (address->type) {
  case IPV4:
    result->num_alternatives = 1;
    result->alternatives = g_new(struct in6_addr, 1);
    map_ipv4_to_ipv6(address->addr.ipv4_address, result->alternatives[0]);
    break;
  case IPV6:
    result->num_alternatives = 1;
    result->alternatives = g_new(struct in6_addr, 1);
    result->alternatives[0] = address->addr.ipv6_address;
  case NAME:
    resolve_name(address->addr.name, result);
		
  }
}


static void
resolve_name(const gchar* name, GstNumAddress* result)
{
  int error;
  struct addrinfo* result;
  struct addrinfo* link;
  error = getaddrinfo(name, NULL, NULL, &result);
  if (error != 0) 
    return;
  result->num_alternatives = 0;
  link = result;
  while(link != NULL) {
    if (link->ai_family == AF_INET || link->ai_family == AF_INET6) {
      result->num_alternatives++;
    }
    link = link->ai_next;
  }
  result->alternatives = g_new0(struct in6_addr, result->num_alternatives);
  link = result;
  alternative = result->alternatives;
  while(link != NULL) {
    if (link->ai_protocol == AF_INET || link->ai_family == AF_INET6) {
      *alternative = ((struct sockaddr_in6 *) link->ai_addr)->sin6_addr;
      alternative++;
    }
    link = link->ai_next;
  }
  freeaddrinfo(result);
}

gboolean 
try_bind(int socket, const GstNumAddress* address, int port)
{
  int error;
  int i;
  struct in6_addr addr;
  g_assert(address->num_alternatives > 0);
  for (i = 0; i < address->num_alternatives; i++) {
    addr = address->alternatives[i];
    addr.sin_port = port;
    error = bind(socket, &addr, sizeof(struct in6_addr));
    if (error == 0) {
      break;
    }
  }
  return (error != 0);
}

void
try_send(int socket, GstNumAddressPort* remote, const char* data, guint size)
{
  struct sockaddr_in6 peer;
  guint i;
  gboolean sucess;
  peer.sin6_port = remote->port;
  peer.sin6_flowinfo = 0;
  peer.sin6_scope_id = 0;
  for (i = 0; i < remote->address.num_alternatives; i++) {
    peer.sin6_addr = remote->address.alternatives[i];
    error = sendto(socket, data, size, 0, &peer, sizeof(sockaddr_in6));
    sucess = error >= 0;
    if (sucess)
      break;
  }
}
