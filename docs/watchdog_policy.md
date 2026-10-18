# Görev heartbeat denetimi ve watchdog sınırı

Güncelleme: 2026-10-17. Yazılımsal heartbeat/FDIR korumasına kullanıcı onayıyla
SDK tabanlı supervisor/idle-stall panic + otomatik reset politikası eklendi.
Task restart eklenmedi. 16 Ekim'de sınırlı owner-validated task-timeout çözümü
kod/host/normal build düzeyinde eklendi; ardından iki-episode kontrollü kart yolu
ve normal restore geçti. [Politika](watchdog_recovery.md),
[sınırlı kart kanıtı](watchdog_recovery_target.md).
Reset bütün per-boot RAM arşivini/aktif ledger'ı kaybettirir. 17 Ekim'de ayrı
NVS özeti ve kötü reset/geçmiş sonrası SAFE boot kapısı kodlandı; **henüz karta
yüklenmedi**. Sağlık penceresi + o boot'a özel `FDIR_RearmBoot` onayı yalnız
kilidi açar, sistem SAFE'te kalır. [Kalıcı recovery ve bounded yazma](recovery_policy.md).
Uçuş güvenliği veya kesin donanım tepki
süresi garantisi değildir. Sonraki kontrollü kart paketinin kapsamı ve sonuçları
[kart testi belgesindedir](watchdog_target_tests.md).

## Başlangıç ve veri koruması

Core'dan önce recovery/shared NVS tek girişimi ve unfinished-boot commit'i vardır.
Init/read/write hatasında operating inhibit bırakılır; NVS otomatik silinmez.
Arm'dan sonra ayrı RECOVERY observer, ardından mevcut mission task'lar açılır.
Observer yedi heartbeat ID'sine eklenmedi; durursa yer onayının örnek tazeliği
geçersiz olur. Yeni task'ın gerçek stack/flash I/O etkisi henüz ölçülmedi.

Önceki kart testlerindeki reset→otomatik mission ilerleme oracle'ı yeni ciddi
geçmiş kapısıyla uyumsuzdur. `test_watchdog_system.ps1` recovery kaynakları varken
artık build/flash öncesi reddeder. Tarihsel CPU0/CPU1/global/owner-timeout sonuçları
kendi imajları içindir; yeni politika için persistent-state-aware target runner
ve ayrı arşiv/NVS/reset onaylı fiziksel koşu gerekir. Koruma bypass edilmez.

Core sıra artık `TIME → MODE → FDIR → WATCHDOG_Init → sender → Archive/HK/payload/
ADCS/SIM` şeklindedir. Watchdog init bir required mutex oluşturur ve SDK recovery
port'unu hazırlar; iki adımdan biri başarısızsa
core helper sonraki servis/resource/network/task aşamasından önce log + abort yapar.
Tekrar init reddedilir; timestamp veya fault latch'i sıfırlanmaz. SDK config
hatasından sonra da arm yapılamaz; kısmi init'i yeniden deneyerek geçiş gizlenmez.

Watchdog init henüz uygulama heartbeat sürelerini saydırmaz; SDK idle koruması
bu aşamada 15 s/panic politikasına geçer. `app_main()`, opsiyonel network/log başlangıcı
tamamlandıktan sonra **ilk task-create çağrısından hemen önce** `WATCHDOG_Arm()`
çağırır. Ortak grace epoch bu anda, heartbeat mutex'i altında bütün yedi kayıt için
alınır. Böylece uzun network startup'ı görevlerin çalışma süresinden sayılmaz.
Arm başarısızsa hiçbir uygulama görevi oluşturulmaz; mevcut log + abort politikası
izlenir. İkinci arm reddedilir; geç çağrıyla timeout gizlenemez.

Eski public timestamp array'i kaldırıldı. Timestamp ve timeout latch'leri private,
okuma/yazma aynı mutex altındadır. Arm/pet/check bu mutex'i 10 ms istekle alır;
kilit içinde kısa state/tick işlemleri ve recovery'nin quiet zero-wait MODE
revision/resolve işlemleri yapılır. FDIR report/warning mutex'i, printf veya
başka I/O çağrısı heartbeat mutex'i bırakılmadan yapılmaz. MODE meşgulse recovery
beklemez ve clear yapmaz. Task-only API'lerdir;
pet bütün diğer service mutex'leri bırakıldıktan sonra çağrılır.

16 Ekim eklemesi: `WATCHDOG_GetTimingSnapshot`, aynı mutex altında cumulative pet
sayısı/ardışık pet minimum-maksimum aralığı ve son pet yaşını salt-okunur kopyalar.
İlk gerçek pet baseline'dır; startup beklemesi interval sayılmaz. Read failure
çıktıyı değiştirmez, fault veya SDK feed üretmez; hiçbir latch/epoch temizlenmez.
10 ms tick çözünürlüklü liveness interval'i CPU/WCET/deadline kanıtı değildir.
[Kaynak ölçüm kapsamı ve kart durumu](resource_target.md).

`watchdog_pet()` unsigned enum sınır kontrolü yapar; negatif/COUNT/büyük kimlikler
diziye erişmeden reddedilir. Arm öncesindeki pet/check süre üretmez. Monitor task
başlarken kayıtları yeniden başlatmaz; task'ların daha erken heartbeat'lerini korur.

## İzlenen görevler

| ID | Görev | Normal loop/bekleme | FDIR timeout kodu |
| --- | --- | --- | --- |
| 0 | TM_GEN | 1000 ms; legacy arşiv üretimi 5000 ms | 0xC200 |
| 1 | CMD_PROC | Kuyruk boşken en çok 100 ms bekleme | 0xC201 |
| 2 | EPS_MON | 1000 ms | 0xC202 |
| 3 | DATA_LOG | Kuyrukta en çok 100 ms bekleme | 0xC203 |
| 4 | ADCS_MGMT | 100 ms | 0xC204 |
| 5 | EPS_MGMT | 100 ms | 0xC205 |
| 6 | FDIR | 1000 ms nominal delay-until | 0xC206 |

Eski ID'ler 0..4 korunur; yeni iki ID sona eklendi. EPS manager ve FDIR, kendi loop
sonunda bütün kilitler bırakılmışken pet eder. Bir iterasyonun tamamlanması yalnız
**liveness** işaretidir: ölçüm/permission/servis işleminin başarılı veya fiziksel
sistemin sağlıklı olduğunun kanıtı değildir. Hata dalından dönen fakat çalışan loop
da pet edebilir; bu domain hatalarını ayrıca FDIR/servis politikası denetler.

TC_RX, bir kerelik DEPLOY, simülasyon görevleri ve supervisor'ın kendisi bu yedi-ID
listesine eklenmedi. Supervisor ayrıca SDK TWDT'ye kendisini kaydeder; aşağıdaki
katman bunu denetler. Network davranışı değişmez. One-shot deadline/unregister
ayrı iştir; bu liste bütün 12 görev izlendi demek değildir.

## Süre sınırı ve timeout kararı

Mevcut eşik **strictly `elapsed > 15000 ms`**, monitor beklemesi **5000 ms**'dir.
Tam 15 saniyede timeout yoktur. `CONFIG_FREERTOS_HZ=100` için bunlar 1500 ve 500
tick'tir; normal tarama hizasına göre ilk tespit yaklaşık 15–20 saniye aralığında
olabilir. Monitor artık başlarken de ilk check'i yapar; erken pet'leri ezmez.
Scheduler, mutex ve log gecikmesiyle daha uzun sürebilir; hard deadline
veya üst tepki süresi garantisi verilmez. `vTaskDelay()` hâlâ check işleminden sonra
yeni beş saniyelik bekleme oluşturur, bütün loop'u sabit WCET periyoduna çevirmez.

Check, current tick'i **aynı heartbeat mutex'i altındayken** alır. Böylece current
tick alındıktan sonra gelecekteki pet timestamp'inin okunup unsigned subtraction
ile sahte büyük elapsed oluşturması önlenir. Fark `TickType_t`'ye açıkça cast edilir;
tek sayaç rollover'ı doğru işlenir. Tam counter periyodundan uzun gözetimsizlik için
geçmiş garantisi yoktur; monitor'ın böyle uzun çalışamaması bu katmanın dışında kalır.

Eşik aşılınca task'ın episode latch'i ve local pending report listesi mutex altında
oluşturulur. Kilit bırakıldıktan sonra `(SRC_CDH, FAULT_WDT_TIMEOUT_BASE + ID)`
critical raporu üretilir. FDIR'ın gerçek aktif neden tablosu/merkezi mode guard'ı
BOOT/NOMINAL/DEGRADED'dan SAFE koruması üretir; mevcut CRITICAL/EMERGENCY kararını
zayıflatmaz. Her task için çözülmemiş episode başına tek timeout raporu vardır; aynı eşik her beş
saniyede yeni revision/log spam'i üretmez.

Pet check transaction'ından önce kazanırsa yeni timestamp değerlendirilir. Timeout
kararı commit edildikten sonra tek pet gelmesi geçmişteki timeout'u iptal etmez.
ACK_FAULT, tekrar init veya arm FDIR kaydını temizlemez. Sonraki 16 Ekim
politikasında token sonrası >=30 s, >=3 farklı-tick pet, <=15 s gap ve <=5 s
freshness doğrulanınca owner yalnız kendi critical timeout revision'ını çözer ve
latch'i rearm eder. Yeni timeout tekrar raporlanır; mode değiştirilmez. Ground
NOMINAL isteği başka aktif neden veya deployment engeli varken NACK alır.
Task restart yoktur; [güncel koşullar ve target sınırı](watchdog_recovery.md).

Heartbeat mutex'i runtime'da alınamazsa kayıp veriden belirli bir task'ın durduğu
sonucu üretilmez: ayrı `0xC20F` (heartbeat state unavailable) critical neden raporlanır.
Pet/check birlikte kullandığı C11 atomic flag, bu nedenin tekrarlı raporlarını sınırlar.
Bu neden de sonraki başarılı pet/check ile otomatik temizlenmez. Init/arm başarısızlığı
ise görevler başlamadan önce required startup abort yoludur; sahte task-timeout değildir.
Başarısız check SDK recovery'yi beslemez. Tek geçici hata sonraki başarılı check'te
beslemeye dönse bile FDIR nedenini silmez; devam eden başarısızlık SDK timeout'una gidebilir.

FDIR record başarısızlığı/full ledger için mevcut conservative overflow/uncertainty
guard'ı korunur. Report teslimi ve fiziksel yük kesme anlık bir interrupt interlock
değildir; supervisor/raporlayıcı çalışabilmeli ve scheduler yürümelidir.

## SDK katmanı: onaylı otomatik reset politikası

Kurulu ESP-IDF 5.5 kaynakları esas alındı. Platforma özel çağrılar
`components/watchdog/watchdog_port_esp32.c` içindedir; `watchdog.c` yalnız saf C port
arayüzünü kullanır. Yeni platformda bu port ayrıca uygulanmalıdır; STM32 port'u
yapıldı demek değildir. `cdh` private `esp_system` bağımlılığı açıkça bildirildi.

- SDK app startup, TWDT'yi app_main'den önce başlatır. `sdkconfig.esp32dev`
  dosyası değiştirilmedi: bootstrap 5 s, iki idle task açık, TWDT panic kapalıdır.
- `WATCHDOG_Init()` içindeki port, SDK'nın zaten initialized olduğunu status API'si
  ile doğrular ve **reconfigure** ile çalışma zamanı politikasını **15000 ms,
  iki idle core (mask 3), trigger_panic=true** yapar. init/deinit veya diğer SDK
  abonelerini silme yoktur. SDK hazır değilse ya da reconfigure başarısızsa required
  init başarısızdır; core sonraki aşamalara geçmeden mevcut abort yoluna gider.
- TWDT timer/timeout/panic politikası **paylaşımlıdır**: idle-core kontrolü de 5 s
  yerine 15 s/panic olur. Bu ayrı, yalnız monitor'a ait bir 15 s timer değildir.
  SDK tüm aboneler o turda reset verdiğinde timer'ı besler; per-task son başarıdan
  itibaren kesin 15 s'lik tepki garantisi çıkarılamaz.
- `WDT_MON` kendi gövdesinde, ilk log/check/beklemeden önce `add(NULL)` ile kendini
  kaydeder. İlk check hemen, sonraki check'ler tamamlanma + 5 s bekleme ile yapılır.
  Yalnız `WATCHDOG_Check()` true döndükten, FDIR raporları/logları tamamlandıktan ve
  bütün uygulama mutex'leri bırakıldıktan sonra `reset()` çağrılır. Başka görev,
  timer veya ISR onun yerine beslemez. False check, blocked check/report/log veya
  supervisor starvation besleme üretmez; SDK timer katmanı bağımsız tespit sağlar.
- Subscribe/feed SDK API'si hata döndürürse eksik korumayla sessiz devam yoktur:
  port hata logu + required abort. Bu, 15 s beklemeden **doğrudan panic/reset** yolu
  olabilir; uydurma mission-task timeout kaydı üretilmez. Geçici check lock hatası
  ise FDIR + beslememe yoludur, doğrudan abort değildir.
- Tek mission-task timeout'u FDIR critical kaydı ve SAFE inhibit üretir; check
  başarıyla tamamlanırsa monitor SDK'yı beslemeye devam eder. Bu olay tek başına
  reset isteği değildir. Fakat aynı arıza idle/supervisor starvation da üretirse
  bağımsız SDK koruması resetleyebilir.
- Interrupt WDT 300 ms/CPU1 kontrolü değiştirilmedi. Genel panic davranışı
  print + reboot, delay 0 kalır. Custom ISR hook, ISR'den FDIR/mutex/log çağrısı,
  `esp_restart()`, SDK deinit veya ayrı besleme görevi eklenmedi.

WDT_MON priority 6, EPS manager priority 24 kalır; supervisor en yüksek öncelikli
değildir. WDT_MON stack'i kart-test adımında 2048'den 4096 bayta çıkarıldı;
ek 2048 bayt dinamik stack tahsisi PIO statik RAM sayısında görünmez.
SDK interrupt/timer/panic zincirinin çalışması gerekir. CPU0 ve CPU1'de ayrı ayrı
idle starvation (supervisor beslemeye devam ederken) ve interrupt masking için gerçek TWDT/
IWDT panic/reset nedenleri [ayrı kart paketinde](watchdog_starvation_target.md)
doğrulandı. IRQ-açık iki priority-24 worker ile supervisor/idle starvation'ın
sınırlı çift-çekirdek yolu da [global pakette](watchdog_global_target.md) doğrulandı.
SDK tur bayrakları bağımsız last-feed timestamp'i değildir; failed supervisor
ve iki CPU'nun çalışan worker snapshot'ları birlikte değerlendirilir.
Genel deadlock/cache-off, farklı IRQ seviyeleri, debug/JTAG etkileri ve kesin
en-kötü target latency doğrulanmadı; bu katman bütün
deadlock'ları veya kesin reboot süresini kanıtlamaz.

Reset **RAM arşivini, aktif neden tablosunu, deployment-complete işaretini ve
heartbeat latch'lerini** kaybettirir. Watchdog/ISR/panic handler flash'a yazmaz;
panic anında veri flush etme denemesi yoktur. Ancak 17 Ekim'de eklenen ayrı
[recovery servisi](recovery_policy.md) artık startup/worker bağlamında küçük CRC'li
NVS özeti tutar: kötü SDK reset nedeni, unfinished-boot sayacı ve yapışkan geçmiş
yeni boot'u SAFE kilidinde başlatabilir. Sağlık penceresi ve bu boot'a özel ayrı
yer onayı gerekir; kilidin açılması SAFE'te bırakır, NOMINAL ayrı komuttur.
Bu katman kalıcı arşiv veya aktif fault ledger'ın eksiksiz replay'i değildir.
Watchdog port'u reset nedeni tanısını verir; çalışma kilidinin sahibi ayrı recovery/
MODE katmanıdır. Reset verified repair değildir, devam eden arıza yeniden reset
üretebilir. Kalıcı politika kodlanmıştır; bütün reset/güç-kesintisi/bozuk-NVS matrisi
ve yerinde onarım kapanmış değildir. Kontrollü kart paketleri RAM kaybı ve kalıcı
geçmiş yan etkileri açıklanarak ayrı onayla çalıştırılır; test-only enjeksiyon
bastırma normal firmware'e bir repair politikası eklemez.

## Doğrulama ve maliyet

`tests/host/watchdog/check.c`, gerçek watchdog + FDIR + MODE ile **132** ayrı süreç
çalıştırır. İlk SDK kapsamı 70'ti: yedi görev × üç boundary (21), yedi rollover, yedi latched resume,
altı mode koruma, önceki 20 init/arm/invalid/startup/interleaving/lock/ledger/task-loop
yolu ve 9 yeni SDK-config failure/repeat, subscription/feed abort, unarmed monitor,
healthy monitor, transient/persistent check failure ve blocked-report yolu.
Ardından sekiz timing snapshot ve 54 liveness-recovery senaryosu eklendi;
güncel recovery kapsamı [ayrı belgededir](watchdog_recovery.md).
Port mock'ları çağrı sırasında kilit tutulmadığını, kayıt öncesinde log/check
çalışmadığını ve SAFE/task-timeout durumunda beslemenin devam ettiğini doğrular.
Bir SDK bütçesi boyunca beslememe mock senaryosu **donanım reset testi değildir**.
Test RTOS tick dönüşümü target gibi **100 Hz**'dir. FDIR raporları/kodları, mode
guard, tek rapor, early pet korunması ve heartbeat kilidi dışında I/O doğrulanır.
RTOS mutex/tick/delay/time/network sınırları deterministik taklit edilir; gerçek
çok-core scheduling, priority inheritance, kernel deadlock veya donanım fault testi değildir.
`tests/host/watchdog_port/check.c` gerçek ESP32 adapter'ında **13** süreç:
config/status/subscription/feed success/error; shared 15 s, idle mask 3 ve
panic=true parametreleri. SDK API'leri taklit edilir, gerçek SDK timer veya panic
çalıştırılmaz; init/deinit kullanımı fixture'da link edilemez.

Core startup grubu artık **31** süreç senaryosudur: watchdog init hatası,
o hatadan sonra repeat core reddi ve arm hatasında sıfır task oluşturma eklendi.
Mevcut EPS/FDIR task testleri de doğru ID ile, servis kilitleri dışında pet edildiğini
ve her tamamlanan iterasyonda heartbeat/bekleme yolunu doğrular.

İlk SDK ekleme adımı (2026-10-14): **21 C host grubu**, mevcut **79 Python kontrolü** ve normal `esp32dev`
derlemesi geçti. Statik RAM **42556/327680**, flash **799881/1740800** bayt; önceki
heartbeat imajına göre +8 statik RAM / +1652 flash baytı. SDK supervisor aboneliğinin
dinamik heap maliyeti ve target stack/high-water ölçülmedi; statik RAM raporu bu
heap maliyetini içermez. Yeni task/uygulama mutex'i eklenmedi. `eps`/
`fdir` heartbeat header bağı `cdh` private requirement olarak bildirildi; bütün
component döngü/header sahipliği incelemesi kapanmış değildir.

İlk SDK ekleme adımında SDK config **dosyasının** SHA256'sı aynıydı; runtime TWDT
policy yukarıdaki gibi değişti. Compiler uyarıları kaldı, o adımda upload/reset
yapılmadı. Sonraki kart paketinde firmware yüklenir/resetlenir; önceki RAM arşivi
geri alınamaz şekilde kaybolur. Wi-Fi/router/firewall/YAMCS/credential davranışı
değiştirilmez. Yeni imaj maliyetleri ve bounded target kanıtı ayrı kart belgesinde
kaydedilir; ilk derleme sonuçları canlı kart testi olarak sayılmaz.

Sonraki kart adımı (2026-10-14): **23 C grubu / 122 Python kontrolü** ve üç imaj
derlemesi geçti. Normal baseline 95.8 s; task-loss 65.7 s boyunca SAFE/tek C200
nedeni ve dokuz SDK-fed snapshot, reboot yok. Kontrollü supervisor self-stall'dan
15.00 s sonra gerçek SDK timeout/panic, ikinci boot'ta TASK_WDT reason 6 ve
45 s mission ilerlemesi gözlendi. WDT_MON 4096 bayt stack ile bu test yollarında
minimum 1588 bayt boşluk gösterdi; bu tüm task/fault yolları için stack/WCET
garantisi değildir. Normal statik RAM 42556, flash 799997 bayt; +116 flash baytı
boot reset-reason tanısı içindir. Test hook'ları normal ELF'te bulunmaz.
Normal restore sonucu ve üretilmiş kanıt logları [kart belgesindedir](watchdog_target_tests.md).
