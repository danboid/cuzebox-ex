/* UDP between virtual Wi-Fi interfaces. Overlay ID zero is reserved for these
 * local links; IDs 1..N continue to identify external-network relays. */
#define CU_ESP_LAN_CTL_UDP 9u
#define CU_ESP_LAN_UDP_HEADER 36u

uint8 cu_esp_lan_local_destination(cu_state_esp_t *es, const char *host)
{
 uint32 ip, local, mask;
 if(!es || !host || !es->soft_ap_enabled || !es->lan.enable)return 0;
 ip=cu_esp_ipv4_to_be_local(host,0);
 if(es->lan.joined){
  local=cu_esp_ipv4_to_be_local((char*)es->station_ip,0);
  mask=cu_esp_ipv4_to_be_local((char*)es->station_netmask,0);
 }else if(es->lan.adv_enable){local=es->lan.ap_ip_be;mask=es->lan.netmask_be;}
 else return 0;
 return ip && local && mask && (ip&mask)==(local&mask);
}

static uint8 cu_esp_lan_direct_udp(const cu_esp_lan_vlink_t *vl)
{
 return vl->active && !vl->overlay_id && (vl->type&ESP_PROTO_UDP);
}

static sint32 cu_esp_lan_udp_open(cu_state_esp_t *es, uint32 sock,
 const char *host, uint32 port, uint32 local_port, uint32 udp_mode)
{
 cu_esp_lan_vlink_t *vl=&es->lan.vlinks[sock];
 if(!port || port>65535 || local_port>65535 || udp_mode>2)return -1;
 if(!local_port)local_port=49152U+sock;
 for(uint32 i=0;i<ESP_MAX_LINKS;i++)
  if(i!=sock && cu_esp_lan_direct_udp(&es->lan.vlinks[i]) && es->lan.vlinks[i].local_port==local_port)return -1;
 memset(vl,0,sizeof(*vl));vl->active=1;vl->type=ESP_PROTO_UDP;
 vl->local_port=(uint16)local_port;vl->udp_mode=(uint8)udp_mode;
 vl->peer_addr.sin_family=AF_INET;
 vl->peer_addr.sin_addr.s_addr=cu_esp_ipv4_to_be_local(host,0);
 vl->peer_addr.sin_port=htons((uint16)port);
 es->sock_info[sock]=vl->peer_addr;
 es->proto[sock]=es->protocol[sock]=es->link_type[sock]=ESP_PROTO_UDP;
 es->link_port[sock]=port;es->udp_local_port[sock]=(uint16)local_port;
 es->udp_mode[sock]=(uint8)udp_mode;
 snprintf(es->link_host[sock],sizeof(es->link_host[sock]),"%s",host);
 es->link_state[sock]=CU_ESP_LS_OPEN;es->link_err[sock]=0;
 es->link_notice_ok[sock]=1;es->link_notice_err[sock]=0;
 return 0;
}

static boole cu_esp_lan_same_peer(const struct sockaddr_in *a,const struct sockaddr_in *b)
{
 return a->sin_family==AF_INET && b->sin_family==AF_INET &&
        a->sin_port==b->sin_port && a->sin_addr.s_addr==b->sin_addr.s_addr;
}

static sint32 cu_esp_lan_udp_send(cu_state_esp_t *es,uint32 sock,const uint8 *data,uint16 len)
{
#ifndef __EMSCRIPTEN__
 uint8 packet[CU_ESP_LAN_UDP_HEADER+CU_ESP_LAN_DATA_MAX]={0};
 cu_esp_lan_ctl_hdr_t h;
 cu_esp_lan_vlink_t *vl=&es->lan.vlinks[sock];
 const struct sockaddr_in *to=NULL;
 uint32 src;uint16 port;
 if(len>CU_ESP_LAN_DATA_MAX)return -1;
 if(es->lan.joined){
  to=&es->lan.joined_ap_addr;
  src=cu_esp_ipv4_to_be_local((char*)es->station_ip,0);
  memcpy(packet+12,es->lan.joined_ap_bssid,6);
  if(!cu_esp_ap_get_station_mac_bytes(es,packet+18))return -1;
 }else if(es->lan.adv_enable){
  src=es->lan.ap_ip_be;memcpy(packet+12,es->lan.local_mac,6);
  for(uint32 i=0;i<CU_ESP_LAN_DHCP_MAX_LEASES;i++){
   cu_esp_lan_lease_t *lease=&es->lan.leases[i];
   if(lease->in_use && lease->ip_be==vl->peer_addr.sin_addr.s_addr){to=&lease->peer_addr;break;}
  }
  /* UDP can be opened/sent before the station joins, as Tank Fu does. */
  if(!to)return len;
 }else return -1;
 h.magic_be=htonl(CU_ESP_LAN_CTL_MAGIC);h.version=CU_ESP_LAN_CTL_VERSION;
 h.type=CU_ESP_LAN_CTL_UDP;h.len_be=htons((uint16)(24+len));h.seq_be=0;
 memcpy(packet,&h,sizeof(h));memcpy(packet+24,&src,4);
 memcpy(packet+28,&vl->peer_addr.sin_addr.s_addr,4);
 port=htons(vl->local_port);memcpy(packet+32,&port,2);
 memcpy(packet+34,&vl->peer_addr.sin_port,2);memcpy(packet+36,data,len);
 return sendto(es->lan.ucast_sock,(char*)packet,36+len,0,(const struct sockaddr*)to,sizeof(*to))==36+len?len:-1;
#else
 (void)es;(void)sock;(void)data;(void)len;return -1;
#endif
}

static void cu_esp_lan_udp_receive(cu_state_esp_t *es,const uint8 *packet,
 sint32 length,const struct sockaddr_in *from)
{
 uint32 src,dst,local;uint16 src_port,dst_port;boole authenticated=FALSE;
 if(length<(sint32)CU_ESP_LAN_UDP_HEADER || length>CU_ESP_LAN_UDP_HEADER+CU_ESP_LAN_DATA_MAX)return;
 memcpy(&src,packet+24,4);memcpy(&dst,packet+28,4);
 memcpy(&src_port,packet+32,2);memcpy(&dst_port,packet+34,2);
 if(es->lan.joined && cu_esp_lan_same_peer(from,&es->lan.joined_ap_addr) &&
    !memcmp(packet+12,es->lan.joined_ap_bssid,6)){
  local=cu_esp_ipv4_to_be_local((char*)es->station_ip,0);authenticated=TRUE;
 }else if(es->lan.adv_enable && !memcmp(packet+12,es->lan.local_mac,6)){
  local=es->lan.ap_ip_be;
  for(uint32 i=0;i<CU_ESP_LAN_DHCP_MAX_LEASES;i++){
   cu_esp_lan_lease_t *lease=&es->lan.leases[i];
   if(lease->in_use && lease->ip_be==src && !memcmp(lease->mac,packet+18,6) &&
      cu_esp_lan_same_peer(from,&lease->peer_addr)){authenticated=TRUE;break;}
  }
 }else return;
 if(!authenticated || dst!=local || !src_port || !dst_port)return;
 for(uint32 i=0;i<ESP_MAX_LINKS;i++){
  cu_esp_lan_vlink_t *vl=&es->lan.vlinks[i];
  if(!cu_esp_lan_direct_udp(vl) || vl->local_port!=ntohs(dst_port))continue;
  if(!vl->udp_mode && (vl->peer_addr.sin_addr.s_addr!=src || vl->peer_addr.sin_port!=src_port))continue;
  if(length==CU_ESP_LAN_UDP_HEADER)return;
  if(cu_esp_lan_queue_vrx(es,i,packet+36,(uint16)(length-36))<0)return;
  if(vl->udp_mode==2 || (vl->udp_mode==1 && !es->udp_mode1_latched[i])){
   vl->peer_addr.sin_addr.s_addr=src;vl->peer_addr.sin_port=src_port;
   es->udp_mode1_latched[i]=1;
  }
  es->sock_info[i].sin_family=AF_INET;es->sock_info[i].sin_addr.s_addr=src;
  es->sock_info[i].sin_port=src_port;
  return;
 }
}
