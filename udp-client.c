#include "contiki.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>

#include "sys/node-id.h"
#include "sys/log.h"
#include "ota-protocol.h"

/* Kestiğimiz 30KB'lık GERÇEK dosyayı dahil ediyoruz */
#include "firmware_data.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678
#define TIMEOUT_INTERVAL (4 * CLOCK_SECOND)

static struct simple_udp_connection udp_conn;
static uint16_t current_chunk_no = 0;
static uint16_t current_offset = 0;
static bool ack_received = false;

PROCESS(udp_client_process, "UDP client OTA Sender");
AUTOSTART_PROCESSES(&udp_client_process);

static void udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr, uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr, uint16_t receiver_port,
         const uint8_t *data, uint16_t datalen)
{
  if (datalen == sizeof(ota_ack_packet_t)) {
    ota_ack_packet_t *ack = (ota_ack_packet_t *)data;
    if (ack->magic == FIRMWARE_PACKET_MAGIC && ack->chunk_no == current_chunk_no) {
      if (ack->status == 0) {
        ack_received = true;
        process_poll(&udp_client_process);
      } else {
        LOG_WARN("NACK alindi (Blok: %u). Tekrar gonderilecek.\n", ack->chunk_no);
        process_poll(&udp_client_process); 
      }
    }
  }
}

PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer timeout_timer;
  static ota_data_packet_t pkt;
  
  static uip_ipaddr_t dest_ipaddr;
  static uint16_t remaining;
  static uint8_t payload_len;
  static uint16_t total_chunks; /* DÜZELTME: uint16_t olmalı! */

  PROCESS_BEGIN();

  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, udp_rx_callback);
  
  /* DÜZELTME: Toplam blok sayısı senin değişkenin olan slice_z1_len ile hesaplanıyor */
  total_chunks = (slice_z1_len + FIRMWARE_CHUNK_SIZE - 1) / FIRMWARE_CHUNK_SIZE;

  etimer_set(&timeout_timer, random_rand() % TIMEOUT_INTERVAL);

  while(1) {
    PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL || etimer_expired(&timeout_timer));

    if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
      if(node_id == 2) {
        
        if(ack_received) {
           current_offset += payload_len;
           current_chunk_no++;
           ack_received = false;
        }

        /* DÜZELTME: Dosya sonu kontrolü slice_z1_len ile yapılıyor */
        if (current_offset < slice_z1_len) {
          
          remaining = slice_z1_len - current_offset;
          payload_len = remaining > FIRMWARE_CHUNK_SIZE ? FIRMWARE_CHUNK_SIZE : remaining;

          pkt.magic = FIRMWARE_PACKET_MAGIC;
          pkt.chunk_no = current_chunk_no;
          pkt.offset = current_offset;
          pkt.total_chunks = total_chunks;
          pkt.payload_len = payload_len;
          
          /* DÜZELTME: Veri slice_z1 dizisinden okunuyor */
          memcpy(pkt.data, slice_z1 + current_offset, payload_len);
          pkt.crc = calculate_crc16(pkt.data, payload_len);

          LOG_INFO("OTA Gonderiliyor -> Blok: %u/%u (Boyut: %u)\n", 
                   current_chunk_no + 1, total_chunks, payload_len);
                   
          simple_udp_sendto(&udp_conn, &pkt, sizeof(ota_data_packet_t), &dest_ipaddr);

          etimer_set(&timeout_timer, TIMEOUT_INTERVAL);

        } else {
          LOG_INFO("Tum bloklar basariyla gonderildi!\n");
          etimer_set(&timeout_timer, TIMEOUT_INTERVAL * 10);
        }
      }
    } else {
      etimer_set(&timeout_timer, TIMEOUT_INTERVAL);
    }
  }
  PROCESS_END();
}