#ifndef PROJECT_TITAN_NETWORK_H
#define PROJECT_TITAN_NETWORK_H

#define TITAN_NETWORK_SSID_CAPACITY 33u
#define TITAN_NETWORK_TEXT_CAPACITY 64u
#define TITAN_NETWORK_WIFI_AP_CAPACITY 8u

typedef enum TitanNetworkPreference
{
    TITAN_NETWORK_PREFER_WIFI = 0,
    TITAN_NETWORK_PREFER_ETHERNET = 1
} TitanNetworkPreference;

typedef enum TitanNetworkState
{
    TITAN_NETWORK_OFFLINE = 0,
    TITAN_NETWORK_INITIALIZING,
    TITAN_NETWORK_WAITING_CONFIG,
    TITAN_NETWORK_CONNECTING,
    TITAN_NETWORK_ONLINE,
    TITAN_NETWORK_ERROR
} TitanNetworkState;

typedef enum TitanNetworkWifiSecurity
{
    TITAN_NETWORK_WIFI_OPEN = 0,
    TITAN_NETWORK_WIFI_WEP,
    TITAN_NETWORK_WIFI_WPA,
    TITAN_NETWORK_WIFI_WPA2,
    TITAN_NETWORK_WIFI_WPA3,
    TITAN_NETWORK_WIFI_UNKNOWN
} TitanNetworkWifiSecurity;

typedef struct TitanNetworkWifiAp
{
    char ssid[TITAN_NETWORK_SSID_CAPACITY];
    unsigned char bssid[6];
    signed char rssi_dbm;
    unsigned char channel;
    unsigned char security;
    unsigned char seen_count;
} TitanNetworkWifiAp;

typedef struct TitanNetworkStatus
{
    TitanNetworkPreference preference;
    int enabled;
    TitanNetworkState state;
    int wifi_irx_loaded;
    int wifi_adapter_present;
    int wifi_scanning;
    unsigned int wifi_ap_count;
    int wifi_rpc_bound;
    int wifi_configured;
    int wifi_online;
    int ethernet_stack_loaded;
    int ethernet_link_up;
    int ethernet_online;
    char ssid[TITAN_NETWORK_SSID_CAPACITY];
    char ip[TITAN_NETWORK_TEXT_CAPACITY];
    char gateway[TITAN_NETWORK_TEXT_CAPACITY];
    char dns[TITAN_NETWORK_TEXT_CAPACITY];
    char detail[96];
} TitanNetworkStatus;

void TitanNetwork_Initialize(void);
void TitanNetwork_Update(void);
const TitanNetworkStatus *TitanNetwork_GetStatus(void);
void TitanNetwork_SetEnabled(int enabled);
int TitanNetwork_IsEnabled(void);
void TitanNetwork_SetPreference(TitanNetworkPreference preference);
TitanNetworkPreference TitanNetwork_GetPreference(void);
int TitanNetwork_SetWifiSsid(const char *ssid);
int TitanNetwork_SetWifiPassword(const char *password);
int TitanNetwork_WifiHasKey(void);
int TitanNetwork_RequestWifiScan(void);
unsigned int TitanNetwork_GetWifiNetworkCount(void);
const TitanNetworkWifiAp *TitanNetwork_GetWifiNetwork(unsigned int index);
const char *TitanNetwork_GetWifiSecurityName(unsigned int security);
unsigned int TitanNetwork_GetWifiSignalPercent(const TitanNetworkWifiAp *ap);
int TitanNetwork_ConnectWifi(void);
int TitanNetwork_Retry(void);
const char *TitanNetwork_GetStateName(void);
const char *TitanNetwork_GetPreferenceName(void);

#endif
