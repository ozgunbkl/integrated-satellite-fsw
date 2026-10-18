# ESP32 Wi-Fi ve UDP çalışma politikası

Bu, mevcut CubeSat simülasyonunun geliştirme/yer-istasyonu bağlantısıdır;
uçuşa hazır veya gerçek uydu radyo linki iddiası yoktur.

## Başlangıç

Zorunlu uydu servisleri ve kuyruk/mutexler önce hazırlanır. `WIFI_Start()` NVS,
TCP/IP, default event loop, station interface, event group, driver, RAM storage,
iki uygulama event handler'ı, station mode/config, driver start ve reconnect task
sonuçlarını kontrol eder. Uygulamanın bu hata yollarında abort/otomatik NVS erase yoktur.
TCP/IP hazır bilgisinin yayınlanamaması da artık ESP_FAIL döndürür.

SDK'nin abort eden `esp_netif_create_default_wifi_sta()` yardımcısı kullanılmaz:
aynı varsayılan STA config ile `esp_netif_new()`, attach ve default handlers ayrı
kontrol edilir. Önceden mevcut STA sahiplenilmez/silinmez. Bu servis bu projede
Wi-Fi'nin tek startup sahibidir; genel amaçlı çok-arayüz/hot-reinit API'si değildir.
NULL netif sonucu ESP_FAIL olarak bildirilir; bunun kesin NO_MEM olduğu iddia edilmez.

Başlangıç hatasında uygulama handler'ları kaldırılır, başlatılması denenmiş driver
durdurulur/deinit edilir, yalnız bu girişimin arayüz/driver/group kaynakları temizlenir.
İlk hata kodu korunur. Cleanup SDK çağrısı başarısızsa hâlâ kullanılabilecek kaynaklar
serbest bırakılmaz, hata bildirilir ve core devam eder. Tam heap rollback garantisi yoktur.
TCP/IP ve başarılı oluşturulmuş default event loop bilerek korunur; tam lwIP deinit/retry yoktur.

Bağlantı için en fazla 15 saniye beklenir. Timeout'ta temel görevler başlatılır;
Wi-Fi manager/sürücü kapatılmaz. Sonradan gelen gerçek IP ile haberleşme/log başlayabilir.
`Network at startup`/`log mirror` satırı yalnız ilk açılışın anlık görüntüsüdür.

## Çalışma sırasındaki bağlantı

`WIFI_MGR` (4096 bayt stack, priority 2) saniyede bir kontrol eder. Bağlantı denemeleri
1, 2, 4, 8, 16, en fazla 30 saniyelik backoff ile süresiz sürer; 10 denemede bırakılmaz.
Sonuç/events gelmeyen bir connect girişimi 20 saniye sonra disconnect/retry yoluna girer.
Driver connect/disconnect dönüşleri kontrol edilir. GOT_IP/STA_DISCONNECTED/STA_STOP/
LOST_IP olayları gerçek bağlı bayrağını günceller; IP kaybında yeni association istenir.
Otomatik full SDK init tekrarı veya başka bileşenin durdurduğu driver'ı sahiplenme yoktur.

TCP/IP hazırlığı ile canlı IP bağlantısı ayrı durumdur. TM/event sender hem hazır
stack hem canlı IP ister. Kopma IP bayrağını temizler ve sender socket'ini mutex altında
kapatır. Sonraki başarılı bağlantıda socket bir sonraki gönderimde yeniden açılır.
Send socket'inde 250 ms timeout vardır; sendto/timeout kurulumu hatası socket'i kapatır.
UDP başarı sonucu yalnız yerel gönderimdir, ground delivery/ACK değildir. Blind resend yoktur.

TC receiver, TCP/IP hazırsa IP yokken de UDP 10025'i dinleyebilir. Socket/bind/timeout
kurulum hataları 1 saniye beklemeyle retry edilir. Receive timeout/EINTR soketi korur;
kalıcı receive hatası kapatıp yeniden açar. Hata döngülerinde delay vardır, busy spin yoktur.
Stack hiç başlatılamadıysa receiver kendini sonlandırır; full network init recovery yapılmaz.

Log sender yalnız manager tarafından oluşturulur. İlk IP geç gelse de başlar;
allocation/task hatasında 30 saniye sonra retry edilir. Başarıdan sonra tek görev/queue
korunur, her kopmada yeniden tahsis edilmez. Offline loglar veya full queue'daki yeni
debug satırı düşürülür; güvenilir olay arşivi/ground ACK sistemi olduğu iddia edilmez.

Her 30 saniyede bağlantı durumu, bağlantı denemeleri, free/minimum heap ve manager
stack high-water headroom yazılır. ESP-IDF 5.5 Xtensa'da StackType_t uint8_t olduğundan
bu stack değerleri bayttır; diğer platformlara aynı birim varsayılmamalıdır.

## Yerel Wi-Fi ayarı ve gizlilik

Gerçek SSID/parola `main.c` veya `platformio.ini` içinde değildir. Bu makinedeki mevcut
ayarlar `local/wifi_credentials.h` dosyasına mekanik taşındı; `local/` Git tarafından yok sayılır.
Yeni klonda `config/wifi_credentials.example.h` örneğinden bu dosyayı oluşturun,
mevcut özel dosyanın üzerine yazmayın. Bu dosya yoksa firmware derlenir fakat Wi-Fi
eksik ayarı bildirir; temel uydu görevleri yine çalışır. WPA2 için SSID 1..32 bayt,
parola 8..64 bayt kontrol edilir; SDK ek config doğrulamasını yapar.

Wi-Fi configuration tüm profillerde WIFI_STORAGE_RAM kullanır; kullanıcı ayarları
uygulama tarafından Wi-Fi NVS'ye kaydedilmez. PHY/SDK'nin başka kalıcı yazımları veya
firmware upload yazımları engellenmiş sayılmaz. Şifreli NVS/secure boot/flash encryption
eklenmemiştir: derlenmiş firmware ve özel yerel dosya hâlâ credential içerebilir.
Önceki firmware'in NVS'ye kaydetmiş olabileceği Wi-Fi bilgileri otomatik silinmez.

`tools/check_wifi_credentials.ps1` değerleri yazdırmadan ignored durumu ve güncel
Git'e girebilecek metin dosyalarında eşleşen gerçek credential literal'larının yokluğunu
kontrol eder. Eski HEAD'de önceki credential bulundu. Git geçmişi/remote değiştirilmedi;
önceden paylaşılmış parolayı geçersiz kılmak için kullanıcı router parolasını değiştirmelidir.
Bu kontrol tüm git geçmişi/artifact/log sızıntılarının tarandığı iddiası değildir.

Mevcut yerel ağda PC/YAMCS hedefi `192.168.1.3:10015`, komut alıcısı UDP 10025,
YAMCS TC hedefi ESP32 `192.168.1.5` olarak yapılandırılmıştır. DHCP/yer-istasyonu IP
değişirse bu adres sözleşmesi de eşleştirilmelidir; otomatik ground discovery veya
router yapılandırma değişikliği bu firmware özelliği değildir.

## Doğrulama

12 C host grubu (Wi-Fi servisinde 25 senaryo) ve 79 Python kontrolü vardır. Host'ta
SDK/RTOS/network sınırları taklit edilir; gerçek SDK iç assert/abort, bellek çökmesi,
her olası kernel/radio arızası veya tüm sistem mod politikası doğrulanmış sayılmaz.

Kart runner: `tools/test_wifi_system.ps1 -Port COM7 -Duration 600`.
İki açık test profili dışında normal firmware'de fault injection/disconnect test kodu yoktur.
Testler gerçek NVS silmez/router/firewall/YAMCS kaynak ayarlarını değiştirmez.
Upload/reset RAM arşivini geri alınamaz biçimde siler. Runner finally normal firmware'i
geri yükler; normal boot/IP kontrolü ve exit 0 olmadan başarılı kabul edilmez.

2026-10-13 ortak fiziksel test başarılı tamamlandı (runner exit 0 + RESTORE PASS):

- Netif allocation dönüşü taklit edilirken gerçek heap bozulmadı; IP/log olmadan
  üç NOMINAL döngüsü ve core startup gözlendi. Sonraki driver init test trap'ine girilmedi.
- 600 saniye lifecycle: gerçek 15 saniye başlangıç timeout'u, geç IP ve log start,
  üç gerçek disconnect/reconnect, dört IP acquisition, 116 core döngüsü (58 NOMINAL).
  INITIAL_BOOT ve SAFE (1) bekleme döngüleri de devam etti; 12 saniyelik heartbeat
  üst sınırı aşılmadı, reboot/panic/watchdog/telemetry-queue failure görülmedi.
- Free heap örnek aralığı 164600..215500 bayt, minimum heap 160796 bayt;
  WIFI_MGR minimum stack headroom 1616 bayt. Reconnect sonrası ilk/son bağlı heap
  farkı 228 bayt (270 saniye 169064 → 570 saniye 168836). Bu, leak yokluğunun matematiksel kanıtı değildir.
- Soak sonrası gerçek YAMCS → kart → YAMCS iki GET aynı 20 bayt kaydı verdi:
  CRC16 0xDB3F, TM sequence 0→1, TC 9→10, udp-out 8→10. Kayıt silinmedi.
- Normal firmware geri yüklendi, boot/IP `192.168.1.5`, connected/log-started ve
  NOMINAL doğrulandı. Normal imajla iki GET daha geçti: CRC16 0x3865, TC 11→12,
  TM 0→1, udp-out 10→12; reset/WIPE/ACK komutu gönderilmedi.
- Normal ELF'te SDK/NVS/YAMCS fault wrapper yok; interface test imajında allocation/
  driver trap wrapper'ları bağlı. SDK config SHA256 değişmedi.
  Normal build statik RAM 42180/327680, flash 792797/1740800 bayt; mevcut ADCS/FDIR/HAL
  compiler uyarıları bu işte giderilmiş sayılmaz. Router/firewall/YAMCS kaynak ayarları değişmedi.

Süreli gözlem günlük/haftalık dayanıklılık, her ortamda reconnect veya uçuş
güvenilirliği garantisi değildir. Bu mevcut Wi-Fi kapsamı tamamlandı; sonraki işler
genel startup/mod/FDIR politikasıdır, yeni Wi-Fi feature roadmap'i değildir.

İlk 600 saniyelik denemede monitör yanlış biçimde bütün süreyi NOMINAL kabul etti;
315 saniye civarında NOMINAL sayımı durunca test başarısız oldu ve normal firmware
geri yüklendi. Firmware'in güç/FDIR politikası EPS critical raporunda SAFE isteyebilir;
ayrı çok düşük bus-voltage yolu CRITICAL'a geçebilir. Tekrar gözleminde SAFE (1) görüldü.
TM görevi NOMINAL dışında da 5 saniyelik bekleme/housekeeping döngüsü ve watchdog pet
çalıştırır. Düzeltilen ölçüt gerçek mod numaralarını doğrular, en az 3 NOMINAL ve
süre boyunca core döngülerini ister; 12 saniyeyi aşan heartbeat boşluğunu reddeder.
Wi-Fi testi için enerji eşikleri değiştirilmedi veya ek NOMINAL'a zorlama komutu
gönderilmedi; projedeki mevcut command-injector davranışı da değiştirilmedi.
