/* main.c - Application main entry point */

/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(net_test, CONFIG_NET_ICMPV6_LOG_LEVEL);

#include <errno.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <misc/printk.h>
#include <linker/sections.h>

#include <tc_util.h>

#include <net/buf.h>

#include "icmpv6.h"
#include <ztest.h>

static int handler_called;
static int handler_status;

#define TEST_MSG "foobar devnull"

#define ICMPV6_MSG_SIZE 105

static char icmpv6_echo_req[] =
	"\x60\x02\xea\x12\x00\x40\x3a\x40\xfe\x80\x00\x00\x00\x00\x00\x00" \
	"\xda\xcb\x8a\xff\xfe\x34\xc8\xf3\xfe\x80\x00\x00\x00\x00\x00\x00" \
	"\xec\x88\x2d\x63\xfd\x67\x31\x66\x80\x00\xa4\x24\x0b\x95\x00\x01" \
	"\x97\x78\x0f\x5c\x00\x00\x00\x00\xf7\x72\x00\x00\x00\x00\x00\x00" \
	"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f" \
	"\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f" \
	"\x30\x31\x32\x33\x34\x35\x36\x37";

static char icmpv6_echo_rep[] =
	"\x60\x09\x23\xa0\x00\x40\x3a\x40\xfe\x80\x00\x00\x00\x00\x00\x00" \
	"\xec\x88\x2d\x63\xfd\x67\x31\x66\xfe\x80\x00\x00\x00\x00\x00\x00" \
	"\xda\xcb\x8a\xff\xfe\x34\xc8\xf3\x81\x00\xa3\x24\x0b\x95\x00\x01" \
	"\x97\x78\x0f\x5c\x00\x00\x00\x00\xf7\x72\x00\x00\x00\x00\x00\x00" \
	"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f" \
	"\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f" \
	"\x30\x31\x32\x33\x34\x35\x36\x37";

static char icmpv6_inval_chksum[] =
	"\x60\x09\x23\xa0\x00\x40\x3a\x40\xfe\x80\x00\x00\x00\x00\x00\x00" \
	"\xec\x88\x2d\x63\xfd\x67\x31\x66\xfe\x80\x00\x00\x00\x00\x00\x00" \
	"\xda\xcb\x8a\xff\xfe\x34\xc8\xf3\x00\x00\xa3\x24\x0b\x95\x00\x01" \
	"\x97\x78\x0f\x5c\x00\x00\x00\x00\xf7\x72\x00\x00\x00\x00\x00\x00" \
	"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f" \
	"\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f" \
	"\x30\x31\x32\x33\x34\x35\x36\x37";

NET_PKT_TX_SLAB_DEFINE(pkts_slab, 2);
NET_BUF_POOL_DEFINE(data_pool, 2, 128, 0, NULL);

static enum net_verdict handle_test_msg(struct net_pkt *pkt)
{
	struct net_buf *last = net_buf_frag_last(pkt->frags);
	enum net_verdict ret;

	if (last->len != ICMPV6_MSG_SIZE) {
		handler_status = -EINVAL;
		ret = NET_DROP;
	} else {
		handler_status = 0;
		ret = NET_OK;
	}

	handler_called++;

	return ret;
}

static struct net_icmpv6_handler test_handler1 = {
	.type = NET_ICMPV6_ECHO_REPLY,
	.code = 0,
	.handler = handle_test_msg,
};

static struct net_icmpv6_handler test_handler2 = {
	.type = NET_ICMPV6_ECHO_REQUEST,
	.code = 0,
	.handler = handle_test_msg,
};

void test_icmpv6(void)
{
	k_thread_priority_set(k_current_get(), K_PRIO_COOP(7));

	struct net_pkt *pkt;
	struct net_buf *frag;
	int ret;

	net_icmpv6_register_handler(&test_handler1);
	net_icmpv6_register_handler(&test_handler2);

	pkt = net_pkt_get_reserve(&pkts_slab, K_SECONDS(1));
	zassert_true(pkt != NULL, "Could get net_pkt from slab");

	frag = net_buf_alloc(&data_pool, K_SECONDS(1));
	zassert_true(frag != NULL, "Could not allocate buffer from pool");

	net_pkt_frag_add(pkt, frag);

	net_pkt_set_family(pkt, AF_INET6);
	net_pkt_set_ip_hdr_len(pkt, sizeof(struct net_ipv6_hdr));

	memcpy(net_buf_add(frag, sizeof(icmpv6_inval_chksum)),
	       icmpv6_inval_chksum, sizeof(icmpv6_inval_chksum));

	ret = net_icmpv6_input(pkt);

	/**TESTPOINT: Check input*/
	zassert_true(ret == NET_DROP, "Callback not called properly");

	handler_status = -1;

	frag->len = 0;
	memcpy(net_buf_add(frag, sizeof(icmpv6_echo_rep)),
	       icmpv6_echo_rep, sizeof(icmpv6_echo_rep));

	ret = net_icmpv6_input(pkt);

	/**TESTPOINT: Check input*/
	zassert_true(!(ret == NET_DROP || handler_status != 0),
		     "Callback not called properly");

	handler_status = -1;
	frag->len = 0;

	memcpy(net_buf_add(frag, sizeof(icmpv6_echo_req)),
	       icmpv6_echo_req, sizeof(icmpv6_echo_req));

	ret = net_icmpv6_input(pkt);

	/**TESTPOINT: Check input*/
	zassert_true(!(ret == NET_DROP || handler_status != 0),
			"Callback not called properly");

	/**TESTPOINT: Check input*/
	zassert_true(!(handler_called != 2), "Callbacks not called properly");
}

/**test case main entry*/
void test_main(void)
{
	ztest_test_suite(test_icmpv6_fn,
		ztest_unit_test(test_icmpv6));
	ztest_run_test_suite(test_icmpv6_fn);
}
