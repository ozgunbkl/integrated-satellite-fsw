# ESP32 kaynak ve zamanlama ölçümleri

2026-10-16. Mevcut normal ESP32 firmware'ine opsiyonel `RES_MON` ölçüm görevi ve
salt-okunur heartbeat istatistiği eklendi. Wi-Fi politikası, kontrol/mod eşikleri,
SDK ayarları, paket biçimi veya fault-clear yetkileri değiştirilmedi.

17 Ekim recovery uygulamasında kaynak listesine priority-1, **6144 bayt** bütçeli
RECOVERY eklendi; firmware ve okuyucu artık 14 rol bekler. Yeni imaj karta
sonraki onaylı recovery adımlarında yüklendi; en son C0719A… normal imajda
bir saatlik SAFE kaynak gözlemi **3600 s / 120 rapor PASS** olarak tamamlandı.
[Güncel14 rollü sonuç ve sınırlar](long_run_target.md).
Aşağıdaki tarihsel 13 görevli sonuçlar önceki firmware içindir; yeni okuyucuyu
eski kart çıktısında PASS bekleyerek kullanmayın. [Politika ve build sınırı](recovery_policy.md).

## Ölçümün anlamı

- `heap_caps_get_info(MALLOC_CAP_8BIT)`: serbest 8-bit erişilebilir heap, heap
  bölgelerinin minimum boş değerlerinin toplamı ve en büyük boş blok. Minimumların
  toplamı aynı anda görülmüş global minimum olmak zorunda değildir; konservatif
  alt sınırdır. Statik RAM/build yüzdesiyle veya model kapasitesiyle aynı şey değildir.
- Güncel 14 kalıcı uygulama görevinin stack high-water mark'ı: görev ömrü boyunca en az
  boş kalan stack. Görevlerden biri için düşük sayı diğer göreve RAM aktarmaz.
  Kendini silen DEPLOY/CMD_INJECT ve SDK iç görevleri bu listede değildir. Görev
  yaşam süreleri değişirse handle okuma varsayımı yeniden incelenmelidir.
- İki mission kuyruğunun doluluğu: TM kapasite 10, TC kapasite 5. Nominal 100 ms
  örneklemedeki en yüksek değer saklanır; aradaki kısa tepeyi/dolup boşalmayı
  yakalama garantisi yoktur. Log mirror'ın private/drop-newest kuyruğu ölçülmez.
- Yedi watchdog görevinin heartbeat sayısı, ardışık heartbeat minimum/maksimum
  aralığı ve son heartbeat yaşı. İlk gerçek pet yalnız baseline kurar; startup
  beklemesi bir loop aralığı sayılmaz. Mevcut 100 Hz tick'te çözünürlük 10 ms'dir.
  Kuyruk beklemesi, preemption, log ve servis işi bu aralığa dâhildir; CPU süresi,
  WCET, end-to-end task deadline veya kesin scheduler jitter ölçümü değildir.
- `observer_ms`: raporu üretmenin duvar-saati maliyeti; UART bloklaması ve diğer
  görevlerin çalışması da buna dâhildir. CPU kullanım yüzdesi değildir. Rapor
  üretirken 100 ms kuyruk örneklemesi gecikebilir.

Pinned SDK'nin Xtensa `StackType_t` tipi 1 bayttır. Okuyucu/firmware bu platformda
stack değerlerini bayt olarak yorumlar; farklı platforma taşırken yeniden
doğrulanmalıdır. SDK'nin [FreeRTOS API belgesi](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/freertos_idf.html)
ile birlikte yerel 5.5.0 FreeRTOS kaynakları incelendi. Genel trace/runtime-stat
özelliği açılmadı, tüm sistem task/CPU yüzdesi taraması eklenmedi.

## Uygulama ve güvenlik

`RESOURCE_MONITOR_Start` mission görevlerinden sonra bir kez çağrılır. Null
kuyruk/tekrar başlatma reddedilir. Görev oluşturma başarısızsa mission çalışmaya
devam eder; ölçümün mevcut olmadığı açıkça loglanır. Ölçüm için görev öldürme,
kuyruk tüketme, heap'i doldurma, mod değiştirme veya arşiv silme yapılmaz.
Seri raporlar `fprintf` ile printf-to-YAMCS mirror'ını kullanmaz; 64 bayt event
kesmesi veya yeni event yükü oluşturmaz.

`WATCHDOG_GetTimingSnapshot` mevcut heartbeat mutex'ini sınırlı süreyle alır;
kilit altında yalnız kısa tick/state kopyası yapar. Başarısız okuma çıkışı
değiştirmez, fault raporlamaz, SDK watchdog feed etmez veya timeout latch'ini
temizlemez. Mevcut pet/check/arm güvenlik kararı korunur. Cumulative sayaç saturate
olur; tick farkı unsigned wrap ile hesaplanır. İstatistik tutmanın küçük bir
ek çalışma maliyeti vardır; ölçümler ölçüm kodu açıkken alınmıştır.

Normal build: 42796 bayt statik RAM, 808369 bayt flash; bin 808768 bayt.
Önceki normal imaja göre statik RAM +128, flash +2092 bayt. Ayrı 4096 bayt
RES_MON stack'i ve task kontrol yapısı çalışma zamanı heap'inden ayrılır.
Firmware SHA256:
`E168D8839F6E7BE067DBA0BF1F8A4B919A622FE7A45E1D8B99600DF3E8EA5292`.
SDK config değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.

## Kart koşusu ve güç geçişi

Kullanıcı arşiv korunarak yüklemeye açıkça izin verdi. Yükleme öncesi iki ayrı
BEGIN sorgusu EMPTY döndü; eski disk kopyaları korunuyordu. Normal firmware
COM7'ye bir kez yüklendi; flash hash doğrulandı ve yükleyici reset yaptı.
Sonraki seri gözlemciler DTR/RTS düşük açıldı; ek reset/mod/fault-clear yapılmadı.
RAM'deki eski geçici hata durumu bu onaylı yükleme/reset ile kayboldu.

İlk gözlemde 30 saniyelik raporlar yaklaşık 157 KB boş heap ve en az 1076 bayt
stack payı gösterdi. Arşiv dolduğunda üç warning ve DEGRADED işlendi. Ardından
UTC 05:30:43 civarında source=EPS/severity=CRITICAL bildirimi üzerine sıkı
gözlemci durdu; ilk koşu 600 saniyelik başarı olarak sayılmadı.

HK gerilimi UTC 05:30:40'da 5.069 V, 05:30:45'te 4.995 V idi. EPS manager'ın
mevcut 5.0 V altı koruma yoluyla uyumludur. Ground event'teki fault kodunun sonu
64 bayt metin sınırı nedeniyle kesildi; tam kod yalnız bu event'ten okunmuş gibi
gösterilmez. Düşük gerilim nedeni kod yolu/EPS kaynak ve severity/telemetri
birlikte incelenerek çıkarılmıştır. TM_GEN raporları UTC 05:28:40'ta mode 3
(DEGRADED), 05:30:45'te mode 1 (SAFE) gösterdi. Bu olay stack overflow veya
watchdog reset diye sınıflandırılmadı; koruma eşiği düşürülmedi/atlanmadı.

SAFE durumunda reset atmadan ayrı 600 saniyelik kaynak gözlemi başlatıldı.
Bu, bütün sürenin NOMINAL yükte geçtiği veya boot'ta hiçbir domain fault
oluşmadığı iddiası değildir. İlk dönemden gelen stack/heartbeat minimum ve
maksimumları kartta cumulative kalır; ilk okuyucu kesilince bunlar sıfırlanmaz.

### Son kaynak raporu

İkinci, reset istemeyen gözlem **600.0 saniye / 20 tam rapor** ile geçti.
Rapor sıra numaraları 14–33, uptime 425250–995250 ms; ilk-son rapor arası 570 s.
Gözlem başlangıcı ilk rapordan önceydi. Bu pencere SAFE durumundaydı: yeni
critical/fatal, panic, reset, eksik görev veya gözlenen mission-queue drop yoktu.
Seri boot marker sayısının 0 olması late-attach sonucudur, boot sayısı kanıtı değildir.

| Ölçüm | Sonuç |
| --- | --- |
| Örneklenen boş heap | 157088–157088 bayt |
| Heap bölgelerinin minimumları toplamı (alt sınır) | 141532 bayt |
| En büyük boş bloğun örneklenen minimumu | 110592 bayt |
| TM / TC örneklenen peak | 0/10 ve 0/5 |
| Seri rapor duvar-saati maksimumu | 113 ms |

Queue peak'in 0 olması komut gönderilmediği veya hiç kuyruk öğesi bulunmadığı
anlamına gelmez: kısa enqueue/dequeue aralıkları 100 ms örnekleri arasındaydı.
Gerçek PENDING/BEGIN/READ komutları bu boot'ta işlendi. İlk dönemdeki geçici
heap tüketimi, toplam alt sınırdan tek tek hangi göreve ait diye çıkarılamaz.

| Görev | Stack bütçesi (bayt) | Lifetime minimum boş (bayt) |
| --- | --- | --- |
| WDT_MON | 4096 | 3132 |
| EPS_MGMT | 4096 | 1600 |
| CMD_PROC | 4096 | 1496 |
| TC_RX | 4096 | 1536 |
| EPS_MON | 2048 | 1076 |
| FDIR | 4096 | 1632 |
| DATA_LOG | 4096 | 1304 |
| ADCS_MGMT | 4096 | 1648 |
| TM_GEN | 4096 | 1676 |
| SOLAR_HARV | 4096 | 1600 |
| WIFI_MGR | 4096 | 1656 |
| YLOG_TX | 3072 | 1232 |
| RES_MON | 4096 | 2136 |

En düşük pay EPS_MON'da 1076 bayttır; bu sonuç stack bütçelerini küçültme veya
bütün hata yollarında yeterli oldukları garantisi değildir. FDIR'ın bu imajdaki
1632 bayt değeri, önceki ayrı imaj/koşudaki 1636 bayt sonucunun yerine geçmiş
gibi yazılmaz; iki ölçüm farklı build/iş yükleridir.

Cumulative heartbeat aralıkları: TM_GEN 1000–1020 ms; CMD_PROC 0–100 ms;
EPS_MON 1000–1000 ms; DATA_LOG 0–110 ms; ADCS ve EPS_MGMT 100–100 ms;
FDIR 1000–1000 ms. Son snapshot'ta yaşlar 10–90 ms aralığındaydı.
0 ms birden fazla pet'in aynı tick'e düşmesidir, sıfır CPU işi değildir.

Rapor: `.pio/live-check/resource-target-20261016-0832.json`.
SHA256 `9445737D0B63056E6C11A6E93CB411E07CD9F17001C4317D58B36076E9EFB89B`.
Önceki nominal/degraded bölümün 600 s PASS raporu yoktur; ilk başarısız koşu
bu başarılı SAFE ölçümüne gizlice eklenmedi. Bütün ömür minimumlarının geçmiş
yolları içermesi, o yolların 600 s boyunca tekrar çalıştırıldığı anlamına gelmez.

## Haberleşme gecikmesi ve veri koruması

Üç zararsız PENDING sorgusu 455.529, 279.233, 288.769 ms sürdü. Bunlar HTTP link
kontrolü/dryRun/baseline/issue, Sent history ve eşleşen gerçek kart yanıtının
tamamını kapsayan client duvar-saati süreleridir; bare UDP RTT veya üç örnekten
p95/deadline garantisi değildir. Retry kapalıydı; NOT_FOUND yanıtları normaldi.

Arşiv dolduktan sonra 30 kayıt gerçek BEGIN/READ ile kaydedildi, CRC16/toplam
digest/fsync/read-back doğrulandı. CONFIRM/CANCEL/WIPE kullanılmadı; karttaki
kopyalar ve pending oturum korunur. Bu sıradaki TC işleme/transfer yolları da
görevlerin lifetime stack minimumlarına dâhildir.
Sonraki PENDING aynı manifest'i, READ(0) dosyayla birebir aynı kaydı verdi:
boot `10711167524647239147`, transfer 1, request `166651817`, digest `1999430223`,
count 30. Retention raporu `.pio/live-check/resource-retention-20261016.json`.

Disk kopyası: `local/archive/resource-soak-20261016-0830.json`.
SHA256 `401D47D6A052BCE5136DE098D6BBAED4644BF48D7C102CE0716619B3F766D508`.
Önceki export/proof dosyaları silinmedi. Arşiv hâlâ RAM'dir; kullanıcı kartı
sökerse yalnız karttaki kopya kaybolur, bilgisayar kopyası kalır.

Firmware sonrasında ayrıca üç HK örneği/yedi alan-birim ve 34 olaylık ayrı
20 saniyelik pencere mevcut salt-okunur araçla geçti. Kontrol boyunca ground
TC sayacı 36 → 36 kaldı. Bu, eski packet/Java sözleşmesinin tüm hata yollarının
yeniden sınandığı anlamına gelmez.

## Tekrarlama ve regresyon

```powershell
# Reset istemez; çalışan aynı normal imaja bağlanır. Seri portu tek süreç açmalı.
& C:\.platformio\penv\Scripts\python.exe tools/resource_monitor_check.py --port COM7 --duration 600 --output .pio/live-check/NEW_RESOURCE_REPORT.json
```

Çıktı dosyası yeni olmalıdır; üzerine yazılmaz. İlk partial seri rapor atılır,
sonraki raporların tamlığı, sırası, sürekliliği ve cumulative değerleri denetlenir.
Yeni critical/fatal, reset/panic, eksik görev, 512 bayt altı stack veya belirlenen
heap/timing sınırları gözlemi durdurur. Alan dışı seri metin/credential kaydedilmez.
Bir ortamda/iş yükünde geçen ölçüm bütün ortamlarda geçen ürün limiti değildir.

37 C grubu/47 bağımsız header, 22 PowerShell graf/layout ve 17 dosyada 292 Python
testi geçti. Yeni monitor C fixture'ı 7 durum; startup entegrasyonu opsiyonel
oluşturma hatasını kapsar; watchdog'a 8 timing senaryosu eklendi. Python okuyucu
19 senaryo ile malformed/partial/duplicate/stale/low-resource/reset/fault/timing
kanıtlarını sınar. Mocks gerçek scheduler/fiziksel kart yerine geçmez.
Normal metadata 50 production C kaynağını birer kez doğrular; test ayrımı,
dependency grafı, üç negatif profil ve normal ELF hook yokluğu tekrar geçti.
Native YAMCS MDB/command/preprocessor kontrolü ve tüm 292 Python testi de son
okuyucu düzeltmelerinden sonra yeniden geçti. Host günlük:
`.pio/live-check/resource-host-20261016-0821.log`.

Geniş watchdog/recovery matrisi, uzun süreli NOMINAL/çeşitli iş yükleri ve genel
yeniden üretilebilirlik hâlâ [roadmap](roadmap.md) kapsamında açıktır. Bu adım
uçuş yeterliliği, kalıcı flash arşivi veya tüm heap/stack senaryoları değildir.

## Uzun koşu için okuyucu hazırlığı

2026-10-16, yalnız bilgisayar aracı: firmware veya gözlenen kart değiştirilmedi.
`resource_monitor_check.py` artık **300..7200 saniye** kabul eder. Bir saat için
3600 s kullanılabilir; bu hazırlık gerçek uzun koşunun yapılmış olduğu anlamına gelmez.

- Her tam rapor, yeni açılmış JSON dosyasına `RUNNING` checkpoint olarak yazılır,
  flush/fsync yapılır. Sadece süre/kapsama koşulları sağlanırsa `PASS` / exit 0;
  gözlem hatasında `FAIL` / exit 1, Ctrl+C'de `INTERRUPTED` / exit 130 yazılır.
  Tamamlanmış ölçümler ve eksik blok bilgisi saklanır, bozuk/yarım blok başarıya
  eklenmez. Dosyanın var olması başarı değildir. Ani süreç/güç kesilmesi veya
  disk hatası eski RUNNING/eksik JSON bırakabilir; bu durumda PASS iddia edilmez.
- 45 s içinde tam kaynak raporu gelmezse erken durur; başka seri mesajların
  gelmesi bu süreyi uzatmaz. Hem cihaz uptime/sıra artışı hem bilgisayarda rapor
  aralığı (25..40 s) kontrol edilir; arka arkaya boşaltılan eski raporlar uzun
  çalışma kanıtı değildir. Heartbeat sayıları ilerlemelidir (UINT32_MAX saturasyonu hariç).
- UART timeout'uyla bölünen satırlar sınırlı buffer'da birleştirilir; iki parça
  gelen panic/reset metni atlanmaz. Ham seri metin/credential çıktıya girmez;
  hatalarda yalnız sabit sınıflandırma ve doğrulanmış ölçümler saklanır.
- Mevcut 5 s `TM GEN` mesajlarından mod **örnek sayıları**, ilk/son örnek ve en
  büyük örnek aralığı kaydedilir. Kaynak raporunda son 15 s içinde mod örneği
  yoksa `sampled_mode: null` olur; bunu NOMINAL diye varsaymaz. Sürekli mod
  geçmişi, her kısa geçiş veya payload/actuator iş yükü kanıtlanmaz.
- Opsiyonel `--require-mode NOMINAL` yalnız örneklenen NOMINAL koşusunu kabul
  eder; farklı mod veya 15 s'den uzun mod-verisi kesintisi başarısızdır. **Mod
  değiştirmez**, fault çözmez; SAFE'teki karta bunu vermek SAFE'i atlatmaz.
  Bayrak olmadan karışık/bilinmeyen modlu kaynak gözlemi geçebilir ve bu açıkça
  raporlanır; all-NOMINAL yük testi diye sunulmaz.

Aşağıdaki komutlar **şimdi çalıştırılmadı**. Seri portu başka monitor kullanmamalı;
test edilecek normal imaj, iş yükü, mod ve kabul sınırları önce belirlenir.
Upload gerekiyorsa önce güncel RAM arşivi korunur ve reset izni alınır.

```powershell
# Mevcut moda müdahale etmeyen bir saatlik kaynak gözlemi; yeni dosya/dizin gerekir.
& C:\.platformio\penv\Scripts\python.exe -B tools/resource_monitor_check.py --port COM7 --duration 3600 --output .pio/live-check/NEW_ONE_HOUR.json

# Yalnız önceden hazırlanmış NOMINAL iş yükünü gözle; NOMINAL'e geçiş komutu DEĞİL.
& C:\.platformio\penv\Scripts\python.exe -B tools/resource_monitor_check.py --port COM7 --duration 3600 --require-mode NOMINAL --output .pio/live-check/NEW_NOMINAL_ONE_HOUR.json
```

Sonuç değerlendirmesinde en az yeni fault/reset, rapor/heartbeat sürekliliği,
stack/heap/en büyük blok sınırları, örneklenen kuyruk doluluğu, iş yükü/mod ve
ilk-son serbest heap farkı incelenir. Tek bir heap farkı otomatik leak teşhisi
veya leak yokluğu kanıtı değildir. Önceki simüle EPS düşük gerilimi, kontrol
eşikleri düşürülerek başarıya çevrilmez. GitHub yayını ancak sonuç değerlendirmesi,
gereken düzeltme/tekrar ve nihai README/temiz klon kontrollerinden sonra yapılır.

Okuyucu fixture'ı 19'dan 37 teste çıktı; bir saatlik 120 rapor/720 mod örneği
sahte saat/seri portla doğrulandı. Toplam 20 dosyadaki 390 Python testi geçti.
Bu adımda seri port açılmadı, gerçek TC/reset/upload veya uzun test yapılmadı;
firmware değişmediği için C/header/normal build tekrar çalıştırılmadı.
