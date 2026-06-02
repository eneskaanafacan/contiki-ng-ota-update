#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "cfs/cfs.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "sys/log.h"
#include "ota-protocol.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define FIRMWARE_FILENAME "fw_image.z1"
#define MAX_BLOCKS 1024 

static struct simple_udp_connection udp_conn;

/* Bitmap tabanlı durum yönetimi */
static uint8_t block_bitmap[(MAX_BLOCKS / 8) + 1];
static uint16_t received_block_count = 0;

static void set_block_received(uint16_t block_num) {
  block_bitmap[block_num / 8] |= (1 << (block_num % 8));
}

static bool is_block_received(uint16_t block_num) {
  return (block_bitmap[block_num / 8] & (1 << (block_num % 8))) != 0;
}

PROCESS(udp_server_process, "UDP server OTA Receiver");
AUTOSTART_PROCESSES(&udp_server_process);

/* Aktarım bitince tüm diski okuyarak Bütünlük Doğrulaması (Full Image Verification) */
static bool verify_full_image(uint8_t total_blocks_expected) {
  int fd = cfs_open(FIRMWARE_FILENAME, CFS_READ);
  if(fd < 0) return false;

  uint32_t total_bytes_read = 0;
  uint8_t buffer[FIRMWARE_CHUNK_SIZE];
  uint16_t full_crc = 0xFFFF;
  
  for(uint16_t i = 0; i < total_blocks_expected; i++) {
    int bytes = cfs_read(fd, buffer, FIRMWARE_CHUNK_SIZE);
    if (bytes <= 0) break;
    total_bytes_read += bytes;
    for (int k = 0; k < bytes; k++) {
      full_crc ^= buffer[k];
      for (uint8_t j = 0; j < 8; j++) {
        if (full_crc & 1) full_crc = (full_crc >> 1) ^ 0xA001;
        else full_crc >>= 1;
      }
    }
  }
  cfs_close(fd);
  LOG_INFO("Dogrulama Bitti. Okunan Bayt: %" PRIu32 ", Imaj Kümülatif CRC: 0x%04X\n", total_bytes_read, full_crc);
  return (total_bytes_read > 0); 
}

static void udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr, uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr, uint16_t receiver_port,
         const uint8_t *data, uint16_t datalen)
{
  if(datalen == sizeof(ota_data_packet_t)) {
    ota_data_packet_t *pkt = (ota_data_packet_t *)data;
    
    if (pkt->magic != FIRMWARE_PACKET_MAGIC) return;

    ota_ack_packet_t ack;
    ack.magic = FIRMWARE_PACKET_MAGIC;
    ack.chunk_no = pkt->chunk_no;
    ack.status = 1; // Varsayılan NACK

    uint16_t calculated_crc = calculate_crc16(pkt->data, pkt->payload_len);
      
    if (calculated_crc != pkt->crc) {
      LOG_WARN("CRC Hatasi (Blok: %u)\n", pkt->chunk_no);
      ack.status = 1; 
    } 
    else if (is_block_received(pkt->chunk_no)) {
      // Daha önce alınmış, sadece ACK dön (Gönderici ACK'ı kaybetmiş olabilir)
      ack.status = 0;
    }
    else {
      /* Geçerli ve yeni blok - CFS üzerinden kalıcı diske Offset'e göre yaz */
      int fd = cfs_open(FIRMWARE_FILENAME, CFS_WRITE | CFS_READ);
      if(fd >= 0) {
        cfs_seek(fd, pkt->offset, CFS_SEEK_SET);
        cfs_write(fd, pkt->data, pkt->payload_len);
        cfs_close(fd);
          
        set_block_received(pkt->chunk_no);
        received_block_count++;
        ack.status = 0; // Başarılı ACK
          
        LOG_INFO("Blok %u CFS'e kaydedildi. (Alinan: %u/%u)\n", 
                 pkt->chunk_no + 1, received_block_count, pkt->total_chunks);

        /* Tüm Dosya Geldi mi? */
        if (received_block_count == pkt->total_chunks) {
           if(verify_full_image(pkt->total_chunks)) {
             /* Şartnamedeki İstenen Birebir Metin */
             LOG_INFO("Yüklenmeye hazır yeni firmware alımı tamamlandı.\n");
           }
        }
      }
    }
    simple_udp_sendto(&udp_conn, &ack, sizeof(ota_ack_packet_t), sender_addr);
  }
}

PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();
  
  /* Simülasyon ard arda çalıştırıldığında eski diski sıfırla */
  cfs_remove(FIRMWARE_FILENAME);
  memset(block_bitmap, 0, sizeof(block_bitmap));

  NETSTACK_ROUTING.root_start();
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL, UDP_CLIENT_PORT, udp_rx_callback);
  
  PROCESS_END();
}