/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2021, Silicon Labs
 * Main authors:
 *     - Jérôme Pouiller <jerome.pouiller@silabs.com>
 */
#include "nanostack/mlme.h"

#include "bus_uart.h"
#include "wsmac_mac.h"
#include "wsmac.h"
#include "spinel.h"
#include "utils.h"
#include "log.h"

static uint8_t wsbr_get_spinel_hdr(struct wsmac_ctxt *ctxt)
{
    uint8_t hdr = FIELD_PREP(0xC0, 0x2) | FIELD_PREP(0x30, ctxt->spinel_iid);

    ctxt->spinel_tid = (ctxt->spinel_tid + 1) % 0x10;
    if (!ctxt->spinel_tid)
        ctxt->spinel_tid = 1;
    hdr |= FIELD_PREP(0x0F, ctxt->spinel_tid);
    return hdr;
}

static void wsmac_spinel_set_bool(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    bool data;
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = &data,
        .value_size = sizeof(data),
    };

    BUG_ON(frame_len != sizeof(data));
    spinel_datatype_unpack(frame, frame_len, "b", &data);
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_u8(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    uint8_t data;
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = &data,
        .value_size = sizeof(data),
    };

    BUG_ON(frame_len != sizeof(data));
    spinel_datatype_unpack(frame, frame_len, "C", &data);
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_u16(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    uint16_t data;
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = &data,
        .value_size = sizeof(data),
    };

    BUG_ON(frame_len != sizeof(data));
    spinel_datatype_unpack(frame, frame_len, "S", &data);
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_u32(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    uint32_t data;
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = &data,
        .value_size = sizeof(data),
    };

    BUG_ON(frame_len != sizeof(data));
    spinel_datatype_unpack(frame, frame_len, "L", &data);
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_eui64(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    mlme_set_t req = {
        .attr = attr,
        .value_size = 8,
    };

    BUG_ON(frame_len != 8);
    spinel_datatype_unpack(frame, frame_len, "E", &req.value_pointer);
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_data(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = frame,
        .value_size = frame_len,
    };

    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_cca_threshold(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    int data_len;
    mlme_set_t req = {
        .attr = attr,
    };

    BUG_ON(frame_len != sizeof(uint8_t));
    spinel_datatype_unpack(frame, frame_len, "d", &req.value_pointer, &data_len);
    req.value_size = data_len;
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_cca_threshold_start(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    uint8_t data[4];
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = data,
        .value_size = sizeof(data),
    };

    spinel_datatype_unpack(frame, frame_len, "CCCC", &data[0], &data[1], &data[2], &data[3]);
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_multi_csma_parameters(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    struct mlme_multi_csma_ca_s data;
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = &data,
        .value_size = sizeof(data),
    };

    spinel_datatype_unpack(frame, frame_len, "CS",
                           &data.number_of_csma_ca_periods,
                           &data.multi_cca_interval);
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_rf_configuration(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    struct phy_rf_channel_configuration_s data;
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = &data,
        .value_size = sizeof(data),
    };
    uint8_t tmp[2];

    spinel_datatype_unpack(frame, frame_len, "LLLSCC",
                           &data.channel_0_center_frequency,
                           &data.channel_spacing, &data.datarate,
                           &data.number_of_channels, &tmp[0],
                           &tmp[1]);
    data.modulation = tmp[0];
    data.modulation_index = tmp[1];
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_device_table(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    struct mlme_device_descriptor_s data;
    bool exempt;
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = &data,
        .value_size = sizeof(data),
    };
    int ret;

    ret = spinel_datatype_unpack_in_place(frame, frame_len, "CSSELb",
                           &req.attr_index, &data.PANId, &data.ShortAddress,
                           data.ExtAddress, &data.FrameCounter,
                           &exempt);
    BUG_ON(ret != frame_len);
    data.Exempt = exempt;
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_key_table(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    mlme_key_id_lookup_descriptor_t descr = { };
    mlme_key_descriptor_entry_t data = {
        .KeyIdLookupList = &descr,
    };
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = &data,
        .value_size = sizeof(data),
    };
    int len_key = sizeof(data.Key);
    int len_data = sizeof(data.KeyIdLookupList->LookupData);
    int ret;

    BUG_ON(attr != macKeyTable);
    ret = spinel_datatype_unpack_in_place(frame, frame_len, "Cdd", &req.attr_index,
                                   data.Key, &len_key,
                                   data.KeyIdLookupList->LookupData, &len_data);
    BUG_ON(ret != frame_len);
    BUG_ON(len_key != sizeof(data.Key));
    if (len_data) {
        data.KeyIdLookupListEntries = 1;
        if (len_data == 9)
            data.KeyIdLookupList->LookupDataSize = 1;
        else
            BUG_ON(len_data != 5);
    }
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_set_frame_counter(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    uint32_t data;
    mlme_set_t req = {
        .attr = attr,
        .value_pointer = &data,
        .value_size = sizeof(data),
    };
    int ret;

    ret = spinel_datatype_unpack(frame, frame_len, "iL",
                           &req.attr_index, req.value_pointer);
    BUG_ON(ret != frame_len);
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_SET, &req);
}

static void wsmac_spinel_start(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    mlme_start_t req = { };
    uint8_t tmp8[2];
    bool tmpB;
    int ret;

    ret = spinel_datatype_unpack(frame, frame_len, "SCCLCCb",
                           &req.PANId, &req.LogicalChannel, &req.ChannelPage,
                           &req.StartTime, &tmp8[0], &tmp8[1], &tmpB);
    BUG_ON(ret != frame_len);
    req.BeaconOrder = tmp8[0];
    req.SuperframeOrder = tmp8[1];
    req.PANCoordinator = tmpB;
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_START, &req);
}

static void wsmac_spinel_reset(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len)
{
    mlme_reset_t req = { };

    spinel_datatype_unpack(frame, frame_len, "b", &req.SetDefaultPIB);
    ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_RESET, &req);
}

static const struct {
    const char *str;
    mlme_attr_t attr;
    void (*prop_set)(struct wsmac_ctxt *ctxt, mlme_attr_t attr, const void *frame, int frame_len);
    unsigned int prop;
} mlme_prop_cstr[] = {
    { "macRxOnWhenIdle",                 macRxOnWhenIdle,                 wsmac_spinel_set_bool,                  SPINEL_PROP_WS_RX_ON_WHEN_IDLE,                  },
    { "macSecurityEnabled",              macSecurityEnabled,              wsmac_spinel_set_bool,                  SPINEL_PROP_WS_SECURITY_ENABLED,                 },
    { "macAcceptByPassUnknowDevice",     macAcceptByPassUnknowDevice,     wsmac_spinel_set_bool,                  SPINEL_PROP_WS_ACCEPT_BYPASS_UNKNOW_DEVICE,      },
    { "macEdfeForceStop",                macEdfeForceStop,                wsmac_spinel_set_bool,                  SPINEL_PROP_WS_EDFE_FORCE_STOP,                  },
    { "macAssociationPermit",            macAssociationPermit,            wsmac_spinel_set_bool,                  SPINEL_PROP_WS_ASSOCIATION_PERMIT,               },
    { "phyCurrentChannel",               phyCurrentChannel,               wsmac_spinel_set_u8,                    SPINEL_PROP_PHY_CHAN,                            },
    { "macAutoRequestKeyIdMode",         macAutoRequestKeyIdMode,         wsmac_spinel_set_u8,                    SPINEL_PROP_WS_AUTO_REQUEST_KEY_ID_MODE,         },
    { "macAutoRequestKeyIndex",          macAutoRequestKeyIndex,          wsmac_spinel_set_u8,                    SPINEL_PROP_WS_AUTO_REQUEST_KEY_INDEX,           },
    { "macAutoRequestSecurityLevel",     macAutoRequestSecurityLevel,     wsmac_spinel_set_u8,                    SPINEL_PROP_WS_AUTO_REQUEST_SECURITY_LEVEL,      },
    { "macBeaconPayloadLength",          macBeaconPayloadLength,          wsmac_spinel_set_u8,                    SPINEL_PROP_WS_BEACON_PAYLOAD_LENGTH,            },
    { "macMaxFrameRetries",              macMaxFrameRetries,              wsmac_spinel_set_u8,                    SPINEL_PROP_WS_MAX_FRAME_RETRIES,                },
    { "macTXPower",                      macTXPower,                      wsmac_spinel_set_u8,                    SPINEL_PROP_PHY_TX_POWER,                        },
    { "macPANId",                        macPANId,                        wsmac_spinel_set_u16,                   SPINEL_PROP_MAC_15_4_PANID,                      },
    { "macCoordShortAddress",            macCoordShortAddress,            wsmac_spinel_set_u16,                   SPINEL_PROP_WS_COORD_SHORT_ADDRESS,              },
    { "macShortAddress",                 macShortAddress,                 wsmac_spinel_set_u16,                   SPINEL_PROP_MAC_15_4_SADDR,                      },
    { "macDeviceDescriptionPanIDUpdate", macDeviceDescriptionPanIDUpdate, wsmac_spinel_set_u16,                   SPINEL_PROP_WS_DEVICE_DESCRIPTION_PAN_ID_UPDATE, },
    { "macAckWaitDuration",              macAckWaitDuration,              wsmac_spinel_set_u16,                   SPINEL_PROP_WS_ACK_WAIT_DURATION,                },
    { "mac802_15_4Mode",                 mac802_15_4Mode,                 wsmac_spinel_set_u32,                   SPINEL_PROP_WS_15_4_MODE,                        },
    { "macAutoRequestKeySource",         macAutoRequestKeySource,         wsmac_spinel_set_eui64,                 SPINEL_PROP_WS_AUTO_REQUEST_KEY_SOURCE,          },
    { "macCoordExtendedAddress",         macCoordExtendedAddress,         wsmac_spinel_set_eui64,                 SPINEL_PROP_WS_COORD_EXTENDED_ADDRESS,           },
    { "macDefaultKeySource",             macDefaultKeySource,             wsmac_spinel_set_eui64,                 SPINEL_PROP_WS_DEFAULT_KEY_SOURCE,               },
    { "macBeaconPayload",                macBeaconPayload,                wsmac_spinel_set_data,                  SPINEL_PROP_WS_BEACON_PAYLOAD,                   },
    { "macCCAThreshold",                 macCCAThreshold,                 wsmac_spinel_set_cca_threshold,         SPINEL_PROP_WS_CCA_THRESHOLD,                    },
    { "macCCAThresholdStart",            macCCAThresholdStart,            wsmac_spinel_set_cca_threshold_start,   SPINEL_PROP_WS_CCA_THRESHOLD_START,              },
    { "macMultiCSMAParameters",          macMultiCSMAParameters,          wsmac_spinel_set_multi_csma_parameters, SPINEL_PROP_WS_MULTI_CSMA_PARAMETERS,            },
    { "macRfConfiguration",              macRfConfiguration,              wsmac_spinel_set_rf_configuration,      SPINEL_PROP_WS_RF_CONFIGURATION,                 },
    { "macDeviceTable",                  macDeviceTable,                  wsmac_spinel_set_device_table,          SPINEL_PROP_WS_DEVICE_TABLE,                     },
    { "macKeyTable",                     macKeyTable,                     wsmac_spinel_set_key_table,             SPINEL_PROP_WS_KEY_TABLE,                        },
    { "macFrameCounter",                 macFrameCounter,                 wsmac_spinel_set_frame_counter,         SPINEL_PROP_WS_FRAME_COUNTER,                    },
    { "mlmeStart",                       0 /* Special */,                 wsmac_spinel_start,                     SPINEL_PROP_WS_START,                            },
    { "mlmeReset",                       0 /* Special */,                 wsmac_spinel_reset,                     SPINEL_PROP_WS_RESET,                            },
    { }
};

void uart_rx(struct wsmac_ctxt *ctxt)
{
    uint8_t hdr;
    int cmd, prop;
    uint8_t buf[256];
    uint8_t *data;
    int len, data_len;
    int i;

    len = wsbr_uart_rx(ctxt->os_ctxt, buf, sizeof(buf));
    spinel_datatype_unpack(buf, len, "CiiD", &hdr, &cmd, &prop, &data, &data_len);
    for (i = 0; mlme_prop_cstr[i].prop; i++)
        if (prop == mlme_prop_cstr[i].prop)
            break;

    if (cmd == SPINEL_CMD_PROP_VALUE_GET) {
        int index;
        spinel_datatype_unpack(data, data_len, "i", &index);
        mlme_get_t req = {
            .attr_index = index,
            .attr = mlme_prop_cstr[i].attr,
        };
        TRACE("get %s", mlme_prop_cstr[i].str);
        ctxt->rcp_mac_api->mlme_req(ctxt->rcp_mac_api, MLME_GET, &req);
    } else if (cmd == SPINEL_CMD_PROP_VALUE_SET) {
        TRACE("set %s", mlme_prop_cstr[i].str);
        if (mlme_prop_cstr[i].prop_set)
            mlme_prop_cstr[i].prop_set(ctxt, mlme_prop_cstr[i].attr, data, data_len);
    } else {
        WARN("not implemented");
        return;
    }
}

void wsmac_mlme_get(struct wsmac_ctxt *ctxt, const void *data)
{
    const mlme_get_conf_t *req = data;

    TRACE("mlmeGet");
    switch (req->attr) {
    case macDeviceTable: {
        uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
        const mlme_device_descriptor_t *req2 = req->value_pointer;
        uint8_t frame[1 + 3 + 3 + 3 + sizeof(uint8_t)];
        int frame_len;

        frame_len = spinel_datatype_pack(frame, sizeof(frame), "CiiiSSELb",
                                         hdr, SPINEL_CMD_PROP_VALUE_IS,
                                         SPINEL_PROP_WS_DEVICE_TABLE,
                                         req->attr_index, req2->PANId,
                                         req2->ShortAddress, req2->ExtAddress,
                                         req2->FrameCounter, req2->Exempt);
        wsbr_uart_tx(ctxt->os_ctxt, frame, frame_len);
        break;
    }
    case macFrameCounter: {
        uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
        uint8_t frame[1 + 3 + 3 + 3 + sizeof(uint8_t)];
        int frame_len;

        BUG_ON(req->value_size != sizeof(uint32_t));
        //BUG_ON(req->attr_index != XXXsecurity_frame_counter);
        frame_len = spinel_datatype_pack(frame, sizeof(frame), "CiiiL", hdr,
                                         SPINEL_CMD_PROP_VALUE_IS,
                                         SPINEL_PROP_WS_FRAME_COUNTER,
                                         req->attr_index,
                                         *((uint32_t *)req->value_pointer));
        wsbr_uart_tx(ctxt->os_ctxt, frame, frame_len);
        break;
    }
    case macCCAThreshold: {
        uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
        uint8_t frame[1 + 3 + 3 + 50 * sizeof(uint8_t)];
        int frame_len;

        BUG_ON(req->value_size > 50);
        frame_len = spinel_datatype_pack(frame, sizeof(frame), "Ciid", hdr,
                                         SPINEL_CMD_PROP_VALUE_IS,
                                         SPINEL_PROP_WS_CCA_THRESHOLD,
                                         req->value_pointer, req->value_size);
        wsbr_uart_tx(ctxt->os_ctxt, frame, frame_len);
        break;
    }
    default:
        WARN("not implemented");
        break;
    }
}

void wsmac_mlme_start(struct wsmac_ctxt *ctxt, const void *data)
{
    const mlme_start_conf_t *req = data;
    uint8_t hdr = wsbr_get_spinel_hdr(ctxt);
    uint8_t frame[1 + 3 + 3 + 3];
    int frame_len;

    TRACE("mlmeStart");
    WARN_ON(req->status);
    frame_len = spinel_datatype_pack(frame, sizeof(frame), "Ciii", hdr,
                                     SPINEL_CMD_PROP_VALUE_IS,
                                     SPINEL_PROP_LAST_STATUS,
                                     SPINEL_STATUS_OK);
    wsbr_uart_tx(ctxt->os_ctxt, frame, frame_len);
}

void wsmac_mlme_scan(struct wsmac_ctxt *ctxt, const void *data)
{
    //const mlme_scan_conf_t *req = data;

    WARN("not implemented");
}

void wsmac_mlme_confirm(const mac_api_t *api, mlme_primitive id, const void *data)
{
    struct wsmac_ctxt *ctxt = &g_ctxt;
    static const struct {
        uint32_t id;
        void (*fn)(struct wsmac_ctxt *, const void *);
    } table[] = {
        { MLME_GET,   wsmac_mlme_get },
        { MLME_START, wsmac_mlme_start },
        { MLME_SCAN,  wsmac_mlme_scan },
        { -1 },
    };
    int i;

    BUG_ON(!api);
    BUG_ON(ctxt->rcp_mac_api != api);
    for (i = 0; table[i].id != -1; i++)
        if (id == table[i].id)
            break;
    if (!table[i].fn) {
        WARN("not implemented");
        return;
    }
    table[i].fn(ctxt, data);
}

void wsmac_mcps_data_confirm(const mac_api_t *mac_api, const mcps_data_conf_t *data)
{
    WARN("not implemented");
}

void wsmac_mcps_data_indication(const mac_api_t *mac_api, const mcps_data_ind_t *data)
{
    WARN("not implemented");
}

void wsmac_mcps_purge_confirm(const mac_api_t *mac_api, mcps_purge_conf_t *data)
{
    WARN("not implemented");
}

void wsmac_mlme_indication(const mac_api_t *mac_api, mlme_primitive id, const void *data)
{
    WARN("not implemented");
}

void wsmac_mcps_data_confirm_ext(const mac_api_t *mac_api, const mcps_data_conf_t *data,
                                 const mcps_data_conf_payload_t *conf_data)
{
    WARN("not implemented");
}

void wsmac_mcps_data_indication_ext(const mac_api_t *mac_api, const mcps_data_ind_t *data,
                                    const mcps_data_ie_list_t *ie_ext)
{
    WARN("not implemented");
}

void wsmac_mcps_ack_data_req_ext(const mac_api_t *mac_api, mcps_ack_data_payload_t *data,
                                 int8_t rssi, uint8_t lqi)
{
    WARN("not implemented");
}

void wsmac_mcps_edfe_handler(const mac_api_t *mac_api, mcps_edfe_response_t *response_message)
{
    WARN("not implemented");
}