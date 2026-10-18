# Ortak başlangıç sırası ve hata politikası

Güncelleme: 2026-10-17. Bu belge mevcut uygulama davranışını ve kapanış kontrol
listesini kaydeder; flight-qualified/flight-ready veya tüm init arızalarının
çözüldüğü iddiası değildir.

## Başlangıç sözleşmesi

`app_main()` önce `RECOVERY_APP_Prepare()` ile shared NVS init ve kalıcı
unfinished-boot işaretini dener, sonra `STARTUP_InitCoreServices()`, ardından
`STARTUP_CreateRequiredResources()` çağırır. Ortak servisler ve üç kaynak hazır
olmadan ağ/log başlangıcına veya uygulama görevlerini oluşturmaya geçilmez.
NVS/recovery hatası otomatik erase/abort döngüsü yerine bağımsız MODE kilidi
bırakır; mevcut required core init hatalarının abort politikası değişmez.
[Reset sonrası politika ve henüz yapılmayan fiziksel doğrulama](recovery_policy.md).

Ortak servislerin sırası korunmuştur:

`TIME → SYSTEM_MODE(INITIAL_BOOT) → FDIR → WATCHDOG_Init → ortak TM sender → RAM archive → HK → payload(OFF) → ADCS(DETUMBLE) → ilk simülasyon dönmesi`

Watchdog heartbeat mutex'i ve SDK recovery port'u core içinde required olarak
hazırlanır. Port mevcut TWDT'yi 15 s/iki idle/panic politikasıyla reconfigure eder;
SDK init/deinit tekrarlanmaz. Supervisor kendi task gövdesinde abone olur; başarılı
check sonrası besler, subscription/feed API hatasında abort eder. Supervision
epoch'u ise opsiyonel network/log startup'ından **sonra**, ilk task-create'ten
önce `WATCHDOG_Arm()` ile alınır. Uzun network startup'ı heartbeat süresine eklenmez.
Arm'dan sonra, mevcut 12 mission task'tan önce ayrı priority-1/6144 bayt
`RECOVERY` observer oluşturulur; başarısızsa per-boot MODE inhibit kalır.
Init/arm hatasında sonraki aşamaya/task oluşturma işlemine geçilmez; log + abort.
Tekrar arm epoch'u resetleyemez. [Watchdog sözleşmesi](watchdog_policy.md).

ADCS simülasyon başlangıç değeri `(10, -5, 0.5)` aynıdır. Getter'lardan okunan
payload/ADCS durumları ve mevcut readiness mesajları korunur. Payload otomatik
çalıştırılmaz; arşiv hâlâ RAM'dedir. Core helper init sıra/defaultları,
helper ayırma adımında görev öncelikleri/stack büyüklükleri ve haberleşme kodu değişmedi.
2026-10-14 watchdog kart testi için `WDT_MON` stack'i 2048'den 4096 bayta
çıkarıldı (+2048 bayt dinamik tahsis); önceliği 6 ve diğer görevlerin metadata'sı aynı.
FDIR warning adımında listeye required `FDIR` (2048 bayt, priority 4) eklendi;
ilk kontrol + 1000 ms nominal periyot [FDIR belgesindedir](fdir_policy.md). Sonraki runtime mod/
recovery ve servis mutex değişiklikleri [alt servis belgesindedir](subsystem_safety.md).

`STARTUP_InitCoreServices()` yalnız başlangıçta, tek caller tarafından çağrılır;
eşzamanlı init veya runtime restart API'si değildir. İlk girişte attempted flag
konur. Başarıdan veya yarım kalmış girişimden sonra ikinci çağrı, TIME/Archive
dahil hiçbir servise dokunmadan hata + abort üretir. Böylece helper üzerinden
yanlışlıkla MET reseti/arşiv silme/mutex yeniden tahsisi önlenir.
Başka public `*_Init()` API'lerine doğrudan çağrıları engelleyen global mekanizma değildir.

`STARTUP_CreateRequiredResources()` da ilk girişimden önce attempted flag koyar.
Başarılı, NULL çıktılı veya yarım kalmış bir ilk girişimden sonra tekrar çağrı,
hiçbir çıktı alanına dokunmadan ve yeni kuyruk/mutex tahsis etmeden log + abort
üretir. İlk girişimdeki NULL kontrolü ve tahsis sırası aynıdır. Bu tek caller
korumasıdır; eşzamanlı başlatma, cleanup veya runtime recovery sağlamaz.

## Mevcut hata sınıflandırması

| Adım | Bağımlılık / hazır olma koşulu | Mevcut hata davranışı |
| --- | --- | --- |
| Recovery prepare / shared NVS | Core'dan önce, boot başına tek SDK init sonucu ve 48 bayt sürümlü/CRC'li özet | Okuma/init/yazma/read-back hatası → bu boot'ta kaldırılamayan MODE inhibit; no auto-erase/retry. Kötü reset/geçmiş/3 başarısız açılış → sağlık + ayrı yer onayına kadar SAFE kapısı |
| TIME | SDK `esp_timer` app_main'den önce hazır; `time` component'i kullanılır | `void` API; ek hata sonucu yok |
| SYSTEM_MODE, FDIR, WATCHDOG_Init, ortak TM sender | Sırayla hazırlanır; servis mutexleri ve watchdog SDK recovery config'i başarılı olmalı | `false` → servis adıyla log + abort; sonraki init/resource/task yok |
| Archive | Ortak koruma/mesaj servislerinden sonra, consumer'lardan önce | `Archive_Init() == false` → log + abort; sonraki HK/payload/ADCS init yok |
| HK | Archive'dan sonra, payload/ADCS ve bütün görevlerden önce | `HK_Init() == false` → servis adıyla log + abort; sonraki servis/resource/network/task yok. Mutex oluşturma, 10 ms alma ve bırakma sonucu kontrol edilir |
| SIM başlangıç dönmesi | ADCS init'ten sonra, görevlerden önce sabit finite `(10,-5,0.5)` atanır | Setter `void`, tahsis/SDK/HAL erişimi yok; runtime sensör veya donanım sağlık kanıtı değil |
| Payload, ADCS | Her servis kendi required state mutex'ini init'te oluşturur; API hâlâ `void` | Mutex allocation/init lock hatasında servis mesajı + abort; core devam etmez |
| TM queue, TC queue, EPS mutex | Core helper tamamlanmış olmalı; üçü hazır olmadan global handle atanmaz | Tahsis hatası → log + abort; önceki local tahsisler panic/reset'e bırakılır |
| Ağ/log başlangıcı | Ortak servisler ve gerekli kaynaklar hazır olmalı | Mevcut opsiyonel devam/retry politikası korunur; ayrıntılar önceki ağ belgesinde |
| WATCHDOG_Arm | Network/log denemesi dönmüş, henüz task-create yok | `false` → log + abort; sıfır task oluşturma |
| RECOVERY observer | Arm'dan sonra, 12 required mission task'tan önce | Create/tekrar start hatası → kalıcı per-boot MODE inhibit; mission task açılışı devam eder, otomatik reset yok |
| `create_required_tasks()` içindeki 12 required task | Yukarıdaki aşamalar geçilmiş olmalı | `xTaskCreate != pdPASS` → task adı/stack/priority/`required_created=N/12` logu + abort; sonraki task oluşturulmaz |
| WDT_MON task gövdesi | SDK subscription ve her başarılı check sonrası feed | SDK API failure → port logu + abort; failed check ise beslemez, persistent stall SDK timeout'una gidebilir |

Ortak TM sender'ın mutex tahsisinin fatal sayılması mevcut `main.c` politikasının
korunmasıdır; "bütün haberleşme hataları opsiyoneldir" varsayımı yapılmaz.
Task listesinde komut alıcısı ve simülasyon görevleri de hâlâ required sınıfındadır.
Bu sınıflandırma bu alt adımda yeniden tasarlanmadı.

SDK config şu anda `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y`, reboot delay 0'dır.
Fatal hata kaynaklarını temizleyip aynı boot'ta tekrar başlatma eklenmedi. Kalıcı
bellek yetersizliği required init'e ulaşmayı engelliyorsa abort/reset hâlâ olabilir;
yeni sayaç/SAFE kapısı fiziksel arızayı onarmaz veya SDK panic'ini kapatmaz.
Panic/reset RAM arşivini kaybettirir; küçük NVS özeti arşiv yedeği değildir.

17 Ekim'de `STARTUP_InitNvs()` boot başına ilk SDK sonucunu cache'ler hale geldi.
Recovery önce, opsiyonel Wi-Fi daha sonra aynı sonucu görür; ikinci SDK init/retry
yapılmaz. Artık NVS başarısızlığı yalnız Wi-Fi'ı atlamakla sınırlı değildir:
çalışma modu da inhibit edilir. Eski ağ/NVS fiziksel testleri kendi önceki
imajları için geçerlidir; bu yeni davranış henüz karta yüklenmedi.

## Açık component bağımlılıkları

Yeni helper production component header'larını kullanır. `main/CMakeLists.txt`
`adcs`, `archive`, `hk`, `payload`, `comms`, `fdir`, `system_mode`, `freertos` yanında
`time` bağımlılığını artık açıkça bildirir. `time` component'i `esp_timer` gerektirir.
Mode init NOMINAL/DEGRADED ile deployment kapısını atlayamaz; production'ın
INITIAL_BOOT başlangıcı aynıdır. [Deployment sözleşmesi](deployment_policy.md)
tamamlanma bilgisini ve güvenli mod korunmasını açıklar.
Global include flag'lerinin bütün component bağımlılıklarını gizlemediği veya bütün
header sahipliğinin kapandığı iddia edilmez; tüm component incelemesi ayrı roadmap maddesidir.
Sonraki ilk bildirim düzeltmesinde `main` için doğrudan CDH/EPS private bağları ve
diğer component'lerde 12 eksik private proje bağı eklendi. Kaynak audit'i ve normal
CMake configure geçti; full compile/link ve global include kaldırma henüz yapılmadı.
[Bağımlılık incelemesi](component_dependencies.md).
2026-10-15: global proje include/LDF yolu kaldırıldı; normal compile/link geçti.
Main'in private local config yolu korundu. 47 proje kaynağı tek derleme yolunda;
görev metadata'sı ve init/runtime kaynakları değişmedi. Yeni imaj karta yüklenmedi.

## Doğrulama

`tests/host/startup_core/check.c` gerçek `main/main.c`, `main/startup_core.c` ve
`main/startup_resources.c` dosyalarını birlikte derler. Servis/SDK/RTOS sınırları
taklit edilir; xTaskCreate metadata'sı kaydedilir, task gövdeleri çalıştırılmaz.
Watchdog init/arm yollarının eklenmesiyle 28 ayrı süreç senaryosu:

- Başarı: servis/getter/ilk dönme sırası, kuyruk kapasite/eleman boyları, üç global
  handle ve 12 task'ın function/name/stack/priority/sırası doğrulanır; `WDT_MON`
  stack'i 4096 bayt, diğer 11 görevin metadata'sı korunur.
- SYSTEM_MODE, FDIR, watchdog ve sender başlangıcı ayrı ayrı başarısız: doğru servis logu,
  tek abort, caller'a devam olmaması, sonraki init/queue/network/task çağrılarının yokluğu.
- Üç required resource tahsis hatası: global handle'lar NULL kalır; network/task yoktur.
- Opsiyonel başlangıç sonucu ve log mirror yokluğu: mevcut akışta 12 task oluşturma
  isteği devam eder. Bu, task gövdelerinin canlılığı veya gerçek ağ testi değildir.
- Başarıdan ve dört kritik servis hatasından sonra tekrar core init: ikinci hata/abort
  dışında trace değişmez; seeded arşiv durumu korunur, TIME ve diğer init'ler tekrar çağrılmaz.
- 12 task oluşturmanın her biri ayrı ayrı başarısız: doğru task logu/abort,
  daha sonraki task oluşturma ve caller continuation yoktur.
- Arm hatası: core/resource/network adımları tamamlanmışken sıfır task oluşturma;
  doğru hata logu/abort, caller continuation yoktur.

2026-10-14: toplam 13 C host grubu (bu grupta 24, mevcut ağ grubunda 25 senaryo),
79 Python kontrolü ve normal `esp32dev` derlemesi geçti. Statik RAM 42188/327680,
flash 792953/1740800 bayt. SDK config SHA256 değişmedi. Mevcut ADCS/FDIR/HAL compiler
uyarıları kaldı; bu işlem uyarı temizliği değildir. Kart yüklenmedi/resetlenmedi;
bu değişikliğin fiziksel boot/fault/recovery doğrulaması yapıldı denmez.

### 2026-10-16: Kaynak oluşturma tekrar giriş koruması

Kaynak helper'ına tek girişim koruması eklendi. Beş ilk giriş senaryosu
(başarı, NULL, üç tahsis hatası) ve yedi tekrar giriş senaryosu ayrı süreçlerde
geçti. Başarıdan sonra aynı/farklı/NULL çıktı; her tahsis hatasından ve NULL ilk
girişimden sonra tekrar çağrı reddedilir. Çıktı byte'larının değişmediği ve yeni
tahsis/caller continuation olmadığı kontrol edildi. Test için production reset
API'si eklenmedi; statik korumanın boot sınırı ayrı süreçlerle modellenir.

İlgili iki C grubu (12 kaynak + 31 gerçek app_main senaryosu), ardından toplu
42 C/47 header/346 Python, 22 graf/layout, 9 runner-refusal ve 47 negatif profil
kontrolü geçti. Normal `esp32dev` derlemesi, güncel metadata/ELF ve 51 kaynak için
tek compilation yolu da doğrulandı. Statik RAM 42796/327680, flash
808501/1740800 bayt. SDK config SHA256 değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.

Karta yükleme/reset yapılmadı; karttaki önceki normal imaj değişmedi. Bu koruma
masaüstü ve target derleme düzeyinde doğrulandı; fiziksel allocation/panic,
kısmi task açılışı, reboot-loop ve genel recovery açıklarını kapatmaz.

## Kapanış kontrol listesi

- [x] Ortak servisler için tek orchestration giriş noktası ve tek caller sözleşmesi.
- [x] Mevcut init sırası/defaultlar/readiness mesajları korunmuş.
- [x] Main için doğrudan `time` component bağımlılığı bildirilmiş.
- [x] Kritik servis hatasında gerçek app_main'in sonraki aşamalara geçmediği host'ta test edilmiş.
- [x] Tekrar helper çağrısı hiçbir init/clock/archive reset çağrısından önce reddedilmiş.
- [x] Required resource/task hata ve opsiyonel devam dalları gerçek app_main ile host'ta test edilmiş.
- [ ] Bütün component bağımlılıklarını/header sahipliğini kapat.
- [ ] `void` init API'lerinin kapsamını ve varsa gerçek HAL/SDK başarısızlıklarını ayrı değerlendir.
- [ ] Kısmen çalışmaya başlamış gerçek RTOS görevleri varken fatal task-create politikasını target'ta doğrula.
- [x] SDK supervisor/idle-stall recovery politikası: paylaşılan 15 s + panic/reset,
  check tamamlanmadan besleme yok; priority 6/24 aynı. Runtime config dosyaya yazılmaz.
- [x] Kontrollü supervisor self-stall → gerçek SDK timeout/panic/reboot → TASK_WDT
  reason 6 ve ikinci boot mission ilerlemesi kartta doğrulandı.
- [ ] Genel idle/global starvation/interrupt masking ve kesin reboot sürelerini doğrula;
  reset RAM'i kaybettirir, verified repair/persistent restart inhibit henüz yoktur.
- [x] Yedi kritik görev heartbeat'i, required init/arm, korumalı timestamp ve FDIR timeout kaydı.
- [ ] Boot/deployment/SAFE/NOMINAL akışını EPS/ADCS/payload davranışlarıyla eşleştir/test et.
- [x] FDIR warning processing için required task ve başarısız kararda sayaç koruma/retry.
- [x] FDIR critical/fatal RAM neden kaydı, merkezi operating gate ve EPS owner resolve.
- [ ] Verified diğer-source/fatal/uncertainty recovery ve bütün mode/mission politikasını kapat.

### 2026-10-16: HK başlangıcında sessiz hata kapısı

HK'nin eski `void` init'i mutex oluşturulamaz/alınamazsa sessiz dönüyordu;
core helper payload init'e ve "HK and payload initialized" mesajına devam
edebiliyordu. `HK_Init()` artık `bool` döndürür. Başarı için mutex tahsisi,
10 ms ile sınırlı alma, mevcut tablo/limit/periyot reseti ve başarılı mutex
bırakma gerekir. `false` core'da "Failed to initialize HK service" + abort'tur.
Payload/ADCS/resource/network/task başlangıcına ve readiness mesajına geçilmez.
Fatal/reboot politikası değiştirilmedi; runtime retry veya cleanup eklenmedi.

Allocation/acquire hatasında entry/limit/periyot/son-due durumu değişmez.
Release hatası resetten sonra görülebilir; rollback garantisi verilmez, caller
devam etmez. Başarılı pre-task resetler mevcut mutex'i yeniden kullanır; bu,
runtime reset yetkisi değildir. Ana core helper'ın once-only koruması aynıdır.

Mevcut diğer `void` başlangıç yolları kaynak üzerinden incelendi: TIME port'u
SDK'nin app_main'den önce hazırladığı esp_timer'ı kullanır; simülasyon setter'ı
yalnız finite sabit state yazar. Payload/ADCS required mutex init hatasında zaten
abort eder ve alt servis host grubunda allocation/lock yolları sınanır. Mock
`sat_hal` bu core init zincirinde çağrılmaz; gerçek sensör HAL entegrasyonu
ve SDK'nin kendi app_main öncesi hata yolları bu kapsamda değildir.

HK fixture'ı artık altı ayrı süreçte çalışır: mevcut tam servis regresyonu,
ilk allocation/acquire/release hataları, dolu state üzerinde acquire hatasının
değişiklik yapmaması ve başarılı resetin aynı mutex'i kullanması. Gerçek
app_main grubuna HK failure ve sonrasında repeat-init reddi eklendi: **33 süreç**.
Readiness printf'i ayrıca yakalanır; HK başarısızken sahte hazır mesajı verilmediği,
sonraki çağrı/handle/task/caller continuation olmadığı doğrulanır. Toplam yedi
yeni process case vardır; fault injection yalnız host mock sınırındadır.

Tam **43 C / 47 header / 372 Python (20 dosya)**, **22 graf/layout**, **15 runner
refusal**, **59 negatif profil** ve güncel normal metadata/ELF kontrolü geçti.
52 production C kaynağı tek derleme yolunda. Normal build 27.49 s; RAM **42884**
(değişmedi), flash **809293** (**+80**). Yeni task/mutex/tahsis eklenmedi;
mevcut HK mutex'i korunur. Firmware SHA256:
`940AD74F6AEA45127D37EC193267B6DF635A3C1C8B09E9A04AB2098C54F00D1D`.
SDK config aynı:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Yerel loglar: `.pio/host-tests/hk-init-20261016/`.

Bu adımda karta upload/reset/komut yok; RAM arşivi ve karttaki önceki recovery
paketinin normal imajı korunur. Yeni HK guard'ının fiziksel allocation/panic
veya kısmi RTOS startup doğrulaması yapılmış sayılmaz. Genel startup/mode/recovery
üst maddesi açık kalır. Uzun süreli test kullanıcı isteğiyle yarına bırakıldı,
başlatılmadı. Native YAMCS/Java paketi wire/MDB değişmediği için tekrarlanmadı.

2026-10-14 alt servis adımında 16 C grubu (yeni güvenlik grubunda 164 senaryo),
79 Python kontrolü ve normal derleme geçti. Yeni payload/ADCS state mutex'lerinin
allocation ve init lock fatal yolları ayrı host senaryolarıdır; core grubu gerçek
bu servislerin gövdelerini çalıştırmaz. Yeni runtime heap maliyeti ölçülmedi.
Son imaj RAM 42188/327680, flash 795061/1740800; karta yükleme/reset yapılmadı.

2026-10-14 FDIR warning adımında 18 C grubu (FDIR 73, güncel core grubu 25),
79 Python kontrolü ve normal derleme geçti. Statik RAM 42188/327680, flash
796085/1740800; yeni task'ın runtime stack/TCB heap maliyeti ölçülmedi.
Karta yükleme/reset yok; host task-create hata testi target panic/reboot kanıtı değildir.

2026-10-14 aktif FDIR adımında 19 C grubu (yeni registry/gate grubu 117), 79 Python
kontrolü ve normal derleme geçti. Statik RAM 42524/327680, flash 797241/1740800;
tablo için +336 statik RAM baytı, yeni heap/mutex/task yok. Core sırası/12 görev
metadata'sı ve required init politikası aynı; karta yükleme/reset yapılmadı.

2026-10-14 software watchdog adımında 20 C grubu (yeni watchdog 61, core 28),
79 Python kontrolü ve normal derleme geçti. Statik RAM 42548/327680, flash
798229/1740800; +24 statik RAM yanında yeni mutex'in dinamik maliyeti ölçülmedi.
12 task'ın metadata'sı aynı; SDK ayarı/yükleme/reset ve arşiv silme yok.

2026-10-14 SDK recovery adımında 21 C grubu (watchdog 70, ESP32 port 13, core 28),
79 Python kontrolü ve normal derleme geçti. Statik RAM 42556/327680, flash
799881/1740800; önceki heartbeat imajına göre +8 RAM/+1652 flash. Yeni SDK task
aboneliğinin runtime heap/stack maliyeti ölçülmedi. sdkconfig dosyası aynı, fakat
TWDT runtime policy artık 15 s/iki idle/panic olur. Karta yükleme/reset yok.

Sonraki watchdog kart paketi (2026-10-14): 23 C grubu ve 122 Python kontrolü,
üç imaj derlemesi, normal kart baseline'ı, heartbeat kaybında SAFE/tek C200 nedeni
ve gerçek supervisor SDK timeout/reset reason 6 testi geçti. WDT_MON 4096 bayt
stack ile minimum 1588 bayt boşluk gösterdi; diğer 11 görev metadata'sı aynı.
Bu yalnız bu görevin bu bounded yolundaki high-water ölçümüdür, tüm sistem
heap/stack/WCET bütçesi değildir. Resetler eski RAM arşiv/fault/deployment state'ini
geri alınamaz şekilde kaybettirdi. [Kart kanıtı ve restore](watchdog_target_tests.md).

Host'ta abort'un longjmp ile yakalanması gerçek cihaz panic/reboot testinin yerine
geçmez. İlk task, xTaskCreate'den hemen sonra diğer task'lar henüz oluşturulmadan
çalışabilir; startup bütün görevlerin atomik olarak açıldığı bir transaction değildir.

### 2026-10-16: Kısmi görev başlangıcının host modeli

Required görevlerin aynı metadata ve oluşturma sırası `main.c` içindeki tek
`static const` tabloya taşındı. Stack/priority/function/name/NULL argument ve
NULL output handle korunur; ek startup gate, task, heap tahsisi, suspend/delete,
rollback veya retry yoktur. Hata mesajı artık `required_created=N/12` içerir.
Bu sayı yalnız daha önce `pdPASS` dönen required oluşturmalardır; SDK/Wi-Fi
görevleri veya gerçekten çalışan/sağlıklı görev sayısı değildir.

Mevcut 33 core senaryosuna **25 süreç** eklendi; toplam **58**:

- Başarılı açılışta her mock task, kendi xTaskCreate çağrısı henüz dönmeden
  bir kez ilerler. Readiness yalnız 12 başarılı oluşturma ve diagnostics
  başlangıç denemesinden sonra basılabilir.
- On iki ayrı hata konumunda failed task hiç ilerlemez. Öncekiler varsa
  oluşturma sırasında ve hata logu ile abort arasında birer kez ilerleyebilir.
  İlk task hatasında sıfır çalışma sınırı da kapsanır. Negatif allocation
  dönüşü (`-1`) `pdPASS` sanılmaz.
- Her hata için görev kimliği/stack/priority ve exact `N/12` logu; sonraki
  oluşturma, resource monitor, core-ready mesajı ve caller continuation yokluğu
  doğrulanır. Yayınlanmış kaynak handle'ları değiştirilmez.
- Aynı on iki hata sonrası tekrar core init ayrıca reddedilir. Task probe
  sayıları/shared-state canary ve testte seed edilen arşiv durumu korunur;
  TIME/init/resource/task yolları tekrar çağrılmaz.

Probe'lar **stub task gövdeleri ve deterministik actor sırasıdır**. Gerçek
WDT/EPS/deployment/CDH task kodları veya FreeRTOS scheduler bu fixture'da
çalışmaz; IRQ, iki-core yarış, panic sırasında freeze veya ikinci boot kanıtı
değildir. Gerçek app_main/core/resource orchestration C kaynakları kullanılır.
Bağımsız fixture metadata tablosu production tablosuyla karşılaştırılır;
beklenti production tablosundan kopyalanarak çalışma anında üretilmez.

Tam **43 C / 47 header / 372 Python (20 dosya)**, **22 graf/layout**, **15 runner
refusal**, **59 negatif profil** ve normal metadata/ELF kontrolü tekrar geçti.
52 production C kaynağı tek derleme yolunda; normal ELF test hook'u içermiyor.
Normal build 13.07 s: RAM **42884**, flash **809293**, önceki HK build'iyle aynı.
Yeni firmware SHA256:
`C7C5AC48712C5BC92CF30F814173B8C4AF102F9A2368F213AA48C3A0CBE0884E`.
SDK config SHA256 aynı:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Yerel kanıt: `.pio/host-tests/partial-startup-20261016/`.

Bu paket host/model/build kapsamını kapatır; gerçek kartta kısmi task-create
hatası → panic/reset ve yeniden boot testi **açıktır**. Karta upload/reset/komut,
RAM arşiv silme, ağ/MDB değişikliği ve uzun süreli test yapılmadı. Önceki fiziksel
watchdog sonuçları bu yeni startup akışının target kanıtı olarak kullanılmaz.

### 2026-10-16: Pre-init hata durumunda doğru mode diagnostic'i

Mode init'ten önce raporlanan critical/fatal neden uncertainty floor'u bırakır.
Servis bu durumda doğru SAFE/EMERGENCY'yi kullanıyordu ancak istenen INITIAL_BOOT'u
logluyordu. Init mesajı artık effective init-mode snapshot'ını raporlar; fault
ve operating gate davranışı değişmedi. İki yeni regression, gerçek alt servislerle
birlikte kalan uncertainty/operation inhibit'i de doğrular. Toplam 12 lifecycle
senaryosu/183 güvenlik süreci ve tam host/build/ELF/metadata/59 profil geçti.
Son normal RAM 42884, flash 809289; SDK config aynı; karta yüklenmedi.
[Güncel hash ve kapsam](subsystem_safety.md#2026-10-16-birlikte-boot-safe-ve-owner-recovery-akışı).

### 2026-10-16: Seçilen gerçek kart başlangıç hata yolları

Yukarıdaki host adımlarından sonra üç ayrı opt-in return-code profili, güncel
RAM'in korunması ve ayrı kullanıcı onayıyla kartta geçti: HK caller false,
CMD_PROC 3/12 ve SOLAR_HARV 11/12. Gerçek abort/PANIC reset nedeni 4 ve ikinci
boot'ta normal görev ilerlemesi doğrulandı. Normal baseline 95.8 s, son normal
restore/gözlem 95.7 s / 15 NOMINAL döngüsü; paket exit 0 / RESTORE PASS.
İlk normal restore %77'de seri aktarım hatası verdi; standart PIO fallback
başarılı oldu. Hata gizlenmedi, USB/seri kök nedeni çözülmüş sayılmaz.

Kartta artık güncel normal `esp32dev` var. RAM 42884 / flash 809269; normal ELF
test hook'u içermiyor, SDK config aynı. Hazırlıkta 46 C/48 header/408 Python,
22 runner-refusal ve 75 negatif profil geçti; bu kart adımında kaynak değişmediği
için tam C/Python tekrar yapılmadı. Native YAMCS ve restore sonrası üç HK/yedi
birim/28 olay kontrolü ayrıca geçti. 30 eski RAM kaydı aynı SHA256'lı üç yerel
export'ta korunur; eski boot'un RAM/pending/fault durumu onaylı resetlerle kayboldu.

Bu, seçilen caller/panic/ikinci boot kanıtıdır: gerçek HK mutex allocation veya
heap exhaustion enjekte edilmedi; bütün task konumları/SDK/HAL failure ve genel
persistent recovery doğrulanmış sayılmaz. [Profil, tam kanıt ve sınırlar](startup_target_tests.md).
