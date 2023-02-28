#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_flash_spi_init.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#include "oledfont.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_sntp.h" //时间获取
#include <time.h>
#include <sys/time.h>
#include "esp_attr.h"
#include "esp_sleep.h"

// wifi
uint16_t ap_num = 0;
uint16_t max_scan_num = 15;
#define STA_START BIT2
#define SCAN_DONE BIT3
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define EXAMPLE_ESP_WIFI_SSID ""
#define EXAMPLE_ESP_WIFI_PASS ""
#define EXAMPLE_ESP_MAXIMUM_RETRY 6

static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;

// chip info
const char *hw = "hardware";
const char *sw = "software";


void WifiScan()
{
    ESP_LOGI("Wifi", "wifi scaning......");
    wifi_country_t country_config = {
        .cc = "01",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };

    wifi_scan_config_t scan_config = {
        .show_hidden = true /**< enable to scan AP whose SSID is hidden */
    };

    ESP_ERROR_CHECK(esp_wifi_set_country(&country_config));
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
}

void WifiScanPrint()
{
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));
    ESP_LOGI("Wifi", "scan %d AP count", ap_num);

    wifi_ap_record_t ap_records[max_scan_num];
    memset(ap_records, 0, sizeof(ap_records));
    esp_wifi_scan_get_ap_records(&max_scan_num, ap_records);
    ESP_LOGI("Wifi", "scan AP list");
    printf("%36s %10s %10s \n", "ssid", "channel", "rssi");

    for (int i = 0; i < max_scan_num; i++)
    {
        printf("%36s %10d %10d\n", ap_records[i].ssid, ap_records[i].primary, ap_records[i].rssi);
    }
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        /**************************************************************************/
        ESP_LOGI("wifi_event_handle", "WIFI_EVENT_STA_START");
        xEventGroupSetBits(s_wifi_event_group, STA_START);
        /**************************************************************************/
        // esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        ESP_LOGI("wifi_event_handle", "WIFI_EVENT_SCAN_DONE");
        xEventGroupSetBits(s_wifi_event_group, SCAN_DONE);
    }
    /**************************************************************************/
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI("event_handler", "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI("event_handler", "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI("wifi_event_handle", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void Wifi_ScanPrint_task(void *pvParameters)
{
    ESP_LOGI("Wifi_ScanPrint_task", "Task create success!");
    // esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, run_on_event, NULL);
    // esp_event_handler_unregister(esp_event_base_t event_base, int32_t event_id,esp_event_handler_t event_handler);

    xEventGroupWaitBits(s_wifi_event_group, STA_START, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI("Wifi_ScanPrint_task", "receive STA_START, AP scan start");
    WifiScan();

    xEventGroupWaitBits(s_wifi_event_group, SCAN_DONE, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI("Wifi_ScanPrint_task", "receive SCAN_DONE, AP scan stop, print AP message");
    WifiScanPrint();

    esp_wifi_connect();

    vTaskDelete(NULL);
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("wifi_init_sta", "wifi_init_sta finished.");

    xTaskCreate(Wifi_ScanPrint_task, "Wifi_ScanPrint_task", 1024 * 10, NULL, 1, NULL);

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI("wifi_init_sta", "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI("wifi_init_sta", "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE("wifi_init_sta", "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void Task_List()
{
    // enable FreeRTos trace facility and FreeRTos stats formatting functions in menuconfig before use!
    char ptrTaskList[500];
    vTaskList(ptrTaskList);
    printf("**********************************************\n");
    printf("Task        State    Prio       Stack   Num\n");
    printf("**********************************************\n");
    printf(ptrTaskList);
    printf("**********************************************\n");
}

void fun(struct timeval *tv)
{
    time_t now = 0;
    struct tm timeinfo = {0};

    time(&now);                   // 获取当前时间
    localtime_r(&now, &timeinfo); // converts the calendar time timep to broken-time representation

    char str[64];
    strftime(str, sizeof(str), "%c", &timeinfo); // 将给定格式的日期时间对象转换为字符串
    ESP_LOGI("fun", "time updated: %s", str);

    sntp_stop(); // 在成功获取了网络时间后，必须调用 sntp_stop(); 停止NTP请求，不然设备重启后会造成获取网络时间失败的现象
    //     struct tm
    // {
    //   int	tm_sec;    //秒钟
    //   int	tm_min;    //分钟
    //   int	tm_hour;    //小时
    //   int	tm_mday;    //日期：日，从1开始
    //   int	tm_mon;    //日期：月，从0开始
    //   int	tm_year;    //年，距离1900年的差值，默认是70
    //   int	tm_wday;    //星期，1对应星期一
    //   int	tm_yday;    //一年的过去的天数
    //   int	tm_isdst;    //是否为夏时制
    // #ifdef __TM_GMTOFF
    //   long	__TM_GMTOFF;
    // #endif
    // #ifdef __TM_ZONE
    //   const char *__TM_ZONE;
    // #endif
    // };
}

void sntp_TimeGet_Task(void *pvParameters)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");

    sntp_init(); // init and start SNTP service

    setenv("TZ", "CST-8", 1);
    tzset();
    // GMT:Greenwich Mean Time
    // UCT:Coordinated Universal Time
    // DST: Daylight Saving Time
    // CST:
    // Central Standard Time (USA) UT-6:00 美国标准时间
    // Central Standard Time (Australia) UT+9:30 澳大利亚标准时间
    // China Standard Time UT+8:00 中国标准时间
    // Cuba Standard Time UT-4:00 古巴标准时间
    sntp_set_time_sync_notification_cb(fun);

    vTaskDelete(NULL);
}

void app_main()
{

    //  wifi_init
    esp_err_t ret = nvs_flash_init(); // Initialize NVS
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI("main", "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    Task_List(); // 打印当前任务栈大小

    xTaskCreate(sntp_TimeGet_Task, "sntp_TimeGet_Task", 1024 * 4, NULL, 1, NULL);

    time_t now = 0;
    struct tm timeinfo = {0};

    while (1)
    {
        time(&now);
        localtime_r(&now, &timeinfo); // converts the calendar time timep to broken-time representation

        char str[64];
        strftime(str, sizeof(str), "%c", &timeinfo); // 将给定格式的日期时间对象转换为字符串
        ESP_LOGI("main", "time updated: %s", str);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}
