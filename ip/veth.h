#ifndef __NET_VETH_H__
#define __NET_VETH_H__

enum {
	VETH_INFO_UNSPEC,
	VETH_INFO_PEER,

	__VETH_INFO_MAX
#define VETH_INFO_MAX	(__VETH_INFO_MAX - 1)
};

#endif
