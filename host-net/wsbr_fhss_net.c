/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2021, Silicon Labs
 * Main authors:
 *     - Jérôme Pouiller <jerome.pouiller@silabs.com>
 */
/* Interface with nanostack/source/Service_Libs/fhss that is now relocated to
 * the device.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "nanostack/fhss_config.h"
#include "nanostack/fhss_api.h"

#include "host-common/spinel.h"
#include "wsbr.h"
#include "wsbr_mac.h"
#include "wsbr_fhss_net.h"

#include "host-common/log.h"

struct fhss_api *ns_fhss_ws_create(const struct fhss_ws_configuration *config,
                                   const fhss_timer_t *fhss_timer)
{
    struct wsbr_ctxt *ctxt = &g_ctxt;
    uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
    uint8_t frame[2048];
    int frame_len;

    TRACE();
    // fhss_timer is filled by wsbr_configure(). We know we know we pass -1.
    BUG_ON(fhss_timer != (fhss_timer_t *)-1);
    frame_len = spinel_datatype_pack(frame, sizeof(frame), "CiiCCSCLCCCddSC",
                                     hdr, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_WS_FHSS_CREATE,
                                     config->ws_uc_channel_function,
                                     config->ws_bc_channel_function,
                                     config->bsi,
                                     config->fhss_uc_dwell_interval,
                                     config->fhss_broadcast_interval,
                                     config->fhss_bc_dwell_interval,
                                     config->unicast_fixed_channel,
                                     config->broadcast_fixed_channel,
                                     config->channel_mask,
                                     sizeof(config->channel_mask),
                                     config->unicast_channel_mask,
                                     sizeof(config->unicast_channel_mask),
                                     config->channel_mask_size,
                                     config->config_parameters.number_of_channel_retries);
    BUG_ON(frame_len <= 0);
    ctxt->rcp_tx(ctxt->os_ctxt, frame, frame_len);
    ctxt->fhss_conf_valid = true;
    memcpy(&ctxt->fhss_conf, config, sizeof(*config));
    // Upper layers absolutly want something != NULL
    return FHSS_API_PLACEHOLDER;
}

int ns_fhss_delete(struct fhss_api *fhss_api)
{
    struct wsbr_ctxt *ctxt = &g_ctxt;
    uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
    uint8_t frame[7];
    int frame_len;

    TRACE();
    BUG_ON(fhss_api != FHSS_API_PLACEHOLDER);
    frame_len = spinel_datatype_pack(frame, sizeof(frame), "Cii",
                                     hdr, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_WS_FHSS_DELETE);
    BUG_ON(frame_len <= 0);
    ctxt->rcp_tx(ctxt->os_ctxt, frame, frame_len);
    ctxt->fhss_conf_valid = false;
    return 0;
}

const struct fhss_ws_configuration *ns_fhss_ws_configuration_get(const struct fhss_api *fhss_api)
{
    struct wsbr_ctxt *ctxt = &g_ctxt;

    BUG_ON(fhss_api != FHSS_API_PLACEHOLDER);
    if (ctxt->fhss_conf_valid)
        return &ctxt->fhss_conf;
    else
        return NULL;
}

int ns_fhss_ws_configuration_set(const struct fhss_api *fhss_api,
                                 const struct fhss_ws_configuration *config)
{
    struct wsbr_ctxt *ctxt = &g_ctxt;
    uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
    uint8_t frame[2048];
    int frame_len;

    BUG_ON(!ctxt->fhss_conf_valid);
    BUG_ON(fhss_api != FHSS_API_PLACEHOLDER);
    frame_len = spinel_datatype_pack(frame, sizeof(frame), "CiiCCSCLCCCddSC",
                                     hdr, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_WS_FHSS_SET_CONF,
                                     config->ws_uc_channel_function,
                                     config->ws_bc_channel_function,
                                     config->bsi,
                                     config->fhss_uc_dwell_interval,
                                     config->fhss_broadcast_interval,
                                     config->fhss_bc_dwell_interval,
                                     config->unicast_fixed_channel,
                                     config->broadcast_fixed_channel,
                                     config->channel_mask,
                                     sizeof(config->channel_mask),
                                     config->unicast_channel_mask,
                                     sizeof(config->unicast_channel_mask),
                                     config->channel_mask_size,
                                     config->config_parameters.number_of_channel_retries);
    BUG_ON(frame_len <= 0);
    ctxt->rcp_tx(ctxt->os_ctxt, frame, frame_len);
    memcpy(&ctxt->fhss_conf, config, sizeof(*config));
    return 0;
}

int ns_fhss_ws_set_tx_allowance_level(const fhss_api_t *fhss_api,
                                      const fhss_ws_tx_allow_level global_level,
                                      const fhss_ws_tx_allow_level ef_level)
{
    struct wsbr_ctxt *ctxt = &g_ctxt;
    uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
    uint8_t frame[15];
    int frame_len;

    BUG_ON(fhss_api != FHSS_API_PLACEHOLDER);
    frame_len = spinel_datatype_pack(frame, sizeof(frame), "Ciiii",
                                     hdr, SPINEL_CMD_PROP_VALUE_SET,
                                     SPINEL_PROP_WS_FHSS_SET_TX_ALLOWANCE_LEVEL,
                                     (int)global_level, (int)ef_level);
    BUG_ON(frame_len <= 0);
    ctxt->rcp_tx(ctxt->os_ctxt, frame, frame_len);
    return 0;
}

int ns_fhss_ws_set_parent(const struct fhss_api *fhss_api, const uint8_t eui64[8],
                          const broadcast_timing_info_t *bc_timing_info, const bool force_synch)
{
    struct wsbr_ctxt *ctxt = &g_ctxt;
    uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
    uint8_t frame[2048];
    int frame_len;

    BUG_ON(fhss_api != FHSS_API_PLACEHOLDER);
    frame_len = spinel_datatype_pack(frame, sizeof(frame), "CiiEbCCSSSLLL",
                                     hdr, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_WS_FHSS_SET_PARENT,
                                     eui64, force_synch,
                                     bc_timing_info->broadcast_channel_function,
                                     bc_timing_info->broadcast_dwell_interval,
                                     bc_timing_info->fixed_channel,
                                     bc_timing_info->broadcast_slot,
                                     bc_timing_info->broadcast_schedule_id,
                                     bc_timing_info->broadcast_interval_offset,
                                     bc_timing_info->broadcast_interval,
                                     bc_timing_info->bt_rx_timestamp);
    BUG_ON(frame_len <= 0);
    ctxt->rcp_tx(ctxt->os_ctxt, frame, frame_len);
    ctxt->fhss_conf.fhss_bc_dwell_interval = bc_timing_info->broadcast_dwell_interval;
    ctxt->fhss_conf.fhss_broadcast_interval = bc_timing_info->broadcast_interval;
    return 0;
}

void ns_fhss_ws_set_neighbor(const struct fhss_api *fhss_api, const uint8_t eui64[8],
                             fhss_ws_neighbor_timing_info_t *fhss_data)
{
    struct wsbr_ctxt *ctxt = &g_ctxt;
    uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
    uint8_t frame[2048];
    int frame_len;

    BUG_ON(fhss_api != FHSS_API_PLACEHOLDER);
    frame_len = spinel_datatype_pack(frame, sizeof(frame), "CiiECCSdCCSSLL",
                                     hdr, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_WS_FHSS_SET_NEIGHBOR,
                                     eui64, fhss_data->clock_drift, fhss_data->timing_accuracy,
                                     fhss_data->uc_channel_list.channel_count,
                                     fhss_data->uc_channel_list.channel_mask,
                                     sizeof(fhss_data->uc_channel_list.channel_mask),
                                     fhss_data->uc_timing_info.unicast_channel_function,
                                     fhss_data->uc_timing_info.unicast_dwell_interval,
                                     fhss_data->uc_timing_info.unicast_number_of_channels,
                                     fhss_data->uc_timing_info.fixed_channel,
                                     (uint32_t)fhss_data->uc_timing_info.ufsi,
                                     fhss_data->uc_timing_info.utt_rx_timestamp);
    BUG_ON(frame_len <= 0);
    ctxt->rcp_tx(ctxt->os_ctxt, frame, frame_len);
}

int ns_fhss_set_neighbor_info_fp(const struct fhss_api *fhss_api,
                                 fhss_get_neighbor_info *get_neighbor_info)
{
    TRACE();
    BUG_ON(fhss_api != FHSS_API_PLACEHOLDER);
    //BUG_ON(get_neighbor_info != ws_get_neighbor_info);
    return 0;
}

int ns_fhss_ws_set_hop_count(const struct fhss_api *fhss_api, const uint8_t hop_count)
{
    struct wsbr_ctxt *ctxt = &g_ctxt;
    uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
    uint8_t frame[2048];
    int frame_len;

    TRACE();
    BUG_ON(fhss_api != FHSS_API_PLACEHOLDER);
    frame_len = spinel_datatype_pack(frame, sizeof(frame), "CiiC",
                                     hdr, SPINEL_CMD_PROP_VALUE_SET, SPINEL_PROP_WS_FHSS_SET_HOP_COUNT,
                                     hop_count);
    BUG_ON(frame_len <= 0);
    ctxt->rcp_tx(ctxt->os_ctxt, frame, frame_len);
    return 0;
}
