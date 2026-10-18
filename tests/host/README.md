# Masaüstü kontrolleri

18 Ekim yayın kontrolünde mevcut watchdog runner-refusal testinin uzun checkout
yoluna bağlı ekran metni varsayımı düzeltildi: dört invalid-argument vakası
`ValidateSet` kelimesinin ekrandaki satır düzeni yerine invariant validation error
ID'sini ve ilgili parametreyi ister.34 refusal asıl ve kaynak-snapshot klasörlerinde
geçti; firmware/runner güvenlik kapıları ve senaryo sayısı değişmedi. Özel ayarsız
kaynak snapshot'ından normal build ve tam52 C/53 header/535 Python(27 dosya),
metadata/ELF,49 refusal/39 workflow/97 invalid-profile kontrolleri de PASS oldu.
[Kaynak-only build ve yayın kontrolü](../../docs/release_preflight.md).

17 Ekim NOMINAL hazırlığı: tam **52 C grubu /53 header /533 Python (27 dosya)**
ve metadata/normal ELF ayrımı geçti. Yeni `eps_sim_check`, gerçek solar task ile
bounded enerji/dt/SOC ve kilit dışı snapshot kontrollerini çalıştırır. Mevcut
EPS sunrise/SOC ve payload rate vakaları düzeltildi/genişletildi.
`archive_client/test_drain.py` artık13 testle iki doğrulanmış dosya + checkpoint
öncesi CONFIRM olmamasını, tail retention, belirsiz yanıtta retry olmaması ve
batch arası graceful stop/refusal kapılarını kontrol eder. Transfer sorguları yalnız
transfer-container indeksini ister; existing client testi bütün packet sorgularında
bu filtreyi doğrular. Son ground düzeltmeleriyle **535 Python/27 dosya** tekrar
geçti. Native Yamcs16 komut/21 postprocessed/10 range reddi geçti; bunlar saatlik
kart PASS'i değildir. [Uygulama ve sınırlar](../../docs/nominal_soak_target.md).

Bu kontroller, gerçek C servislerini bilgisayarda derleyip çalıştırır. Test kaynakları
`tests/host/` altında, üretilen programlar `.pio/host-tests/` altında tutulur.
PlatformIO firmware derlemesi `main/` ve `components/` kaynaklarını kullanır.

## Çalıştırma

Tüm bilgisayar kontrolleri için tek giriş (mevcut normal firmware derleme
kayıtlarını/ELF'i de denetler; firmware derlemez veya karta yüklemez):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_checks.ps1 -Compiler C:\cygwin64\bin\gcc.exe -Python C:\.platformio\penv\Scripts\python.exe -BuildMetadata
```

Normal build henüz yoksa `-BuildMetadata` atlanabilir; ELF/profile kontrolleri
açıkça `SKIP` yazılır, normal firmware ayrımı doğrulanmış sayılmaz. Bu komut
51 C/52 header, keşfedilen PowerShell kontrolleri ve 26 dosyadaki 522 Python
testini çalıştırır (2026-10-17 kontrol noktası). Python dosyaları ayrı süreçlerde çalışır; aynı isimli
`test_monitor` modülleri birbirini gölgelemez. Her hata veya sıfır Python testi
paketi başarısız yapar. `-BuildMetadata` ile 59 geçersiz watchdog profil/core ve
21 geçersiz startup ve 17 geçersiz mission lifecycle profil kombinasyonunun derleyicide reddi de kontrol edilir. Seri port, UDP, firewall veya target runner
çağrısı yoktur; hardware testleri ayrı ve açıkça istenen işlemlerdir.

PowerShell ve pthread desteği olan GNU GCC gerekir (bu makinede Cygwin GCC).
Proje kökünden bütün C kontrol gruplarını ve bağımsız header kontrollerini birlikte çalıştırın:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_host_tests.ps1
```

GCC PATH üzerinde bulunmuyorsa derleyici yolunu belirtin:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_host_tests.ps1 -Compiler C:\cygwin64\bin\gcc.exe
```

Betik herhangi bir derleme veya kontrol başarısızlığında durur. Başarı durumunda
grupların `PASS` sonuçları ve `All host checks passed: ...` özeti görünür.
Yeni recovery politika/port/app grupları 47+12+31 ayrı mock süreçtir. Native YAMCS
11 komutlu kaynak MDB'sini ayrı offline test eder; kurulu ground'a yüklemez.
Legacy reset runner'ı persistent recovery kodu varken build/flash öncesi durur;
34 refusal testi bunu gerçek tool/port kullanmadan doğrular. Yeni güvenli target
runner ayrı pakette hazırlandı; eski runner engeli kaldırılmadı.
[Politika](../../docs/recovery_policy.md), [kısa kart paketi ve yapılmayanlar](../../docs/recovery_target.md).

`persistent_recovery/test_reader.py` 55, `test_archive_guard.py` 11 offline Python
testidir. Gerçek yeni araç kodu sahte seri/HTTP/saat ile çalışır; queued/Sent ve
board COMPLETE ayrı doğrulanır, 30 kayıtlık tam retained kopya kapısı sınanır.
Yerel iki boot demo TC'si yalnız tam injector/CRC/router zinciriyle ayrı tanınır;
fallback CLI sonucu paket başarısı veya upload boyunca kalıcılık iddia etmez.
Yeni reader, passive baseline/ilk boot sayacı ve tam 0/2/32/34 geçmiş bağlama için
14 ek test içerir. FAULT history ayrı onay ister; eksilen/yeni history, fatal/
uncertainty/overflow/boot-loop ve sayaç kaybı otomatik onaylanmaz. Passive inspection
reset/TC/NVS yazmaz; tamamlanmış durumun kimliğini bağlar, sağlık/onarım kanıtlamaz.
`check.ps1` 15 erken-ret ve 39 gerçek workflow/artifact mock senaryosu çalıştırır;
builder/uploader/HTTP/serial çağrıları gerçeğe gitmez. Hata öncesi flash olmaması,
finally-normal-restore, failed seçimin PASS olmaması, image/SDK hash reddi test edilir.
Koruma gerektiren runner artık `UseRomLoader` seçimini I/O öncesinde reddeder;
ROM/no-stub fallback yoktur. Bu koruma eski seri hata kök nedeninin çözümü değildir.
Arşiv öncesi/sonrası baseline eşleşmesi, her upload öncesi/sonrası NVS probe kapısı,
pre-restore proof kaybında normal flash girişimi ama app'i loader'da bırakma da sınanır.
Yeni gerçek imaj kontrolü `tools/check_persistent_recovery_images.ps1` ayrı normal/
seçilen HK-panic veya BOOT_LOOP build gerektirir; tam host runner bu hedef imajı derlemez veya yüklemez.

`recovery_loop_injection_check` test-only üç-PANIC enjeksiyonunun 16 C mock
senaryosudur; invalid journal/storage/reset nedeni yeni abort üretmez, üçüncü
başarısız açılıştan sonra gerçek HK çağrısı korunur. `test_boot_loop.py` 16 testte
dört boot/üç abort, exact sayaç ve BOOT_LOOP eşiği, ayrı test/geçmiş onayları,
sticky normal restore ve fake USB/HTTP/saat CLI zincirini sınar. Gerçek kart
reset/NVS kanıtı değildir. Runner'ın BOOT_LOOP seçimi iki ayrı opt-in ister;
normal tek-PANIC seçimi eski BOOT_LOOP geçmişini sessizce onaylamaz.
[Kart paketi ve açık sınırlar](../../docs/recovery_boot_loop_target.md).

`persistent_recovery/test_flash_probe.py` 15 testtir: salt-okunur loader probe'unda
beklenen MAC, gerçek resident tablo/layout/MD5, çift NVS okuması, kısa/kararsız veri,
değişen hash, no-overwrite ve raw hata sansürü. Probe'un fiziksel çalışması RAM'i
resetler ve loader'da bırakır; masaüstü fixture gerçek port açmaz. Raw NVS/credential
saklanmaz; byte-level eşitlik fonksiyonel recovery/uzun koşu veya aralıklı aktarım
hatasının çözümü değildir. [Gerçek tekli yazım kanıtı](../../docs/recovery_target.md).

Mission lifecycle driver'ının 25 mock süreç testi ve 16 okuyucu testi
[kart hazırlığından](../../docs/mission_lifecycle_target.md) ayrıdır; hiçbir gerçek TC
göndermez. Startup test hook grupları 5 + 15 + 15 süreçtir.
Watchdog test hook grupları 11 + 8 + dört kez 10 + 15 global + 17 recovery; watchdog grubu 132,
ESP32 watchdog port'u 13, aktif FDIR grubu 127, FDIR warning grubu 74,
deployment grubu 212, alt servis güvenlik grubu 183, EPS yük grubu 316,
güç/mod grubu 78, ortak başlangıç grubu 58, başlangıç kaynakları grubu 12,
Wi-Fi servis grubu 25 ayrı süreç senaryosu çalıştırır. Güncel grup/kaynak/case
listesinin tek sahibi manifest'tir; belge sayıları yukarıdaki tarihli kontrol noktasıdır.

FDIR stack regresyonu: `fdir_check` içindeki `task_stack_periodic`, ölçümün servis
kilitleri dışında ve 30 döngüde bir yapıldığını, farklı mock StackType_t genişliği
ile bayta dönüşümü doğrular. `fdir_stack/test_monitor.py` 12 evidence-parser testi
içerir ve ortak runner tarafından keşfedilir. Gerçek kart komutu/reset yan etkisi
ve bounded sonuçlar [FDIR kart belgesindedir](../../docs/fdir_stack_target.md).

Arşiv target aracının `archive_target/test_target.py` içindeki 19 kontrolü fake API
modeliyle çalışır; ortak host runner gerçek donanıma komut göndermez. Gerçek kart
aracı default dry-run'dır; silme için `--send --confirm-clear` birlikte gerekir.
Koşullar, yan etkiler ve gerçek fiziksel sonuç [işlem belgesindedir](../../docs/archive_target_tests.md).

Küçük bir değişiklikte yalnız ilgili C grubunu çalıştırmak için:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_host_tests.ps1 -List
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_host_tests.ps1 -Compiler C:\cygwin64\bin\gcc.exe -TestName hk_service_check -SkipHeaders
```

Seçimli çalıştırma tam regresyon değildir; atlanan header'lar ve kısmi sonuç
açıkça yazılır. Geçersiz grup adı derlemeden önce hata verir. `-List` derleyici
gerektirmez. Grup/kaynak/case listelerinin tek sahibi
`tools/host_test_manifest.ps1` içindeki `Get-HostTestPlan` fonksiyonudur; listeyi
okumak test çalıştırmaz veya dosya üretmez.

## Test ve normal firmware ayrımı

Kalıcı fixture'lar `tests/host/`, çıktılar `.pio/host-tests/` altındadır.
Önceden açık kalmış `.pio/.../check.c` dosyaları bakımı yapılan test kaynağı
değildir; örneğin TC testi `tests/host/tc_frame/check.c` altında yaşar.
`tools/check_test_layout.ps1` tüm 46 C test/yardımcı kaynağının kayıtlı olduğunu, kayıtlı
kaynak/include yollarının bulunduğunu ve üretilen `.pio` girdisi kullanılmadığını
denetler. Bu korumanın 14 PowerShell testine sekiz mevcut graf testi eşlik eder.

`-BuildMetadata` kontrolü normal `esp32dev` compile kayıtlarında host
kaynak/include yollarını ve `FSW_TEST_*`/`SI_TEST_MODE` define'larını reddeder;
son üretim kaynaklarından eski ELF'i kabul etmez ve bilinen target injection
sembollerini kontrol eder. C macro kontrolü case-sensitive'dir; Türkçe yerel
ayarda IgnoreCase karakter aralıklarına dayanmaz.

Target enjeksiyon kaynakları `main/*_fault_injection.c`,
`components/watchdog/watchdog_test_esp32.c` ve
`components/watchdog/watchdog_global_test_esp32.c` ve
`components/watchdog/watchdog_recovery_test_esp32.c` içinde profil guard'larıyla kalır;
normal build'de bu dosyaların kayıtlı olması enjeksiyonun aktif olduğu anlamına
gelmez. Normal ELF'de gerçek test hook/wrapper olmaması ayrıca denetlenir.
SDK iç testleri, bütün olası transitive/preprocessor kaçakları veya yüzde coverage
bu guard'ın kanıtı değildir. Fiziksel test çalıştırıcıları `tools/test_*.ps1`
altında ayrı kalır; birleşik host komutu onları çağırmaz.

`main/startup_fault_injection.c`, yalnız üç opt-in startup imajında HK init / erken
ve geç task-create dönüşlerini değiştirir; normal ELF'de `__wrap_HK_Init` ve
`__wrap_xTaskCreatePinnedToCore` yokluğu denetlenir. Üç C wrapper grubu 5+15+15
süreçtir. Diğer task/SDK çağrıları aynen geçirilir, gerçek heap tüketilmez.
18 Python okuyucu testi sahte reset/port/saat ve seçilen hata→abort→ikinci boot
kanıtını sınar. Runner'ın güncel 34 refusal kontrolü gerçek araç/port kullanmadan yanlış
seçimleri reddeder. [Kart hazırlığı ve ayrı fiziksel sonuç](../../docs/startup_target_tests.md).

## Kapsam

### Component/build kontrolü (firmware çalıştırmaz)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\check_component_dependencies.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\component_dependencies\check.ps1
```

Bu ayrı salt-okuma kontrolü 16 component'in doğrudan proje-header bağlarını,
proje bağlarının gereksiz public olmamasını, bildirilen public/private proje
grafının döngüsüz olmasını ve genel proje `-I`/ikinci LDF yolunun geri gelmediğini
denetler. Sekiz ayrı PowerShell graf kontrolü leaf/chain/diamond/empty sıralamayı
ve self/private cycle, cycle tüketicisi ve bilinmeyen düğüm reddini doğrular.
Tam CMake parser'ı, implicit SDK/link-only bağı veya runtime çağrı/kilit döngüsü
denetimi değildir. Normal firmware derlemesinden sonra
`-BuildMetadata` seçeneği 57 kayıtlı proje C dosyasının güncel SDK compilation
database'de birer kez bulunduğunu ve link map'inde LDF arşivi olmadığını da ister.
Kaynak/metadata kontrolleri 2026-10-17'de tekrar geçti; C/Python test sayısına eklenmez.
[Derleme sonucu ve kalan sınırlar](../../docs/component_dependencies.md).

### C servis kontrolleri

- `recovery/check.c`: gerçek politika + MODE kapısı, sahte 48 bayt port; 47 süreç.
  CRC/version/reserved/counter, önceki boot/geçmiş ve 3 failed boot, bounded writes,
  kalıcı IO failure, sağlık/rollover/token/queued-rearm ve fatal/release-race.
- `recovery/port_check.c`: gerçek ESP32 adapter'ı, SDK/NVS/RTOS mock'ları; 12 süreç.
  Namespace/key/size/close, set/commit/read-back hata yolları; erase yok, gerçek NVS yok.
- `recovery/app_check.c`: gerçek observer/startup adapter'ı, servis/RTOS mock'ları;
  31 süreç. Archive/FDIR/mode/deployment/EPS/payload/7 heartbeat, create/repeat/init
  hataları. Stack high-water/gerçek çok-core/flash latency bu testlerin kanıtı değildir.

- `adcs_math/check.c`: ölçekli vector/quaternion normu, zero/tiny/nonfinite/null,
  dot/cross, bilinen Hamilton product, iki operand alias'ı, conjugate/inverse,
  +90° rotation, orthogonality ve q/-q eşitliği; B-Dot dt ve PD rad/s algebra'sı.
- `adcs_sim/check.c`: raw modelin rad/s/µT gürültüsü, salt ölçümün plant'i
  ilerletmemesi, dt damping/decay, overflow/invalid step atomicity, normalize edilmiş
  attitude ve km/yüzde yakıt bütçesi. Bunlar saf C'dir, RTOS/SDK içermez.
  Nominal kontrolün braking işaretine dönüşümü ve izin/kilit davranışı gerçek
  servislerle `subsystem_safety` grubunda doğrulanır. [Birim/model sınırları](../../docs/simulation_contracts.md).
- `headers/translation_unit.c`: eski 9 sabit fixture yerine `components/*` ve
  `main` altındaki 52 exported top-level header otomatik bulunur. Her header
  ayrı compiler çağrısında iki kez `-include` edilerek `-fsyntax-only` ve
  `-Wall -Wextra -Werror -pedantic-errors` ile kontrol edilir. Yalnız kendi dizini
  ve açıkça eşlenen public bağımlılık yolları verilir; runtime fixture'ların geniş
  include listeleri buraya sızmaz. EPS/main public RTOS/SDK tipleri minimal mock
  bildirimleri kullanır. SDK'nin bütün header'ları, test include dizinleri, başka
  derleyiciler veya C++ uyumu bu kontrolün kapsamı değildir.
- `radio_link/check.c`: taşınan gerçek demo parser/frame, `TM_SendReport` ve
  CCSDS wrapper kaynakları; CRC16 referansı, 1..64 bayt frame roundtrip,
  bozuk CRC/0-65-255 uzunluk reddi ve resync, mid-frame reset ve güvenli sınırda
  0/1/50 bayt telemetri gövdesi (başlık/zaman/CRC) test edilir. Router ve clock
  taklit edilir; `SI_TEST_MODE` yalnız host binary'sinde açılır. Eski demo
  type biti 15 Ekim'de `0x1800` → `0x0800` olarak düzeltildi; bu fiziksel radyo,
  gerçek command routing veya geçersiz TM argümanlarında bellek güvenliği testi değildir.
- `mission_contract/check.c`: mevcut APID/komut/router fault numaralarını,
  watchdog rol sırasını, 14 bayt başlık/17..64 bayt iç TC sınırlarını, 68 bayt
  command queue ve 20 bayt GCC HK yapı yerleşimlerini sabit referanslara karşı
  doğrular. Host'ta CDH/RTOS/SDK include yolları olmadan derlenip çalıştırılır.
  Aynı dosya taşıma öncesi ve sonrasında ESP32 GCC ile `-fsyntax-only` olarak
  geçti. Bu layout'un başka derleyicilerde taşınabilirliği, bitfield endian düzeni
  veya bütün wire formatlarının/komut davranışlarının doğrulanması değildir.
  Arşiv okuma/downlink testleri de CDH include yolu olmadan çalışır.
- `hal/check.c`: gerçek mock HAL'in 256 kayıt adresinin tamamını, 0/255 sınırlarını
  ve bağımsız kayıtların korunmasını; GPIO 0/31 ve geçersiz pinleri, init/loopback'i,
  I2C NACK/boş/null, UART busy/ready/boş/null ve SPI transfer/deselect yollarını
  kontrol eder. I2C/UART'ın yalnız ilk baytı işlemesi mevcut mock sözleşmesidir;
  çok baytlı fiziksel sürücü veya gerçek STM32/ESP32 donanım testi değildir.
- `watchdog_injection/check.c`: gerçek test-only hook'ları için 11 task-loss ve
  8 supervisor-stall süreç senaryosu. Tek hedef/tek drop mesajı, diğer heartbeat'lerin
  korunması, gerçek snapshot'ın salt okunması, altıncı check'te yalnız current-task
  suspension ve gerçek SDK reset nedenine bağlı test reinjection suppression.
  SDK/RTOS/ledger sınırları taklit edilir; cihaz reseti değildir.
  [Fiziksel paket, veri kaybı ve kapsam](../../docs/watchdog_target_tests.md).
- `watchdog_starvation/check.c`: CPU0/CPU1 idle-starvation ve IRQ-block hook'ları
  için dört ayrı grupta onar süreç senaryosu. Core seçimi, task oluşturma,
  altıncı başarılı check kapısı ve SDK reset nedenine bağlı reinjection suppression.
- `watchdog_starvation/global_check.c`: iki pinned worker için 15 süreç senaryosu;
  priority/core seçimi, iki task'ın hazır olma kapısı, hata/timeout ve marker flush
  tamamlanmadan spin'in başlamaması. Bunlar taklit RTOS testleridir; gerçek iki
  çekirdek reset kanıtı [ayrı kart belgesindedir](../../docs/watchdog_global_target.md).
- `watchdog_recovery/check.c`: gerçek drop/resume/re-drop hook'unda 17 süreç;
  iki episode, ground bekleme, diğer fault/fatal/uncertainty ve normalde enjeksiyon
  olmaması. Hook mode/clear/SDK feed/reset çağırmaz. `test_recovery.py` 26 offline
  reader/allowlisted mode-command testi içerir. [Kontrollü kart paketi](../../docs/watchdog_recovery_target.md).
- `watchdog_port/check.c`: gerçek ESP32 adapter'ı, taklit SDK API'leriyle 13 süreç.
  Hazır SDK'ya reconfigure (15 s, iki idle, panic), status/config/add/reset hata
  yolları. Yeniden SDK init/deinit yok; hardware timer/reset kanıtı değildir.
- `watchdog/check.c`: gerçek supervisor/FDIR/MODE, target gibi 100 Hz tick ile
  132 süreç. Yedi görevde sınır/rollover/latch, altı mode, init/arm/invalid/lock,
  check/pet transaction sırası, early heartbeat, gerçek beş saniyelik monitor loop
  ve dolu/başarısız fault kaydı. SDK config/init failure, subscription/feed abort,
  tamamlanmış check sonrası besleme, başarısız/bloke check'te beslememe,
  transient recovery'de fault korunması ve SAFE/task timeout'ta beslemeye devam.
  RTOS/time/network/recovery port'u taklit edilir; gerçek scheduling,
  watchdog hardware veya otomatik reset testi değildir.
  [Watchdog sözleşmesi](../../docs/watchdog_policy.md).
  Yeni 54 süreç, revision sonrası stable-heartbeat timeout recovery/relapse,
  30 s/gap/freshness/rollover, yarış/lock failure ve diğer neden/mod/deployment
  korunmasını sınar. Yalnız liveness cause çözülür; bounded iki-episode kart yolu
  ayrıca geçti, diğer task/domain/fatal matrisi bu sonuçla kapanmaz.
  [Toparlanma koşulları ve sınırlar](../../docs/watchdog_recovery.md).
- `fdir_faults/check.c`: gerçek MODE/FDIR/EPS/Archive/CRC kaynakları; 127 süreç
  senaryosu. 16-slot kayıt, severity promotion, her mode API/ground/deployment
  kapısı, effective permission, overflow/recording failure, revision ile eski
  clear'ın reddi, EPS 8.5 V/finite/lock/recovery/race ve gerçek CRC hata yolu.
  RTOS/time/network sınırları taklit edilir; CRC bozulması yalnız test RAM'inde.
  Target scheduler/hardware/kalıcı fault history değildir.
  Son 10 süreç zero-wait owner Try API'lerinde busy/invalid/null/uninitialized,
  stale revision/fatal ve başka neden/uncertainty korunmasını doğrular.
  [Aktif hata sözleşmesi](../../docs/fdir_faults.md).
- `fdir/check.c`: gerçek FDIR service/task ve mode service; 74 süreç senaryosu.
  Altı mod × 0..4 warning, altı kaynak, altı gerçek task akışı ve init/kilit/retry/
  ACK/karar yarışları. Eşik altındaki sayaçların korunması, başarısız geçişte retry,
  BOOT/safety modlarında bekletme, ACK transaction sırası, yeni warning'in eski
  batch temizliğiyle kaybolmaması ve kilit dışında I/O doğrulanır. RTOS/YAMCS/time
  sınırları taklit edilir; tam aktif-fault registry matrisi yeni ayrı gruptadır,
  target timing testi değildir. [FDIR sözleşmesi](../../docs/fdir_policy.md).
- `deployment/check.c`: gerçek mode service, deployment task ve FDIR ground
  handler; 212 süreç senaryosu. Öncesi/sonrası Set/Transition/ground (180), altı
  mode completion, 11 gerçek task akışı ve 15 init/lock/invalid/ACK/callback yolu.
  İki simülasyon beklemesi, yalnız completion bookkeeping retry, mevcut safety
  mode/committed fault korunması ve başarısızlıkta self-delete/başarı mesajı yokluğu
  doğrulanır. RTOS/YAMCS/time sınırları taklit edilir, fiziksel timer/anten/TC/
  kernel delete doğrulaması değildir. [Deployment sözleşmesi](../../docs/deployment_policy.md).
- `subsystem_safety/check.c`: gerçek EPS, ADCS service/manager/control/math/SIM,
  payload, gerçek deployment task'ı ve FDIR/global mode ile 183 süreç senaryosu.
  96 permission, 30 ADCS/payload/boost mode eşlemesi, 45 özel hata/race/recovery
  ve 12 bütünleşik lifecycle senaryosu. Sensörler/
  irtifa kontrollü; actual SIM physics/boost çağrılarında kilit/zero koşulları
  doğrulanır. Payload generation/archive taklit edilir. Stale output temizliği,
  prepare/submit sınırı, explicit recovery, ACK/NACK, dört mutex init fatal yolu,
  NULL/kilit/geçersiz ölçüm ve gerçek manager heartbeat kontrol edilir.
  SDK/RTOS/network/physical actuator doğrulaması değildir; [sözleşme ve sınırlar](../../docs/subsystem_safety.md).
  Klasik operation fixture'ları completion API'siyle post-deployment state hazırlar.
  Lifecycle senaryoları ise gerçek deployment task'ının iki delay argümanı ve
  delete yolunu modelleyerek BOOT'tan başlar; gerçek duvar-saati/scheduler testi değildir.
  EPS owner-clear, ayrı yer mode komutu, yerel EPS recovery ve explicit ADCS/payload
  restart ayrı kapılardır. Diğer critical/fatal/uncertainty, PDU kesme ve relapse
  engelleri; pre-init effective-mode diagnostic'i kontrol edilir.
  LOW seed'in active cause'u operating/BOOT mode isteğini reddeder; tablo sıfırlanıp kapı atlanmaz.
- `eps_loads/check.c`: gerçek EPS manager, EPS commands/monitor, global mod ve
  FDIR; 316 ayrı süreç senaryosu. Altı global × dört gerçek döngülerle hazırlanan
  yerel mod × 12 voltaj eşlemesi (288; ULP komşuları ve NaN/±Inf dahil), kilit/NULL/uninitialized/retry yolları,
  açma öncesi güvenli mod kararları, açma sonrası yeni kararın sonraki döngüde
  uygulanması, recovery/relapse, COMMS/diğer bit ve flag koruması, heater stub,
  SoC sınır/monotonluk/geçersiz değer hesabı, NULL komut ve gerçek fault-clear/
  explicit restart/threshold chatter akışı. Sunrise ve mevcut 3.3 V eşiği korunur.
  Callback EPS → mode kilit sırasıyla PDU'yu
  günceller; fault raporları ve loglar kilitler bırakıldıktan sonra çalışır.
  Yeni quiet EPS resolve, EPS data → MODE sırasıyla ölçüm kilidi altında çalışır;
  LOW seed için engellenen mode istekleri de doğrulanır. Yerel mod getter'ı
  aynı EPS mutex'ini kullanır. RTOS/time/ağ taklit edilir; gerçek donanım yük kesme,
  scheduler/tepki süresi veya ADCS/payload üretiminin durması doğrulanmaz.
  [Mod politikası](../../docs/system_modes.md) kapsam ve kalan yolları açıklar.
- `eps_mode/check.c`: gerçek EPS monitor task, sistem modu ve FDIR servisleri;
  78 ayrı süreç senaryosu. Altı modda düşük/toparlanma eşikleri, tam sınırlar,
  float ULP komşuları ve NaN/±Inf,
  EMERGENCY'nin korunması, yalnız CRITICAL → SAFE toparlanması, EPS/mod mutex
  hataları, başlatılmamış mod servisi ve geçiş öncesi deterministik mod değişimleri.
  Bir/iki gerçek task döngüsünde watchdog/1000 ms delay, başarısız geçişte sahte
  başarı olmaması ve sonraki döngüde yeniden değerlendirme test edilir.
  Geçersiz gerilim active power fault üretir; ölçülmüş düşük gerilim gibi
  CRITICAL zorlamaz, EMERGENCY/CRITICAL'ı korur ve toparlanma yetkisi vermez.
  RTOS/time/ağ sınırları taklit edilir; gerçek scheduler veya fiziksel gerilim
  hatası değildir. FDIR unused-variable uyarısı 15 Ekim'de temizlendi; grupta
  `-Werror` istisnasız korunur. [Mod politikası](../../docs/system_modes.md)
  bu sınırlı düzeltmeyi ve alt servislerde açık kalan davranışları ayırır.
- `startup_core/check.c`: gerçek `app_main()`, core ve resource helper'ları birlikte
  derlenir. Servis/RTOS/SDK sınırları taklit edilir. 58 süreç senaryosu; servis sırası,
  ilk mod/dönme, kuyruk tipleri/global handle'lar, 12 task'ın function/name/stack/priority,
  kritik servis (HK dahil) ve üç resource hatası, watchdog arm hatası, her task-create hatası, opsiyonel devam ve
  başarılı/başarısız ilk girişim sonrası ikinci core init'in reddi. HK hazır mesajının
  başarılı kapıdan önce verilmediği printf sınırında kontrol edilir. Yeni 25 case:
  erken çalışan task probe'larıyla başarı, 12 hata konumu ve her birinden sonra
  tekrar-init reddi. Negatif SDK dönüşü, exact `required_created=N/12`, hata logu
  sırasında önceki görevlerin ilerleyebilmesi, shared canary ve handle korunması,
  core-ready/diagnostics/sonraki oluşturma yokluğu sınanır. Gerçek task gövdesi,
  RTOS scheduler, ağ veya cihaz panic/reboot çalıştırılmaz. [Başlangıç politikası](../../docs/startup_policy.md)
  kapsanan ve açık kalan yolları ayırır.
- `tc_ingress/check.c`: gerçek UDP receiver/CRC/processor/router zinciri; 85 offline
  girdi, 7..54 baytın tamamı, büyük datagramın kırpılması, version/type/secondary/
  sequence/length reddi, golden 19 bayt iç paket/CRC, queue full ve altı hedef + unknown
  APID. Kuyruktan 55 komut işlenir. Ek primary helper döngüsü unaligned buffer'da
  2048 APID × dört type/secondary eşleşmesini ve legacy TM canary'lerini kontrol eder.
  RTOS/socket/alt servisler mock'tur; gerçek ağ/kart komutu yoktur.
  [Wire/iç paket ve CRC'nin sınırları](../../docs/tm_tc_contract.md).
- `tc_frame/check.c`: kısa/taşan paketler, geçerli boy sınırları, CRC reddi, komut
  verisinden CRC'nin ayrılması ve watchdog çağrıları. Kuyruk ve alt servisler taklit
  edilir; gerçek komut işlemcisi ve yönlendirici derlenir. Body/trailer/cached CRC
  bozulması ayrı ayrı denenir; sadece cached CRC'ye güvenilmez.
- `archive_read/read_bounds_check.c`: kayıt indeksleri, kapasite sınırı, bozuk
  uzunluklar ve checksum uyuşmazlığı. Bozulma sadece test programının RAM'inde yapılır.
- `archive_downlink/downlink_check.c`: CRC16 referans vektörü, sürümlü/big-endian
  arşiv TM gövdesi, 0/128 bayt veri, son kayıt, bozuk kayıt reddi ve gönderim
  başarılı/başarısız olduğunda kaydın korunması. UDP göndericisi taklit edilir.
- `yamcs_sender/sender_check.c`: gerçek gönderici kodunu pthread mutexleriyle
  çalıştırır. Dört iş parçacığının 800 telemetri/olay paketini, APID sıra numaralarını,
  mesaj kırpmayı ve başarısızlık yollarında kilidin bırakılmasını kontrol eder.
  Soket çağrıları taklit edilir; ağ üzerinden paket gönderilmez.
  IP kaybında soketin kapanması, offline gönderimin engellenmesi, gönderim/timeout
  ayarı hatasında sonraki çağrıda yeni soket açılması ve eşzamanlı link değişimi de test edilir.
  FreeRTOS görev öncelikleri ve priority inheritance davranışı bu masaüstü kontrolünün kapsamı dışındadır.
- `hk_service/check.c`: gerçek HK servisini çalıştırır; beş parametrenin inclusive
  limitleri, negatif/taşan ID, ters limit reddi, reset, null/kısa buffer ve canary,
  25 bayt tablo ve 28 bayt master raporun byte-order/float/rad/s alanlarını kontrol eder.
  1..60 saniyenin her biri, hatalı komutlar, force/periodic ayrımı, uint32 zaman
  taşması, catch-up burst olmaması, mutex/snapshot/TX hataları ve aynı due slot'a
  yeniden giriş test edilir. Altı ayrı süreçte mevcut tam servis kontrolüne ek
  init allocation/acquire/release hatası, seeded state korunması ve mutex reuse
  sınanır. Init sonucu bool, alma bounded 10 ms'dir; false startup'ı durdurmalıdır.
  RTOS, EPS/ADCS ve gönderici mock'tur; gerçek ağ yoktur.
- `tm_gen/check.c`: gerçek görev gövdesini 10 ms tick ile çalıştırır; 1 s HK/pet,
  5 s NOMINAL-only arşiv kuyruğu, deterministik tüm paket baytları, tick/ms ayrımı,
  sıra sayacı, kuyruk dolduktan sonra devam, moda geçiş ve ms wrap kontrol edilir.
  Kuyruk/RTOS/HK/mod/watchdog sınırları mock'tur; gerçek kart sıfırlanmaz.
  Kapsam ve kalan sınırlamalar [HK sözleşmesindedir](../../docs/hk_contracts.md).
- `startup_services/check.c`: gerçek `HK_Init()`/`Payload_Init()` fonksiyonlarını
  önceden doldurulmuş durum üzerinde kontrol eder. HK değer/zaman/geçerlilik alanları
  temizlenir; payload telemetrisi sıfırlanır ve `OFF` durumunda veri/yazma üretmez.
  RTOS, ADCS, arşiv, FDIR ve ağ sınırları taklit edilir; veri üretimi ve arşiv dolu
  hatası yalnız test durumunu doldurmak içindir, gerçek karta komut gönderilmez.
  HK limit/rapor zamanlaması ve bütün payload komut politikası bu testin kapsamı dışındadır.
- `adcs_startup/check.c`: gerçek ADCS servis/görev/simülasyon/control/math kaynaklarını
  derler. `DETUMBLE`, simüle actuator bayrağı ve ilk dönme değerlerini kontrol eder;
  görevden önce işlenen `NOMINAL` komutunun ilk gerçek görev döngüsünde korunduğunu,
  init/ilk dönme ayarının tekrarlanmadığını ve watchdog/100 ms delay çağrılarını doğrular.
  RTOS delay noktasında ilk döngüden çıkılır; EPS/FDIR/time sınırları taklit edilir.
  Ağ/kart komutu gönderilmez. Bu, tüm ADCS matematiği, birimleri, mod geçişleri,
  runtime eşzamanlılık veya recovery doğrulaması değildir.
  Kullanılmayan `world_mag_field` 15 Ekim'de kaldırıldı; bütün C gruplarından
  `-Wno-error=unused-variable` istisnası çıkarıldı. ADCS ve alt servis güvenlik
  fixture'ları kullandıkları FDIR API'sini artık doğrudan include eder.
- `startup_resources/check.c`: gerçek `main/startup_resources.c` yardımcısının
  iki kuyruk için kapasite/eleman boylarını ve üç geçerli handle üretmesini kontrol eder.
  İlk kuyruk, ikinci kuyruk ve EPS mutex tahsislerinin her biri ayrı ayrı başarısız
  yapılır; doğru kaynak mesajı, tek `abort()`, sonraki tahsisatların yapılmaması ve
  çağıran koda dönülmemesi doğrulanır. NULL çıktı argümanı tahsisatsız reddedilir.
  RTOS tahsisleri/loglar taklit edilir, abort yalnız host'ta yakalanır. Gerçek
  `app_main()`/Wi-Fi görevleri veya fiziksel kartta bellek yetersizliği çalıştırılmaz.
- `network_startup/check.c`: gerçek ağ başlangıç yardımcısını, YAMCS göndericisini
  ve UDP komut alıcısını birlikte derler. Servis henüz kurulmamışken, servis kurulup ağ hazır değilken ve
  hazır bilgisinin yayınlanması/okunması başarısızken soket/bind/receive/send
  çağrılarının yapılmadığını kontrol eder. Hazır durumda komut alıcısının IP kontrolü
  olmadan bind/receive yoluna girdiğini; socket/bind/timeout ayarı hatasında bekleyerek
  yeniden denediğini doğrular. EAGAIN/EINTR soketi korur, EIO kapatıp yeniden açar.
  Başarılı receive sınırında testten çıkılır; paket işleme bu testin kapsamı değildir.
  Dört SDK init hata sonucunun tek çağrıdan sonra aynen döndüğünü, deinit/retry
  yapılmadığını ve hazır bilgisinin kapalı kaldığını kontrol eder. Başarı hazır
  bilgisini açar; bu bilginin yayınlanması başarısızsa artık ESP_FAIL dönülür, core
  abort edilmez. SDK iç kısmi tahsis/heap tüketimi veya assert test edilmez.
  Olay altyapısı yardımcısı da ESP_OK ve dört hata sonucuyla test edilir: tek create,
  sıfır delete/retry/abort, hazır bilgisinin korunması ve doğru hata logu. Başka
  yerde oluşturulmuş olay altyapısı dokunulmadan kalır; TC hâlâ bind/receive yapabilir.
  Tüm RTOS/soket sınırları taklit edilir; gerçek ağ veya kart komutu kullanılmaz.
- `startup_nvs/check.c`: gerçek `STARTUP_InitNvs()` yardımcısını ESP_OK, boş sayfa
  yokluğu, sürüm uyumsuzluğu, bellek yetersizliği, bölüm bulunamaması, genel hata ve
  tanınmayan hata ile yedi ayrı süreçte kontrol eder. Her süreçte üç çağrı aynı
  cache'lenmiş sonucu döndürür; SDK init yalnız bir kez çağrılır;
  erase/retry/abort yoktur, başarısızlık logu doğru hata adını ve politikayı bildirir.
  SDK NVS/log sınırları taklit edilir; gerçek flash, SDK iç recovery veya fiziksel
  bellek yetersizliği test edilmez. `app_main()` görev devamı ayrı kart testi konusudur.

Python fixture'ları `tests/host/**/test_*.py` ile otomatik keşfedilir. Yukarıdaki
kontrol noktasında 21 dosyada 408 test vardır. Watchdog okuyucusunun genel,
CPU0/CPU1 ve çift-worker testleri, kaynak izleme, HK/event, arşiv ve native MDB
profil guard'ları bu toplama dahildir. Güncel toplam ve dosya başına sayılar
`run_checks.ps1` çıktısında bildirilir; fiziksel donanım testi sayılmazlar.

`resource_monitor/test_monitor.py` 37 kontrolle kaynak okuyucusunu sınar: bir
saatlik sahte saat/port akışı, 45 s rapor sessizliği, split UART panic satırı,
buffered raporların reddi, heartbeat ilerlemesi/saturasyonu, mod örnekleri ve
opsiyonel mod şartı, yeni dosya koruması, RUNNING checkpoint ve hata/Ctrl+C/disk
başarısızlığı. Gerçek seri port açmaz; [uzun koşu hazırlığı](../../docs/resource_target.md#uzun-koşu-için-okuyucu-hazırlığı)
gerçek kartta uzun test sonucu değildir.

`yamcs_mdb/YamcsMdbCheck.java` isteğe bağlı native YAMCS kontrolüdür; sıradan
Python/C komutu Java bağımlılığı gerektirmez veya bu testi çalıştırmış sayılmaz.
`tools/check_yamcs_mdb.ps1 -GroundProject ...` mevcut pinned 5.12.0 jar'larıyla
gerçek TM extraction ve dokuz komut türünün gerçek builder/postprocessor akışını
ağ olmadan çalıştırır. [Profil, kurulum ve canlı reload sınırı](../../docs/yamcs_profile.md).
Watchdog ayrıştırıcısı normal/drop/stall kanıt sırasını, SAFE/tek neden korunmasını,
SDK'nin unserved subscriber listesini, gerçek reset nedenini ve recovery mission
ilerlemesini sınar. Beklenmeyen panic/reset, eksik/yanlış kanıt ve başka SDK
subscriber timeout'u reddedilir; seri port açılmaz.

2026-10-14 watchdog kart paketi de geçti: normal baseline 95.8 s, task-loss 65.7 s
SAFE/tek C200 nedeni ve reboot yok; supervisor self-stall'dan 15.00 s sonra gerçek
SDK panic/TASK_WDT reason 6, ikinci boot mission ilerlemesi. Minimum WDT stack
boşluğu 1588 bayt; stack 4096 bayt. Son normal restore/gözlem 95.8 s, exit 0 ve
RESTORE PASS ile tamamlandı. Kartta normal imaj var; eski RAM arşivi resetlerle
kayboldu. [Paket, düşük hızlı yükleme ve kapsam](../../docs/watchdog_target_tests.md).
Wi-Fi monitor kontrolleri seri port açmadan timeout kanıt
ayrıştırıcısını test eder: geçerli üç döngü, eksik/zamansız kanıt, beklenmedik IP,
reboot/panic/watchdog/queue hatası, eksik görevler, stack hazırlık sırası ve IP olmadan
TC dinleme kanıtı. Bu testler Wi-Fi SDK hata yollarının veya fiziksel bağlantının
yerine geçmez.
NVS monitor kontrolleri ise hata profili/politika/sıra kanıtını, ağın kapalı kalmasını,
TC'nin atlanmasını ve üç NOMINAL döngüsünü ister; IP/ağ hazırlığı/yanlış profil,
erase/retry trap, eksik görev, reboot/panic/watchdog/queue hatalarını reddeder.
NVS/ağ/olay başlangıç monitorleri aynı parametreli `StartupFailureEvidence` çekirdeğini
kullanır. Ağ monitor kontrolleri kendi profil/hata/politika mesajlarını ister,
NVS/timeout profillerini reddeder; retry/deinit/yanlış ready/event-loop trap'leri,
sıra/tekrar hataları ve eksik çekirdek kanıtı ayrıca kontrol edilir.
Olay altyapısı monitorü farklı olarak stack hazırlığı ve TC dinlemesi ister;
TC'nin atlanması hatadır. IP, olay altyapısının başarılı oluşturulduğu mesajı,
yanlış profil, trap ve eksik/sırasız kanıt reddedilir. Önceki NVS/ağ monitorleri
kapalı-stack kontrolünü korur ve yeni olay hata profilini reddeder.

## ESP32 arşiv başlangıç sırası kontrolü

`archive_client/test_client.py` 51 sahte API/board/opener/clock testinde kontrollü
HTTP istemcisinin retain/save/clear, repeat/unknown receipt, pagination/baseline,
wrong target/binary/CRC, file failure, explicit CLI ve HTTP sınırlarını doğrular.
Export/resync grubu 15'tir. Ağ/seri/card veya JVM server başlatılmaz.
[İstemci kullanımı ve gerçek end-to-end sınırı](../../docs/yamcs_archive_client.md).

`archive_wire_check` gerçek dispatcher/core ile beş yeni komutun 235 hatalı boyunu,
yanıtın golden BE baytlarını, 0/9/128 payload ve send/receipt/clear korumalarını
offline sınar. Yeni Python wire grubu 11 test; profile guard 10→14'tür. Native
YAMCS kontrolü toplam 19 postprocessed komutu/19 geçersiz girdiyi doğrular.
[Wire/ground kaynak bağlantısı ve fiziksel sınır](../../docs/archive_transfer_wire.md).

Yeni `archive_transfer_check` prefix/ACK API'sini ve `archive_port_check` ESP RNG
adaptörünün sınırlı hata davranışını kontrol eder. `archive_transfer/test_export.py`
12 offline doğrulama/dosya hata testi ekler. Bunlar yeni TC/XTCE/live ground veya
fiziksel clear doğrulaması değildir; [çekirdek kapsamı](../../docs/archive_transfer.md).

Arşiv runtime eşzamanlılık/doluluk host kontrolleri ayrı
`archive_sync_check` ve `data_logger_check` gruplarındadır. İlki gerçek pthread
mutex ile 60 paralel yazma ve 3000 karışık işlem dahil snapshot/timeout/full/wipe
sınırlarını; ikincisi gerçek tüketici görevinde hata sonrası ilerlemeyi sınar.
Memory-corruption hook yalnız açık test define'ıyla derlenir; normal ELF'de yokluğu
denetlenir. [API ve doğrulama sınırları](../../docs/archive_concurrency.md).

`Archive_Init()` yalnız `app_main()` içinde, Wi-Fi/log sender ve mission görevleri
başlatılmadan önce çağrılır. Data Logger arşivi yeniden başlatmaz. `main` component'i
arşiv bağımlılığını açıkça bildirir.

Seri monitor kapalıyken açılış sırasını gözlemek için:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_startup_check.py --port COM7 --reset
```

Bu kontrol kartı **yeniden başlatır; mevcut RAM kayıtları kaybolur**. Hazırlık
mesajının gözlenen açılışta bir kez, TC PROC ve DATA LOGGER görev mesajlarından önce
geldiğini, ardından IP ve NOMINAL döngüsünü doğrular. Komut göndermez, firewall veya
YAMCS ayarlarını değiştirmez. Arşiv erişiminin mutex korumasını, başka init/hata
yollarını veya kalıcı flash depolamayı doğrulamaz. Sonraki kayıt okuması aşağıdaki
YAMCS komut kontrolüyle ayrıca doğrulanır.

HK ve payload da `app_main()` içinde arşivden sonra, uygulama görevlerinden önce
hazırlanır; `main` bu iki component'e açıkça bağımlıdır. Payload otomatik olarak
çalıştırılmaz. Aşağıdaki seçenek ek olarak HK/payload hazırlık mesajını ve gerçek
payload telemetrisindeki `OFF` (`0`) durumunu ister; TC PROC, DATA LOGGER ve TM
Generator görev mesajlarının üçünden de önce hazırlanmış olmalıdır:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_startup_check.py --port COM7 --reset --services
```

Bu seçenek de resetle RAM arşivini siler. Açılış kontrolü görev sırası ve payload
durumunu gözler; HK tablosunun temizliği ayrı C host kontrolünde doğrulanır. Mevcut
init fonksiyonlarının davranışı değişmez; runtime recovery/factory reset eklenmez.

ADCS hazırlığı ve ilk simülasyon dönme ayarı da `app_main()` içindedir. ADCS görevi
bunları tekrar çağırmaz; `main` açık `adcs` bağımlılığı taşır. Tüm hazırlık sırası için:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_startup_check.py --port COM7 --reset --adcs
```

`--adcs`, `--services` kontrollerini de açar. ADCS init mesajını bir kez ve gerçek
modu `DETUMBLE` (`1`) olarak ister; hazırlık TC PROC, DATA LOGGER, TM Generator ve
ADCS TASK mesajlarının dördünden önce gelmelidir. Bu da kartı resetler ve RAM
kayıtlarını siler. Erken mod komutu yalnız C host kontrolünde işlenir; kartta ADCS
modunu değiştiren bir test komutu gönderilmez. Mevcut başlangıç değerleri ve control
algoritmaları korunur; sistemin global moduyla ADCS politikasının hizalanması ayrı iştir.

## Zorunlu başlangıç kaynakları

`main/startup_resources.c/.h`, telemetri kuyruğunu (10 × `HK_Telemetry_t`), komut
kuyruğunu (5 × `TelecommandPacket_t`) ve EPS data mutex'i oluşturur. Üçü de hazır
olmadan dönmez; `app_main()` global handle'ları ancak başarıdan sonra atar. Bu çağrı
Wi-Fi ve `YAMCS_LogInit()` öncesindedir; log görevi artık zorunlu kaynaklardan önce
oluşmaz. Kuyruk kapasiteleri ve paket tipleri değiştirilmedi.

Bu üç tahsisattan herhangi biri başarısızsa kaynak adıyla log basılır ve `abort()`
edilir; önceki `app_main()` return davranışı yoktur. Daha önce başarılı tahsisatlar
temizlenmez; mevcut panic/reset politikasına bırakılır. Yardımcı boot'ta bir kez
çağrılmak içindir, retry/recovery veya global `SAFE` geçişi sağlamaz. Mevcut
koruma, başarı veya başarısız ilk girişimden sonraki çağrıyı log + abort ile
reddeder: eski/yeni çıktı alanına dokunulmaz ve tekrar tahsis yapılmaz.
`startup_resources/check.c` 12 ayrı süreçte normal/null/üç tahsis hatasını ve
aynı/farklı/null çıktı ile tekrar girişleri sınar. Eşzamanlı çağrı desteklenmez.
Mevcut `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT` ayarında kart yeniden başlatılır; kalıcı
bellek yetersizliği reboot döngüsü oluşturabilir. Zorunlu görev oluşturmanın mevcut
abort davranışı, Wi-Fi sonuç hatası ve log init hatasında devam yolları değişmedi.
SDK Wi-Fi init hatalarının tamamının non-critical olduğu iddia edilmez.

Normal kart açılışında önceki servis kontrolleriyle birlikte kaynak hazır mesajının
Wi-Fi IP mesajı ve dört tüketici görevden önce geldiğini gözlemek için:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_startup_check.py --port COM7 --reset --resources
```

Bu da resetle mevcut RAM arşivini siler. Kartta bellek-hatası enjekte etmez; log
görevinin çağrı sırası source incelemesinde, üç başarısız tahsisat yolu ise C host
kontrolünde doğrulanır. Tüm kaynak yetersizliği, panic/reboot veya ağ hatalarının
target üzerinde doğrulanması bu adımın kapsamı dışındadır.

## Ağ altyapısı hazır değilken soket erişimi

Amaç, ağ altyapısı hiç başlatılamamışken çekirdek görevlerin haberleşme çağrılarıyla
henüz hazır olmayan lwIP soket altyapısına girmesini engellemektir. Bu hazırlık
kontrolü Wi-Fi bağlantısı veya IP adresiyle aynı şey değildir.

`YAMCS_Init()` yalnız gönderici mutex'ini kurar; ağ hazırlığı varsayılan olarak
kapalıdır. `app_main()` içindeki `wifi_init_sta()`, NVS başarısından sonra
`STARTUP_InitNetworkStack()` yardımcısını çağırır. Yardımcı ancak `esp_netif_init()`
başarıyla döndükten sonra `YAMCS_MarkNetworkStackReady()` çağırır. Hazır bilgisi mevcut
gönderici mutex'iyle korunur; yeni mutex/kuyruk tahsis edilmez. Bu tek yönlü,
başlangıç kontrolüdür; runtime stack deinit/yeniden başlatma mekanizması değildir.

Hazır değilken telemetri/olay gönderimi `false` döner; soket açılmaz veya gönderilmez.
UDP komut alıcısı soket açmadan kendini sonlandırır. Atlanan alıcı sonradan otomatik
yeniden oluşturulmaz. Wi-Fi timeout/kopması ise hazır bilgisini kapatmaz: stack zaten
kurulmuştur ve mevcut geç bağlantı davranışı bu adımda değiştirilmez. Bu kontrol
SDK init'lerinin iç assert/abort yollarını kaldırmaz. Sonraki NVS politika adımı
uygulamanın otomatik tam bölüm silme/abort davranışını kaldırır; sürücü/arayüz cleanup
ve diğer erken init hata yolları ayrı işlerdir.

Normal kart açılışında hazırlığın IP ve görevlerden önce geldiğini ve TC'nin UDP
10025'i dinlediğini görmek için (reset mevcut RAM arşivini siler):

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_startup_check.py --port COM7 --reset --network
```

`--network`, önceki `--resources`/`--adcs`/`--services` kontrollerini de içerir.
Wi-Fi timeout kontrolü de stack hazırlığını ve IP olmadan TC dinlemesini ister.
Gerçek stack-init/NVS hatası karta enjekte edilmez; kapalı kontrol yolu C host
testinde, normal/timeout başlangıçları ise kart smoke testinde ayrı doğrulanır.

2026-10-13 bu koruma eklendikten sonraki kontrol: sekiz C ve 25 Python testi,
normal/test derlemeleri ve fiziksel timeout testi geçti. IP yokken stack hazırlığı,
TC dinlemesi ve en az 9 saniyeye yayılan üç NOMINAL döngüsü gözlendi. Normal firmware
geri yüklendi; `--network` açılış kontrolü, IP `192.168.1.5` ve network/log
`connected`/`started` doğrulandı. Ardından YAMCS'ten iki indeks 0 okuması aynı 20 bayt
kaydı verdi (CRC16 `0x75D6`, TM sıra 0 → 1). SDK config hash'i değişmedi. Bu test
eski RAM kayıtlarını sildi; geri alınamaz. NVS/sürücü hata yönetimi ve mevcut compiler
uyarıları değiştirilmedi; uzun süreli offline/recovery doğrulaması değildir.

## Ağ altyapısı başlatma hatasında çekirdeğin devamı

`STARTUP_InitNetworkStack()` yalnız `esp_netif_init()` başarılı döndükten sonra
soket hazırlığını yayınlar. SDK hata kodu döndürürse hata adı loglanır, aynı kod
çağırana döner ve Wi-Fi event loop/sürücü/arayüz başlatılması atlanır. Hazır bilgisi
kapalı kalır; TC soket açmadan sonlanır, TM/olay gönderimi `false` döner ve çekirdek
görev oluşturma devam eder. Başarıdaki mevcut hazır mesajı ve Wi-Fi timeout davranışı
korunur. Güncel ortak Wi-Fi düzeltmesinde hazır bilgisinin yayınlanması başarısızsa
ESP_FAIL dönülür; önceki fatal/abort politikası kaldırılmıştır.

Bu yardımcı yalnız boot'ta bir kez çağrılmak içindir; otomatik tekrar deneme veya
`esp_netif_deinit()` eklenmedi. Kurulu SDK, lwIP için tam deinit desteklemiyor;
`esp_netif_init()` hata döndürmeden önce bazı SDK kaynaklarını oluşturmuş olabilir.
Bu kaynaklar tam geri alınmış sayılmaz. SDK iç assert/abort, gerçek heap yetersizliği,
kısmi kaynakların uzun süreli etkileri ve sonraki event-loop/Wi-Fi sürücü/arayüz
hataları bu adımda çözülmüş/doğrulanmış değildir.

`esp32dev_netif_failure` açık test profili aynı SDK config/partition/normal flags'i
kullanır. `main/netif_fault_injection.c`, gerçek `esp_netif_init()` çağırmadan linker
wrap ile ESP_FAIL döndürür. Retry, desteklenmeyen deinit, hazır bilgisini açma veya
sonraki event-loop init denemesi gerçek SDK/soket erişimine geçmeden test trap'i olur.
Normal imajda wrapper yoktur ve `esp32dev` varsayılan kalır. NVS bu profilde normal
başlatılır; gerçek kalıcı veri/heap/SDK bozulması enjekte edilmez. Bu sıfır flash/NVS
yazısı iddiası değildir. Yükleme/reset mevcut RAM arşivini siler; geri alınamaz.

Seri monitor kapalıyken derleme → hata profili → kart gözlemi → normal firmware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_netif_failure.ps1 -Port COM7
```

Monitor profil/hata/atlama mesajlarını, ağ hazırlığının açılmamasını, TC'nin atlanmasını,
çekirdek görevleri ve en az 9 saniyeye yayılan üç NOMINAL döngüsünü ister. IP veya
ağ hazırlığı, yanlış test profili, trap/reboot/panic/watchdog/queue hatası başarısızlıktır.
Komut/WIPE/ACK gönderilmez. Bu, fiziksel SDK arızası değil, hata kodunu alan gerçek
başlangıç akışının kısa entegrasyon testidir.

Normal imaj önce hazırlanır; test yüklemesi başlarsa hata halinde de `finally` içinde
geri yüklenip `--network` açılış kontrolü çalıştırılır. Çıkış kodu 0 ve `RESTORE PASS`
birlikte beklenir. Zorla sonlandırma/kablo çıkmasında geri dönüş garanti edilmez;
gerekirse normal firmware'i elle geri yükleyin:

```powershell
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev -t upload --upload-port COM7
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_startup_check.py --port COM7 --reset --network
```

Normal monitor yeni ağ hata profili mesajını da reddeder. Loglar
`.pio/host-tests/netif_*` altındadır. Ayrıştırıcının on offline kontrolü:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tests/host/netif_failure/test_monitor.py
```

2026-10-13 kontrolü geçti: dokuz C ve 49 Python kontrolü, normal/test derlemeleri,
taklit ESP_FAIL kart gözlemi ve normal firmware geri dönüşü. Hatalı başlangıçta
hazır kontrolü kapalı, TC atlanmış ve log mirror kapalı kaldı; en az 9 saniyeye
yayılan üç NOMINAL döngüsünde reboot/panic/watchdog/queue hatası görülmedi. Normal
açılışta IP `192.168.1.5`, network/log `connected`/`started` doğrulandı. YAMCS'ten
indeks 0 iki kez aynı 20 bayt kaydı verdi (CRC16 `0xFAC8`, TM sıra 0 → 1).
SDK config hash'i değişmedi. ELF kontrolünde normal imajda hata wrapper'ları yok,
test imajında init/ready/event-loop guard'ları bağlıydı. Test SDK'nin gerçek kısmi
başlangıç kaynaklarını veya fiziksel bellek yetersizliğini taklit etmez; eski RAM
arşiv kayıtları yükleme/resetle silindi ve geri alınamaz. Kartta normal firmware bırakıldı.

## Wi-Fi olay altyapısı hatasında çekirdeğin devamı

`STARTUP_CreateWifiEventLoop()` NVS ve ağ altyapısı hazırlandıktan sonra, Wi-Fi
arayüzü/sürücüsü oluşturulmadan önce tek `esp_event_loop_create_default()` çağrısı
yapar. Hata kodunda hata adı loglanır ve aynı kod `wifi_init_sta()` üzerinden
çağırana döner. Log mirror/Wi-Fi başlangıcı atlanır, temel görevlerin oluşturulması
devam eder. Başarıda yeni hazır mesajı bir kez basılır. Sonraki Wi-Fi sürücü/arayüz
hataları bu değişiklikte ele alınmaz.

Bu hata ağ altyapısını başlatma hatası değildir: TCP/IP stack zaten hazırdır.
Soket hazırlığı kapatılmaz; TC alıcısı IP olmadan UDP 10025'i dinleyebilir. Wi-Fi
bağlantısı olmadan dinlemek yer istasyonundan komut alınabildiği anlamına gelmez.
Güncel TM/olay göndericisi ayrıca canlı IP hazır bilgisini ister; IP yoksa soket açmaz.
TC alıcısı ise hazır TCP/IP üzerinde IP olmadan dinleyebilir.

ESP_ERR_INVALID_STATE mevcut default loop anlamına gelir; yardımcı bunu başarı
saymaz, başka bileşenin loop'unu sahiplenmez, silmez veya tekrar oluşturmaz. Delete
ve otomatik retry eklenmedi. Normal başarıdan sonraki hata yollarında bizim
oluşturduğumuz kaynakların cleanup politikası artık ortak Wi-Fi servisinde uygulanır.
SDK iç assert/abort ve
gerçek bellek yetersizliği bu adımın test kapsamı değildir.

`esp32dev_event_loop_failure` açık profili aynı SDK config/partition/normal flags'i
kullanır; gerçek NVS ve TCP/IP başlatılır. `main/event_loop_fault_injection.c`,
gerçek event-loop create çağrısını yapmadan ESP_ERR_NO_MEM döndürür. Tekrar create,
delete, Wi-Fi arayüzü veya sürücüsüne devam denemesi SDK'ye geçmeden test trap'i olur.
Bu gerçek heap yetersizliği veya kalıcı veri bozulması değildir; normal NVS/SDK ve
yüklemeler flash yazabilir. Default `esp32dev` içinde wrapper'lar yoktur.

Seri monitor kapalıyken derleme → hata profili → kart gözlemi → normal firmware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_event_loop_failure.ps1 -Port COM7
```

Monitor kaynak ve stack hazırlığından sonra doğru profil/hata/atlama mesajlarını,
TC dinlemesini, log mirror'ın atlanmasını, IP olmamasını, temel görevleri ve en az
9 saniyeye yayılan üç NOMINAL döngüsünü ister. Reboot/panic/watchdog/queue full,
trap, yanlış profil veya TC atlanması başarısızlıktır. Komut/WIPE/ACK gönderilmez.
Bu yalnız dönen hata kodunu alan gerçek başlangıç akışının kısa entegrasyon testidir.

Normal imaj önce hazırlanır; test yüklemesi başlarsa hata halinde de `finally`
normal imajı geri yükler ve `--event-loop` açılış kontrolünü çalıştırır. Çıkış kodu
0 ve `RESTORE PASS` birlikte gerekir. Zorla sonlandırma/kablo çıkmasında geri dönüş
garanti edilmez. Gerekirse normal firmware'i elle geri yükleyin:

```powershell
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev -t upload --upload-port COM7
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_startup_check.py --port COM7 --reset --event-loop
```

`--event-loop` önceki network/resources/adcs/services kontrollerini içerir ve yeni
hazır mesajının stack'ten sonra, IP ve görevlerden önce bir kez gelmesini ister.
Normal monitor olay hata profilini de reddeder. Tüm yükleme/resetler mevcut RAM
kayıtlarını siler; geri alınamaz. Loglar `.pio/host-tests/event_*` altındadır.
Ayrıştırıcının on iki offline kontrolü:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tests/host/event_loop_failure/test_monitor.py
```

2026-10-13 kontrolü geçti: dokuz C ve 61 Python kontrolü, normal/test derlemeleri,
taklit event-loop NO_MEM kart gözlemi ve normal firmware geri dönüşü. Hatalı event
oluşturmada gerçek TCP/IP stack hazır kaldı ve TC IP olmadan dinledi; Wi-Fi/log mirror
atlanmışken üç NOMINAL döngüsü görüldü, kısa gözlemde reboot/panic/watchdog/queue
hatası yoktu. Normal açılışta event hazır mesajı bir kez doğru sırada, IP
`192.168.1.5` ve network/log `connected`/`started` doğrulandı. YAMCS'ten iki indeks 0
okuması aynı 20 bayt kaydı verdi (CRC16 `0x816C`, TM sıra 0 → 1). SDK config hash'i
değişmedi; ELF'te create/interface/driver guard'ları yalnız test imajında vardı.
Gerçek heap arızası veya kalıcı veri bozulması enjekte edilmedi; eski RAM kayıtları
yükleme/resetle silindi ve geri alınamaz. Kartta normal firmware bırakıldı.

## NVS hatasında ayarları silmeden çekirdeğin devamı

NVS, ESP32'nin kalıcı ayar alanıdır; mevcut partition tablosunda `nvs` bölümü
`0x9000` adresinde 24 KiB'dir. Otomatik `nvs_flash_erase()` yalnız bu bölümün tümünü
siler, firmware bölümlerini değil. Wi-Fi ayarları/radyo kalibrasyonu burada tutulur;
uydu arşivi ise hâlâ RAM'dedir ve resetle kaybolur.

`main/startup_nvs.c/.h` tek `nvs_flash_init()` çağrısının sonucunu aynen döndürür;
başarısızlıkta hata adı ve Wi-Fi'nin atlandığı loglanır. Otomatik tam bölüm silme,
retry veya bu hata için uygulama abort'u yoktur. `WIFI_Start()` başarısız sonucu
network stack/sürücü/arayüz başlatmadan döndürür; soket hazırlığı kapalı kalır,
log mirror atlanır ve çekirdek görevlerin oluşturulmasına devam edilir. TC alıcısı
soket açmadan sonlanır, TM/olay göndericisi `false` döner. NVS'nin onarımı/formatı
ve atlanan haberleşmenin otomatik tekrar başlatılması bu adımda yoktur.

Bu, sıfır flash/NVS yazısı veya SDK iç veri recovery'sinin kapatılması iddiası
değildir. Normal `nvs_flash_init()` SDK iç sayfa/veri recovery'si yapabilir; normal
Wi-Fi/PHY de kalıcı ayar yazabilir. Gerçek flash/heap arızası enjeksiyonu yapılmaz.
NVS'den dönen NO_MEM güvenle iletilse de gerçek bellek yetersizliği daha sonra
zorunlu görev tahsisini başarısız yapabilir; mevcut fatal kaynak politikası korunur.

`esp32dev_nvs_failure` açık test profili aynı SDK config/partition/normal flags'i
kullanır. `main/nvs_fault_injection.c`, linker wrap ile gerçek init'i çağırmadan
`ESP_ERR_NVS_NO_FREE_PAGES` döndürür. İkinci init veya açık `nvs_flash_erase()`
denemesi gerçek flash silmeye ulaşmadan `TEST ERROR` + abort ile test hatası olur.
Normal `esp32dev` içinde bu wrapper'lar yoktur ve varsayılan profil değişmedi.
Bu profil gerçek NVS'yi bilerek silmez/bozmaz; yükleme/reset yine RAM arşivini siler.

Seri monitor kapalıyken derleme → hata profili → gözlem → normal firmware geri dönüşü:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_nvs_failure.ps1 -Port COM7
```

`tools/nvs_failure_check.py`, hata/no-erase mesajlarını, network stack'in açılmamasını,
IP olmamasını, TC'nin atlanmasını, çekirdek görevleri ve en az 9 saniyeye yayılan
üç NOMINAL döngüsünü ister. Erase/retry trap, yanlış profil veya ağ hazırlığı,
reboot/panic/watchdog/queue hatası başarısızlıktır. Komut, WIPE veya ACK gönderilmez.
Bu kısa gözlem gerçek NVS bozulması/SDK kaynak tüketimi/uzun süreli recovery testi değildir.

Normal imaj testten önce hazırlanır. Test yüklemesi başlarsa hata halinde de
`finally` normal `esp32dev` yükleyip `--network` açılış kontrolünü çalıştırır. Betik
çıkış kodu 0 ve `RESTORE PASS` birlikte beklenir. Zorla sonlandırma/kablo çıkması
halinde geri dönüş garanti edilmez; normal firmware'i elle geri yükleyin:

```powershell
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev -t upload --upload-port COM7
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_startup_check.py --port COM7 --reset --network
```

Normal açılış kontrolü NVS hata profili mesajını da reddeder. Loglar
`.pio/host-tests/nvs_*` altındadır. Ayrıştırıcının 14 offline kontrolü:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tests/host/nvs_failure/test_monitor.py
```

2026-10-13 kontrolü geçti: dokuz C ve 39 Python kontrolü, normal/test derlemeleri,
taklit NO_FREE_PAGES kart gözlemi ve normal firmware geri dönüşü. Taklit hatada ağ
hazırlığı açılmadı, IP alınmadı ve TC atlandı; üç NOMINAL döngüsü gözlendi. Normal
başlangıçta IP `192.168.1.5`, network/log `connected`/`started` doğrulandı. Son iki
YAMCS indeks 0 okuması aynı 20 bayt kaydı verdi (CRC16 `0x96C1`, TM sıra 0 → 1).
SDK config hash'i değişmedi. ELF sembol kontrolünde wrapper yalnız test imajında
bulundu, normal firmware'de yoktu. Gerçek NVS bilerek silinmedi/bozulmadı; eski RAM
kayıtları yükleme/resetle kayboldu ve geri alınamaz. Diğer NVS hata kodları yalnız
host'ta test edildi; gerçek flash/heap arızası veya sürücü cleanup doğrulanmadı.

## Wi-Fi bağlantı timeout'u ve çekirdeğin devamı

Başlangıç artık `Core tasks created`, `Network at startup` ve `log mirror`
durumlarını ayrı bildirir; "bütün sistemler çalışıyor" mesajı kullanılmaz. Ağ alanı
başlangıç sonucunun anlık görüntüsüdür, devamlı link/delivery garantisi değildir.
Bağlantı timeout'unda log mirror atlanır ama çekirdek görevlerin oluşturulması devam
eder. Bu timeout adımı tek başına SDK/NVS/sürücü hata yollarını değiştirmedi;
sonraki NVS politika değişikliği yukarıda ayrı açıklanmıştır.

`esp32dev` varsayılan profildir. `esp32dev_wifi_timeout` yalnız açıkça seçilen bir
kart testi profilidir; aynı `sdkconfig.esp32dev`/partition/flags kullanılır ve ek
`FSW_TEST_WIFI_TIMEOUT=1` tanımıyla RAM'de rastgele geçici SSID/test parolası seçilir.
Gerçek Wi-Fi bilgileri ve router ayarları değiştirilmez; test bağlantı ayarları NVS'ye
kaydedilmez. Bu, tüm firmware'in sıfır flash/NVS yazısı yaptığı iddiası değildir:
yüklemeler ve mevcut NVS/PHY başlangıç davranışları ayrıca vardır.

Seri monitor kapalıyken derleme → geçici firmware → fiziksel test → normal firmware
ve bağlı açılış kontrolü akışını çalıştırın:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_wifi_timeout.ps1 -Port COM7
```

Bu işlem birden çok yükleme/reset yapar; eski RAM arşivi kaybolur ve geri alınamaz.
`tools/wifi_timeout_check.py` gerçek yaklaşık 15 saniyelik timeout mesajını, IP
alınmamasını, stack hazırlığını, TC dinlemesini, ağ/log durumunu, çekirdek görev
mesajlarını ve en az 9 saniyeye yayılan
üç NOMINAL döngüsünü ister. Reboot/panic/watchdog/telemetri queue full veya eksik kanıt
başarısızlıktır. Gözlem kısa bir smoke testtir; uzun süreli offline davranış, archive
kapasitesi veya tam watchdog recovery doğrulanmış sayılmaz. Betik komut/ACK/WIPE göndermez.

Normal imaj önce hazırlanır. Test yüklemesi başlarsa, hata dahil `finally` içinde
`esp32dev` geri yüklenip IP/servis/kaynak/NOMINAL açılışı kontrol edilir. Çıkış kodu
0 ve `RESTORE PASS` birlikte beklenir; sadece restore mesajı offline testin geçtiği
anlamına gelmez. Zorla süreç sonlandırma veya kablo çıkarma halinde dönüş garanti
edilemez. Bu durumda karta normal firmware'i açıkça geri yükleyin:

```powershell
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev -t upload --upload-port COM7
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_startup_check.py --port COM7 --reset --resources
```

Normal açılış kontrolü test profili mesajı görürse reddeder. Çıktılar
`.pio/host-tests/wifi_*` loglarındadır. Monitor'ün 13 offline Python kontrolü:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tests/host/wifi_timeout/test_monitor.py
```

2026-10-13 kart testi geçti: bağlantı timeout'u/IP yokluğu, çekirdek görevler ve üç
NOMINAL döngüsü gözlendi; normal firmware geri yüklenip IP `192.168.1.5`, network
`connected` ve log mirror `started` doğrulandı. SDK config hash'i değişmedi. Son iki
YAMCS okuması indeks 0 için aynı 20 bayt kaydı verdi (CRC16 `0x60F8`, TM sıra 0 → 1).
Bu değerler o açılışa özgüdür. Yeni mesajlar veya test profili NVS/sürücü init'lerinin
assert/abort yollarını kaldırmaz. Uygulamanın arayüz/sürücü/ayar başlangıç hataları
artık ortak Wi-Fi servisinin kontrollü hata/cleanup politikasıyla ele alınır.

## ESP32 üzerinde UDP kontrolü

`tools/tc_udp_read_check.py` Python 3 ve `pyserial` kullanır. PlatformIO'nun Python
ortamında `pyserial` zaten bulunur. Seri monitor kapalı olmalıdır.

Çalışmakta olan kartın IP'sini vererek mevcut arşivden okuma yapın:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' tools/tc_udp_read_check.py --port COM7 --ip 192.168.1.5
```

Temiz bir açılış gözlemi için `--reset` seçeneği kartı yeniden başlatır ve IP'yi
açılış logundan alır. Yeniden başlatma mevcut RAM arşivini temizler:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' tools/tc_udp_read_check.py --port COM7 --reset
```

Kart kontrolü normal kayıt okumasını, eksik komutu, boş iç paketi ve yüksek kayıt
indekslerini denetler. `WIPE_ALL` komutu göndermez; eksik komut denemesi FDIR'e bir
uyarı kaydeder. İlk kayıt henüz oluşmadıysa veya seri port/IP yanlışsa kontrol durur.

## Gerçek arşiv telemetrisi alma

`tools/archive_downlink_check.py`, bilgisayarda UDP 10015'i dinler ve karttan aynı
kaydı iki kez ister. Gelen CCSDS boyunu, arşiv gövdesini, CRC16'yı ve ikinci okumada
kaydın aynı kaldığını doğrular. Seri logdaki "gönderildi" mesajını başarı saymaz.
Seri monitor ve bu UDP portunu kullanan YAMCS alıcısı kapalı olmalıdır; betik başka
bir programı kapatmaz. Firmware'deki `YAMCS_DEST_IP` bu bilgisayarın IP'si olmalıdır.

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/archive_downlink_check.py --port COM7 --ip 192.168.1.5 --index 0
```

IP'yi açılıştan bulmak için `--ip ...` yerine `--reset` kullanılabilir. Bu seçenek
mevcut RAM arşivini temizler; betik `WIPE_ALL` veya yer istasyonu ACK'i göndermez.
Paket biçimi ve kalan sınırlar [arşiv downlink belgesinde](../../docs/archive_downlink.md)
açıklanır. YAMCS GUI/XTCE tanımları bu adımda değiştirilmez.

Alıcı ayrıştırıcısının beş masaüstü kontrolünü ayrıca çalıştırın (ağ/seri erişimi yok):

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tests/host/archive_downlink/test_decoder.py
```

### Geçici firewall izniyle kontrol

`tools/test_archive_udp_with_firewall.ps1` yönetici olarak çalıştırıldığında yalnızca
ESP32 `192.168.1.5` → PC `192.168.1.3`, UDP 10015, Wi-Fi/Public için benzersiz
isimli bir izin kuralı açar. Kartı resetlemeden mevcut kaydı test eder ve `finally`
içinde kendi kuralını kaldırıp kaldırıldığını doğrular. IP/arayüz değerleri bu
bilgisayar için sabittir; başka ağda çalıştırmadan önce doğrulanmalıdır.

Çıktı `.pio/host-tests/archive_udp_firewall_check.log` altında tutulur. Varsayılan
çalıştırmada mevcut engelleme kuralları yalnızca okunur; devre dışı bırakılmaz.
PlatformIO'nun altta kullandığı `C:\.platformio\python3\python.exe` için Public
profilinde etkin UDP engelleme kuralı bulunduğundan yeni izin tek başına yetmemiştir.

Mevcut UDP engelini geçici değiştirme onayı verildikten sonra, **Yönetici PowerShell**
üzerinden bu makineye özel kontrolü çalıştırın:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_archive_udp_with_firewall.ps1 -SuspendPythonUdpBlock
```

Bu seçenek yalnızca önceden doğrulanmış tek UDP engelleme kuralını seçer; kuralın
program/profil/protokol/adres/port özellikleri beklenenden farklıysa durur. Eski
`Enabled` değeri testten önce kaydedilir, `finally` içinde geri yüklenip doğrulanır.
TCP kuralına yazılmaz; test sonunda durumunun değişmediği de kontrol edilir.

2026-10-12 tarihli gerçek kart kontrolü başarılıdır: indeks 0, HK türü `0x0001`,
20 bayt veri iki kez alınmış; her ikisinde CRC16 `0x89C2` ve aynı ham veri doğrulanmış,
CCSDS sıra numarası 5 → 6 ilerlemiştir. UDP engeli yaklaşık 1,8 saniye sonra geri
açılmış, geçici izin kaldırılmış, TCP engeli etkin kalmıştır. Kart resetlenmemiş ve
`WIPE_ALL`/ACK gönderilmemiştir. Bu CRC/veri/sıra değerleri o kayda özgüdür; sonraki
açılışlarda farklı olabilir. Bu doğrulama Python alıcısıyla yapılmıştır; YAMCS GUI
alan ayrıştırması veya YAMCS üzerinden komut gönderimi henüz doğrulanmış değildir.

Betik normal biçimde tamamlanmalıdır; zorla sonlandırılırsa `TEMP RULE` adıyla
yalnızca testin izin kuralının kalıp kalmadığı ve `ORIGINAL UDP BLOCK` kaydındaki
kuralın eski `Enabled` değerine dönüp dönmediği kontrol edilmelidir. Temizlik hatası
durumunda çıkış kodu 2'dir ve log geri yüklenecek/kaldırılacak tam kuralı gösterir.

## YAMCS → ESP32 → YAMCS komut kontrolü

YAMCS kataloğunda `/myproject/ARCHIVE_GET_BY_INDEX` komutu ve `udp-out` üzerinde
CCSDS başlık işlemcisi bulunmalıdır. YAMCS çalışırken aşağıdaki kontrol yalnız
komut baytlarını üretir; UDP komutu göndermez, seri port açmaz:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/yamcs_archive_command_check.py
```

Gerçek karttan iki kayıt okuması için ayrıca `--send` gerekir:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/yamcs_archive_command_check.py --send --port COM7 --board-ip 192.168.1.5 --index 0
```

YAMCS komut geçmişindeki gönderim ve postprocess edilmiş CCSDS başlığı, kartın seri
logundaki iç CRC/okuma sonucu, YAMCS'e dönen kaydın boyu/payload CRC16'sı ve iki
okumanın aynı veriyi vermesi kontrol edilir. Başarı yalnız "Sent OK" durumuna dayanmaz.
Betik reset, WIPE, mod değiştirme veya ground ACK göndermez; Java'nın aldığı ham TM
paketini yerel HTTP ile okuduğundan Python UDP firewall engelini değiştirmez.

Komut korumasının yedi masaüstü kontrolü ağ/seri erişimi olmadan çalışır:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tests/host/archive_downlink/test_yamcs_command.py
```

Gerçek YAMCS/kart doğrulama sonucu ve GUI alan ayrıştırmasının kalan kapsamı
[arşiv downlink belgesinde](../../docs/archive_downlink.md#yamcs-üzerinden-kayıt-okuma)
açıklanır. Kalıcı C kontrol grubu sayısı 17'dir; burada kullanılan iki Python kontrol dosyası ayrı
çalıştırılır.

## Ortak Wi-Fi / UDP toparlanma doğrulaması

- `wifi_service/check.c`: gerçek `main/wifi_service.c`, 25 ayrı süreç senaryosu;
  NVS/stack/event, eksik config, mevcut arayüz, netif/attach/default-handler/group,
  driver/storage/uygulama-handler/mode/config/start/task hataları, cleanup hataları,
  timeout, IP alınması, geç bağlantı, log retry, kopma, STA stop/start ve IP kaybı.
  Başka sahibin arayüzü silinmez; primary error korunur. Cleanup belirsizse kaynak
  serbest bırakılmaz. Gerçek SDK heap arızası/işletim sistemi zamanlaması taklit edilmez.
- `link_retry/check.c`: 1..30 saniye backoff, 40 başarısız denemeden sonra hâlâ retry,
  20 saniye pending timeout, bağlı/kopuk geçişi ve uint32 millisecond wrap.
- `yamcs_log/check.c`: queue/task allocation failure retry, tek persistent sender,
  disconnected durumda log düşürme, queue-full newest drop, SDK satırı/partial satır filtresi.
- `wifi_service/test_monitor.py`: fiziksel test kanıt ayrıştırıcısının 18 offline kontrolü;
  gerçek güç politikasıyla NOMINAL → SAFE/CRITICAL geçişi, geçersiz mod ve heartbeat boşluğu dahil.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\check_wifi_credentials.ps1
& 'C:\.platformio\penv\Scripts\python.exe' -B -m unittest discover -s tests/host/wifi_service -p test_monitor.py
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_wifi_system.ps1 -Port COM7 -Duration 600
```

Son komut gerçek kartı resetler/yükler, eski RAM arşivini siler. Önce gerçek heap'i
bozmayan netif allocation-return hatası, sonra gerçek geç bağlantı ve üç
`esp_wifi_disconnect()` döngüsüyle süreli gözlem çalışır. Soak sonrası YAMCS'ten
aynı kayıt iki kez okunur. `finally` normal firmware'i geri yükler ve normal boot/IP
kontrolü yapar; tamamlanma için script exit 0 ve RESTORE PASS birlikte gereklidir.
Kablo kesilmesi veya zorla süreç sonlandırılması `finally`yi engelleyebilir.
Loglar `.pio/host-tests/wifi_system_*` altındadır.
Uygulama politikası/yerel config/sınırlamalar [Wi-Fi belgesindedir](../../docs/wifi.md).

2026-10-13: 12 C grubu, 25 Wi-Fi senaryosu ve 79 Python kontrolü geçti. Kartta
allocation-return/core testi ve düzeltilmiş 600 saniye lifecycle gözlemi geçti:
116 core döngüsü (58 NOMINAL), INITIAL_BOOT/SAFE bekleme, dört IP, üç gerçek kopma.
Minimum heap 160796 bayt, manager minimum stack headroom 1616 bayt; sürekli NOMINAL
şartı yerine 12 saniyeyi aşmayan core heartbeat ve geçerli modlar doğrulandı.
Soak sonrası YAMCS GET tekrarı CRC16 0xDB3F ile, normal firmware'e dönüş sonrası
GET tekrarı CRC16 0x3865 ile geçti; 20 bayt kayıtlar korundu. Script exit 0 ve
RESTORE PASS gözlendi; SDK config değişmedi, gerçek NVS/router/firewall/YAMCS ayarları
bozulmadı. Upload/reset eski RAM kayıtlarını kaybettirdi; geri alınamaz.
