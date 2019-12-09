#include <mem.h>
#include <stdarg.h>
#include <lwip/netif.h>
#include <lwip/app/espconn_tcp.h>
#include <lwip/app/espconn_udp.h>


#include "user_interface.h"


static os_timer_t ptimer;
static uint32 count;
static struct espconn *gatecontConn = NULL;
extern uint32 system_rf_cal_sector;
extern uint32 system_param_sector_start;
extern uint32 system_phy_init_sector;

void user_pre_init() {
#if 0
    static const partition_item_t at_partition_table[] = {
            { SYSTEM_PARTITION_BOOTLOADER,                      0x0,         0x1000},
            { SYSTEM_PARTITION_OTA_1,                           0x1000,     0x6A000},
            { SYSTEM_PARTITION_OTA_2,                           0x81000,    0x6A000},
            { SYSTEM_PARTITION_RF_CAL,                          0x3fb000,    0x1000},  // 1024 - 5 sectors of 4096
            { SYSTEM_PARTITION_PHY_DATA,                        0x3fc000,    0x1000},
            { SYSTEM_PARTITION_SYSTEM_PARAMETER,                0x3fd000,    0x3000}
    };

    static const unsigned long SPI_FLASH_SIZE_MAP = 4u;
    if(!system_partition_table_regist(at_partition_table, sizeof(at_partition_table)/sizeof(at_partition_table[0]), SPI_FLASH_SIZE_MAP)) {
        for(;;) {
            os_printf("Invalid partition table\r\n");
        }
    }
#else 
    static const partition_item_t at_partition_table[] = {
            { 1 + SYSTEM_PARTITION_CUSTOMER_BEGIN,                      0x0,         0x1000},
            { 2 + SYSTEM_PARTITION_CUSTOMER_BEGIN,                           0x1000,     0x39000},
            { SYSTEM_PARTITION_RF_CAL,                          0x7b000,     0x1000},  // 128 - 5 sectors of 4096
            { SYSTEM_PARTITION_PHY_DATA,                        0x7c000,     0x1000},
            { SYSTEM_PARTITION_SYSTEM_PARAMETER,                0x7d000,     0x3000}
    };

    unsigned long MAP = system_get_flash_size_map();
    system_rf_cal_sector = 123;
    system_phy_init_sector = 124;
    system_param_sector_start = 125;
    os_printf("fccal is %d chip is %d\r\n", system_rf_cal_sector, MAP);
#endif
}

static void gatecontConnectCb(void *arg) {
    char const openMessage[] = "GET /open HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    espconn_send(gatecontConn, openMessage, sizeof(openMessage));
}

static void gatecontSentCb(void *arg) {
}

static void gatecontRecvCb(void *arg, char *data, unsigned short length) {
    os_printf("gatecontRecvCb %s\r\n", data);
    if(data == strstr(data, "HTTP/1.1 200")) {
        os_printf("got ok going to sleep\r\n");
        system_deep_sleep_instant(0xffffffffull);
    }

}

static void connectToGate() {
    ip_addr_t gatecontIp;
    sint8 ret = 0;
    os_printf("connecting to gate\r\n");
    IP4_ADDR(&gatecontIp, 192, 168, 4, 1);
    if(gatecontConn != NULL) {
        espconn_delete(gatecontConn);
        os_free(gatecontConn->proto.tcp);
        os_free(gatecontConn);
        gatecontConn = NULL;
    }

    gatecontConn = (struct espconn *)os_zalloc(sizeof(struct espconn));
    gatecontConn->type  = ESPCONN_TCP;
    gatecontConn->state = ESPCONN_NONE;
    gatecontConn->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
    gatecontConn->proto.tcp->local_port = espconn_port();
    gatecontConn->proto.tcp->remote_port = 80;
    os_memcpy(gatecontConn->proto.tcp->remote_ip, &gatecontIp, sizeof(gatecontConn->proto.tcp->remote_ip));

    espconn_create(gatecontConn);
    espconn_regist_connectcb(gatecontConn, gatecontConnectCb);
    espconn_regist_recvcb(gatecontConn, gatecontRecvCb);
    espconn_regist_sentcb(gatecontConn, gatecontSentCb);
    ret = espconn_connect(gatecontConn);
}


static void wifi_event_cb(System_Event_t *evt) {
    switch (evt->event) {
        case EVENT_STAMODE_CONNECTED:
            connectToGate();
            break;
        case EVENT_STAMODE_DISCONNECTED:
            break;

        case EVENT_STAMODE_AUTHMODE_CHANGE:
            break;

        case EVENT_STAMODE_GOT_IP:
            break;

        case EVENT_SOFTAPMODE_STACONNECTED:
            break;

        case EVENT_SOFTAPMODE_STADISCONNECTED:
            break;

        default:
            break;
    }
}


static void timerCb(void* arg) {
    static bool toggle = FALSE;
    GPIO_OUTPUT_SET(D4, toggle);
    toggle = !toggle;
    --count;
    os_printf("CHIPPPP LED OUT FOR NON LED MODEL\r\n");
    if(0 == count) {
        system_deep_sleep_instant(0xffffffffull);
    }
}

void user_init() {
    struct ip_info info;
    ip_addr_t dnsServerIp;
    struct softap_config apConfig;
    struct station_config stationConf;
    uint8_t mac[6];
    struct rst_info* resetInfo = system_get_rst_info();
    uint32 ChipId = system_get_chip_id();

 /// CHANGE THIS FOR OUT

    system_set_os_print(1);
    system_deep_sleep_set_option(0);
    system_phy_set_powerup_option(3);
    system_phy_set_max_tpw(82);

    if(REASON_DEEP_SLEEP_AWAKE != resetInfo->reason) {
        os_printf("Reset reason %d goind to sleep\n", resetInfo->reason);
        system_deep_sleep_instant(0xffffffffull);
    }
    wifi_station_set_auto_connect(1);
    wifi_set_event_handler_cb(wifi_event_cb);
    wifi_set_opmode(STATION_MODE);
    wifi_station_dhcpc_stop();

    os_memset(stationConf.ssid, 0, sizeof(stationConf.ssid));
    os_memcpy(stationConf.ssid, WIFI_SSID, sizeof(WIFI_SSID));
    os_memset(stationConf.password, 0, sizeof(stationConf.password));
    os_memcpy(stationConf.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    stationConf.bssid_set = 0;
    stationConf.threshold.authmode = AUTH_WPA_WPA2_PSK;
    wifi_station_set_config(&stationConf);

    os_printf("chipid is %x\r\n",  ChipId);
    if(0x38d76b == ChipId) {
         IP4_ADDR(&info.ip, 192, 168, 4, 254);
    }  else if(0x154924 == ChipId) {
         IP4_ADDR(&info.ip, 192, 168, 4, 253);
    }
    IP4_ADDR(&info.gw, 192, 168, 4, 1);
    IP4_ADDR(&info.netmask, 255, 255, 255, 0);
    IP4_ADDR(&dnsServerIp, 9, 9, 9, 9);
    wifi_set_ip_info(STATION_IF, &info);
 
    os_memset(&stationConf, 0, sizeof(stationConf));
    count = 4 * 30;
    os_timer_setfn(&ptimer, timerCb, 0);
    os_timer_arm(&ptimer, 256, 1);
}

