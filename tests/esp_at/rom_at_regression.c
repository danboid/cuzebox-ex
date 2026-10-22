#include "cu_esp_at.c"
#include <assert.h>
void cu_message(char const *fmt,...){(void)fmt;}
void cu_error(char const *fmt,...){(void)fmt;}
void cu_unf(char const *s){(void)s;}
static void run(char *cmd,const char *expected){
 esp_state.read_buf_pos_in=esp_state.read_buf_pos_out=0;
 cu_esp_at_sysflash((sint8*)cmd);
 esp_state.read_buf[esp_state.read_buf_pos_in]=0;
 assert(strstr((char*)esp_state.read_buf,expected));
}
int main(int argc,char **argv){
 assert(argc==2);cu_esp_rom_set_path(argv[1]);cu_esp_rom_reset(0);
 esp_state.at_firmware_profile=CU_ESP_AT_FW_UZENET;
 cu_esp_at_uzfw((sint8*)"AT+UZFW?\r\n");
 esp_state.read_buf[esp_state.read_buf_pos_in]=0;
 assert(strstr((char*)esp_state.read_buf,"+UZFW:4,1"));
 assert(cu_esp_at_dispatch_find("UZFW",4)->id==CU_AT_ID_UZFW);
 esp_state.read_buf_pos_in=esp_state.read_buf_pos_out=0;
 esp_state.at_firmware_profile=CU_ESP_AT_FW_ESPAT_230;
 cu_esp_at_uzfw((sint8*)"AT+UZFW?\r\n");
 esp_state.read_buf[esp_state.read_buf_pos_in]=0;
 assert(strstr((char*)esp_state.read_buf,"ERROR"));
 run("AT+SYSFLASH=0,\"client_ca\"\r\n","OK");
 run("AT+SYSFLASH=1,\"client_ca\",0,4096\r\n",">");
 for(unsigned i=0;i<4096;i++)assert(cu_esp_rom_write_byte((uint8)i));
 assert(!cu_esp_rom_write_byte(0));
 uint32 offset,size;assert(cu_esp_rom_partition("client_ca",&offset,&size));
 const uint8 *data=cu_esp_rom_read(offset,4096);for(unsigned i=0;i<4096;i++)assert(data[i]==(uint8)i);
 run("AT+SYSFLASH=2,\"client_ca\",0,4\r\n","+SYSFLASH:4,");
 run("AT+SYSFLASH=1,\"client_ca\",8191,2\r\n","ERROR");
 run("AT+SYSFLASH=0,\"client_ca\",1,4096\r\n","ERROR");
 run("AT+SYSFLASH=0,\"nonexistent\"\r\n","ERROR");
 run("AT+SYSFLASH=1,\"client_key\",0,100\r\n",">");
 assert(cu_esp_rom_write_byte(1));cu_esp_rom_reset(0);assert(!cu_esp_rom_write_byte(1));
 puts("PASS: AT named-partition commands, streamed 4 KiB binary writes, reads, bounds and reset cancellation");
}
