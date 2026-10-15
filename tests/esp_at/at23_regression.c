#define ENABLE_ESP 1
#define HEADLESS 0
#include "cu_esp_at.c"
#include <stdio.h>
#include <string.h>

const char at_gmr_string[]="AT version:1.7 legacy\r\n";
static unsigned save_count;
void cu_message(char const* fmt, ...){ (void)fmt; }
void cu_error(char const* fmt, ...){ (void)fmt; }
void cu_unf(char const*s){ (void)s; }
void cu_esp_save_config(void){ save_count++; }
void cu_esp_reset_network(void){}
void cu_esp_apply_serial_route_live(void){}
void cu_esp_lan_on_softap_change(cu_state_esp_t *s){ (void)s; }
static uint8 mqtt_stub_state;
static uint8 mqtt_stub_sub_count;
static char mqtt_stub_topic[CU_ESP_MQTT_TOPIC_MAX+1u]="demo/topic";
static uint8 mqtt_stub_qos=1u;
sint32 cu_esp_mqtt_connect(const char*h,uint16 p,uint8 s,const char*path,uint8 r){(void)h;(void)p;(void)s;(void)path;(void)r;mqtt_stub_state=1u;return 0;}
void cu_esp_mqtt_disconnect(void){mqtt_stub_state=0u;mqtt_stub_sub_count=0u;}
uint8 cu_esp_mqtt_state(void){return mqtt_stub_state;}
sint32 cu_esp_mqtt_publish(const char*t,const uint8*d,uint32 n,uint8 q,uint8 r){(void)t;(void)d;(void)n;(void)q;(void)r;return 0;}
sint32 cu_esp_mqtt_subscribe(const char*t,uint8 q){snprintf(mqtt_stub_topic,sizeof(mqtt_stub_topic),"%s",t);mqtt_stub_qos=q;mqtt_stub_sub_count=1u;return 0;}
sint32 cu_esp_mqtt_unsubscribe(const char*t){(void)t;mqtt_stub_sub_count=0u;return 0;}
uint8 cu_esp_mqtt_subscription_count(void){return mqtt_stub_sub_count;}
sint32 cu_esp_mqtt_subscription_get(uint8 i,char*t,auint z,uint8*q){if(i>=mqtt_stub_sub_count)return -1;if(t&&z)snprintf(t,z,"%s",mqtt_stub_topic);if(q)*q=mqtt_stub_qos;return 0;}
sint32 cu_esp_mqtt_take_event(uint8*k,char*t,auint ts,uint8*d,auint ds,uint32*dl){(void)k;(void)t;(void)ts;(void)d;(void)ds;(void)dl;return 0;}
void cu_esp_mqtt_tick(void){}

static void out_clear(void){ esp_state.read_buf_pos_in=0; esp_state.read_buf_pos_out=0; memset(esp_state.read_buf,0,sizeof(esp_state.read_buf)); }
static const char *out_text(void){ auint n=esp_state.read_buf_pos_in; if(n>=sizeof(esp_state.read_buf)) n=sizeof(esp_state.read_buf)-1; esp_state.read_buf[n]=0; return (const char*)esp_state.read_buf; }
static int has(const char *s){ return strstr(out_text(),s)!=NULL; }
static int no(const char *s){ return strstr(out_text(),s)==NULL; }
#define CHECK(x,msg) do{ if(!(x)){ fprintf(stderr,"FAIL: %s\nOUT=[%s]\n",msg,out_text()); return __LINE__; } }while(0)

int main(void){
    memset(&esp_state,0,sizeof(esp_state));
    esp_state.at_firmware_profile=CU_ESP_AT_FW_ESPAT_230;
    esp_state.wifi_mode=2;
    esp_state.soft_ap_max_conn=4;
    esp_state.soft_ap_dhcp_lease_min=5;

    out_clear(); cu_esp_at_gmr();
    CHECK(has("AT version:2.3.0.0"),"modern GMR version");

    out_clear(); { sint8 c[]="AT+SYSMSG=7\r\n"; cu_esp_at_sysmsg(c); }
    CHECK(esp_state.sysmsg_flags==7 && has("OK\r\n"),"SYSMSG set");
    out_clear(); { sint8 c[]="AT+SYSMSG?\r\n"; cu_esp_at_sysmsg(c); }
    CHECK(has("+SYSMSG:7\r\n"),"SYSMSG query");

    save_count=0; esp_state.sysstore_mode=0;
    { sint8 c[]="AT+CWSAP=\"TEST\",\"12345678\",6,3,7,1\r\n"; cu_esp_at_cwsap(c); }
    CHECK(save_count==0 && esp_state.soft_ap_max_conn==7 && esp_state.soft_ap_hidden==1,"CWSAP RAM-only when SYSSTORE=0");
    out_clear(); { sint8 c[]="AT+CWSAP?\r\n"; cu_esp_at_cwsap(c); }
    CHECK(has("+CWSAP:\"TEST\",\"12345678\",6,3,7,1\r\n"),"CWSAP full query");
    esp_state.sysstore_mode=1; out_clear();
    { sint8 c[]="AT+CWSAP=\"TEST2\",\"abcdefgh\",1,3,4,0\r\n"; cu_esp_at_cwsap(c); }
    CHECK(save_count==1,"CWSAP persisted when SYSSTORE=1");

    esp_state.state|=ESP_MUX; out_clear();
    { const uint8 d[3]={'a','b','c'}; cu_esp_cipbuf_push(&esp_state,0,d,3); sint8 c[]="AT+CIPRECVLEN?\r\n"; cu_esp_at_ciprecvlen(c); }
    CHECK(has("+CIPRECVLEN:3,,,,\r\n"),"CIPRECVLEN five-link blank-zero wire format");

    out_clear(); { sint8 c[]="AT+CIPSSLCCONF=5,3,0,0\r\n"; cu_esp_at_cipsslcconf(c); }
    for(int i=0;i<ESP_MAX_LINKS;i++) CHECK(esp_state.ssl_auth_mode[i]==3,"SSL all-link CCONF");
    out_clear(); { sint8 c[]="AT+CIPSSLCSNI=5,\"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-\"\r\n"; cu_esp_at_cipsslcsni(c); }
    for(int i=0;i<ESP_MAX_LINKS;i++) CHECK(strlen((char*)esp_state.ssl_sni[i])==64,"64-byte SNI all links");
    out_clear(); { sint8 c[]="AT+CIPSSLCALPN=5,5,\"h2\",\"http/1.1\",\"mqtt\",\"x\",\"y\"\r\n"; cu_esp_at_cipsslcalpn(c); }
    for(int i=0;i<ESP_MAX_LINKS;i++) CHECK(esp_state.ssl_alpn_count[i]==5 && !strcmp((char*)esp_state.ssl_alpn4[i],"y"),"five ALPN all links");
    out_clear(); { sint8 c[]="AT+CIPSSLCPSK=5,\"identity\",\"hint\"\r\n"; cu_esp_at_cipsslcpsk(c); }
    for(int i=0;i<ESP_MAX_LINKS;i++) CHECK(!strcmp((char*)esp_state.ssl_psk_id[i],"identity")&&!strcmp((char*)esp_state.ssl_psk_key[i],"hint"),"PSK wire state");

    out_clear(); { sint8 c[]="AT+CIPSSLCCIPHER?\r\n"; cu_esp_at_cipsslccipher(c); }
    CHECK(has("+CIPSSLCCIPHER:0,0x") && no("ECDHE"),"cipher query is ESP IDs only");

    out_clear(); { sint8 c[]="AT+CMD?\r\n"; cu_esp_at_cmd(c); }
    CHECK(has("\"AT+CWDHCPS\"") && has("\"AT+CIFSR\"") && has("\"AT+CIPDNS\"") && has("\"AT+MDNS\"") && has("\"AT+MQTTUSERCFG\""),"CMD contains stock groups");
    CHECK(no("CIPSSLCPSKHEX"),"2.3 CMD excludes later PSKHEX extension");

    CHECK(cu_esp_at_dispatch_find("CIPSTATE",8u)!=NULL,"exact dispatcher finds command");
    CHECK(cu_esp_at_dispatch_find("CIPSTATEX",9u)==NULL,"exact dispatcher rejects prefix extension");
    out_clear(); cu_esp_txi((sint32)(-2147483647-1));
    CHECK(!strcmp(out_text(),"-2147483648"),"direct integer formatter handles INT32_MIN");

    /* ESP8266 stock MQTT command limits/wire responses. */
    out_clear(); { sint8 c[]="AT+MQTTUSERCFG=0,1,\"id\",\"user\",\"pass\",0,0,\"/mqtt\"\r\n"; cu_esp_at_mqttusercfg(c); }
    CHECK(esp_state.mqtt_user_configured==1u && esp_state.mqtt_scheme==1u && has("OK\r\n"),"MQTTUSERCFG stock TCP scheme");
    out_clear(); { sint8 c[]="AT+MQTTUSERCFG=0,2,\"id\",\"\",\"\",0,0,\"\"\r\n"; cu_esp_at_mqttusercfg(c); }
    CHECK(has("ERROR\r\n"),"ESP8266 stock MQTT rejects TLS schemes");
    out_clear(); { sint8 c[]="AT+MQTTLONGUSERNAME=0,1024\r\n"; cu_esp_at_mqttlongusername(c); }
    CHECK(esp_state.mqtt_input_expected==1024u && has("OK\r\n>"),"MQTTLONGUSERNAME accepts 1024 bytes");
    esp_state.mqtt_input_kind=CU_ESP_MQTT_INPUT_NONE; esp_state.mqtt_input_expected=0u; esp_state.rx_await_bytes=0u;
    out_clear(); { sint8 c[]="AT+MQTTPUBRAW=0,\"t\",500,0,0\r\n"; cu_esp_at_mqttpubraw(c); }
    CHECK(esp_state.mqtt_input_expected==500u && has("OK\r\n>"),"MQTTPUBRAW stock 512-byte packet budget accepts fitting payload");
    esp_state.mqtt_input_kind=CU_ESP_MQTT_INPUT_NONE; esp_state.mqtt_input_expected=0u; esp_state.rx_await_bytes=0u;
    out_clear(); { sint8 c[]="AT+MQTTPUBRAW=0,\"t\",507,0,0\r\n"; cu_esp_at_mqttpubraw(c); }
    CHECK(has("ERROR\r\n"),"MQTTPUBRAW rejects packet beyond stock 512-byte MQTT buffer");
    mqtt_stub_state=1u; mqtt_stub_sub_count=1u; out_clear(); { sint8 c[]="AT+MQTTSUB?\r\n"; cu_esp_at_mqttsub(c); }
    CHECK(has("+MQTTSUB:0,6,\"demo/topic\",1\r\n"),"MQTTSUB query includes connected/subscribed state");
    out_clear(); { sint8 c[]="AT+MQTTSUB=0,\"demo/topic\",1\r\n"; cu_esp_at_mqttsub(c); }
    CHECK(has("ALREADY SUBSCRIBE\r\n") && has("OK\r\n"),"duplicate MQTT subscription wire response");
    out_clear(); { sint8 c[]="AT+MQTTUNSUB=0,\"missing/topic\"\r\n"; cu_esp_at_mqttunsub(c); }
    CHECK(has("NO UNSUBSCRIBE\r\n") && has("OK\r\n"),"missing MQTT unsubscribe wire response");

    puts("esp_at23_regression: PASS");
    return 0;
}
