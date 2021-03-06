/* Iperf Example - wifi commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "tcpip_adapter.h"
#include "esp_event_loop.h"
#include "iperf.h"

#include "lwip/stats.h"

typedef struct {
    struct arg_str *ip;
    struct arg_lit *server;
    struct arg_lit *udp;
    struct arg_int *port;
    struct arg_int *interval;
    struct arg_int *time;
    struct arg_lit *abort;
    struct arg_end *end;
} wifi_iperf_t;
static wifi_iperf_t iperf_args;

typedef struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_args_t;
static wifi_args_t sta_args;
static wifi_args_t ap_args;

typedef struct {
    struct arg_str *ssid;
    struct arg_end *end;
} wifi_scan_arg_t;
static wifi_scan_arg_t scan_args;

typedef struct {
    struct arg_str *hostname;
    struct arg_end *end;
} wifi_hostname_arg_t;
static wifi_hostname_arg_t hostname_args;

static bool reconnect = true;
static const char *TAG="cmd_wifi";

static char Wifi_hostname[TCPIP_HOSTNAME_MAX_SIZE+1] = "NOT_SET";

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;

static void scan_done_handler(void)
{
    uint16_t sta_number = 0;
    uint8_t i;
    wifi_ap_record_t *ap_list_buffer;

    esp_wifi_scan_get_ap_num(&sta_number);
    ap_list_buffer = malloc(sta_number * sizeof(wifi_ap_record_t));
    if (ap_list_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to malloc buffer to print scan results");
        return;
    }

    if (esp_wifi_scan_get_ap_records(&sta_number,(wifi_ap_record_t *)ap_list_buffer) == ESP_OK) {
        for(i=0; i<sta_number; i++) {
            ESP_LOGI(TAG, "[%s][rssi=%d]", ap_list_buffer[i].ssid, ap_list_buffer[i].rssi);
        }
    }
    free(ap_list_buffer);
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_SCAN_DONE:
            scan_done_handler();
            ESP_LOGI(TAG, "sta scan done");
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            if (reconnect) {
                ESP_LOGI(TAG, "sta disconnect, reconnect...");
                esp_wifi_connect();
            } else {
                ESP_LOGI(TAG, "sta disconnect");
            }
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            xEventGroupSetBits(wifi_event_group, DISCONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_START:
            ESP_ERROR_CHECK( tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, Wifi_hostname) );
            break;
        case SYSTEM_EVENT_AP_START: 
            ESP_ERROR_CHECK( tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_AP, Wifi_hostname) );
            break;
        default:
            break;
    }
    return ESP_OK;
}

void initialise_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static bool initialized = false;

    if (initialized) {
        return;
    }

    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    initialized = true;

    static uint8_t base_macaddr[6] = {0xf0, 0xda, 0x5e, 0x24, 0x24, 0x01}; ESP_ERROR_CHECK( esp_efuse_mac_get_default(base_macaddr) );

    snprintf(Wifi_hostname, sizeof(Wifi_hostname), "ESP-%02X%02X%02X", base_macaddr[3], base_macaddr[4], base_macaddr[5]); //default hostname, same as in esp8266-arduino 
    ESP_LOGI(TAG, "initialise_wifi(): set default hostname to '%s'", Wifi_hostname);
}

static bool wifi_cmd_sta_join(const char* ssid, const char* pass)
{
    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);

    wifi_config_t wifi_config = { 0 };

    strlcpy((char*) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strlcpy((char*) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    if (bits & CONNECTED_BIT) {
        reconnect = false;
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        xEventGroupWaitBits(wifi_event_group, DISCONNECTED_BIT, 0, 1, portTICK_RATE_MS);
    }

    reconnect = true;
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 5000/portTICK_RATE_MS);

    return true;
}

static int wifi_cmd_sta(int argc, char** argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &sta_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, sta_args.end, argv[0]);
        return 1;
    }

    ESP_LOGI(TAG, "sta connecting to '%s'", sta_args.ssid->sval[0]);
    wifi_cmd_sta_join(sta_args.ssid->sval[0], sta_args.password->sval[0]);
    return 0;
}

static bool wifi_cmd_sta_scan(const char* ssid)
{
    wifi_scan_config_t scan_config = { 0 };
    scan_config.ssid = (uint8_t *) ssid;

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_scan_start(&scan_config, false) );

    return true;
}

static int wifi_cmd_scan(int argc, char** argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &scan_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, scan_args.end, argv[0]);
        return 1;
    }

    ESP_LOGI(TAG, "sta start to scan");
    if ( scan_args.ssid->count == 1 ) {
        wifi_cmd_sta_scan(scan_args.ssid->sval[0]);
    } else {
        wifi_cmd_sta_scan(NULL);
    }
    return 0;
}


static bool wifi_cmd_ap_set(const char* ssid, const char* pass)
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "",
            .ssid_len = 0,
            .max_connection = 4,
            .password = "",
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    reconnect = false;
    strlcpy((char*) wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
    if (pass) {
        if (strlen(pass) != 0 && strlen(pass) < 8) {
            reconnect = true;
            ESP_LOGE(TAG, "password less than 8");
            return false;
        }
        strlcpy((char*) wifi_config.ap.password, pass, sizeof(wifi_config.ap.password));
    }

    if (strlen(pass) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config) );
    return true;
}

static int wifi_cmd_ap(int argc, char** argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &ap_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, ap_args.end, argv[0]);
        return 1;
    }

    wifi_cmd_ap_set(ap_args.ssid->sval[0], ap_args.password->sval[0]);
    ESP_LOGI(TAG, "AP mode, %s %s", ap_args.ssid->sval[0], ap_args.password->sval[0]);
    return 0;
}

static int wifi_cmd_query(int argc, char** argv)
{
    wifi_config_t cfg;
    wifi_mode_t mode;

    esp_wifi_get_mode(&mode);
    if (WIFI_MODE_AP == mode) {
        esp_wifi_get_config(WIFI_IF_AP, &cfg);
        ESP_LOGI(TAG, "AP mode, %s %s", cfg.ap.ssid, cfg.ap.password);
    } else if (WIFI_MODE_STA == mode) {
        int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
        if (bits & CONNECTED_BIT) {
            esp_wifi_get_config(WIFI_IF_STA, &cfg);
            ESP_LOGI(TAG, "sta mode, connected %s", cfg.ap.ssid);
        } else {
            ESP_LOGI(TAG, "sta mode, disconnected");
        }
    } else {
        ESP_LOGI(TAG, "NULL mode");
        return 0;
    }

    return 0;
}

static uint32_t wifi_get_local_ip(void)
{
    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
    tcpip_adapter_if_t ifx = TCPIP_ADAPTER_IF_AP;
    tcpip_adapter_ip_info_t ip_info;
    wifi_mode_t mode;

    esp_wifi_get_mode(&mode);
    if (WIFI_MODE_STA == mode) {
        bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
        if (bits & CONNECTED_BIT) {
            ifx = TCPIP_ADAPTER_IF_STA;
        } else {
            ESP_LOGE(TAG, "sta has no IP");
            return 0;
        }
     }

     tcpip_adapter_get_ip_info(ifx, &ip_info);
     return ip_info.ip.addr;
}

static int wifi_cmd_iperf(int argc, char** argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &iperf_args);
    iperf_cfg_t cfg;

    if (nerrors != 0) {
        arg_print_errors(stderr, iperf_args.end, argv[0]);
        return 0;
    }

    memset(&cfg, 0, sizeof(cfg));

    if ( iperf_args.abort->count != 0) {
        iperf_stop();
        return 0;
    }

    if ( ((iperf_args.ip->count == 0) && (iperf_args.server->count == 0)) ||
         ((iperf_args.ip->count != 0) && (iperf_args.server->count != 0)) ) {
        ESP_LOGE(TAG, "should specific client/server mode");
        return 0;
    }

    if (iperf_args.ip->count == 0) {
        cfg.flag |= IPERF_FLAG_SERVER;
    } else {
        cfg.dip = ipaddr_addr(iperf_args.ip->sval[0]);
        cfg.flag |= IPERF_FLAG_CLIENT;
    }

    cfg.sip = wifi_get_local_ip();
    if (cfg.sip == 0) {
        return 0;
    }

    if (iperf_args.udp->count == 0) {
        cfg.flag |= IPERF_FLAG_TCP;
    } else {
        cfg.flag |= IPERF_FLAG_UDP;
    }

    if (iperf_args.port->count == 0) {
        cfg.sport = IPERF_DEFAULT_PORT;
        cfg.dport = IPERF_DEFAULT_PORT;
    } else {
        if (cfg.flag & IPERF_FLAG_SERVER) {
            cfg.sport = iperf_args.port->ival[0];
            cfg.dport = IPERF_DEFAULT_PORT;
        } else {
            cfg.sport = IPERF_DEFAULT_PORT;
            cfg.dport = iperf_args.port->ival[0];
        }
    }

    if (iperf_args.interval->count == 0) {
        cfg.interval = IPERF_DEFAULT_INTERVAL;
    } else {
        cfg.interval = iperf_args.interval->ival[0];
        if (cfg.interval <= 0) {
            cfg.interval = IPERF_DEFAULT_INTERVAL;
        }
    }

    if (iperf_args.time->count == 0) {
        cfg.time = IPERF_DEFAULT_TIME;
    } else {
        cfg.time = iperf_args.time->ival[0];
        if (cfg.time <= cfg.interval) {
            cfg.time = cfg.interval;
        }
    }

    ESP_LOGI(TAG, "mode=%s-%s sip=%d.%d.%d.%d:%d, dip=%d.%d.%d.%d:%d, interval=%d, time=%d",
            cfg.flag&IPERF_FLAG_TCP?"tcp":"udp",
            cfg.flag&IPERF_FLAG_SERVER?"server":"client",
            cfg.sip&0xFF, (cfg.sip>>8)&0xFF, (cfg.sip>>16)&0xFF, (cfg.sip>>24)&0xFF, cfg.sport,
            cfg.dip&0xFF, (cfg.dip>>8)&0xFF, (cfg.dip>>16)&0xFF, (cfg.dip>>24)&0xFF, cfg.dport,
            cfg.interval, cfg.time);

    iperf_start(&cfg);

    return 0;
}

static esp_err_t wifi_cmd_hostname(int argc, char **argv)
{
    if (argc == 1) {
        ESP_LOGI(TAG, "fn_autorun_cmd_hostname(): current hostname is '%s'", Wifi_hostname);
        return ESP_OK;
    }

    int nerrors = arg_parse(argc, argv, (void **) &hostname_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, hostname_args.end, argv[0]);
        return 1;
    }

    strlcpy(Wifi_hostname, hostname_args.hostname->sval[0], sizeof(Wifi_hostname));

    ESP_LOGI(TAG, "wifi_cmd_hostname(): this node's hostname set to '%s'.", hostname_args.hostname->sval[0]);

    return ESP_OK;
}

static int wifi_cmd_stats(int argc, char** argv)
{
    ESP_LOGI(TAG, "wifi_cmd_stats(): Wifi adapter network stats follow");

    // link-level statistics
    printf("link.{xmit=%d, recv=%d, fw=%d, drop=%d, chkerr=%d, lenerr=%d, memerr=%d, rterr=%d, proterr=%d, opterr=%d, err=%d}\n",
        lwip_stats.link.xmit, lwip_stats.link.recv, lwip_stats.link.fw, lwip_stats.link.drop, lwip_stats.link.chkerr, lwip_stats.link.lenerr,
        lwip_stats.link.memerr, lwip_stats.link.rterr, lwip_stats.link.proterr, lwip_stats.link.opterr, lwip_stats.link.err);

    // IP fragmentation statistics
#if IPFRAG_STATS // ip_frag stats are only available in certain circunstances (right now, if either IP_REASSEMBLY or IP_FRAG are set), so only proceed if they are
    printf("ip_frag.{xmit=%d, recv=%d, fw=%d, drop=%d, chkerr=%d, lenerr=%d, memerr=%d, rterr=%d, proterr=%d, opterr=%d, err=%d}\n",
        lwip_stats.ip_frag.xmit, lwip_stats.ip_frag.recv, lwip_stats.ip_frag.fw, lwip_stats.ip_frag.drop, lwip_stats.ip_frag.chkerr, lwip_stats.ip_frag.lenerr,
        lwip_stats.ip_frag.memerr, lwip_stats.ip_frag.rterr, lwip_stats.ip_frag.proterr, lwip_stats.ip_frag.opterr, lwip_stats.ip_frag.err);
#endif

    // IP-level statistics
    printf("ip.{xmit=%d, recv=%d, fw=%d, drop=%d, chkerr=%d, lenerr=%d, memerr=%d, rterr=%d, proterr=%d, opterr=%d, err=%d}\n",
        lwip_stats.ip.xmit, lwip_stats.ip.recv, lwip_stats.ip.fw, lwip_stats.ip.drop, lwip_stats.ip.chkerr, lwip_stats.ip.lenerr,
        lwip_stats.ip.memerr, lwip_stats.ip.rterr, lwip_stats.ip.proterr, lwip_stats.ip.opterr, lwip_stats.ip.err);

    // UDP protocol statistics
    printf("udp.{xmit=%d, recv=%d, fw=%d, drop=%d, chkerr=%d, lenerr=%d, memerr=%d, rterr=%d, proterr=%d, opterr=%d, err=%d}\n",
        lwip_stats.udp.xmit, lwip_stats.udp.recv, lwip_stats.udp.fw, lwip_stats.udp.drop, lwip_stats.udp.chkerr, lwip_stats.udp.lenerr,
        lwip_stats.udp.memerr, lwip_stats.udp.rterr, lwip_stats.udp.proterr, lwip_stats.udp.opterr, lwip_stats.udp.err);

    // TCP protocol statistics
    printf("tcp.{xmit=%d, recv=%d, fw=%d, drop=%d, chkerr=%d, lenerr=%d, memerr=%d, rterr=%d, proterr=%d, opterr=%d, err=%d}\n",
        lwip_stats.tcp.xmit, lwip_stats.tcp.recv, lwip_stats.tcp.fw, lwip_stats.tcp.drop, lwip_stats.tcp.chkerr, lwip_stats.tcp.lenerr,
        lwip_stats.tcp.memerr, lwip_stats.tcp.rterr, lwip_stats.tcp.proterr, lwip_stats.tcp.opterr, lwip_stats.tcp.err);

    ESP_LOGI(TAG, "wifi_cmd_stats(): Wifi adapter network stats follow");
    return 0;
}

void register_wifi()
{
    /*** set up and register each of the console commands processed by this module ***/

    //Command: ap
    ap_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    ap_args.password = arg_str0(NULL, NULL, "<pass>", "password of AP");
    ap_args.end = arg_end(2);
    const esp_console_cmd_t ap_cmd = {
        .command = "ap",
        .help = "AP mode, configure ssid and password",
        .hint = NULL,
        .func = &wifi_cmd_ap,
        .argtable = &ap_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&ap_cmd) );

    //Command: sta
    sta_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    sta_args.password = arg_str0(NULL, NULL, "<pass>", "password of AP");
    sta_args.end = arg_end(2);
    const esp_console_cmd_t sta_cmd = {
        .command = "sta",
        .help = "WiFi is station mode, join specified soft-AP",
        .hint = NULL,
        .func = &wifi_cmd_sta,
        .argtable = &sta_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&sta_cmd) );

    //Command: scan
    scan_args.ssid = arg_str0(NULL, NULL, "<ssid>", "SSID of AP want to be scanned");
    scan_args.end = arg_end(1);
    const esp_console_cmd_t scan_cmd = {
        .command = "scan",
        .help = "WiFi is station mode, start scan ap",
        .hint = NULL,
        .func = &wifi_cmd_scan,
        .argtable = &scan_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&scan_cmd) );

    //Command: query
    const esp_console_cmd_t query_cmd = {
        .command = "query",
        .help = "query WiFi info",
        .hint = NULL,
        .func = &wifi_cmd_query,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&query_cmd) );

    //Command: iperf
    iperf_args.ip = arg_str0("c", "client", "<ip>", "run in client mode, connecting to <host>");
    iperf_args.server = arg_lit0("s", "server", "run in server mode");
    iperf_args.udp = arg_lit0("u", "udp", "use UDP rather than TCP");
    iperf_args.port = arg_int0("p", "port", "<port>", "server port to listen on/connect to");
    iperf_args.interval = arg_int0("i", "interval", "<interval>", "seconds between periodic bandwidth reports");
    iperf_args.time = arg_int0("t", "time", "<time>", "time in seconds to transmit for (default 10 secs)");
    iperf_args.abort = arg_lit0("a", "abort", "abort running iperf");
    iperf_args.end = arg_end(1);
    const esp_console_cmd_t iperf_cmd = {
        .command = "iperf",
        .help = "iperf command",
        .hint = NULL,
        .func = &wifi_cmd_iperf,
        .argtable = &iperf_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&iperf_cmd) );

    //Command: hostname
    hostname_args.hostname = arg_str1(NULL, NULL, "<hostname>", "This node's hostname will be set to <hostname>\n"
                                                                "(will be added to DHCP requests and, if the DHCP server integrates with the\n"
                                                                "LAN's DNS server, will show in direct and reverse DNS)\n"
                                                                "This command should be called *before* the commands `sta` and `ap`, so the\n"
                                                                "hostname is already set when they run; otherwise, a MACAddr-based default\n"
                                                                "hostname will be used instead.");
    hostname_args.end = arg_end(2);
    const esp_console_cmd_t hostname_cmd = {
        .command = "hostname",
        .help = "Set this node's hostname",
        .hint = NULL,
        .func = &wifi_cmd_hostname,
        .argtable = &hostname_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&hostname_cmd) );

    //Command: stats
    const esp_console_cmd_t stats_cmd = {
        .command = "stats",
        .help = "Network wifi statistics",
        .hint = NULL,
        .func = &wifi_cmd_stats,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&stats_cmd) );
}
