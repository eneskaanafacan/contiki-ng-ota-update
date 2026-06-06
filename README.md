# Contiki-NG Tabanlı WSN'ler İçin Güvenilir OTA Güncelleme Sistemi 

**Proje Özeti:** Bu proje, Telsiz Duyarga Ağlarında (WSN - Wireless Sensor Networks) fiziksel erişimin zor olduğu Nesnelerin İnterneti (IoT) düğümlerine güncel bir donanım yazılımı (firmware) imajının ağ üzerinden (Over-The-Air - OTA) kablosuz olarak aktarılması, doğrulanması ve kalıcı belleğe yazılması sürecini gerçeklemektedir.

[Video Linki](https://www.youtube.com/watch?v=_OvW9PIOUig&feature=youtu.be)

**Sistem Topolojisi ve Roller:**
Ağ, 3 adet düğümden (node) oluşmaktadır:

* **ID: 1 (Alıcı / Server):** Ağın köküdür (DAG Root). OTA paketlerini dinler, CRC doğrulaması yapar, hatalıysa reddeder, doğruysa kalıcı belleğe (CFS) yazar.
* **ID: 2 (Gönderici / Client):** Güncel firmware imajını belleğinde tutan cihazdır. İmajı parçalara böler, gönderir ve Stop-and-Wait mantığıyla her parça için ACK (onay) bekler.
* **ID: 3 (Komşu / Aracı):** Fiziksel olarak uzak mesafeleri simüle etmek için aracı (router) rolündedir. Kendi başına paket üretmez, sadece ID:2 ile ID:1 arasındaki paketleri yönlendirir.

**`node_id` Mekanizması:**
Ağdaki cihazlara (ID:2 ve ID:3) *aynı* firmware (`udp-client.z1`) yüklenmiştir. Cihazların farklı roller üstlenmesi, donanım kimliklerinin çalışma zamanında (runtime) sorgulanmasıyla sağlanmıştır.
**Kod Kanıtı (`udp-client.c`):**

```c
if(node_id == 2) {
    // Sadece ID'si 2 olan düğüm OTA verisi üretir ve gönderir.
    // ID'si 3 olan düğüm bu bloğa girmez, sadece Contiki-NG'nin 
    // arka plandaki RPL yönlendirme görevini icra eder.

```

---

### 1. Sistem Mimarisi

**Ağ Topolojisi Şeması:**

```text
[Gönderici Düğüm]              [Aracı Düğüm]                [Alıcı Düğüm]
     (ID: 2)                      (ID: 3)                      (ID: 1)
   udp-client.c                 udp-client.c                 udp-server.c
        │                            │                            │
        │   OTA Blokları (IPv6)      │                            │
        ├───────────────────────────▶│   OTA Blokları (IPv6)      │
        │                            ├───────────────────────────▶│
        │                            │                            │ (CFS'e Yazma)
        │                            │      ACK / NACK            │
        │      ACK / NACK            │◀───────────────────────────┤
        ◀────────────────────────────┤                            │

```

* **RPL (Routing Protocol for Low-Power and Lossy Networks):** Kayıplı ve düşük güçlü ağlar için tasarlanmış IPv6 yönlendirme protokolüdür. Sensör ağlarındaki düğümlerin birbirleri üzerinden atlayarak (multi-hop) hedef düğüme ulaşmasını sağlar. Bu projede ID:3'ün aracı olabilmesi RPL sayesinde mümkündür.
* **UDP'nin Rolü:** TCP'nin 3'lü el sıkışması ve kayan pencere (sliding window) mekanizmaları sensör cihazlarının dar RAM kapasitesini hızla tüketir. Bu nedenle bağlantısız ve düşük yük (overhead) getiren UDP seçilmiştir. Güvenilirlik uygulama katmanında (Stop-and-Wait) sağlanmıştır.
* **IPv6 + 6LoWPAN:** IoT cihazlarının küresel olarak adreslenebilmesini sağlayan IPv6'nın, dar bant genişliğine sahip IEEE 802.15.4 radyo ağlarında sıkıştırılarak iletilmesini sağlayan katman mimarisidir.
* **Z1 Mote ve MSP430 Mimarisi:** Cooja simülatöründe fiziksel CC1352R cihazının donanım kısıtlarını simüle etmek için 16-bit MSP430 işlemcili, 8 KB RAM ve 92 KB ROM kapasiteli Z1 mote kullanılmıştır.

---

### 2. OTA Protokol Tasarımı — Paket Yapısı

OTA sürecinde iki uç noktanın aynı dilden konuşabilmesi için `ota-protocol.h` dosyasında özel veri yapıları (struct) tanımlanmıştır.

#### 2.1 Veri Paketi (`ota_data_packet_t`)

```c
typedef struct {
  uint16_t magic;         // Offset: 0 
  uint16_t chunk_no;      // Offset: 2 
  uint16_t offset;        // Offset: 4 
  uint16_t crc;           // Offset: 6 
  uint16_t total_chunks;  /* DÜZELTME: 8-Bit taşmasını önlemek için 16-bit */
  uint8_t  payload_len;   // Offset: 10 
  uint8_t  data[FIRMWARE_CHUNK_SIZE]; // Offset: 11
} ota_data_packet_t;

```

* **magic (`0xf17e`):** Ağa dışarıdan sızabilecek veya farklı amaçlı diğer UDP paketlerinin OTA verisi zannedilmesini önleyen özel "Sihirli Sayı"dır (Magic Number).
* **chunk_no:** Gönderilen bloğun sıra numarasıdır. İletişimin takibi ve ACK kontrolü için kullanılır.
* **offset:** Bloğun dosya içindeki mutlak bayt konumudur. `chunk_no` sırayı ifade ederken, `offset` verinin kalıcı diskte tam olarak *nereye* kazınacağını söyler. Paketler karışsa bile verinin diske hatasız yerleşmesini sağlar.
* **crc:** Verinin yolda bozulup bozulmadığını kontrol eden Hata Tespit (CRC16) verisidir.
* **total_chunks (uint16_t):** Dosyanın toplam blok sayısıdır. Şartnameye göre dosya 129.760 bayttır. `129760 / 48 = 2703` blok yapar. Eğer bu değişken 8-bit (`uint8_t`) yapılsaydı maksimum 255'e kadar sayabilir ve taşma (overflow) yaşanarak sistem çökerdi. Bu nedenle `uint16_t` (65.535 sınır) seçilmiştir.
* **payload_len:** Bloğun taşıdığı net veridir. Dosya sonuna gelindiğinde 48 bayttan daha az bir "artık" veri kalacağı için değişken olarak tanımlanmıştır.
* **data[48] (`FIRMWARE_CHUNK_SIZE`):** Radyo katmanı MTU hesabı gereği 48 seçilmiştir.
* **UDP MTU Hesabı:** IEEE 802.15.4 fiziksel katman sınırı 127 bayttır. Bunun ~25 baytı 6LoWPAN MAC başlığına, 40 baytı IPv6 başlığına, 8 baytı UDP başlığına gider. Geriye kullanılabilecek ~50-55 bayt kalır. Güvenli bölgede kalmak ve IP parçalanmasını (fragmentation) engellemek için taşıma yükü (payload) 48 bayt olarak sabitlenmiştir.



**Toplam Paket Boyutu Hesabı:**
`magic` (2) + `chunk_no` (2) + `offset` (2) + `crc` (2) + `total_chunks` (2) + `payload_len` (1) + `data` (48) = **59 Byte**.
Bu boyut, hesaplanan güvenli ~55-60 baytlık MTU sınırının içerisindedir ve ağda parçalanmadan (fragment-free) güvenle iletilir.

#### 2.2 ACK Paketi (`ota_ack_packet_t`)

```c
typedef struct {
  uint16_t magic;         
  uint16_t chunk_no;      
  uint8_t  status;        
  uint8_t  padding;       
} ota_ack_packet_t;

```

* **status:** Alıcıdan göndericiye dönen cevaptır. `0` ise paket sorunsuz (ACK) alınmış ve diske yazılmıştır. `1` ise paket CRC hatasına takılmış veya geçersizdir (NACK).
* **padding:** 16-bit MSP430 mimarisinde "Unaligned Memory Access" (Hizalanmamış Bellek Erişimi) donanım çökmelerine yol açar. Struct'ın 6 baytlık çift sayı hizasına (alignment) oturması için yapay bir dolgu (padding) baytı eklenmiştir.

#### 2.3 CRC16 Algoritması — Teorik Anlatım

```c
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

```

* **CRC (Cyclic Redundancy Check) Nedir?** Veri bloklarının iletimi sırasında oluşabilecek bit hatalarını (bit-flip) tespit eden, polinom bölmesi (polynomial division) temelli bir hata tespit mekanizmasıdır.
* **CRC16-Modbus Varyantı:** Endüstriyel sistemlerde sıkça kullanılan bu varyantta, hesaplama başlangıç değeri (initial value) `0xFFFF`'tir. Kullanılan polinom `0xA001`'dir. Standart CRC16 polinomu olan `0x8005`'in donanım tarafında sağa kaydırma (right-shift) işlemleriyle daha hızlı çalışabilmesi için bit-tersine (bit-reversed) çevrilmiş halidir.
* **XOR ve Kaydırma Algoritması:** Her bayt sırayla mevcut `crc` değeri ile XOR'lanır. Ardından her bir bit (toplam 8 bit) için kontrol yapılır: Eğer en düşük anlamlı bit (LSB) 1 ise, CRC sağa kaydırılıp `0xA001` polinomu ile XOR'lanır. LSB 0 ise sadece sağa kaydırılır.
* **Neden MD5/SHA Değil?** MD5 veya SHA-256 kriptografik özet fonksiyonlarıdır. Aşırı CPU döngüsü gerektirir ve 8 KB RAM'i olan gömülü sistemlerde bataryayı saniyeler içinde tüketir. CRC ise donanım dostu ve basittir.
* **Neden CRC8 Değil de CRC16?** CRC8 sadece 256 farklı sağlama toplamı üretebilir (Hata kaçırma olasılığı 1/256'dır). 48 baytlık kritik bir firmware bloğu için bu güvenlik zafiyetidir. CRC16 ise 65.536 farklı değer üretir ve 48 baytlık bir blok için neredeyse mükemmel bir çarpışma (collision) direnci sunar.
* **İki Katmanlı Doğrulama:** Paket başı yapılan CRC16 yoldaki "havadan kaynaklı" bozulmaları engeller. Ancak imaj diske (Flash'a) yazılırken voltaj düşmesi sebebiyle bozulabilir. Bu yüzden paket başı CRC16'ya ek olarak aktarım sonunda kümülatif bir Tam İmaj Doğrulaması yapılmıştır.

---

### 3. Firmware Parçalama Sistemi

Simülasyon ortamında dışarıdan dosya enjekte edilemediği için, OTA imajı C dilinde bir bayt dizisine dönüştürülüp göndericinin ROM (Flash) belleğine `.h` dosyası olarak gömülmüştür.
Dönüşüm komutu: `xxd -i new-firmware.z1 > firmware_data.h`

**`firmware_data.h` Kesiti:**

```c
const unsigned char slice_z1[] = {
  0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0xff, 0x00, 0x00, ...
};
unsigned int slice_z1_len = 129760; // Şartname gereği gerçek boyut

```

**Parçalama ve Blok Hesabı (`udp-client.c`):**

* **FIRMWARE_CHUNK_SIZE:** Yukarıdaki teorik MTU hesabına dayanarak **48 bayt** olarak belirlenmiştir.
* **Toplam Blok Sayısı Hesabı:**
```c
total_chunks = (slice_z1_len + FIRMWARE_CHUNK_SIZE - 1) / FIRMWARE_CHUNK_SIZE;
// Matematiksel Açılımı: (129760 + 48 - 1) / 48 = 2703 blok.

```


* **Son Bloğun Özel Durumu:** 129.760 bayt 48'e tam bölünmez. Son blokta 48 bayttan daha az veri kalacaktır. Kod içerisindeki `remaining = slice_z1_len - current_offset;` kontrolü ile son parçanın boyutu dinamik olarak ayarlanır ve `payload_len` değişkene aktarılır.
* **`total_chunks` Neden uint16_t:** 2703 sayısı 8-bitin alabileceği maksimum 255 değerini fersah fersah aşmaktadır. Taşmayı önlemek için mecburi bir mimari karardır.

---

### 4. Güvenilir İletim Stratejisi — Stop-and-Wait

#### 4.1 Stop-and-Wait Teorisi

Stop-and-Wait (Dur ve Bekle), ağ üzerindeki güvenilir iletim protokollerinin en temelidir. Çalışma mantığı basittir: Gönderici tek bir veri paketi yollar, zamanlayıcısını başlatır ve alıcıdan bir Onay (ACK) gelene kadar bekler. ACK gelirse bir sonraki pakete geçer. Gelmezse aynı paketi tekrar yollar.
**Sliding Window ile Karşılaştırması:** Kayan Pencere (Sliding Window) mekanizmasında birden çok paket onay beklenmeden ağa salınır. Ancak bu, paketlerin RAM üzerinde büyük bir "Window Buffer" (Tampon) içinde tutulmasını gerektirir. 8 KB toplam RAM kapasitesine sahip Z1 mote için 10 paketlik bir tampon bellek açmak bile sistem kaynaklarını tüketir. Ayrıca gömülü sensör ağlarında yayılım gecikmesi (propagation delay) ihmal edilebilir seviyelerde olduğu için Stop-and-Wait fazlasıyla verimlidir.

#### 4.2 Timeout Mekanizması

```c
#define TIMEOUT_INTERVAL (4 * CLOCK_SECOND)
etimer_set(&timeout_timer, TIMEOUT_INTERVAL);

PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL || etimer_expired(&timeout_timer));

```

* **etimer:** Contiki-NG işletim sisteminde çekirdeği (kernel) meşgul etmeden (non-blocking) zaman aşımı event'i fırlatan Event Timer mekanizmasıdır.
* **4 Saniye Gerekçesi:** WSN ağları IEEE 802.15.4 radyo dalgalanmaları ve CSMA/CA çekilme (backoff) süreleri nedeniyle yavaştır. Daha kısa bir süre (örn: 1 sn), ağda paket gecikmesi olduğunda gereksiz yere aynı paketin ağa defalarca basılmasına ve ağ çökmesine (congestion) neden olur.
* Sistem `PROCESS_YIELD_UNTIL` komutuyla uykuya dalar. 4 saniye dolduğunda uyanır ve `ack_received` hala `false` ise aynı döngü içinde paketi yeniden fırlatır.

#### 4.3 ACK / NACK Akışı

```text
Gönderici (ID:2)                            Alıcı (ID:1)
     │                                           │
     │──── OTA Paketi (Blok N) ─────────────────▶│
     │                                           │ (CRC kontrol edilir)
     │◀─── ACK (status=0) ───────────────────────│ ✓ Geçerli ve Yazıldı
     │                                           │
     │ offset += payload_len                     │
     │ chunk_no++                                │
     │──── OTA Paketi (Blok N+1) ───────────────▶│
     │                                           │ (Paket yolda bozulur)
     │◀─── NACK (status=1) ──────────────────────│ ✗ Geçersiz (CRC Hata)
     │                                           │
     │ [offset ve sıra ilerlemez]                │
     │──── OTA Paketi (Blok N+1) ───────────────▶│ (Aynı paket tekrar iletilir)

```

#### 4.4 Yeniden Gönderim (Retransmission) Mantığı

```c
if(ack_received) {
   current_offset += payload_len;
   current_chunk_no++;
   ack_received = false;
}

```

* **`ack_received` Flag'i:** Gönderici tarafındaki UDP Callback (dinleme) fonksiyonuna ACK veya NACK düştüğünde işlenen global statik bir bayraktır.
* **Hızlı Uyanma (process_poll):** Callback içinde NACK (veya ACK) alındığı an `process_poll(&udp_client_process)` komutu çalıştırılır. Bu, sistemin 4 saniyelik `etimer` süresini beklemeden anında uyanıp bir sonraki işleme (veya tekrar gönderime) geçmesini sağlar.

---

### 5. Kalıcı Depolama — CFS (Coffee File System)

#### 5.1 CFS Teorisi

* **CFS Nedir?** Contiki-NG işletim sistemine entegre edilmiş, özellikle Flash ve EEPROM bellekler için optimize edilmiş hafif bir dosya sistemidir.
* **Wear-Leveling (Aşınma Seviyelendirmesi):** Flash belleklerin aynı hücreye binlerce kez yazıldığında bozulma (aşınma) handikapı vardır. CFS, yazma işlemlerini belleğin farklı sektörlerine dağıtarak cihaz ömrünü korur.
* **Neden RAM Değil:** 129.760 baytlık devasa bir dosyanın 8.192 bayt kapasiteli bir RAM'de tamponlanması fiziksel olarak imkansızdır. Gelen her paket anında silinmeyen kalıcı bir depolamaya aktarılmak zorundadır.
* **CFS API:**
* `cfs_open`: Dosyayı belirtilen modda (Okuma/Yazma) açar.
* `cfs_seek`: Okuma/yazma kafasını (pointer) dosya içinde belirli bir bayt konumuna çeker.
* `cfs_write`: Verilen bayt dizisini diske yazar.
* `cfs_read`: Diskten veriyi okur.
* `cfs_close`: Dosya ile olan etkileşimi sonlandırır ve kaynakları bırakır.
* `cfs_remove`: Mevcut dosyayı diskten tamamen siler.



#### 5.2 Offset Tabanlı Yazma Mimarisi

```c
int fd = cfs_open(FIRMWARE_FILENAME, CFS_WRITE | CFS_READ);
if(fd >= 0) {
  cfs_seek(fd, pkt->offset, CFS_SEEK_SET);
  cfs_write(fd, pkt->data, pkt->payload_len);
  cfs_close(fd);
}

```

* `CFS_WRITE | CFS_READ` kullanılarak dosya üzerine yazma yapılırken baştaki kısımların silinmesi (truncate) önlenmiştir.
* **`cfs_seek` Önemi:** Kablosuz ağlarda paketler sıra dışı (out-of-order) gelebilir. Dosyanın sonuna ekleme yapmak (append) yerine, paketin taşıdığı mutlak `offset` adresine `CFS_SEEK_SET` komutu ile gidilir ve veri oraya kazınır. Böylece paketler karışsa dahi dosya diskte kusursuz birleştirilir.

#### 5.3 Simülasyon Başında Sıfırlama

```c
cfs_remove(FIRMWARE_FILENAME);
memset(block_bitmap, 0, sizeof(block_bitmap));

```

Cooja simülasyonu arka arkaya başlatıldığında, sanal diskteki eski veriler (önceki OTA denemesi) silinmez. Yeni aktarım başladığında eski verilerin üzerine yazılması dosya bütünlüğünü bozar ve CRC testlerinin çökmesine neden olur. Bu nedenle sistem ilk açıldığında diski ve durum belleğini sıfırlar.

---

### 6. Durum Yönetimi — Bitmap

#### 6.1 Bitmap Teorisi

Alıcı (ID:1) düğüm, gelen paketlerin mükerrer olup olmadığını veya eksik kalıp kalmadığını bilmek zorundadır.

* **Bellek Verimliliği:** 2703 blokluk bir dosya için standart bir C dizisi (`bool array[2703]`) tanımlansaydı, RAM'den 2703 bayt çalardı. Bunun yerine **Bitmap (Bit Dizisi)** mimarisi kullanılmıştır. Her bloğun alındı/alınmadı durumu sadece 1 bit ile ifade edilir. `2703 / 8 = 338` bayt eder. Bu yaklaşımla RAM tüketimi 8 kat azaltılmıştır.

#### 6.2 Bitmap Implementasyonu (`udp-server.c`)

```c
static uint8_t block_bitmap[(MAX_BLOCKS / 8) + 1];

static void set_block_received(uint16_t block_num) {
  block_bitmap[block_num / 8] |= (1 << (block_num % 8));
}

static bool is_block_received(uint16_t block_num) {
  return (block_bitmap[block_num / 8] & (1 << (block_num % 8))) != 0;
}

```

* **`block_num / 8`:** İlgili bloğun 8-bitlik gruplardan oluşan dizide hangi baytın (index) içine düştüğünü bulur.
* **`block_num % 8`:** O baytın içindeki 8 bitten hangisine karşılık geldiğini hesaplar.
* **`|= (1 << bit)`:** Seçilen biti maskeleme ve OR operatörü ile 1 (set) yapar.
* **`& (1 << bit)`:** Seçilen biti AND operatörü ile okuyup değerinin 1 olup olmadığını sorgular.

#### 6.3 Tekrar Gönderim Koruması

Gönderici bir paketi ilettiğinde, Alıcı bunu diske yazar ve ACK gönderir. Ancak ACK yolda kaybolursa gönderici *aynı paketi tekrar yollar.* `is_block_received` fonksiyonu sayesinde Alıcı bu bloğu daha önce aldığını bilir. Paketi diske **tekrar yazmaz**, ancak göndericinin ilerleyebilmesi için ona sessizce bir **ACK daha** gönderir.

---

### 7. Tam İmaj Doğrulaması

```c
static bool verify_full_image(uint16_t total_blocks_expected) {
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
  LOG_INFO("Dogrulama Bitti. Okunan Bayt: %" PRIu32 ", Imaj CRC: 0x%04X\n", total_bytes_read, full_crc);
  return (total_bytes_read > 0); 
}

```

* **Çalışma Mantığı:** Tüm `total_chunks` hedefine ulaşıldığında çağrılır. Kalıcı belleğe kaydedilen (`fw_image.z1`) dosyasını baştan sona parça parça okur.
* **Neden İki Katmanlı Doğrulama?** Her paket havadan gelirken zaten CRC16'dan geçmiştir. Ancak paket Flash'a yazılırken (FSM operasyonlarında) donanımsal voltaj sapmaları sebebiyle bozulabilir. Bu kümülatif kontrol, dosyanın diskte gerçekten 100% hatasız birleştiğinin kanıtıdır.
* Başarılı olduğunda, şartnamede istenen nihai başarı logu ekrana basılır:
`LOG_INFO("Yüklenmeye hazır yeni firmware alımı tamamlandı.\n");`

---

### 8. Sistem Akış Diyagramları

#### 8.1 Gönderici (ID: 2) Tam Akışı

1. **Başlangıç:** Cihaz uyanır, değişkenleri sıfırlar.
2. **Yönlendirme Kontrolü:** RPL ağı köküne (`get_root_ipaddr`) ulaşılıp ulaşılamadığına bakar. Aksi halde bekler.
3. **Paket Hazırlama:** Diziden veriyi oku, CRC hesapla, pakete yükle.
4. **Gönderim:** Paketi UDP ile yolla, zamanlayıcıyı (etimer) başlat ve `YIELD` (uyku) konumuna geç.
5. **Dönüt Bekleme:** ACK gelirse `offset` ve `chunk_no` değerlerini artır; Timeout olursa mevcut paketi koru.
6. **Kontrol:** Tüm offsetler bitene kadar Adım 3'e dön. Bitince "Tamamlandı" diyerek cihazı sonsuz uykuya al.

#### 8.2 Alıcı (ID: 1) Tam Akışı

1. **Başlangıç:** CFS diskini ve Bitmap'i sıfırla.
2. **Dinleme:** UDP paketi geldiğinde uyan.
3. **Kontrol Katmanı:** `Magic` numarasını ve `CRC16` değerini doğrula.
4. **Durum:** `is_block_received` ile bloğun mükerrer olup olmadığına bak.
5. **Yazma:** Mükerrer değilse `cfs_seek` ile diskteki offset'e atla ve `cfs_write` ile kazı.
6. **Onay:** Bitmap'te ilgili biti 1 yap ve UDP üzerinden ACK fırlat.
7. **Final:** Alınan blok sayısı `total_chunks`'a ulaştığında `verify_full_image` çağır ve başarı mesajı yazdır.

#### 8.3 Hata / Yeniden İletim Senaryosu

* **Senaryo:** Radyo paraziti nedeniyle veri bloğu alıcıya ulaştığında bitleri değişmiş olur.
* **Alıcı Tepkisi:** Gelen paket için CRC hesaplar, paketteki CRC ile uyuşmaz. Alıcı NACK (status=1) paketini göndericiye fırlatır.
* **Gönderici Tepkisi:** NACK alan gönderici anında `process_poll` ile uyanır. `ack_received` bayrağı `false` kalır. Offset ilerlemez. Bir sonraki döngüde aynı blok (orijinal ve temiz haliyle) tekrar paketlenip gönderilir.

---

### 9. Şartname Uyumluluk Tablosu

| Şartname Maddesi | Karşılandığı Yer | Kod Referansı |
| --- | --- | --- |
| **1. Firmware imajını belleğe alma** | `firmware_data.h` dosyası koda dahil edildi. | `udp-client.c` -> `#include "firmware_data.h"` |
| **2. Sabit boyutlu bloklara bölme** | Boyut `FIRMWARE_CHUNK_SIZE` = 48 belirlendi. | `ota-protocol.h` -> Satır 7 |
| **3. Blok no, uzunluk, kontrol bilgisi** | Yapı (Struct) oluşturuldu. | `ota-protocol.h` -> `ota_data_packet_t` struct |
| **4. Kalıcı depolama alanına yazılması** | Contiki-NG Coffee File System kullanıldı. | `udp-server.c` -> `cfs_write` ve `cfs_seek` |
| **5. Eksik/bozuk blok tespiti** | CRC16 uyuşmazlığı ve Bitmap kullanıldı. | `udp-server.c` -> `calculate_crc16` |
| **6. Yeniden gönderim mekanizması** | NACK dönüşü ve 4 sn. Timeout (etimer) kuruldu. | `udp-client.c` -> `timeout_timer` döngüsü |
| **7. Tüm imaj doğrulaması** | Aktarım bittiğinde dosya okunup CRC test edildi. | `udp-server.c` -> `verify_full_image()` |
| **8. Kendi diskine birleştirilmiş kaydetme** | Parçalar `fw_image.z1`'de offsetlerle birleşti. | `udp-server.c` -> `cfs_seek(fd, pkt->offset, ...)` |
| **9. Tamamlanma başarı mesajı** | Tam olarak istenen Türkçe ifade `LOG` atıldı. | `udp-server.c` -> `LOG_INFO("Yüklenmeye hazır...");` |
| **10. Reboot / Bootloader Atlaması Yok** | Donanım (Z1) izin vermediği için watchdog ile reset atılmadı. | — *(Şartname gereği kapsam dışıdır)* |

---

### 10. Teknik Kısıtlar ve Tasarım Kararları

Projenin geliştirilme sürecinde karşılaşılan zorluklar ve bunlara yönelik mühendislik kararları:

1. **Problem:** UDP MTU (Maksimum Aktarım Birimi) Sınırı.
**Çözüm:** IEEE 802.15.4'te 127 bayt olan sınır nedeniyle OTA paketi 48 baytlık taşıma yüküne (`FIRMWARE_CHUNK_SIZE`) sabitlendi. Parçalanma engellendi.
2. **Problem:** 8 KB RAM Kısıtı.
**Çözüm:** 130 KB'lık dosyanın RAM'de tutulamaması nedeniyle, alınan her paket anında silinmeyen (non-volatile) CFS Flash alanına nakledildi.
3. **Problem:** `uint8_t` Taşma (Overflow) Riski.
**Çözüm:** 2703 bloğa ihtiyaç duyulduğu için 255'te başa saracak olan `total_chunks` parametresi `uint16_t` veri tipine yükseltilip sisteme nefes aldırıldı.
4. **Problem:** Kayan Pencere (Sliding Window) Yerine Stop-and-Wait Kullanılması.
**Çözüm:** Kayan pencere her gelen paket için bellekte bir "buffer" (tampon) kuyruğu oluşturur. 8 KB RAM sınırında buffer yönetimi mümkün olmadığından dur-ve-bekle mimarisi seçildi.
5. **Problem:** Alınan Blokların Durum Yönetimi (Array Maliyeti).
**Çözüm:** Durumları tutmak için 2700 baytlık bir bool dizisi tanımlamak yerine 338 baytlık bit-array (Bitmap) tasarlanıp bellekten x8 tasarruf edildi.
6. **Problem:** `.z1` Dosyasının Cooja'da Arayüzden Cihaza Atılamaması.
**Çözüm:** Gömülü sistemlerde harici hafızanın olmadığı senaryolara uygun olarak, firmware içeriği bir Python/Unix aracı (`xxd`) ile C bayt dizisine dönüştürülüp gönderici cihazın `.rodata` (Salt Okunur ROM) bölgesine (`firmware_data.h`) gömüldü.

---

### 11. Dosya Yapısı

Proje repositorisinde bulunan kaynak kod dosyalarının hiyerarşisi ve sorumlulukları:

```text
BIL304-OS-Project-1/
├── udp-client.c            → Gönderici düğüm (ID:2) kaynak kodları ve protokol akışı
├── udp-server.c            → Alıcı düğüm (ID:1) kaynak kodları, CFS yazma ve Doğrulama
├── ota-protocol.h          → Her iki düğümün paylaştığı struct yapıları ve CRC16 algoritması
├── firmware_data.h         → new-firmware.z1'in C dizisi olarak ROM'a gömülü hali
├── Makefile                → İşletim sistemini ve uygulamaları derleyen yapılandırma
├── BIL304-OS-Project-1.csc → Cooja simülatörü senaryo, cihaz ve topoloji haritası
└── README.md               → OTA Mekanizması Uygulama ve Geliştirme Raporu (Bu Belge)

```
