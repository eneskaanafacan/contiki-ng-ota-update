#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

#include <stdint.h>

#define FIRMWARE_PACKET_MAGIC 0xf17e
#define FIRMWARE_CHUNK_SIZE   48

/* GÖNDERİCİ -> ALICI Veri Paketi */
typedef struct {
  uint16_t magic;         // Offset: 0 
  uint16_t chunk_no;      // Offset: 2 
  uint16_t offset;        // Offset: 4 
  uint16_t crc;           // Offset: 6 
  uint16_t total_chunks;  /* DÜZELTME: 8-Bit taşmasını önlemek için 16-bit (uint16_t) yapıldı! */
  uint8_t  payload_len;   // Offset: 10 
  uint8_t  data[FIRMWARE_CHUNK_SIZE]; // Offset: 11
} ota_data_packet_t;

/* ALICI -> GÖNDERİCİ ACK (Onay) Paketi */
typedef struct {
  uint16_t magic;         
  uint16_t chunk_no;      
  uint8_t  status;        
  uint8_t  padding;       
} ota_ack_packet_t;

/* Basit CRC16 Hesaplama Fonksiyonu */
static inline uint16_t calculate_crc16(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

#endif /* OTA_PROTOCOL_H */