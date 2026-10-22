#include "cu_esp_net.c"
#include <assert.h>
static cu_state_esp_t state;
cu_state_esp_t *cu_esp_get_state(void){return &state;}
void cu_esp_rom_output(const uint8 *data,unsigned n){(void)data;(void)n;}
static void install(const char *name,const char *file,unsigned slot){
 FILE *f=fopen(file,"rb");assert(f);uint8 data[8192]={0xf1,0xf1,1,0};
 size_t n=fread(data+12,1,sizeof(data)-12,f);assert(feof(f));fclose(f);
 unsigned padded=(n+3)&~3u,total=4+padded;
 data[4]=total;data[5]=total>>8;data[8]=1;data[9]=slot;data[10]=n;data[11]=n>>8;
 memset(data+12+n,255,padded-n);
 uint32 offset,size;assert(cu_esp_rom_partition(name,&offset,&size));
 assert(cu_esp_rom_erase(offset,size));assert(cu_esp_rom_write_begin(offset,8+total));
 for(unsigned i=0;i<8+total;i++)assert(cu_esp_rom_write_byte(data[i]));
 unsigned got=0;const uint8 *pem=cu_esp_rom_pki(name,slot,&got);
 assert(pem&&got==n&&!memcmp(pem,data+12,n));
}
int main(int argc,char **argv){
 assert(argc==4);cu_esp_rom_set_path(argv[1]);assert(cu_esp_rom_reset(0)==0);
 install("client_ca",argv[2],1);install("client_cert",argv[2],0);install("client_key",argv[3],0);
 cu_esp_rom_set_path(argv[1]);assert(cu_esp_rom_reset(0)==0);
 state.ssl_ca_num[0]=1;assert(cu_tls_ctx_init(0)==0);
 assert(SSL_CTX_get0_certificate(state.tls_ctx[0]));assert(SSL_CTX_check_private_key(state.tls_ctx[0])==1);
 cu_tls_ctx_free(0);
 uint32 offset,size;assert(cu_esp_rom_partition("client_key",&offset,&size));assert(cu_esp_rom_erase(offset,size));
 assert(cu_tls_ctx_init(0)<0);
 puts("PASS: named PKI flash partitions, streamed writes, persistence, TLS credential loading and missing-key rejection");
}
