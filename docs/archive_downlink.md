# Tek kayıtlık arşiv telemetrisi — sürüm 1

`APID_ARCHIVE = 0x060` için `GET_BY_INDEX` komutu (`30 index_hi index_lo`), başarılı
arşiv okumasını aşağıdaki TM paketine dönüştürür. İndeks sıfırdan başlar. UDP paketinde
6 bayt CCSDS primary header vardır; secondary header yoktur. TM gövdesinin boyu
`9 + payload_length`, bütün UDP paketinin boyu `15 + payload_length` bayttır.

Gövde içindeki konumlar:

| Bayt | Alan | Kodlama |
| --- | --- | --- |
| 0 | Biçim sürümü | `1`, uint8 |
| 1–2 | Kayıt indeksi | uint16, big-endian |
| 3–4 | Kayıt türü / `record_id` | uint16, big-endian |
| 5–6 | Veri uzunluğu | uint16, big-endian; 0–128 |
| 7–8 | Verinin CRC16'sı | uint16, big-endian |
| 9 ve sonrası | Veri | Tam olarak belirtilen uzunlukta ham baytlar |

CRC-16/CCITT-FALSE parametreleri: polynomial `0x1021`, başlangıç `0xFFFF`, reflection
yok, xorout `0`. `123456789` için sonuç `0x29B1`; boş veri için `0xFFFF`.
CRC **yalnızca veriyi** kapsar; indeks/tür/uzunluk dahil metadata'yı korumaz ve
kriptografik doğrulama sağlamaz. Alıcı ayrıca başlık, sürüm ve boyları kontrol eder.

C struct'ı doğrudan TM gövdesi olarak gönderilmez; padding ve işlemci byte order'ı
dış paket düzenini değiştirmez. Ancak kaydın *içindeki ham verinin* şeması bu adımda
değiştirilmez: örneğin mevcut HK üreticisi hâlâ bir C struct'ı arşivler. HK alanlarını
STM32/yer istasyonu arasında taşınabilir şekilde yorumlamak için ayrıca açık bir
payload kodlaması gerekir. Alıcı burada iç veriyi ham bayt olarak doğrular.

## Başarı ve veri saklama

Boş/geçersiz indeks veya bozuk kayıt TM üretmez. Gönderim sonucu arşivi değiştirmez.
`submitted to UDP` yalnızca göndericinin yerel `sendto()` çağrısının başarılı olduğunu
ifade eder; karşı bilgisayarın paketi aldığını veya kalıcı kaydettiğini göstermez.
Gönderim hatasında aynı indeks yeniden istenebilir. UDP paket kaybı/tekrarı mümkündür.

Bu adım ground ACK, transfer kuyruğu, toplu aktarım veya otomatik silme eklemez.
Mevcut, açıkça anahtar gerektiren `WIPE_ALL` komutu ayrı kalır; kontrol betikleri
onu göndermez. Arşiv şu anda **4096 bayt RAM** kullanır, kalıcı flash değildir;
reset/yükleme mevcut kayıtları kaybettirir. Eski XOR checksum yerine CRC16 kullanılan
bu sürümde kalıcı eski kayıt migrasyonu yoktur çünkü arşiv zaten açılışta sıfırlanır.
İlk tek-kayıt adımından sonra okuma/yazma/silme mutex koruması eklendi;
[güncel eşzamanlılık ve doluluk sözleşmesi](archive_concurrency.md) geçerlidir.

## Kontroller

`tests/host/archive_downlink/downlink_check.c` gerçek arşiv kodunun oluşturduğu TM
gövdesini taklit göndericiyle yakalar; başarı/hata/tekrar yollarında veriyi koruduğunu
denetler. `test_decoder.py` Python alıcının bozuk/kısa/uzun paketi reddettiğini test eder.

`tools/archive_downlink_check.py` ise gerçek kartın UDP paketini alıp CRC16'yı bağımsız
hesaplar ve aynı indeksin ikinci okumada da aynı veriyi verdiğini doğrular. `--ip`
kullanımı reset talep etmez; `--reset` mevcut RAM arşivini temizleyerek kartı başlatır.
TM dinleme portu başka bir süreçte açıksa betik hata verir; süreç sonlandırmaz veya
firewall ayarı değiştirmez. PC'nin IP'si firmware'deki hedefle eşleşmelidir.

YAMCS alanlarını tanımlayan maintained XTCE profili kaynak ground projesine
kurulmuştur; [native kontrol ve runtime reload sınırları](yamcs_profile.md)
canlı ekran doğrulamasından ayrıdır. Güvenli ground ACK/boşaltma hâlâ açıktır.

## Gerçek kart doğrulaması

2026-10-12 tarihinde çalışan ESP32 resetlenmeden indeks 0 iki kez istendi. Bilgisayar
her iki UDP paketini aldı: HK türü `0x0001`, 20 bayt aynı veri, geçerli CRC16 `0x89C2`,
CCSDS sıra numaraları 5 ve 6. Paket başlık/boy kontrolleri ve ikinci okumada veri
korunumu geçti; arşiv boşaltma veya ground ACK gönderilmedi.

Alımın önündeki engel, Public profilindeki PlatformIO Python UDP engelleme kuralıydı.
Kullanıcı onayıyla yalnız bu kural yaklaşık 1,8 saniye durdurulup ESP32 → PC UDP 10015
için dar bir izin açıldı. Test sonunda eski UDP engeli geri açıldı, test izni kaldırıldı
ve TCP kuralının değişmediği doğrulandı. Firewall tamamen kapatılmadı; kalıcı ağ
ayarı bırakılmadı. Alıcı Python betiğidir; bu sonuç henüz YAMCS GUI/XTCE entegrasyonunu
ve YAMCS TC hedefinin doğru olduğunu doğrulamaz.

## YAMCS üzerinden kayıt okuma

Yer istasyonuna `/myproject/ARCHIVE_GET_BY_INDEX` komutu eklenmiştir. Kullanıcı
yalnız `RecordIndex` alanını (0–65535, sıfırdan başlayan indeks) doldurur. APID `96`,
TC tipi, secondary header yokluğu ve GET kimliği `0x30` sabittir; silme, reset veya
mod değiştirme komutu bu tanıma dahil değildir. Var olmayan indeks için kart kayıt
bulunamadı yanıtı loglar; bu tanım arşiv kapasitesini büyütmez.

Kaynak dosyaları yer istasyonu projesindedir:

- `C:/yamcs-ground/my-mission-control/src/main/yamcs/mdb/xtce.xml`: uint16 big-endian
  `ArchiveIndex_Type` ve GET komut tanımı.
- `C:/yamcs-ground/my-mission-control/src/main/yamcs/etc/yamcs.myproject.yaml`:
  `udp-out` için hazır `MyCommandPostprocessor` bağlantısı. Bu işlemci o linkten
  çıkan CCSDS komutların uzunluğunu ve APID bazlı sıra numarasını doldurur;
  komut verisini değiştirmez. Bu ayar yalnız yeni komutu değil, linki kapsar.

Maven çalışma kopyaları `target/yamcs` altında üretilir. Kalıcı düzenleme kaynak
dosyasında yapılır; `mvnw.cmd -o compile yamcs:run` yeni çalışma kopyalarını oluşturur.
Bu kurulumdaki testte yalnız `myproject:restart` kullanımı yeni MDB tanımını yüklese
de bağlantı YAML'ının eski bellekteki ayarını korumuştur. Bu nedenle bağlantı ayarı
değişince YAMCS süreci yeniden başlatılmıştır. `mvn clean` kullanılmamış, yer
istasyonu arşivi silinmemiş ve ESP32 resetlenmemiştir.

`tools/yamcs_archive_command_check.py` doğrudan ESP32'ye UDP göndermez ve UDP 10015'i
dinlemez. YAMCS API üzerinden komut oluşturur/gönderir, Command History'den gerçek
postprocess edilmiş binary ve `Acknowledge_Sent_Status=OK` sonucunu okur; seri logda
iç CRC kontrolü/arşiv okumasını, YAMCS paket arşivinden gelen ham TM'yi ve payload
CRC16'sını ayrı ayrı doğrular. `--send` verilmezse yalnız dry-run yapar. Seri monitor
kapalı olmalıdır; YAMCS çalışıyor olmalıdır. Python için yeni firewall izni gerekmez:
TM'yi mevcut Java/YAMCS alıcısı alır, Python yerel HTTP API üzerinden okur.

2026-10-12 tarihli tam kontrol geçti: indeks 0 iki kez alındı; tür `0x0001`, uzunluk
20, CRC16 `0x89C2`, aynı veri; TC sıra numaraları 3 → 4, TM sıra numaraları 10 → 11,
`udp-out` sayacı 2 → 4. Ön kontrol indeksleri 0, 4660 (`0x1234`) ve 65535 yalnız
dry-run ile doğru üretildi; -1/65536 API tarafından HTTP 400 ile reddedildi ve paket
çıkmadı. Kalıcı kontrol kaynakları `tools/` ve `tests/host/` altındadır; son çalışma
logu `.pio/host-tests/yamcs_archive_command_check.log` altındadır.

Bu, otomatik onboard komut verifier/ACK entegrasyonu değildir: YAMCS'in "Sent OK"
durumu tek başına kartın komutu işlediğini göstermez. Başarı seri log ve geri dönen
TM ile ayrıca kanıtlanmıştır. Bu ilk test sırasında arşiv container'ı yoktu;
15 Ekim'de kaynak profiline eklendi ve native YAMCS ile offline doğrulandı.
Çalışan sunucunun yeni profili yüklemesi/canlı parametre doğrulaması ayrı açık
iştir. Yer istasyonu onayıyla güvenli silme protokolü de henüz eklenmemiştir.
