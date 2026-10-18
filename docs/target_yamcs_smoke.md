# ESP32–YAMCS başlangıç kontrol noktası

2026-10-15, yaklaşık 23:00–23:03 (+03:00). Kullanıcı onayıyla yalnız normal
firmware yükleme, açılış ve temel canlı YAMCS bağlantısı kontrol edildi.
Arşiv indirme/silme aşamasına geçilmedi.

## Yapılanlar ve kanıt

- `check_test_layout.ps1 -BuildMetadata` geçti: güncel normal ELF/metadata,
  host kaynak/include ve bilinen target test hook ayrımı doğrulandı.
- `platformio run -e esp32dev` başarılı (6.09 s, artımlı güncellik kontrolü).
  Statik RAM 42668/327680, uygulama flash kullanımı 806217/1740800 bayt.
  Bunlar çalışma zamanı minimum heap/stack ölçümü değildir.
- Normal imaj COM7'de ESP32-D0WD-V3 rev. 3.1'e esptool 4.9.0 ile 115200 baud
  hızında yüklendi; yazılan bölgelerin hash kontrolü geçti. Uygulama dosyası
  806624 bayt; uygulama yazımı 44.7 s. Bölgeler: bootloader `0x1000`, partition
  table `0x8000`, otadata `0xf000`, uygulama `0x20000`. NVS bölgesi yazılmadı,
  tam flash erase yapılmadı. Upload ve açılış reseti önceki RAM arşivini kaybettirdi.
- `archive_startup_check.py --port COM7 --event-loop --reset` geçti: archive,
  HK/payload (başlangıçta OFF), ADCS (DETUMBLE), zorunlu kuyruklar/EPS mutex,
  network stack/event loop doğru sırada hazır; beş tüketici hazır servislerden
  sonra başladı, TC UDP 10025 dinliyor. NOMINAL görüldü; kart IP `192.168.1.5`.
- Mevcut `C:/yamcs-ground/my-mission-control` projesinde Maven çevrimdışı
  `compile`, ardından `yamcs:run` çalıştırıldı. `clean` kullanılmadı; mevcut
  ground arşivleri silinmedi. Java/YAML/kaynak XTCE değiştirilmedi.
- Repo, ground SOURCE ve `target/yamcs/mdb/xtce.xml` hash'leri eşleşti.
  HTTP API'de `myproject` instance ve `realtime` processor RUNNING;
  altı container ve on command (abstract taban dahil) yüklendi.
- `yamcs_archive_transfer.py --timeout 5 --retries 0` canlı sunucuda geçti:
  yalnız BEGIN/PENDING dryRun binary üretimi; karta komut ve export dosyası yok.
- `udp-in` ve `udp-out` etkin, OK. Giriş portu 10015; çıkış hedefi
  `192.168.1.5:10025`. Gelen paket sayısı 49 → 124 → 152 arttı; gönderilen TC
  sayısı bu kontrolde sıfır kaldı.
- Canlı paket arşivinde HK APID 5, 34 bayt, `/myproject/MasterHKContainer`;
  event APID 2000, 70 bayt, `/myproject/EventPacket` olarak tanındı.
  Örnek reception zamanları sırasıyla `2026-10-15T20:03:07.258Z` ve
  `2026-10-15T20:03:10.230Z`.
- Processor parameter API aynı HK örneğini ACQUIRED olarak verdi:
  `HK_UptimeMs=65300`, `HK_BatteryVoltage=8.0510025`,
  `HK_GyroX=0.010376065`. Bu, simülasyon verisinin canlı çıkarım kanıtıdır;
  bütün alan/birimlerin bağımsız wire karşılaştırması veya sensör doğruluğu değildir.

Firmware `.bin` SHA256:
`A11E51BD148B500F652BBA6DEABA9FD971E69A1BEB5F0C296FCE0C546F0C5A8C`.
SDK config SHA256:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
MDB SHA256:
`AC52E1E4191002D8F6430B790A339A47EA3EDFE394D745E0C7C9B33D594B5EE3`.

## Bırakılan durum ve sonraki adım

Kartta normal `esp32dev` imajı bırakıldı. Mevcut onboard demo/simülasyon görevleri
değiştirilmedi; YAMCS üzerinden gerçek TC gönderilmedi. YAMCS arka planda açık
bırakıldı: <http://localhost:8090>. Başlatıcı PID 9152 (cmd; Java alt süreciyle),
oturum logları `.pio/live-check/yamcs-20261015-230049.out.log` ve `.err.log`.
PID yalnız bu oturumun kaydıdır; sonraki işlemlerde yeniden doğrulanmalıdır.

Sonraki adım önce veriyi kartta bırakan arşiv export testidir; ardından ayrıca
açık onayla CONFIRM/tail/receipt/lost-reply testleri yapılır. Bunlar yapılmış
sayılmaz. Kart aynı imajla açık kaldıysa yeniden upload/reset gerekmiyor;
reset RAM arşivini ve pending transfer durumunu kaybettirir.

Event servisinin işleme/tekrar davranışı, tüm HK alan/birim karşılaştırması,
gerçek transfer APID 97 yanıtları, heap/stack/jitter ve uzun süreli dayanıklılık
bu kontrolün kapsamı dışındadır. Tam host/native suite tekrar çalıştırılmadı;
firmware kaynakları veya test davranışı bu adımda değiştirilmedi.

## İkinci kontrol noktası: silmeden gerçek arşiv indirme

2026-10-15, yaklaşık 23:06–23:07 (+03:00). Kullanıcı devam onayından sonra
yalnız retained export denendi; yeniden upload/reset yapılmadı.

```powershell
& C:\.platformio\penv\Scripts\python.exe -B tools/yamcs_archive_transfer.py --send --output local/archive/esp32-retained-20261015-230646.json --timeout 8 --retries 0
```

İstemci yaklaşık 8.7 s'de `saved-retained` ile tamamlandı: BEGIN ve 17 READ,
her komutta exact Sent history binary + matching board reply; kayıt CRC16'ları,
tam snapshot CRC32'si, exclusive dosya oluşturma/fsync/readback doğrulandı.
Dosya: `local/archive/esp32-retained-20261015-230646.json` (Git-ignored).
SHA256: `38D4D875FB1FCE73DCF2457F6B75E58EF9B7A1FE4363D331C715C0C38EB0FC85`.
İstemci logu: `.pio/live-check/archive-retained-20261015-230646.log`.

Manifest: boot_id `14338160217562369584`, transfer_id `1`, request_id
`3591049203`, digest `1317850539`, record_count `17`. Boot ID'nin yüksek biti
gerçek HTTP/XTCE/wire turunda korundu; kimlik/CRC bir kimlik doğrulama garantisi değildir.

Sonrasında dosya mevcut helper ile offline yeniden doğrulandı; gerçek PENDING
aynı manifest'i döndürdü ve READ(0) aynı snapshot'tan CRC-valid kayıt verdi.
Toplam 20 gerçek TC: 1 BEGIN + 18 READ + 1 PENDING. Son link sayıları: giriş 635,
çıkış 20, ikisi de OK. APID 97 yanıtı canlı arşivde
`/myproject/ArchiveTransferContainer` olarak tanındı (son örnek 59 bayt,
`2026-10-15T20:07:33.633Z`). Bu, yalnız Sent OK değil, gerçek kart yanıtı kanıtıdır.

CONFIRM, CANCEL, legacy WIPE veya mode komutu gönderilmedi. Dosya ve pending
oturum bırakıldı; normal simülasyon çalışmaya devam eder. Sonraki kontrollü silme
onayı yalnız bu doğrulanmış dosya/manifest ile, ayrı açık izinle yapılmalıdır.
Kayıp yanıt/retry, tail koruma, receipt tekrarı ve event/birim/kaynak/soak işleri
hâlâ açıktır; bu tek başarılı aktarım onları doğrulamaz. Host/native suite tekrar
çalıştırılmadı; firmware/istemci kodu değiştirilmedi.

## Üçüncü kontrol noktası: CONFIRM engellendi, FDIR stack taşması yakalandı

2026-10-15, yaklaşık 23:09–23:15 (+03:00). Kullanıcı kontrollü silme/tekrar
testine devam izni verdi; ancak silme öncesi guard başarısız oldu. Kaydedilmiş
JSON'un hash/CRC kontrolü geçti fakat kart PENDING için NOT_FOUND (4) döndürdü.
Bu yüzden CONFIRM komutu **gönderilmedi**; farklı/yeni bir snapshot benimsenmedi.
Son ground TC sayısı 22: önceki 20 komuta yalnız iki PENDING eklendi.

HK arşivinde uptime geri dönüşü görüldü: `20:08:03.978Z` örneği 180270 ms,
`20:08:24.792Z` örneği 10260 ms; daha sonra `20:11:26.494Z` örneği tekrar
10230 ms. Kullanıcı USB/EN/RESET'e dokunmadığını bildirdi. Silme ön kontrolü
öncesindeki pending oturum, kendiliğinden reboot nedeniyle artık mevcut değil.

COM7'de DTR/RTS false ile açılan, reset pulse/input-buffer reset/komut içermeyen
salt-okunur 160 s üst sınırlı gözlem, 89.5 s'de panic ve sonraki boot'u yakaladı:

```text
77.9s [170] FDIR: Fault from Source 3 (Severity: 1) Code: 0xAF01
82.9s [175] FDIR: Fault from Source 3 (Severity: 1) Code: 0xAF01
87.9s [180] FDIR: Fault from Source 3 (Severity: 1) Code: 0xAF01
88.9s SDK: stack overflow detected in FDIR
88.9s ROM: rst:0xc (SW_CPU_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
89.4s BOOT: firmware initialization
89.5s WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=4.
```

Sanitize edilmiş log: `.pio/live-check/archive-reset-observe-20261015.log`.
Yakalanan backtrace adresleri: `0x4008251d`, `0x40088dd5`, `0x40089ec6`,
`0x4008af67`, `0x40089fd0`, `0x40089f82`. SDK reset_reason 4 PANIC'tir;
bu bulgu TASK_WDT reset_reason 6 veya Wi-Fi sorunu olarak sınıflandırılmaz.

Doğrulanmış hata, arşiv doluluk uyarıları sırasında FDIR görevinde stack taşmasıdır.
`main/main.c` FDIR için 2048 bayt ayırır. Gerekli çağrı-yolu stack payı ayrıca
ölçülmeli; yeterli bütçeyle temsilî doluluk/uyarı döngüsünü geçen target doğrulama
yapılmadan yalnız daha büyük bir sayı yazmak tamamlanma kanıtı değildir.

Firmware/kod bu adımda değiştirilmedi, yeniden flash veya kasıtlı reset yapılmadı.
Kart mevcut imajla kendiliğinden yeniden başlayabildiğinden CONFIRM/tail/receipt
testleri ertelendi. Eski RAM oturumu reboot ile kayboldu; indirilen 17 kayıtlık
disk dosyası aynı SHA256 ile korunuyor. Sonraki adım stack sorununun düzeltilmesi,
onaylı yeni upload ve doluluk/uyarı yolunu kapsayan kararlılık kontrolüdür.
Ardından yeni boot'a ait yeni bir export gerekir; eski manifest yeni verinin
silme izni olarak kullanılmamalıdır.

## Dördüncü kontrol noktası: FDIR düzeltmesi ve 305.8 s kart testi geçti

Sonraki kullanıcı onayıyla FDIR bütçesi 2048 → 4096 bayt yapıldı; 30 döngüde bir
minimum boş stack raporu eklendi. Normal imaj yüklenip gerçek arşiv doluluk/uyarı
yolunda 305.8 s tek boot gözlendi. Uyarı eşiği DEGRADED olarak işlendi; panic/reset
yok, minimum boş stack 1636 bayt. İlgili iki C grubu ve 228 Python testi geçti.

Yeni imajda 27 kayıt ayrıca silinmeden indirildi; bu pending oturumun doluluk
eşiğini geçtikten sonra aynı manifest ile korunduğu ve son kaydın okunabildiği
doğrulandı. CONFIRM/CANCEL/WIPE yok. Eski 17 kayıtlık disk dosyası korunur fakat
eski manifest yeni boot için kullanılamaz. Güncel normal imaj/dosya/manifest ve
tekrarlanabilir komutlar [FDIR kart kontrolü belgesindedir](fdir_stack_target.md).

## Beşinci kontrol noktası: 16 Ekim arşiv işlem testleri geçti

Yeni boot'ta yeni 24 kayıtlık prefix kaydedildi; ardından eklenen altı tail kaydı
silme öncesi ayrıca CRC-checked kaydedildi. CONFIRM/tail/receipt/CANCEL/resume ve
kontrollü HTTP yanıt kaybı/retry testleri 79 gerçek TC ile geçti. Test sonunda
30 kayıt kopyaları korunarak kontrollü silindi; RAM EMPTY/pending yoktu. Yeniden
flash/reset, mode/WIPE, Wi-Fi/router/firewall değişikliği yok. 247 Python geçti.
Önceki 17/27 kayıtlık dosyalar korunur, eski boot manifest'leri tekrar kullanılmaz.
[Sonuç, dosyalar ve kayıp modelinin sınırı](archive_target_tests.md).
