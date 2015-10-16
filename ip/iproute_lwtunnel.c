/*
 * iproute_lwtunnel.c
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Roopa Prabhu, <roopa@cumulusnetworks.com>
 * 		Thomas Graf <tgraf@suug.ch>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <linux/lwtunnel.h>
#include <linux/mpls_iptunnel.h>
#include <errno.h>

#include "rt_names.h"
#include "utils.h"
#include "iproute_lwtunnel.h"

static int read_encap_type(const char *name)
{
	if (strcmp(name, "mpls") == 0)
		return LWTUNNEL_ENCAP_MPLS;
	else if (strcmp(name, "ip") == 0)
		return LWTUNNEL_ENCAP_IP;
	else if (strcmp(name, "ip6") == 0)
		return LWTUNNEL_ENCAP_IP6;
	else
		return LWTUNNEL_ENCAP_NONE;
}

static const char *format_encap_type(int type)
{
	switch (type) {
	case LWTUNNEL_ENCAP_MPLS:
		return "mpls";
	case LWTUNNEL_ENCAP_IP:
		return "ip";
	case LWTUNNEL_ENCAP_IP6:
		return "ip6";
	default:
		return "unknown";
	}
}

static void print_encap_mpls(FILE *fp, struct rtattr *encap)
{
	struct rtattr *tb[MPLS_IPTUNNEL_MAX+1];
	char abuf[256];

	parse_rtattr_nested(tb, MPLS_IPTUNNEL_MAX, encap);

	if (tb[MPLS_IPTUNNEL_DST])
		fprintf(fp, " %s ", format_host(AF_MPLS,
			RTA_PAYLOAD(tb[MPLS_IPTUNNEL_DST]),
			RTA_DATA(tb[MPLS_IPTUNNEL_DST]),
			abuf, sizeof(abuf)));
}

static void print_encap_ip(FILE *fp, struct rtattr *encap)
{
	struct rtattr *tb[LWTUNNEL_IP_MAX+1];
	char abuf[256];

	parse_rtattr_nested(tb, LWTUNNEL_IP_MAX, encap);

	if (tb[LWTUNNEL_IP_ID])
		fprintf(fp, "id %llu ", ntohll(rta_getattr_u64(tb[LWTUNNEL_IP_ID])));

	if (tb[LWTUNNEL_IP_SRC])
		fprintf(fp, "src %s ",
			rt_addr_n2a(AF_INET,
				    RTA_PAYLOAD(tb[LWTUNNEL_IP_SRC]),
				    RTA_DATA(tb[LWTUNNEL_IP_SRC]),
				    abuf, sizeof(abuf)));

	if (tb[LWTUNNEL_IP_DST])
		fprintf(fp, "dst %s ",
			rt_addr_n2a(AF_INET,
				    RTA_PAYLOAD(tb[LWTUNNEL_IP_DST]),
				    RTA_DATA(tb[LWTUNNEL_IP_DST]),
				    abuf, sizeof(abuf)));

	if (tb[LWTUNNEL_IP_TTL])
		fprintf(fp, "ttl %d ", rta_getattr_u8(tb[LWTUNNEL_IP_TTL]));

	if (tb[LWTUNNEL_IP_TOS])
		fprintf(fp, "tos %d ", rta_getattr_u8(tb[LWTUNNEL_IP_TOS]));
}

void lwt_print_encap(FILE *fp, struct rtattr *encap_type,
			  struct rtattr *encap)
{
	int et;

	if (!encap_type)
		return;

	et = rta_getattr_u16(encap_type);

	fprintf(fp, " encap %s", format_encap_type(et));

	switch (et) {
	case LWTUNNEL_ENCAP_MPLS:
		print_encap_mpls(fp, encap);
		break;
	case LWTUNNEL_ENCAP_IP:
		print_encap_ip(fp, encap);
		break;
	}
}

static int parse_encap_mpls(struct rtattr *rta, size_t len, int *argcp, char ***argvp)
{
	inet_prefix addr;
	int argc = *argcp;
	char **argv = *argvp;

	if (get_addr(&addr, *argv, AF_MPLS)) {
		fprintf(stderr, "Error: an inet address is expected rather than \"%s\".\n", *argv);
		exit(1);
	}

	rta_addattr_l(rta, len, MPLS_IPTUNNEL_DST, &addr.data,
		      addr.bytelen);

	*argcp = argc;
	*argvp = argv;

	return 0;
}

static int parse_encap_ip(struct rtattr *rta, size_t len, int *argcp, char ***argvp)
{
	int id_ok = 0, dst_ok = 0, tos_ok = 0, ttl_ok = 0;
	char **argv = *argvp;
	int argc = *argcp;

	while (argc > 0) {
		if (strcmp(*argv, "id") == 0) {
			__u64 id;
			NEXT_ARG();
			if (id_ok++)
				duparg2("id", *argv);
			if (get_u64(&id, *argv, 0))
				invarg("\"id\" value is invalid\n", *argv);
			rta_addattr64(rta, len, LWTUNNEL_IP_ID, htonll(id));
		} else if (strcmp(*argv, "dst") == 0) {
			inet_prefix addr;
			NEXT_ARG();
			if (dst_ok++)
				duparg2("dst", *argv);
			get_addr(&addr, *argv, AF_INET);
			rta_addattr_l(rta, len, LWTUNNEL_IP_DST, &addr.data, addr.bytelen);
		} else if (strcmp(*argv, "tos") == 0) {
			__u32 tos;
			NEXT_ARG();
			if (tos_ok++)
				duparg2("tos", *argv);
			if (rtnl_dsfield_a2n(&tos, *argv))
				invarg("\"tos\" value is invalid\n", *argv);
			rta_addattr8(rta, len, LWTUNNEL_IP_TOS, tos);
		} else if (strcmp(*argv, "ttl") == 0) {
			__u8 ttl;
			NEXT_ARG();
			if (ttl_ok++)
				duparg2("ttl", *argv);
			if (get_u8(&ttl, *argv, 0))
				invarg("\"ttl\" value is invalid\n", *argv);
			rta_addattr8(rta, len, LWTUNNEL_IP_TTL, ttl);
		} else {
			break;
		}
	}

	*argcp = argc;
	*argvp = argv;

	return 0;
}


int lwt_parse_encap(struct rtattr *rta, size_t len, int *argcp, char ***argvp)
{
	struct rtattr *nest;
	int argc = *argcp;
	char **argv = *argvp;
	__u16 type;

	NEXT_ARG();
	type = read_encap_type(*argv);
	if (!type)
		invarg("\"encap type\" value is invalid\n", *argv);

	NEXT_ARG();
	if (argc <= 1) {
		fprintf(stderr, "Error: unexpected end of line after \"encap\"\n");
		exit(-1);
	}

	nest = rta_nest(rta, 1024, RTA_ENCAP);
	switch (type) {
	case LWTUNNEL_ENCAP_MPLS:
		parse_encap_mpls(rta, len, &argc, &argv);
		break;
	case LWTUNNEL_ENCAP_IP:
		parse_encap_ip(rta, len, &argc, &argv);
		break;
	default:
		fprintf(stderr, "Error: unsupported encap type\n");
		break;
	}
	rta_nest_end(rta, nest);

	rta_addattr16(rta, 1024, RTA_ENCAP_TYPE, type);

	*argcp = argc;
	*argvp = argv;

	return 0;
}
