/*
 * Copyright (c) 2021-2022 Silicon Laboratories Inc. (www.silabs.com)
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of the Silicon Labs Master Software License
 * Agreement (MSLA) available at [1].  This software is distributed to you in
 * Object Code format and/or Source Code format and is governed by the sections
 * of the MSLA applicable to Object Code, Source Code and Modified Open Source
 * Code. By using this software, you agree to the terms of the MSLA.
 *
 * [1]: https://www.silabs.com/about-us/legal/master-software-license-agreement
 */
#include <ifaddrs.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/route/addr.h>
#include <netlink/route/link/inet6.h>
#include <arpa/inet.h>
#include "common/log.h"
#include "stack/mac/platform/arm_hal_phy.h"
#include "stack/ethernet_mac_api.h"
#include "stack/net_interface.h"

#include "stack/source/6lowpan/lowpan_adaptation_interface.h"

#include "tun.h"
#include "wsbr.h"

static int8_t wsbr_tun_tx(uint8_t *buf, uint16_t len, uint8_t tx_handle, data_protocol_e protocol)
{
    struct wsbr_ctxt *ctxt = &g_ctxt;
    int ret;

    ret = write(ctxt->tun_fd, buf, len);
    if (ret < 0)
        WARN("write: %m");
    else if (ret != len)
        WARN("write: short write: %d < %d", ret, len);
    ctxt->tun_driver->phy_tx_done_cb(ctxt->tun_driver_id, tx_handle, PHY_LINK_TX_SUCCESS, 0, 0);
    return 0;
}

static uint8_t tun_mac[8] = { 20, 21, 22, 23, 24, 25, 26, 27 };
static struct phy_device_driver_s tun_driver = {
    /* link_type must match with ifr.ifr_flags:
     *   IFF_TAP | IFF_NO_PI -> PHY_LINK_ETHERNET_TYPE
     *   IFF_TUN | IFF_NO_PI -> PHY_LINK_SLIP
     *   IFF_TUN -> PHY_LINK_TUN
     */
    .link_type = PHY_LINK_TUN,
    .PHY_MAC = tun_mac,
    .data_request_layer = IPV6_DATAGRAMS_DATA_FLOW,
    .driver_description = (char *)"TUN BH",
    .tx = wsbr_tun_tx,
};

int get_link_local_addr(char* if_name, uint8_t ip[static 16])
{
    struct sockaddr_in6 *ipv6;
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) < 0) {
            WARN("getifaddrs: %m");
            freeifaddrs(ifaddr);
            return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr)
                continue;

            if (ifa->ifa_addr->sa_family != AF_INET6)
                continue;

            if (strcmp(ifa->ifa_name, if_name))
                continue;

            ipv6 = (struct sockaddr_in6 *)ifa->ifa_addr;

            if (!IN6_IS_ADDR_LINKLOCAL(&ipv6->sin6_addr))
                continue;

            memcpy(ip, ipv6->sin6_addr.s6_addr, 16);
            freeifaddrs(ifaddr);
            return 0;
    }

    freeifaddrs(ifaddr);
    return -2;
}

static int tun_addr_add(struct nl_sock *sock, struct rtnl_link *link, int ifindex, const uint8_t ipv6_prefix[static 8], const uint8_t hw_mac_addr[static 8])
{
    int err = 0;
    char ipv6_addr_str[128] = { };
    uint8_t ipv6_addr_buf[16] = { };
    struct rtnl_addr *ipv6_addr = NULL;
    struct nl_addr* lo_ipv6_addr = NULL;

    memcpy(ipv6_addr_buf, ipv6_prefix, 8);
    memcpy(ipv6_addr_buf+8, hw_mac_addr, 8);
    inet_ntop(AF_INET6, ipv6_addr_buf, ipv6_addr_str, INET6_ADDRSTRLEN);
    strcat(ipv6_addr_str, "/64");
    if ((err = nl_addr_parse(ipv6_addr_str, AF_INET6, &lo_ipv6_addr)) < 0) {
        nl_perror(err, "nl_addr_parse");
        return err;
    }
    ipv6_addr = rtnl_addr_alloc();
    if ((err = rtnl_addr_set_local(ipv6_addr, lo_ipv6_addr)) < 0) {
        nl_perror(err, "rtnl_addr_set_local");
        return err;
    }
    rtnl_addr_set_ifindex(ipv6_addr, ifindex);
    rtnl_addr_set_link(ipv6_addr, link);
    rtnl_addr_set_flags(ipv6_addr, IN6_ADDR_GEN_MODE_EUI64);

    if ((err = rtnl_addr_add(sock, ipv6_addr, 0)) < 0) {
        nl_perror(err, "rtnl_addr_add");
        return err;
    }
    nl_addr_put(lo_ipv6_addr);
    rtnl_addr_put(ipv6_addr);

    return err;
}

static int wsbr_tun_open(char *devname, const uint8_t hw_mac[static 8], uint8_t ipv6_prefix[static 16], bool tun_autoconf)
{
    struct rtnl_link *link;
    struct nl_sock *sock;
    struct ifreq ifr = {
        .ifr_flags = IFF_TUN,
    };
    int fd, ifindex;
    uint8_t hw_mac_slaac[8];

    memcpy(hw_mac_slaac, hw_mac, 8);
    hw_mac_slaac[0] ^= 2;

    if (devname && *devname)
        strcpy(ifr.ifr_name, devname);
    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0)
        FATAL(2, "tun open: %m");
    if (ioctl(fd, TUNSETIFF, &ifr))
        FATAL(2, "tun ioctl: %m");
    if (devname)
        strcpy(devname, ifr.ifr_name);
    sock = nl_socket_alloc();
    if (nl_connect(sock, NETLINK_ROUTE))
        FATAL(2, "nl_connect");
    if (rtnl_link_get_kernel(sock, 0, ifr.ifr_name, &link))
        FATAL(2, "rtnl_link_get_kernel %s", ifr.ifr_name);
    if (rtnl_link_get_operstate(link) != IF_OPER_UP ||
        !(rtnl_link_get_flags(link) & IFF_UP)) {
        ifindex = rtnl_link_get_ifindex(link);
        rtnl_link_put(link);
        link = rtnl_link_alloc();
        rtnl_link_set_ifindex(link, ifindex);
        if (tun_autoconf) {
            rtnl_link_inet6_set_addr_gen_mode(link, rtnl_link_inet6_str2addrgenmode("none"));
            if (rtnl_link_add(sock, link, NLM_F_CREATE))
                FATAL(2, "rtnl_link_add %s", ifr.ifr_name);
            if (tun_addr_add(sock, link, ifindex, ADDR_LINK_LOCAL_PREFIX, hw_mac_slaac))
                FATAL(2, "ip_addr_add ll");
            if (tun_addr_add(sock, link, ifindex, ipv6_prefix, hw_mac_slaac))
                FATAL(2, "ip_addr_add gua");
        }
        rtnl_link_set_operstate(link, IF_OPER_UP);
        rtnl_link_set_mtu(link, 1280);
        rtnl_link_set_flags(link, IFF_UP);
        rtnl_link_set_txqlen(link, 10);
        if (rtnl_link_add(sock, link, NLM_F_CREATE))
            FATAL(2, "rtnl_link_add %s", ifr.ifr_name);
        rtnl_link_put(link);
    } else {
        rtnl_link_put(link);
    }
    nl_socket_free(sock);
    return fd;
}

static void wsbr_tun_accept_ra(char *devname)
{
    char buf[256];
    char content;
    int fd;

    // It is also possible to use Netlink interface through DEVCONF_ACCEPT_RA
    // but this API is not mapped in libnl-route.
    snprintf(buf, sizeof(buf), "/proc/sys/net/ipv6/conf/%s/accept_ra", devname);
    fd = open(buf, O_RDONLY);
    if (fd < 0)
        FATAL(2, "open %s: %m", buf);
    if (read(fd, &content, 1) <= 0)
        FATAL(2, "read %s: %m", buf);
    close(fd);
    // Don't try to write the file if not necessary so wsrbd can launched
    // without root permissions.
    if (content != '2') {
        fd = open(buf, O_WRONLY);
        if (fd < 0)
            FATAL(2, "open %s: %m", buf);
        if (write(fd, "2", 1) <= 0)
            FATAL(2, "write %s: %m", buf);
        close(fd);
    }
}

void wsbr_tun_stack_init(struct wsbr_ctxt *ctxt)
{
    ctxt->tun_driver = &tun_driver;
    ctxt->tun_driver_id = arm_net_phy_register(ctxt->tun_driver);
    if (ctxt->tun_driver_id < 0)
        FATAL(2, "%s: arm_net_phy_register: %d", __func__, ctxt->tun_driver_id);
    ctxt->tun_mac_api = ethernet_mac_create(ctxt->tun_driver_id);
    if (!ctxt->tun_mac_api)
        FATAL(2, "%s: ethernet_mac_create", __func__);
    ctxt->tun_if_id = arm_nwk_interface_ethernet_init(ctxt->tun_mac_api, "bh0");
    if (ctxt->tun_if_id < 0)
        FATAL(2, "%s: arm_nwk_interface_ethernet_init: %d", __func__, ctxt->tun_if_id);
}

void wsbr_tun_init(struct wsbr_ctxt *ctxt)
{
    ctxt->tun_fd = wsbr_tun_open(ctxt->config.tun_dev, ctxt->hw_mac, ctxt->config.ipv6_prefix, ctxt->config.tun_autoconf);
    if (ctxt->config.tun_autoconf)
        wsbr_tun_accept_ra(ctxt->config.tun_dev);
    wsbr_tun_stack_init(ctxt);
}

void wsbr_tun_read(struct wsbr_ctxt *ctxt)
{
    uint8_t buf[1504]; // Max ethernet frame size + TUN header
    int len;

    if (lowpan_adaptation_queue_size(ctxt->rcp_if_id) > 2)
        return;
    len = read(ctxt->tun_fd, buf, sizeof(buf));
    ctxt->tun_driver->phy_rx_cb(buf, len, 0x80, 0, ctxt->tun_driver_id);
}

void wsbr_spinel_replay_tun(struct spinel_buffer *buf)
{
    WARN("%s: not implemented", __func__);
}
