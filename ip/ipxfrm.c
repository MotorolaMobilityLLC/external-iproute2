/* $USAGI: $ */

/*
 * Copyright (C)2004 USAGI/WIDE Project
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * based on ip.c, iproute.c
 */
/*
 * Authors:
 *	Masahide NAKAMURA @USAGI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <netdb.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/xfrm.h>

#include "utils.h"
#include "xfrm.h"

struct xfrm_filter filter;

static void usage(void) __attribute__((noreturn));

static void usage(void)
{
	fprintf(stderr, 
		"Usage: ip xfrm XFRM_OBJECT { COMMAND | help }\n"
		"where  XFRM_OBJECT := { state | policy }\n");
	exit(-1);
}

const char *strxf_flags(__u8 flags)
{
	static char str[16];
	const int sn = sizeof(flags) * 8 - 1;
	__u8 b;
	int i = 0;

	for (b = (1 << sn); b > 0; b >>= 1)
		str[i++] = ((b & flags) ? '1' : '0');
	str[i] = '\0';

	return str;
}

const char *strxf_share(__u8 share)
{
	static char str[32];

	switch (share) {
	case XFRM_SHARE_ANY:
		strcpy(str, "any");
		break;
	case XFRM_SHARE_SESSION:
		strcpy(str, "session");
		break;
	case XFRM_SHARE_USER:
		strcpy(str, "user");
		break;
	case XFRM_SHARE_UNIQUE:
		strcpy(str, "unique");
		break;
	default:
		sprintf(str, "unknown-share(%d)", share);
		break;
	}

	return str;
}

const char *strxf_proto(__u8 proto)
{
	static char buf[32];
	struct protoent *pp;
	const char *p;

	pp = getprotobynumber(proto);
	if (pp)
		p = pp->p_name;
	else {
		sprintf(buf, "%d", proto);
		p = buf;
	}

	return p;
}

void xfrm_id_info_print(xfrm_address_t *saddr, struct xfrm_id *id,
			__u8 mode, __u32 reqid, __u16 family, FILE *fp,
			const char *prefix)
{
	char abuf[256];
	__u32 spi;
	struct protoent *pp;
	char pbuf[32];
	char *p;

	if (prefix)
		fprintf(fp, prefix);

	memset(abuf, '\0', sizeof(abuf));
	fprintf(fp, "src %s ", rt_addr_n2a(family, sizeof(*saddr),
					   saddr, abuf, sizeof(abuf)));
	memset(abuf, '\0', sizeof(abuf));
	fprintf(fp, "dst %s\n", rt_addr_n2a(family, sizeof(id->daddr),
					    &id->daddr, abuf, sizeof(abuf)));

	if (prefix)
		fprintf(fp, prefix);
	fprintf(fp, "\t");

	pp = getprotobynumber(id->proto);
	if (pp)
		p = pp->p_name;
	else {
		sprintf(pbuf, "%d", id->proto);
		p = pbuf;
	}
	fprintf(fp, "proto %s ", p);

	spi = ntohl(id->spi);
	fprintf(fp, "spi %u", spi);
	if (show_stats > 0)
		fprintf(fp, "(0x%08x)", spi);
	fprintf(fp, " ");

	fprintf(fp, "reqid %u", reqid);
	if (show_stats > 0)
		fprintf(fp, "(0x%08x)", reqid);
	fprintf(fp, " ");

	fprintf(fp, "mode %s\n", (mode ? "tunnel" : "transport"));
}

static const char *strxf_limit(__u64 limit)
{
	static char str[32];
	if (limit == XFRM_INF)
		strcpy(str, "(INF)");
	else
		sprintf(str, "%llu", limit);

	return str;
}

void xfrm_stats_print(struct xfrm_stats *s, FILE *fp, const char *prefix)
{
	if (prefix)
		fprintf(fp, prefix);
	fprintf(fp, "stats:\n");

	if (prefix)
		fprintf(fp, prefix);
	fprintf(fp, "  ");
	fprintf(fp, "replay-window %d ", s->replay_window);
	fprintf(fp, "replay %d ", s->replay);
	fprintf(fp, "failed %d", s->integrity_failed);
	fprintf(fp, "\n");
}

static const char *strxf_time(__u64 time)
{
	static char str[32];
	struct tm *tp;
	time_t t;

	if (time == 0) {
		strcpy(str, "(undefined)");
	} else {
		/* XXX: treat time in the same manner of xfrm_{user,state}.c */
		t = (long)time;
		tp = localtime(&t);

		sprintf(str, "%04d/%02d/%02d %02d:%02d:%02d",
			tp->tm_year + 1900, tp->tm_mon + 1, tp->tm_mday,
			tp->tm_hour, tp->tm_min, tp->tm_sec);
	}

	return str;
}

void xfrm_lifetime_print(struct xfrm_lifetime_cfg *cfg,
			 struct xfrm_lifetime_cur *cur,
			 FILE *fp, const char *prefix)
{
	if (cfg) {
		if (prefix)
			fprintf(fp, prefix);
		fprintf(fp, "lifetime config:\n");

		if (prefix)
			fprintf(fp, prefix);
		fprintf(fp, "  ");
		fprintf(fp, "limit: ");
		fprintf(fp, "soft ");
		fprintf(fp, strxf_limit(cfg->soft_byte_limit));
		fprintf(fp, "(bytes), hard ");
		fprintf(fp, strxf_limit(cfg->hard_byte_limit));
		fprintf(fp, "(bytes)\n");

		if (prefix)
			fprintf(fp, prefix);
		fprintf(fp, "  ");
		fprintf(fp, "limit: ");
		fprintf(fp, "soft ");
		fprintf(fp, strxf_limit(cfg->soft_packet_limit));
		fprintf(fp, "(packets), hard ");
		fprintf(fp, strxf_limit(cfg->hard_packet_limit));
		fprintf(fp, "(packets)\n");

		if (prefix)
			fprintf(fp, prefix);
		fprintf(fp, "  ");
		fprintf(fp, "expire add: ");
		fprintf(fp, "soft ");
		fprintf(fp, "%llu", cfg->soft_add_expires_seconds);
		fprintf(fp, "(sec), hard ");
		fprintf(fp, "%llu", cfg->hard_add_expires_seconds);
		fprintf(fp, "(sec)\n");

		if (prefix)
			fprintf(fp, prefix);
		fprintf(fp, "  ");
		fprintf(fp, "expire use: ");
		fprintf(fp, "soft ");
		fprintf(fp, "%llu", cfg->soft_use_expires_seconds);
		fprintf(fp, "(sec), hard ");
		fprintf(fp, "%llu", cfg->hard_use_expires_seconds);
		fprintf(fp, "(sec)\n");
	}
	if (cur) {
		if (prefix)
			fprintf(fp, prefix);
		fprintf(fp, "lifetime current:\n");

		if (prefix)
			fprintf(fp, prefix);
		fprintf(fp, "  ");
		fprintf(fp, "%llu(bytes), ", cur->bytes);
		fprintf(fp, "%llu(packets)\n", cur->packets);
		if (prefix)
			fprintf(fp, prefix);
		fprintf(fp, "  ");
		fprintf(fp, "add %s ", strxf_time(cur->add_time));
		fprintf(fp, "use %s", strxf_time(cur->use_time));
		fprintf(fp, "\n");
	}
}

void xfrm_selector_print(struct xfrm_selector *sel, __u16 family,
			 FILE *fp, const char *prefix)
{
	char abuf[256];
	__u16 f;

	f = sel->family;
	if (f == AF_UNSPEC)
		f = family;
	if (f == AF_UNSPEC)
		f = preferred_family;

	if (prefix)
		fprintf(fp, prefix);

	memset(abuf, '\0', sizeof(abuf));
	fprintf(fp, "src %s/%d ", rt_addr_n2a(f, sizeof(sel->saddr),
					      &sel->saddr, abuf, sizeof(abuf)),
		sel->prefixlen_s);

	memset(abuf, '\0', sizeof(abuf));
	fprintf(fp, "dst %s/%d", rt_addr_n2a(f, sizeof(sel->daddr),
					      &sel->daddr, abuf, sizeof(abuf)),
		sel->prefixlen_d);

	fprintf(fp, "\n");

	if (prefix)
		fprintf(fp, prefix);
	fprintf(fp, "\t");

	fprintf(fp, "upspec proto %u ", sel->proto);
	fprintf(fp, "sport %u dport %u ", sel->sport, sel->dport);

	if (sel->ifindex > 0) {
		char buf[IF_NAMESIZE];

		memset(buf, '\0', sizeof(buf));
		if_indextoname(sel->ifindex, buf);
		fprintf(fp, "dev %s ", buf);
	}

	if (show_stats > 0)
		fprintf(fp, "uid %u", sel->user);
	fprintf(fp, "\n");
}

static void xfrm_algo_print(struct xfrm_algo *algo, FILE *fp,
			    const char *prefix)
{
	int len;
	int i;

	if (prefix)
		fprintf(fp, prefix);

	fprintf(fp, "%s", algo->alg_name);

	len = algo->alg_key_len / 8;
	for (i = 0; i < len; i ++) {
		if (i % 4 == 0)
			fprintf(fp, " ");
		fprintf(fp, "%x", algo->alg_key[i]);
	}

	fprintf(fp, "\n");
}

static const char *strxf_mask(__u32 mask)
{
	static char str[128];
	const int sn = 	sizeof(mask) * 8 - 1;
	__u32 b;
	int finish = 0;
	int broken = 0;
	int i = 0;

	for (b = (1 << sn); b > 0; b >>= 1) {
		if ((b & mask) == 0) {
			if (!finish)
				finish = 1;
		} else {
			if (!finish)
				i ++;
			else {
				broken = 1;
				break;
			}
		}
	}

	if (!broken)
		sprintf(str, "%u", i);
	else
		sprintf(str, "broken(%u)", mask);

	return str;
}

static void xfrm_tmpl_print(struct xfrm_user_tmpl *tmpls, int ntmpls,
			    __u16 family, FILE *fp, const char *prefix)
{
	int i;

	for (i = 0; i < ntmpls; i++) {
		struct xfrm_user_tmpl *tmpl = &tmpls[i];

		if (prefix)
			fprintf(fp, prefix);

		fprintf(fp, "tmpl");
		xfrm_id_info_print(&tmpl->saddr, &tmpl->id, tmpl->mode,
				   tmpl->reqid, family, fp, prefix);

		fprintf(fp, prefix);
		fprintf(fp, "\t");
		fprintf(fp, "level ");
		switch (tmpl->optional) {
		case 0:
			fprintf(fp, "required");
			break;
		case 1:
			fprintf(fp, "use");
			break;
		default:
			fprintf(fp, "%d", tmpl->optional);
			break;
		}
		fprintf(fp, " ");

		if (show_stats > 0) {
			fprintf(fp, "share %s ", strxf_share(tmpl->share));
			fprintf(fp, "algo-mask:");
			fprintf(fp, "E=%s, ", strxf_mask(tmpl->ealgos));
			fprintf(fp, "A=%s, ", strxf_mask(tmpl->aalgos));
			fprintf(fp, "C=%s", strxf_mask(tmpl->calgos));
		}
		fprintf(fp, "\n");
	}
}

void xfrm_xfrma_print(struct rtattr *tb[], int ntb, __u16 family,
		      FILE *fp, const char *prefix)
{
	int i;

	for (i = 0; i < ntb; i++) {
		__u16 type = tb[i]->rta_type;
		void *data = RTA_DATA(tb[i]);

		switch (type) {
		case XFRMA_ALG_CRYPT:
			if (prefix)
				fprintf(fp, prefix);
			xfrm_algo_print((struct xfrm_algo *)data, fp, "algo E ");
			break;
		case XFRMA_ALG_AUTH:
			if (prefix)
				fprintf(fp, prefix);
			xfrm_algo_print((struct xfrm_algo *)data, fp, "algo A ");
			break;
		case XFRMA_ALG_COMP:
			if (prefix)
				fprintf(fp, prefix);
			xfrm_algo_print((struct xfrm_algo *)data, fp, "algo C ");
			break;
		case XFRMA_ENCAP:
			if (prefix)
				fprintf(fp, prefix);
			/* XXX */
			fprintf(fp, "encap: (not implemented yet!)\n");
			break;
		case XFRMA_TMPL:
		{
			int len = tb[i]->rta_len;
			int ntmpls = len / sizeof(struct xfrm_user_tmpl);

			xfrm_tmpl_print((struct xfrm_user_tmpl *)data,
					ntmpls, family, fp, prefix);
			break;
		}
		default:
			if (prefix)
				fprintf(fp, prefix);
			fprintf(fp, "unknown rta_type: %u\n", type);
			break;
		}
	}
}

int xfrm_id_parse(xfrm_address_t *saddr, struct xfrm_id *id, __u16 *family,
		  int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	inet_prefix dst;
	inet_prefix src;
	__u8 proto = 0;

	memset(&dst, 0, sizeof(dst));
	memset(&src, 0, sizeof(src));

	while (1) {
		if (strcmp(*argv, "src") == 0) {
			NEXT_ARG();

			get_prefix(&src, *argv, preferred_family);
			if (src.family == AF_UNSPEC)
				invarg("\"SADDR\" address family is AF_UNSPEC", *argv);
			if (family)
				*family = src.family;

			memcpy(saddr, &src.data, sizeof(*saddr));

			filter.id_src_mask = src.bitlen;

		} else if (strcmp(*argv, "dst") == 0) {
			NEXT_ARG();

			get_prefix(&dst, *argv, preferred_family);
			if (dst.family == AF_UNSPEC)
				invarg("\"DADDR\" address family is AF_UNSPEC", *argv);
			if (family)
				*family = dst.family;

			memcpy(&id->daddr, &dst.data, sizeof(id->daddr));

			filter.id_dst_mask = dst.bitlen;

		} else if (strcmp(*argv, "proto") == 0) {
			struct protoent *pp;

			NEXT_ARG();

			pp = getprotobyname(*argv);
			if (pp)
				proto = pp->p_proto;
			else {
				if (get_u8(&proto, *argv, 0))
					invarg("\"PROTO\" is invalid", *argv);
			}

			switch (proto) {
			case IPPROTO_ESP:
			case IPPROTO_AH:
			case IPPROTO_COMP:
				id->proto = proto;
				break;
			default:
				invarg("\"PROTO\" is unsuppored proto", *argv);
			}

			filter.id_proto_mask = XFRM_FILTER_MASK_FULL;

		} else if (strcmp(*argv, "spi") == 0) {
			__u32 spi;

			NEXT_ARG();
			if (get_u32(&spi, *argv, 0))
				invarg("\"SPI\" is invalid", *argv);

			spi = htonl(spi);
			id->spi = spi;

			filter.id_spi_mask = XFRM_FILTER_MASK_FULL;

		} else {
			PREV_ARG(); /* back track */
			break;
		}

		if (!NEXT_ARG_OK())
			break;
		NEXT_ARG();
	}

	if (src.family && dst.family && (src.family != dst.family))
		invarg("the same address family is required between \"SADDR\" and \"DADDR\"", *argv);
	if (proto == 0)
		missarg("PROTO");

	if (argc == *argcp)
		missarg("ID");

	*argcp = argc;
	*argvp = argv;

	return 0;
}

int xfrm_mode_parse(__u8 *mode, int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;

	if (matches(*argv, "transport") == 0)
		*mode = 0;
	else if (matches(*argv, "tunnel") == 0)
		*mode = 1;
	else
		invarg("\"MODE\" is invalid", *argv);

	*argcp = argc;
	*argvp = argv;

	return 0;
}

/* NOTE: reqid is used by host-byte order */
int xfrm_reqid_parse(__u32 *reqid, int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;

	if (get_u32(reqid, *argv, 0))
		invarg("\"REQID\" is invalid", *argv);

	*argcp = argc;
	*argvp = argv;

	return 0;
}

static int xfrm_selector_upspec_parse(struct xfrm_selector *sel,
				      int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	__u8 upspec;

	while (1) {
		if (strcmp(*argv, "proto") == 0) {
			NEXT_ARG();

			if (strcmp(*argv, "any") == 0)
				upspec = 0;
			else {
				struct protoent *pp;
				pp = getprotobyname(*argv);
				if (pp)
					upspec = pp->p_proto;
				else {
					if (get_u8(&upspec, *argv, 0))
						invarg("\"UPSPEC\" is invalid", *argv);
				}
			}
			sel->proto = upspec;

			filter.upspec_proto_mask = XFRM_FILTER_MASK_FULL;

		} else if (strcmp(*argv, "sport") == 0) {
			NEXT_ARG();

			if (get_u16(&sel->sport, *argv, 0))
				invarg("\"PORT\" is invalid", *argv);
			sel->sport = htons(sel->sport);
			if (sel->sport)
				sel->sport_mask = ~((__u16)0);

			filter.upspec_sport_mask = XFRM_FILTER_MASK_FULL;

		} else if (strcmp(*argv, "dport") == 0) {
			NEXT_ARG();

			if (get_u16(&sel->dport, *argv, 0))
				invarg("\"PORT\" is invalid", *argv);
			sel->dport = htons(sel->dport);
			if (sel->dport)
				sel->dport_mask = ~((__u16)0);

			filter.upspec_dport_mask = XFRM_FILTER_MASK_FULL;

		} else {
			PREV_ARG(); /* back track */
			break;
		}

		if (!NEXT_ARG_OK())
			break;
		NEXT_ARG();
	}
	if (argc == *argcp)
		missarg("UPSPEC");

	*argcp = argc;
	*argvp = argv;

	return 0;
}

int xfrm_selector_parse(struct xfrm_selector *sel, int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	inet_prefix dst;
	inet_prefix src;

	memset(&dst, 0, sizeof(dst));
	memset(&src, 0, sizeof(src));

	while (1) {
		if (strcmp(*argv, "src") == 0) {
			NEXT_ARG();

			get_prefix(&src, *argv, preferred_family);
			if (src.family == AF_UNSPEC)
				invarg("\"SADDR\" address family is AF_UNSPEC", *argv);
			sel->family = src.family;

			memcpy(&sel->saddr, &src.data, sizeof(sel->saddr));
			sel->prefixlen_s = src.bitlen;

			filter.sel_src_mask = src.bitlen;

		} else if (strcmp(*argv, "dst") == 0) {
			NEXT_ARG();

			get_prefix(&dst, *argv, preferred_family);
			if (dst.family == AF_UNSPEC)
				invarg("\"DADDR\" address family is AF_UNSPEC", *argv);
			sel->family = dst.family;

			memcpy(&sel->daddr, &dst.data, sizeof(sel->daddr));
			sel->prefixlen_d = dst.bitlen;

			filter.sel_dst_mask = dst.bitlen;

		} else if (strcmp(*argv, "upspec") == 0) {
			NEXT_ARG();

			xfrm_selector_upspec_parse(sel, &argc, &argv);

		} else if (strcmp(*argv, "dev") == 0) {
			int ifindex;

			NEXT_ARG();

			if (strcmp(*argv, "none") == 0)
				ifindex = 0;
			else {
				ifindex = if_nametoindex(*argv);
				if (ifindex <= 0)
					invarg("\"DEV\" is invalid", *argv);
			}
			sel->ifindex = ifindex;

			filter.sel_dev_mask = XFRM_FILTER_MASK_FULL;

		} else {
			PREV_ARG(); /* back track */
			break;
		}

		if (!NEXT_ARG_OK())
			break;

		NEXT_ARG();
	}

	if (src.family && dst.family && (src.family != dst.family))
		invarg("the same address family is required between \"SADDR\" and \"DADDR\"", *argv);

	if (argc == *argcp)
		missarg("SELECTOR");

	*argcp = argc;
	*argvp = argv;

	return 0;
}

int xfrm_lifetime_cfg_parse(struct xfrm_lifetime_cfg *lft,
			    int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	int ret;

	if (strcmp(*argv, "time-soft") == 0) {
		NEXT_ARG();
		ret = get_u64(&lft->soft_add_expires_seconds, *argv, 0);
		if (ret)
			invarg("\"time-soft\" value is invalid", *argv);
	} else if (strcmp(*argv, "time-hard") == 0) {
		NEXT_ARG();
		ret = get_u64(&lft->hard_add_expires_seconds, *argv, 0);
		if (ret)
			invarg("\"time-hard\" value is invalid", *argv);
	} else if (strcmp(*argv, "time-use-soft") == 0) {
		NEXT_ARG();
		ret = get_u64(&lft->soft_use_expires_seconds, *argv, 0);
		if (ret)
			invarg("\"time-use-soft\" value is invalid", *argv);
	} else if (strcmp(*argv, "time-use-hard") == 0) {
		NEXT_ARG();
		ret = get_u64(&lft->hard_use_expires_seconds, *argv, 0);
		if (ret)
			invarg("\"time-use-hard\" value is invalid", *argv);
	} else if (strcmp(*argv, "byte-soft") == 0) {
		NEXT_ARG();
		ret = get_u64(&lft->soft_byte_limit, *argv, 0);
		if (ret)
			invarg("\"byte-soft\" value is invalid", *argv);
	} else if (strcmp(*argv, "byte-hard") == 0) {
		NEXT_ARG();
		ret = get_u64(&lft->hard_byte_limit, *argv, 0);
		if (ret)
			invarg("\"byte-hard\" value is invalid", *argv);
	} else if (strcmp(*argv, "packet-soft") == 0) {
		NEXT_ARG();
		ret = get_u64(&lft->soft_packet_limit, *argv, 0);
		if (ret)
			invarg("\"packet-soft\" value is invalid", *argv);
	} else if (strcmp(*argv, "packet-hard") == 0) {
		NEXT_ARG();
		ret = get_u64(&lft->hard_packet_limit, *argv, 0);
		if (ret)
			invarg("\"packet-hard\" value is invalid", *argv);
	} else
		invarg("\"LIMIT\" is invalid", *argv);

	*argcp = argc;
	*argvp = argv;

	return 0;
}

int do_xfrm(int argc, char **argv)
{
	memset(&filter, 0, sizeof(filter));

	if (argc < 1)
		usage();

	if (matches(*argv, "state") == 0 ||
	    matches(*argv, "sa") == 0) {
		return do_xfrm_state(argc-1, argv+1);
	} else if (matches(*argv, "policy") == 0)
		return do_xfrm_policy(argc-1, argv+1);
	else if (matches(*argv, "help") == 0) {
		usage();
		fprintf(stderr, "xfrm Object \"%s\" is unknown.\n", *argv);
		exit(-1);
	}
	usage();
}
