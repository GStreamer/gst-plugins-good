#ifndef ADDRESS_H
#define ADDRESS_H

#include <glib-object.h>
#include <netinet/in.h>

struct _GstAddress {
  enum {IPV4, IPV6, NAME} type;
  union addr_fmt {
    struct in_addr ipv4_address;
    struct in6_addr ipv6_address;
    char* name;
  } addr;
};

typedef struct _GstAddress GstAddress;

struct _GstAddrPort {
  GstAddress addr;
  unsigned short port;
};

typedef struct _GstAddrPort GstAddrPort;

GType gst_address_get_type();
GType gst_peer_get_type();
GType gst_address_list_get_type();
GType gst_addr_port_list_get_type();

#define GST_TYPE_ADDRESS (gst_address_get_type())
#define GST_TYPE_ADDR_PORT (gst_addr_port_get_type())
#define GST_TYPE_ADDRESS_LIST (gst_address_list_get_type())
#define GST_TYPE_ADDR_PORT_LIST (gst_addr_port_list_get_type())


/* Represents the address of a single logical machine, 
   that can be accesses throught any of these addresses.
   It is the result of a name resolution that returns a list
   of addresses */
struct _GstNumAddress {
  guint num_alternatives;
  struct in6_addr* alternatives;
};

typedef struct _GstNumAddress GstNumAddress;

/* Represents a list of addresses. For instance, a list of authorized machines */

struct _GstNumAddresses {
  GList* list; /* Actually, a GList of <GstNumAddress> */
};

typedef struct _GstNumAddresses GstNumAddresses;

struct _GstNumAddressPort {
  GstNumAddress address;
  guint16 port;
};

typedef struct _GstNumAddressPort GstNumAddressPort;

struct _GstNumAddressesPorts {
  GList* list; /* GList of <GstNumAddressPort> */
};

typedef struct _GstNumAddressesPorts GstNumAddressesPorts;

void resolve_address(const GstAddress* address, GstNumAddress* result);

void resolve_addresses(const GList* addresses, /* GList of GstAddress */
		       GstNumAddresses* result);

void resolve_addresses_ports(const GList* addresses, /* GList of GstAddrPort */
			     GstNumAddressesPorts* result);

gboolean try_bind(int socket, const GstNumAddress* address, int port);

void try_send(int socket, GstNumAddressPort* remote, const char* data, guint size);

#endif
