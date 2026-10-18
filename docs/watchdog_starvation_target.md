# Watchdog starvation ve interrupt testleri

İki çekirdeğin birlikte aç bırakıldığı ayrı koşu, ilk okuyucu bulgusu ve normal
restore sonucu [global test belgesindedir](watchdog_global_target.md). Aşağıdaki
CPU0/CPU1 tek-çekirdek ölçümleri onun yerine geçmez.

Bu paket önceki [heartbeat / supervisor testlerini](watchdog_target_tests.md)
genişletir. Amaç, supervisor hâlâ çalışırken yalnız idle task'ın aç kalmasını
TWDT'nin; CPU0 kesmeleri engellendiğinde ise IWDT'nin gerçek resetle karşılamasını
sınamaktır. Bütün çekirdekler/ISR'ler/deadlock'lar için doğrulama değildir.

## Test-only enjeksiyonlar

| Profil | Altıncı başarılı supervisor check/feed sonrasında | Kabul edilen neden |
| --- | --- | --- |
| `esp32dev_wdt_idle_starvation` | CPU0'a bağlı, priority 1 busy worker; kesmeler açık | SDK failed-subscriber listesinde yalnız `IDLE0 (CPU 0)`, enjeksiyon sonrasında en az bir başarılı supervisor feed, panic ve ikinci boot'ta `ESP_RST_TASK_WDT` (6) |
| `esp32dev_wdt_interrupt_mask` | Aynı worker stdout'u flush eder, ardından CPU0 kesmelerini engelleyip busy-loop'a girer | CPU0 interrupt-watchdog panic'i ve ikinci boot'ta `ESP_RST_INT_WDT` (5) |

Worker 4096 bayt test stack'i kullanır; kendisi TWDT subscriber değildir.
Busy-loop yalnız compiler fence içerir; SDK çağrısı/yield/feed yoktur. Masking
öncesi uygulama/stdout kilitleri bırakılmıştır. `esp_restart`, NVS yazısı,
uygulama fault-clear veya sahte reset nedeni yoktur. Test imajı yalnız beklenen
gerçek SDK reset nedeninde bir sonraki boot enjeksiyonunu bastırır; kalıcı onarım
veya production reboot-loop politikası değildir.

Normal firmware aynı 15000 ms TWDT / idle mask 3 / panic politikasını korur.
Mevcut IWDT timeout'u 300 ms'dir. Bu testler dual-core ESP32 ve bu sabit IWDT
konfigürasyonunu ister. TWDT/IWDT'nin farklı çalışma amaçları ve JTAG sınırlaması
[ESP-IDF 5.5 belgesinde](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/wdts.html)
açıklanır. Ölçülen tek gecikme evrensel deadline garantisi değildir.

## Güvenli çalıştırma

Önce mevcut RAM arşivini dışa aktarın ve CRC/fsync/read-back doğrulamasını yapın.
Pending snapshot bir prefix ise sonradan eklenen tail kayıtlarının da korunması
gerekir; yalnız eski bir dosyanın varlığı yeterli değildir. Paket otomatik arşiv
yedeği almaz. `-AllowRamReset`, bu ön koşulun sağlandığına dair açık operatör
onayıdır. Kaydedilmiş dosya reset sonrası RAM'e otomatik geri yüklenmez.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_watchdog_system.ps1 -Port COM7 -StarvationProfiles -AllowRamReset
```

Paket bütün imajları ilk flash'tan önce derler. Sıra normal baseline (90 s), idle
starvation, interrupt mask, normal restore (90 s) şeklindedir. İki fault profili
gerçek reboot sonrası en az 45 s supervisor/mission ilerlemesi ister. İlk flash
başladıktan sonra hata durumunda da `finally` normal firmware'i yükleyip doğrular.
Başarı için exit 0 ve `RESTORE PASS` birlikte gerekir. Kabloyu çıkarmak/süreci
zorla kapatmak restore'u engelleyebilir. Arşiv/fault/deployment RAM durumu
yükleme ve resetlerde silinir; bu on-board silinme geri alınamaz.

Yalnız layout/size/path sınırları doğrulanmış dört flash bölgesi yazılır; NVS
erase yoktur. SDK config, Wi-Fi/router/firewall ve YAMCS ayarları değiştirilmez.
Test runner ground komutu göndermez. Önceki arşivi koruma ve son arşivi indirme
işlemleri bundan ayrı, izinli transfer istemcisi adımlarıdır.

Seri okuyucu yanlış çekirdek/reset ailesi, eksik failed-subscriber/feed kanıtı,
beklenmeyen critical/fatal hata, stack/assertion sorunu, ek reboot veya tekrar
enjeksiyonu reddeder. Ham Wi-Fi/SDK logları yerine allowlist'li tanı gösterir.
Loglar benzersiz `.pio/host-tests/wdt-starvation-<UTC timestamp>/` dizinine gider.

## Masaüstü doğrulaması (2026-10-16)

- Her profil için gerçek hook koduyla 10 C süreç senaryosu: init, tekrar init,
  tüm mission pet'lerinin korunması, doğru/yanlış reset nedeni, altıncı check,
  worker oluşturma hatası, flush/mask/spin sırası ve flush hatası.
- Yeni okuyucu için 20 Python senaryosu; önceki 43 watchdog okuyucu testi korunur.
- Tam regresyon: 39 C grup, 47 bağımsız public header, 22 PowerShell graf/layout
  kontrolü, 18 dosyada 312 Python testi geçti. Busy-loop düzeltmesinden sonra
  ilgili iki C grup yeniden geçti. Host mock'ları gerçek IRQ/reset kanıtı değildir.
- Dört watchdog flag'inin altı ikili birleşimi ve altı ağ/startup fault flag'iyle
  24 birleşimi: toplam 30 negatif compile guard geçti. Normal ELF'te test hook'u
  yoktur. Açık RAM-reset onayı olmadan yeni paket build/flash öncesinde reddedilir.

İlk derlemede `portNOP()` genişlemesinin bu SDK/toolchain'de tanımsız `XT_NOP`
üretmesi yakalandı. Target'a yüklenmeden compiler-only C11 fence ile değiştirildi;
bu derleme hatası bir kart watchdog başarısı/başarısızlığı sayılmaz.

## Kart sonucu

16 Ekim yüklemesi öncesi 30/30 kayıt
`local/archive/pre-watchdog-starvation-20261016-1310.json` dosyasına retained
export ile kaydedildi. CRC16/tam CRC32, fsync/read-back doğrulandı. Reset öncesi
PENDING aynı manifest'i, READ(29) diskle aynı kaydı verdi. Kapasite 30 olduğu için
korunmamış append-tail yoktu. CONFIRM/CANCEL/WIPE gönderilmedi.

Manifest: boot `14720499012327764334`, transfer `1`, request `4248231074`,
digest `4188060318`, count `30`. Dosya SHA256:
`F35785E18F94203583AD831DAEB79857CCA0BFF41A1AA1188414760788F564DA`.
Bu dosya bilgisayarda korunur; yüklemeyle silinen eski RAM oturumu geri gelmez.

Normal baseline 95.8 s gözlemde geçti: NOMINAL mission döngüleri, doğru SDK
ayarları ve startup sırası; critical/fatal hata veya reboot yok.

Idle-starvation koşusunda supervisor check/feed 6 sonrasında worker başladı;
check/feed 7 ve 8 başarılı kaldı. SDK yalnız `IDLE0 (CPU 0)` subscriber'ını
zaman aşımında bildirdi. Enjeksiyondan **15.000 s** sonra TWDT panic'i ve gerçek
reboot görüldü. İkinci boot SDK reset nedeni **6 (`ESP_RST_TASK_WDT`)** idi;
test reinjection bastırıldı ve en az 45 s supervisor/mission ilerlemesi geçti.
Toplam gözlem **96.5 s**, supervisor minimum boş stack **1616 bayt**.

Interrupt-mask koşusunda altıncı check/feed sonrasında CPU0 kesmeleri maskelendi.
Seri enjeksiyon işaretinden **0.297 s** sonra CPU0 interrupt-watchdog panic'i
gözlendi. İkinci boot SDK reset nedeni **5 (`ESP_RST_INT_WDT`)** idi; reinjection
bastırıldı ve en az 45 s supervisor/mission ilerlemesi geçti. Toplam gözlem
**81.9 s**, supervisor minimum boş stack **1616 bayt**. Bu seri işaret–tanı
gecikmesidir; donanım osiloskop ölçümü veya her koşulda 297 ms garantisi değildir.

Her iki koşuda beklenmeyen critical/fatal hata, üçüncü boot, stack/assertion
hatası veya tekrar enjeksiyonu görülmedi. Stack minimumu yalnız `WDT_MON`
içindir; fault worker'ın veya tüm uygulamanın stack bütçesi değildir.

Son normal restore dört flash bölgesinin hash doğrulamasıyla tamamlandı.
Ardından **95.8 s** normal gözlem geçti: test profili/hook mesajı yok, NOMINAL
mission ilerlemesi var, critical/fatal hata veya reboot yok. Paket **exit 0** ve
**`RESTORE PASS`** ile bitti. **Kartta normal `esp32dev` bırakıldı.** Normal
binary'nin gömülü ELF digest'i yeniden linklenmiş ELF'in SHA256 değeriyle eşleşti;
güncel metadata/ELF izolasyonu ve component kontrolü geçti. SDK config aynı kaldı.

Restore sonrası salt-okunur HTTP kontrolü üç güncel HK paketi/yedi alan ve
birimi, 20 s pencerede 36 event'i doğruladı. Wire duplicate yok; bu koşuda eş
metin çifti yoktu. TC outgoing sayacı **71 → 71** kaldı. Bu kontrol reset veya
TC göndermez. Artifact `.pio/live-check/hk-events-after-starvation-20261016.json`,
SHA256 `61223F3396E19B53FB973BC3EFCBD5381EA8DA18C0F0ABF3C6F78278E7B7F53E`.

Paket kanıt dizini:
`.pio/host-tests/wdt-starvation-20261016-102456-8370/`:
`wdt_normal_check.log`, `wdt_idle_starvation_check.log`,
`wdt_interrupt_mask_check.log`, `wdt_restore_check.log` ve build/upload logları.
Üst seviye çıktı `.pio/live-check/wdt-starvation-target-20261016.log` dosyasındadır.
Bunlar makineye/koşuya ait ignored çıktılardır; GitHub'a özel arşiv/log yüklenmedi.

### Derlenmiş artifact'lar

| İmaj | Statik RAM (bayt) | Flash (bayt) | SHA256 (`firmware.bin`) |
| --- | ---: | ---: | --- |
| Normal | 42796 | 808369 | `E95AAB43DB6893A148A811FA4812D3853CCC4F46A2662A234D61C41FC71D36AA` |
| Idle starvation | 42804 | 809105 | `779DB95F5986337655708356D356CF66759BC8568923C3759414C1430B479375` |
| Interrupt mask | 42796 | 809089 | `7D03BA964B20E5BFB505A26FBAD7D43CDCCEA9FF967ECE3B6F2FB11A83AEF348` |

Statik RAM worker stack/TCB/heap maliyeti değildir. SDK config SHA256 değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.

## Kapsam sınırı

Bu belgedeki kart koşuları CPU0 ve CPU1 üzerinde ayrı ayrı kontrollü busy-loop/
IRQ-mask enjeksiyonudur. İki çekirdeğin eşzamanlı/global starvation'ı, farklı IRQ seviyeleri/ISR
süreleri, cache-off/JTAG davranışı, bütün deadlock yolları, mission-task restart,
verified fault-clear, kalıcı fault/arşiv ve uzun süreli değişken-yük soak açık
veya ayrı kapsamdır. Normal imajda fault worker/hook bulunmaması ayrıca kontrol edilir.

## CPU1 hazırlığı — yalnız masaüstü (2026-10-16)

Bu bölüm ilk, yalnız-host alt adımının kaydıdır. Sonraki ayrı izinle yapılan
gerçek kart paketi ve bırakılan durum aşağıdaki CPU1 kart bölümündedir.

Kullanıcının tercihiyle bu alt adımda **firmware build/upload/reset veya kart
testi yapılmadı**; RAM arşivine/aktif transfer oturumuna dokunulmadı. Yukarıdaki
CPU0 ölçüm ve SHA256 değerleri önceki başarılı koşuya aittir; güncel kaynakların
yeniden derlenmiş/ölçülmüş imajı oldukları iddia edilmez. Karttaki normal firmware
bu adımda değiştirilmedi.

`FSW_TEST_WDT_CORE` yalnız idle-starvation/interrupt-mask profillerinde 0 veya 1
olabilir; belirtilmezse eski CPU0 seçimi korunur. Normal, task-loss veya supervisor
profilinde selector kullanımı reddedilir. CPU1 IRQ profili ayrıca SDK'nın
`CONFIG_ESP_INT_WDT_CHECK_CPU1` ayarını ister. SDK config değiştirilmedi.

Yeni opt-in ortamlar `esp32dev_wdt_idle_starvation_cpu1` ve
`esp32dev_wdt_interrupt_mask_cpu1` mevcut profilleri devralır. Worker hâlâ priority 1,
4096 bayt stack ve uygulama/stdout kilitleri dışındaki aynı busy-loop'u kullanır.
Normal firmware'e worker, ek görev, helper-feed veya recovery politikası eklenmez.

Seri okuyucunun `--fault-core 1` seçeneği profile/enjeksiyon/core tanısının
CPU1 olmasını zorunlu tutar. Idle koşusunda yalnız failed-subscriber listesinde
`IDLE1 (CPU 1)`, supervisor'ın enjeksiyon sonrası başarılı feed'i ve gerçek
reset nedeni 6; IRQ koşusunda CPU1 interrupt-watchdog panic'i ve reset nedeni 5
istenir. CPU0 kanıtı, yanlış CPU etiketi, ek failed subscriber veya ikinci boot'ta
çekirdek değişmesi başarı sayılmaz. Varsayılan CPU0 okuyucu davranışı korunur.

Doğrulanan masaüstü kapsamı:

- Dört core/fault birleşimi için 10'ar C süreç senaryosu (iki eski CPU0, iki yeni
  CPU1 grubu); gerçek test hook'u derlenir, SDK/task/IRQ/fence sınırları mock'tur.
- 43 eski watchdog okuyucu + 32 starvation okuyucu Python testi; CPU1'e özel
  12 yeni test dahil. Sahte seri satırları fiziksel reset/latency kanıtı değildir.
- Tam ortak regresyon: **41 C grup / 47 header / 324 Python (18 dosya)** ve mevcut
  22 graph/layout PowerShell kontrolü geçti. Ardından eklenen 5 runner-refusal
  kontrolü ayrıca geçti; artık ortak runner tarafından da keşfedilir.
- **36 negatif compile guard** geçti: önceki 30'a selector yanlış profil/değer ve
  CPU1 IWDT kapalı konfigürasyonları eklendi. `-SkipNormalElf` ile yalnız bu
  kontroller çalıştırıldı; eski normal ELF güncel kaynak doğrulaması sayılmadı.
- Hedef betik parse edildi; eksik reset onayı, yanlış paket/core ve baseline-atlama
  girişimleri build/flash'tan önce reddedildi. Bu beş kontrol gerçek araçlar yerine
  var olmayan tool yolları ve donanım olmayan port kullanır; karta erişmez.

Kanıtlar `.pio/live-check/watchdog-cpu1-host-20261016.log` ve
`.pio/live-check/watchdog-cpu1-guards-20261016.log` içindedir. Tekrar:

```powershell
# Yalnız masaüstü; hiçbir target runner çağrılmaz.
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_checks.ps1 -Compiler C:\cygwin64\bin\gcc.exe -Python C:\.platformio\penv\Scripts\python.exe
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check_watchdog_profiles.ps1 -SkipNormalElf
```

Sonraki **ayrı izinli** kart adımında önce o andaki RAM arşivi/tail korunup
doğrulanmalı, güncel normal ve CPU1 imajları derlenmeli ve normal ELF/metadata
izolasyonu kontrol edilmelidir. Ardından tam paket şu komutla çalıştırılabilir:

```powershell
# RESET/UPLOAD: her tekrarda güncel RAM korunmalı; eski yedek yeterli değildir.
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_watchdog_system.ps1 -Port COM7 -StarvationProfiles -StarvationCore 1 -AllowRamReset
```

Paket CPU1 için ayrı log dizini kullanır; normal baseline ve sonunda normal
restore/gözlem atlanmaz. İki çekirdeğin eşzamanlı/global starvation testi bu
CPU1 hazırlığına dahil değildir ve roadmap'te açık kalır.

## CPU1 kart paketi — 2026-10-16

Masaüstü hazırlığından sonra kullanıcı güncel arşiv korunarak iki gerçek CPU1
reset testinin ve sonunda normal restore'un çalıştırılmasına ayrıca izin verdi.
Normal ve iki CPU1 imajı ilk flash'tan önce başarıyla derlendi. Güncel normal
ELF/metadata izolasyonu, 15 component/59 doğrudan header bağı/50 tekil C kaynak
kontrolü ve 36 negatif compile guard geçti. Normal ELF'te test hook'u yok;
iki CPU1 ELF'inde gerekli hook/worker sembolleri mevcut. 75 watchdog okuyucu
Python kontrolü ve 5 runner-refusal kontrolü tekrar geçti. Bu kart adımında C
veya test kodu değiştirilmedi; hazırlıktaki tam 41 C/47 header/324 Python
regresyonu bu yüzden bütünüyle tekrar koşturulmadı.

### Reset öncesi arşiv koruması

Güncel 30/30 kayıt `local/archive/pre-watchdog-cpu1-20261016-1351.json` dosyasına
retained export edildi. CRC16/tam CRC32 ve fsync/read-back geçti. Flash öncesi
PENDING aynı manifest'i; READ(29) diskle aynı kaydı verdi. Kapasite 30 olduğu
için korunmamış append-tail yoktu. CONFIRM/CANCEL/WIPE gönderilmedi.

Manifest: boot `17621734853131518669`, transfer `1`, request `3248669961`,
digest `2342140118`, count `30`. Dosya SHA256:
`15B2B512213A8C76D1B2E67B67299FABC5D333BFA38BD5E5ECF303EFBC954DBD`.
Disk kopyası korunur; eski on-board RAM/transfer/fault/deployment durumu yükleme
ve resetlerde silinir ve RAM'e otomatik geri yüklenmez.

### Derleme artifact'ları

| İmaj | Statik RAM (bayt) | Flash (bayt) | SHA256 (`firmware.bin`) |
| --- | ---: | ---: | --- |
| Normal | 42796 | 808369 | `EE102EE7374B2ACBB0A4CF08BF5F44C0646DC0F08552EF24657864D793907743` |
| CPU1 idle starvation | 42804 | 809093 | `2CE4581D67C4553A753D9FC94A62EAE457A54D7A8CC1084875947295AAA7DBD4` |
| CPU1 interrupt mask | 42804 | 809097 | `E43092914C045B842EBDD2C13BF03EA2796052A63B7C7BDC83B9755851F5626B` |

Statik RAM worker stack/TCB/heap maliyeti değildir. SDK config SHA256 öncekiyle
aynıdır: `DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Normal binary'nin gömülü ELF digest'i güncel ELF'in SHA256 değeriyle eşleşti.

### Kart sonuçları

Normal baseline **95.7 s** gözlemde geçti: SDK 15000 ms/idle mask 3/panic,
startup sırası ve NOMINAL mission döngüleri doğru; critical/fatal hata/reset yok.

CPU1 idle-starvation altıncı check/feed sonrasında başladı; check/feed 7 ve 8
başarılı kaldı. SDK yalnız `IDLE1 (CPU 1)` subscriber'ını timeout listesinde
bildirdi. Enjeksiyondan **15.000 s** sonra TWDT panic/reboot gözlendi; ikinci
boot'ta gerçek reset nedeni **6 (`ESP_RST_TASK_WDT`)** idi. Reinjection bastırıldı
ve en az 45 s supervisor/mission ilerlemesi geçti. Toplam gözlem **96.5 s**,
supervisor minimum boş stack **1616 bayt**. Başka failed subscriber, üçüncü boot
veya critical/fatal/stack/assertion hatası görülmedi.

CPU1 interrupt-mask altıncı check/feed sonrasında başladı. Seri enjeksiyon
işaretinden **0.297 s** sonra SDK CPU1 interrupt-watchdog panic'ini bildirdi;
gerçek reboot sonrası reset nedeni **5 (`ESP_RST_INT_WDT`)** okundu. Reinjection
bastırıldı ve en az 45 s supervisor/mission ilerlemesi geçti. Toplam gözlem
**81.9 s**, supervisor minimum boş stack **1616 bayt**. Başka critical/fatal/
stack/assertion hatası, ek reboot veya tekrar enjeksiyon görülmedi.

Bu gecikmeler seri işaret–SDK tanı aralığıdır; osiloskop ölçümü, WCET veya bütün
koşullarda sabit deadline değildir. Stack minimumları yalnız `WDT_MON` içindir.
Son normal restore dört flash bölgesinin hash doğrulamasıyla tamamlandı.
Ardından **95.8 s** normal gözlem geçti: test profili/hook mesajı yok; NOMINAL
mission ilerlemesi var; critical/fatal hata veya reboot yok. Paket **exit 0** ve
**`RESTORE PASS`** ile bitti. **Kartta normal `esp32dev` bırakıldı.** SDK config
aynı kaldı; NVS erase veya Wi-Fi/router/firewall/YAMCS ayar değişikliği yapılmadı.

Son normal gözlem sırasında salt-okunur YAMCS kontrolü üç güncel HK paketinin
yedi alan/birimini ve 20 s penceredeki **31 event**'i doğruladı. Eş metin çifti
ve duplicate wire packet yoktu; TC outgoing sayacı **104 → 104** kaldı. Bu kontrol
komut/reset göndermez. Artifact `.pio/live-check/hk-events-after-cpu1-20261016.json`,
SHA256 `8003C59AC44D155E551EF950B80EFA08DE1BF330BA93D82391735BDD8DE6B8A1`.

Kanıt dizini `.pio/host-tests/wdt-starvation-cpu1-20261016-110005-5490/`:
`wdt_normal_check.log`, `wdt_idle_starvation_check.log`,
`wdt_interrupt_mask_check.log`, `wdt_restore_check.log` ve build/upload logları.
Üst çıktı `.pio/live-check/watchdog-cpu1-target-20261016.log`; ilk build,
arşiv, preflight ve guard kanıtları aynı dizindeki `watchdog-cpu1-*` dosyalarıdır.
Özel arşiv/log dosyaları ignored kalır; GitHub'a yüklenmedi. Reset öncesi 30
kayıtlık disk kopyasının SHA256 değeri paket sonunda tekrar aynı bulundu.

Bu, CPU1'in belirtilen iki senaryosunu kapatır; eşzamanlı/global starvation,
genel repair/recovery veya uzun süreli soak kapsamını kapatmaz.
