# Çift çekirdekte supervisor/idle starvation

Bu test, [tek çekirdek CPU0/CPU1 koşularını](watchdog_starvation_target.md)
genişletir: IRQ'lar açıkken `WDT_MON` ve iki idle task aynı anda SDK'yı
besleyemediğinde gerçek TWDT panic/reset yolu sınanır. Normal mission yazılımına
yeni recovery, helper-feed, fault-clear veya restart politikası eklenmez.

## Enjeksiyon ve sınır

`esp32dev_wdt_global_starvation` yalnız
`FSW_TEST_WDT_GLOBAL_STARVATION=1` ile açılan ayrı bir imajdır. Kod
`components/watchdog/watchdog_global_test_esp32.c` içindedir; normal imajda hook/
worker sembolü yoktur. Diğer watchdog/ağ hata profilleriyle birleşimi reddedilir.

Altıncı başarılı check ve SDK feed sonrasında CPU0/CPU1'e bağlı `WDT_G0` ve
`WDT_G1` worker'ları oluşturulur. Her biri 4096 bayt stack ve priority 24 kullanır.
Worker gerçek core kimliğini doğrular, atomik ready bitini koyar ve release
kapısı açılana kadar birer tick bekler. Supervisor iki worker için ready mask 3'ü
en çok 100 tick bekler. Creation/affinity/readiness/flush hatası test başarısı
değil abort/failure'dır; kısmen hazır worker çifti busy-loop'a bırakılmaz.

İki worker hazır olduktan sonra marker stdout'a flush edilir ve release kapısı
açılır. Sonrasında worker'lar yalnız compiler-fenced busy-loop çalıştırır;
IRQ maskesi, SDK çağrısı, mutex, stdout, NVS veya watchdog feed yoktur. Priority 24,
`WDT_MON` (6) ve idle task'ları aç bırakır. Aynı priority'deki EPS işi time-slice
edebilir; bu bütün uygulama task'larının/ISR'lerin durduğu veya her deadlock'un
temsil edildiği iddiası değildir. Dual-core / 25-priority konfigürasyonu zorunludur.

Gerçek SDK reset nedeni 6 olduğunda sonraki boot'ta yalnız test enjeksiyonu
bastırılır. BSS/test durumu yeniden başlar; kalıcı repair/rearm kaydı yoktur.
SDK politikası aynı 15000 ms / idle mask 3 / panic'tir. TWDT/IRQ ayrımı için
[ESP-IDF 5.5 watchdog belgesi](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/wdts.html).

## Kabul edilen kanıt

- Başlangıçta doğru kaynak/arm/core sırası ve altı başarılı supervisor check/feed.
- `cores=3 ready=3 priority=24 interrupts=enabled` marker'ı; sonrasında ilk
  boot'ta yeni supervisor check/feed olmaması.
- SDK'nın **failed-subscriber** listesinde `WDT_MON`; varsa idle satırlarının
  doğru CPU etiketiyle gelmesi, başka/tekrar subscriber olmaması.
- Aynı timeout'un **current-running-task** bölümünde CPU0 için `WDT_G0`, CPU1
  için `WDT_G1`. Her iki worker ayrı ayrı zorunludur; yanlış task/core, eksik
  veya tekrar kayıt reddedilir. Running listesi tek başına watchdog timeout/
  failed-subscriber kanıtı değildir; iki farklı SDK görünümü birlikte aranır.
- Gerçek TWDT panic/reboot ve ikinci boot'ta `ESP_RST_TASK_WDT` (6); test
  reinjection bastırılmış, en az 45 s supervisor/mission ilerlemesi.
- Son normal restore ve 90 s supervisor sonrası normal kontrol.

Beklenmeyen IWDT/panic/critical/fatal/stack/assertion, yanlış reset nedeni veya
üçüncü boot başarısızlıktır. Seri işaret–SDK tanı gecikmesi paylaşılan timer için
kesin deadline/WCET garantisi değildir. Raporlanan stack minimumu `WDT_MON`'undur;
worker'ların veya bütün sistemin stack bütçesi değildir.

## Çalıştırma ve veri koruma

Önce o andaki RAM arşivini/tail'i kaydedip CRC/fsync/read-back ile doğrulayın.
Eski bir dosya güncel RAM için yeterli değildir; paket otomatik yedek almaz.
Upload/reset RAM arşivini, aktif transfer/fault/deployment durumunu siler; disk
kopyası korunur ve RAM'e otomatik geri yüklenmez.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_watchdog_system.ps1 -Port COM7 -GlobalStarvation -AllowRamReset
```

İmajlar ilk flash'tan önce derlenir. Normal baseline → global test → normal
restore sırası izlenir; hata durumunda da `finally` restore/gözlem yapar. Başarı
için **exit 0 + RESTORE PASS** birlikte gerekir. Kabloyu çıkarmak/süreci zorla
kapatmak restore'u engelleyebilir. Doğrulanmış aynı dört flash bölgesi yazılır;
NVS erase veya SDK/Wi-Fi/router/firewall/YAMCS config değişikliği yapılmaz.
Default/tek-çekirdek paketleri ayrı kalır; global paket baseline-atlamayı kabul etmez.

Uzun süreli soak bu pakette **yoktur**; kullanıcı onu akşama bıraktı.

## Masaüstü ve build — 2026-10-16

Tam regresyon geçti: **42 C grup / 40 maintained C fixture / 47 header**,
22 graph/layout ve 9 runner-refusal PowerShell kontrolü, **341 Python / 19 dosya**.
Global hook için 15 süreç senaryosu iki worker/gate/creation/partial-ready/
timeout/affinity/flush/spin sırasını mock scheduler ile sınar. Yeni seri okuyucu
için ilk 17 Python senaryosu vardı; ilk kart bulgusu sonrası 5 test daha eklenip
global okuyucu 22, toplam Python **346 / 19 dosya** oldu. Tüm Python ve 9 runner
kontrolü tekrar geçti; C/firmware değişmediği için 42 C/47 header tekrar koşturulmadı.
Önceki 75 watchdog okuyucu testi korunur.
Mock'lar gerçek reset/latency kanıtı değildir.

Beş watchdog flag'inin 10 ikili birleşimi, 30 ağ/startup birleşimi ve 7 selector
hatası: **47 negatif compile guard** geçti. Güncel normal metadata/ELF izolasyonu
ve 15 component / 59 doğrudan header bağı / 51 tekil proje C kaynağı kontrolü
geçti. Bunlar universal preprocessing/link/runtime-lock-cycle kanıtı değildir.

Normal ve global imaj başarıyla derlendi; normal ELF'te hook yok, global ELF'te
worker ve üç test hook'u mevcut. SDK config SHA256 değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.

| İmaj | Statik RAM | Flash | SHA256 (`firmware.bin`) |
| --- | ---: | ---: | --- |
| Normal | 42796 | 808369 | `BD16FF18F1B94CC364D64C18E76909900498CA3A8814F6F877DADCAC8CCF9D34` |
| Global | 42812 | 809429 | `B5648F37C0071355B7C222C287F7A5533571B1F2D34642FD6E2DE0B32A44B701` |

Statik RAM iki worker'ın 8192 bayt stack/TCB/heap maliyetini içermez; bu ek
runtime tahsisleri yalnız test imajındadır.

## Reset öncesi arşiv — 2026-10-16

Kullanıcı bu kısa reset paketini, güncel arşiv korunması ve normal restore
koşuluyla onayladı. 30/30 kayıt `local/archive/pre-watchdog-global-20261016.json`
dosyasına retained export edildi; CRC16/tam CRC32, fsync/read-back geçti.
Flash öncesi PENDING aynı manifest'i ve READ(29) diskle aynı kaydı verdi.
Kapasite 30 olduğu için korunmamış append-tail yoktu. CONFIRM/CANCEL/WIPE yok.

Manifest: boot `4202520720710804242`, transfer `1`, request `994336836`,
digest `1831171153`, count `30`. Dosya SHA256:
`06FA1F4F26C013A185BEB40A29C131FEE8C687F5411F0F2F9A425B7CFF9876D2`.

## Kart sonuçları

### İlk deneme: okuyucu beklentisi reddedildi, normal restore geçti

İlk okuyucu, üç SDK subscriber'ın da aynı ilk hata listesinde görünmesini
istiyordu. Marker'da iki worker hazırdı; SDK timeout bildirdi ancak listede
yalnız `WDT_MON` görüldü. Bu nedenle ilk paket **exit 1** ile başarısız sayıldı;
geriye dönük başarı olarak kabul edilmez. Normal restore/gözlem **95.8 s** ve
**RESTORE PASS** geçti. Kanıt dizini
`.pio/host-tests/wdt-global-20261016-113309-9938/`.

Kurulu SDK kaynakları ve [resmî ESP-IDF v5.5 kaynağı](https://github.com/espressif/esp-idf/blob/v5.5/components/esp_system/task_wdt/task_wdt.c)
incelendi: `esp_task_wdt_reset()` entry'nin `has_reset` bayrağını set eder;
`task_wdt_timer_feed()` bütün entry bayraklarını ancak ortak tur tamamlandığında
temizler. Hata listesi yalnız false bayrakları basar. Yani bu liste bağımsız
task başına son-feed timestamp'i değildir; daha önce beslemiş bir idle'ın
sonradan aç kalmasını ilk snapshot'ta göstermemesi mümkündür. İlk bulgu bu
tur-bayrağı davranışıyla tutarlıdır; idle başına kesin son-feed anı ölçülmedi.

İki sayısal backtrace aynı `global_worker` busy-loop adresine çözüldü, ancak
ilk okuyucu CPU/task snapshot'ını kaydetmediğinden bu veriyle ilk koşu başarıya
çevrilmedi. Yeni oracle, failed `WDT_MON` yanında her iki CPU'nun doğru worker'ı
çalıştırdığı ayrı SDK snapshot'ını da ister. Her iki görünüm, gerçek panic/reset
nedeni ve ikinci boot ilerlemesi birlikte zorunludur. MCU/SDK feed politikası,
priority, gate veya firmware değiştirilmedi; yalnız gözlemci/host testleri düzeltildi.

### Yeniden deneme

Yeni normal boot'un 30/30 kaydı
`local/archive/pre-watchdog-global-retry-20261016.json` dosyasına ayrıca kaydedilip
CRC/fsync/read-back ile doğrulandı. PENDING ve READ(29) diskle aynıydı; korunmamış
tail yoktu, CONFIRM/CANCEL/WIPE yoktu. Manifest: boot `14418195135339971279`,
transfer `1`, request `1185719094`, digest `1831171153`, count `30`. Dosya SHA256:
`D3A94415592FB273A6B22D8DE0795E7EB437C3DE1451C28F5595DACF30EC4E31`.

Aynı normal/global imajlarla tam paket yeniden çalıştırıldı. Normal baseline
**95.8 s** gözlemde geçti. Altıncı check/feed sonrası ready mask 3 ile release
marker'ı görüldü; sonrasında ilk boot'ta supervisor yeni feed yapmadı. SDK,
**15.000 s** sonra timeout ve failed `WDT_MON` bildirdi. Ayrı current-task
snapshot'ında **CPU0: `WDT_G0`, CPU1: `WDT_G1`** doğrulandı. İki sayısal
backtrace'in farklı stack context'leri aynı `0x400d4f7d` PC'sini içeriyordu;
güncel global ELF ile bu adres `global_worker` busy-loop satırına (30) çözüldü.
Bu ek çözümleme koşuya/imaja özeldir; okuyucunun hardcoded adres kuralı değildir.

Gerçek panic/reboot sonrası SDK reset nedeni **6 (`ESP_RST_TASK_WDT`)** okundu;
ikinci boot'ta reinjection bastırıldı ve en az 45 s supervisor/mission ilerlemesi
geçti. Global toplam gözlem **96.5 s**, `WDT_MON` minimum boş stack **1624 bayt**.
Yanlış IWDT/reset ailesi, ek reboot, başka critical/fatal/stack/assertion hatası
görülmedi. Idle'ların ilk failed listesinde olmaması gizlenmedi veya bağımsız
idle-timeout kanıtı gibi sunulmadı.

Son normal restore dört flash bölgesinin hash doğrulamasıyla tamamlandı. **95.8 s**
normal gözlem, NOMINAL döngüleri, doğru SDK/startup sırası ve test hook'u olmaması
geçti; kritik/fatal hata veya reboot yoktu. Yeniden deneme **exit 0 + RESTORE PASS**
ile kapandı. **Kartta normal `esp32dev` bırakıldı.** SDK config aynı kaldı.

Son normal gözlemde salt-okunur YAMCS kontrolü üç güncel HK paketinin yedi
alan/birimini ve 20 s penceredeki **28 event**'i doğruladı. Duplicate wire packet
ve eş metin çifti yoktu; TC outgoing **170 → 170** kaldı. Bu kontrol komut/reset
göndermez. Artifact `.pio/live-check/hk-events-after-global-20261016.json`, SHA256:
`D516EE4467AA554EC7305DCCEE259EE0181E4F967726C4DFED6A2B62B1285418`.

Başarılı kanıt dizini `.pio/host-tests/wdt-global-20261016-114504-1588/`:
`wdt_normal_check.log`, `wdt_global_check.log`, `wdt_restore_check.log` ve
build/upload logları. Üst çıktı `.pio/live-check/watchdog-global-target-retry-20261016.log`.
İlk başarısız packet'ın logları ayrıca korunur; log/özel arşivler ignored'dır,
GitHub'a yüklenmedi. İki reset-öncesi 30 kayıtlık disk kopyası korunur.

Bu sonuç belirtilen IRQ-açık, priority-24 çift-worker starvation senaryosunu
kapatır; bütün deadlock'ları, cache-off/debug/JTAG/farklı IRQ seviyelerini,
verified repair/recovery veya uzun soak'ı kapatmaz. Kullanıcı isteğiyle uzun
süreli test başlatılmadı; akşamki ayrı çalışmaya bırakıldı.
