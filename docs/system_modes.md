# Sistem modları: güç geçişi ve EPS yük politikası

Güncelleme: 2026-10-14. Boot/deployment önkoşulu [deployment belgesindedir](deployment_policy.md).
Bu belge CDH güç izleme görevinin sınırlı otomatik geçiş
ve EPS yük politikasını kaydeder; bütün uydu mod/FDIR/recovery politikasının tamamlandığı
anlamına gelmez. ESP32 simülasyon platformudur, flight-qualified sistem değildir.

17 Ekim'de ayrı recovery çalışma kilidi eklendi: kötü reset/ciddi geçmiş veya
recovery IO hatası operating modlarını engeller; aktif fatal EMERGENCY floor'u
korunur. `FDIR_SetMode` bu kilidi açamaz. Sağlık + ayrı boot onayı yalnız SAFE'te
kilidi kaldırabilir, ardından NOMINAL için ayrı istek gerekir. Yeni davranış
kod/host/build'de doğrulandı, henüz karta yüklenmedi. [Recovery politikası](recovery_policy.md).

## CDH güç izleme davranışı

`components/cdh/eps_control.c` içindeki görev, paylaşılan EPS telemetrisini EPS
mutex'i altında kopyalar; mutex'i bıraktıktan sonra FDIR ve mod servisini kullanır.
Mevcut voltaj eşikleri ve görev periyodu değiştirilmedi:

- Düşük güç: `V < 2.5 V`.
- Toparlanma: `V > 6.0 V` ve gözlenen mod `CRITICAL`.
- Tam `2.5 V` ve `6.0 V` dahil aralıkta bu görev otomatik geçiş istemez.
- Her döngü watchdog'u besler ve 1000 ms bekler; EPS mutex'i alınamazsa o
  döngüde voltaj değerlendirilmez ama watchdog/bekleme yolu devam eder.

Başka eşzamanlı mod kararı veya kilit hatası yokken sonuç:

| Başlangıç modu | `V < 2.5 V` | `V > 6.0 V` |
| --- | --- | --- |
| INITIAL_BOOT / SAFE / NOMINAL / DEGRADED | EPS kritik hata bildirimi, ardından CRITICAL isteği | Mevcut mod korunur |
| EMERGENCY | EPS hata bildirimi yapılır; EMERGENCY üzerine CRITICAL yazılmaz | EMERGENCY korunur |
| CRITICAL | CRITICAL korunur; bu görev tekrar aynı hata bildirimi yapmaz | Yalnız SAFE isteği; otomatik NOMINAL yok |

EPS kritik hata bildirimi, mevcut FDIR yolunda önce `SAFE` isteyebilir. Bu nedenle
monitor bildirimin ardından **yeni** bir mod anlık görüntüsü alır. Görülen
`EMERGENCY` korunur; zaten `CRITICAL` ise yeni istek yapılmaz. Diğer durumlarda
`SYSTEM_MODE_Transition(observed_mode, MODE_CRITICAL)` kullanılır. Toparlanma ise
`SYSTEM_MODE_Transition(MODE_CRITICAL, MODE_SAFE)` kullanır.

Karşılaştırma ve yazma, mevcut mod servisi mutex'inin aynı kritik bölgesindedir.
Okuma ile geçiş arasında mod farklı bir değere değişmişse istek reddedilir; güç
monitorü o yeni kararı koşulsuz `Set()` ile ezmez. Karşılaştırılan değer tekrar
aynı değere dönerse aradaki geçmişi algılayan bir sürüm sayacı yoktur (ABA/history
garantisi verilmez). Başarısız istek kilit alınamamasından da kaynaklanabilir;
mesaj iki olasılığı belirtir ve başarılı geçiş iddiası üretmez. Döngü içinde spin
veya sınırsız tekrar yoktur; sonraki normal döngü voltaj/modu yeniden değerlendirir.
Başarılı geçiş mesajı da işlem anına aittir, gelecekte modun değişmeyeceği garantisi değildir.

## EPS yük kesme ve geri açma

`components/eps/eps_logic.c` içindeki yönetici, paylaşılan PDU register'ındaki
PAYLOAD/HEATER bitlerinin runtime tek yazarıdır. Bu register simüle güç tüketimi
içindir; gerçek donanım sürücüsü/burn-wire/ısıtıcı kontrolü eklenmedi.

| Global durum / yerel durum | EPS yük davranışı |
| --- | --- |
| SAFE / CRITICAL / EMERGENCY | PAYLOAD ve HEATER bitleri kesilir, yerel EPS SAFE ve `safe_mode_active=1` olur; yüksek voltaj tek başına geri açmaz |
| INITIAL_BOOT, sağlıklı yerel NOMINAL | Önceden açık/kapalı bitler korunur; mevcut açılış defaultları değiştirilmez |
| INITIAL_BOOT, daha önce yükleri kesilmiş EPS | Otomatik geri açma yok; RECOVERY de SAFE'e döner |
| NOMINAL / DEGRADED, yerel SAFE | Yalnız `V >= 8.5 V` ise yükler açılır ve yerel RECOVERY olur |
| NOMINAL / DEGRADED, yerel RECOVERY | Sonraki döngüde `V >= 5.0 V` ise yerel NOMINAL; `V < 5.0 V` ise tekrar yük kesme |

Yerel düşük voltaj eşiği `< 5.0 V`, toparlanma eşiği `>= 8.5 V` aynıdır. Düşük
voltaj yükleri tespit edildiği döngüde keser; eski bir sonraki LOW_VOLTAGE döngüsüne
kadar bekleme kaldırıldı. LOW_VOLTAGE enum değeri korunur; normal/recovery'den
düşük voltaj saptandığında yükleri zaten kesilmiş kısa geçiş durumudur. Sonraki
döngüde SAFE olur. `low_voltage_warn` güncel `< 5.0 V` ölçümünü, `safe_mode_active`
yüklerin politika gereği kesildiğini gösterir; yüksek voltajda global SAFE sürerken
ilki temizlenebilir fakat ikincisi açık kalır. Diğer fault flag'leri temizlenmez.
COMMS ve bilinmeyen/ilgili olmayan register bitleri aynen korunur; önceden kapalı
COMMS bitini zorla açma yapılmaz.

Geri açma kontrolü ve PDU yazımı aynı global mod kilidi altında yapılır.
Yeni `SYSTEM_MODE_WithCurrent(action, context)` kısa caller-owned güncellemeyi
mevcut modla çalıştırır; mod değişimine veya yeni tahsisata yol açmaz. Kilit sırası
**EPS data → global mode**'dur. Callback bloklanmaz, log/FDIR/mod API'si çağırmaz
ve başka kilit almaz. FDIR bildirimleri (`EPS_Monitor` mevcut `< 3.3 V` yolu dahil)
ve loglar iki kilit bırakıldıktan sonra çalışır. Böylece kontrol ile PDU açma
arasına giren bir güvenli mod kararı eski bir snapshot ile aşılamaz.

Mod servisi başlatılmamışsa veya 10 ms mod kilidi isteği başarısızsa callback
çalıştırılmaz; EPS data kilidi altında yükler kapatılır ve yerel SAFE seçilir.
Düşük voltaj fault bildirimi bu permission hatası nedeniyle kaybedilmez.
EPS data kilidi alınamazsa paylaşılan register'a korumasız yazı yapılmaz; döngü
atlanır ve 100 ms bekleme yolu sürer. Bu durumda yüklerin o anda kesildiği iddiası
verilemez. Yerel mod artık private'tır; `EPS_GetMode()` aynı EPS mutex'inden
snapshot alır, NULL handle/10 ms kilit hatasında konservatif EPS SAFE döndürür.

Politika periyodiktir. Geçerli bir yük açma işlemi bittikten **sonra** global mod
SAFE/CRITICAL/EMERGENCY olursa, sonraki başarılı yönetici döngüsünde yükler kesilir.
Mod değiştirme API'si bütün yükleri eşzamanlı kapatan donanım interlock'u değildir.
Nominal 100 ms delay; scheduler gecikmesi/kilit beklemesiyle daha uzun sürebilir,
kanıtlanmış en kötü tepki süresi veya fiziksel güç güvenliği garantisi verilmez.
RECOVERY → NOMINAL yerel geçişi yalnız bir sonraki uygun döngüyü temsil eder;
fiziksel batarya kararlılığı/filtrelenmiş gerilim doğrulaması değildir.

Bu yük politikası global SAFE → NOMINAL yazmaz. Sonraki [aktif hata adımı](fdir_faults.md)
critical/fatal RAM kaydı ve operating-mode engelini ekledi. EPS manager yalnız finite
8.5 V ölçümü ve aynı revision ile kendi low-voltage critical kaydını çözer; clear
EPS mutex'i altında quiet MODE işlemiyle yapılır, global mode değiştirilmez. Başka
bir neden varken veya kayıtta belirsizlik varsa mode engeli sürer. `EPS_CMD_HEATER_CTRL` hâlâ stub'dır;
register/HAL değiştirmez, mesaj artık "çalıştırıldı" yerine "stub/no load change"
der. Power-bus reset stub'ı da uygulanmış donanım reseti sayılmaz.

## Kapsam dışında ve açık kalanlar

- EPS'nin simüle PDU bitleri yukarıdaki periyodik politikaya uyar. Mevcut ADCS
  actuator/boost ve payload üretim/START yolları da canlı global/EPS iznine bağlandı;
  kapalı PDU PAYLOAD biti üretimi engeller. [Alt servis sözleşmesi](subsystem_safety.md)
  explicit recovery ve in-flight archive transaction sınırını açıklar.
  Bütün fiziksel alt sistemlerin kapatıldığı iddiası değildir.
- EPS yerel durum makinesinin `5.0 V` düşük/`8.5 V` toparlanma eşikleri değişmedi.
  CDH'nin `6.0 V` eşiği yalnız global `CRITICAL` çıkışıdır; EPS yükleri açmak veya
  bütün hataları temizlemek için yeterlilik kararı değildir.
- Otomatik `SAFE → NOMINAL` eklenmedi. Mode API'leri ve yer komutu active critical/
  fatal kapısına uyar; fatal kaydı SAFE/CRITICAL seçilerek zayıflatılamaz.
  Bu alt politika global bütün modlar için sayısal severity sırası tanımlamaz.
- Deployment görevi completion bilgisini ayrı tutar; ilk kayıt ile
  `INITIAL_BOOT → NOMINAL` aynı mutex bölgesinde yapılır. Completion olmadan
  NOMINAL/DEGRADED init/Set/Transition ve ilgili yer komutu reddedilir.
  Deployment'tan önce bir güç arızası `SAFE/CRITICAL` oluşturmuşsa deployment bu
  durumu normal moda zorlamaz; yalnız voltaj toparlanması da artık NOMINAL sağlamaz.
  Completion yalnız simülasyon startup önkoşuludur; aktif critical/fatal nedenleri
  temizlemez. EPS dışı genel domain repair ve boot/mission grafiği ayrı açık iştir.
  16 Ekim'de yalnız watchdog task-timeout'u için kararlı liveness owner çözümü
  kod/host/build'de eklendi; global SAFE korunur, ayrı mode komutu gerekir.
  TM_GEN iki-episode yolu ve normal restore ayrıca kartta geçti.
  [Koşullar](watchdog_recovery.md), [sınırlı kart kanıtı](watchdog_recovery_target.md).
- `FDIR_Process()` artık required FDIR görevinde 1000 ms nominal periyotla çalışır;
  üç warning kararı yalnız NOMINAL/DEGRADED'da işlenir. Diğer modlarda ve başarısız
  geçişte counters korunur; [FDIR sözleşmesi](fdir_policy.md) ACK/kilit sırasını açıklar.
  Aktif critical/fatal listesi ve normal çalışma engeli de uygulandı. Watchdog ve
  EPS dışı verified repair/fatal/uncertainty recovery ayrı açık işlerdir.
- Sonraki [software watchdog adımı](watchdog_policy.md) yedi görevin timeout'unu
  critical FDIR kaydına bağladı; mevcut CRITICAL/EMERGENCY korunur, task timeout'u
  kendi başına reset/clear istemez. Core'a required WATCHDOG_Init ve task creation
  öncesi arm eklendi. Daha sonra onaylı SDK supervisor/idle-stall 15 s/panic recovery
  eklendi; bunun reset'i per-boot MODE/fault/deployment/arşiv state'ini kaybettirir.
  Kontrollü heartbeat kaybı/SAFE ve supervisor SDK TASK_WDT reset deneyi
  [kartta doğrulandı](watchdog_target_tests.md); persistent reboot/rearm inhibit,
  verified repair ve genel idle/global starvation hâlâ açık.
- Wi-Fi, mevcut görev öncelikleri, arşiv ve YAMCS kaynak ayarları değişmedi;
  task listesine daha önce required FDIR görevi eklenmişti.

## Masaüstü doğrulaması

`tests/host/eps_mode/check.c`, gerçek EPS monitor task gövdesi, sistem modu servisi
ve FDIR servisini birlikte derler. RTOS mutex/delay, zaman ve ağ sınırları taklit
edilir. Her senaryo ayrı süreçte başlar; görev bir veya iki tam döngü çalıştırılır.

36 senaryo:

- Altı sistem modunda `2.49`, `2.5`, `6.0`, `6.01 V`: eşik sınırları, mevcut
  EMERGENCY'nin korunması ve CRITICAL toparlanmasının yalnız SAFE olması (24).
- EPS mutex ve başlatılmamış mod servisi; kritik/toparlanma geçiş kilit hataları (4).
- Hata bildirimi sırasında, FDIR SAFE isteğinden hemen önce, CRITICAL isteğinden
  hemen önce ve toparlanma isteğinden hemen önce EMERGENCY yerleştirilmesi (4).
- CRITICAL/toparlanma isteği öncesi başka moda değişiklik: yeni karar korunur (2).
- Kilit hatasından sonra ikinci normal döngüde kritik/toparlanma isteğinin
  yeniden değerlendirilip başarılı olması, ilk döngüde sahte başarı/spin olmaması (2).

Son mod, gerçek FDIR fault alanları, log başarı/başarısızlık sayıları, EPS mutex'i
bırakıldıktan sonra geçiş yapılması, watchdog ve 1000 ms delay kontrol edilir.
FDIR'nin timestamp `%lu` log argümanı host/target için açıkça `unsigned long`
cast'i kullanır; fault/mod politikası bu format uyarlamasıyla değiştirilmez.
Mevcut FDIR `heater_fault_active` unused-variable uyarısı testte görünür kalır;
yalnız bu grup için `-Wno-error=unused-variable` kullanılır.

2026-10-14: 14 C host grubu (yeni grupta 36 senaryo), mevcut 79 Python kontrolü
ve normal `esp32dev` derlemesi geçti. Statik RAM 42188/327680, flash
793309/1740800 bayt; SDK config SHA256 değişmedi. Kart yüklenmedi/resetlenmedi,
RAM arşivi silinmedi. Deterministik geçiş arası enjeksiyon gerçek eşzamanlı
FreeRTOS scheduler/kilit bekleme süresi, fiziksel düşük gerilim veya cihazdaki
fault/recovery testinin yerine geçmez.

## EPS yük politikası doğrulaması

`tests/host/eps_loads/check.c`, gerçek EPS manager task, EPS komut/monitor, global
mod ve FDIR kaynaklarını birlikte çalıştırır. Yerel durumlar private global'i
değiştirerek değil gerçek görev döngüleriyle hazırlanır. SDK/RTOS/zaman/ağ sınırları
taklit edilir; her senaryo ayrı süreçtir. 144 senaryo:

- Altı global mod × dört erişilebilir yerel EPS modu × beş voltaj (`4.99`, `5.0`,
  `6.0`, `8.49`, `8.5 V`): 120 eşleme/eşik senaryosu.
- 24 özel senaryo: mod/EPS/getter kilit hataları ve sonraki döngüde retry,
  başlatılmamış mod servisi, NULL callback/getter handle, callback'in kilit sözleşmesi,
  permission hatasında düşük voltaj raporu, yük açma öncesi üç güvenli mod,
  geçerli açma sonrası EMERGENCY ve sonraki döngüde kapama, recovery/relapse,
  boot'ta kapalı bitlerin korunması, COMMS/diğer bitler, tekrarlanan SAFE,
  heater stub, mevcut 3.3 V monitor, değişmeyen SoC/sunrise hesabı.

Testler PDU/yerel/global mod, iki fault flag'i ve diğer flag'lerin korunması,
callback/FDIR/log kilit sınırları, 100 ms delay ve gereksiz SAFE log tekrarının
olmamasını kontrol eder. FreeRTOS priority inheritance/scheduler zamanı, fiziksel
voltaj, gerçek heater veya ADCS/payload durması bu host testinde doğrulanmaz.

2026-10-14: toplam 15 C host grubu, mevcut 79 Python kontrolü ve normal `esp32dev`
derlemesi geçti. RAM 42188/327680, flash 793361/1740800 bayt; SDK config SHA256
değişmedi. Karta yükleme/reset veya RAM arşivini silen işlem yapılmadı. Mevcut
ADCS/FDIR/HAL compiler uyarıları bu alt adımda topluca temizlenmedi.

## 15 Ekim: EPS gerilim sınırları ve geçersiz ölçüm

Önceki iki bölüm tarihsel 14 Ekim sonuçlarıdır. Güncel gruplar EPS yüklerinde
316, CDH güç/mod kontrolünde 78 süreçtir. Host toplamı 30 C grubu/45 bağımsız
header; tüm C grupları `-Werror` ile geçer, eski unused-variable istisnası yoktur.

NaN/±Inf, karşılaştırmaların false olmasına bırakılmaz. EPS manager aynı döngüde
PAYLOAD/HEATER bitlerini keser, COMMS/diğer bitleri korur, yerel SAFE ve warning
flag'ini yazar. `EPS_Monitor` kilitler dışında mevcut critical E102 kodunu bildirir.
E102 artık düşük **veya sonlu olmayan** gerilimi ifade eder; ID/severity değişmez.
Global mod FDIR policy ile güvenli duruma geçer, EMERGENCY/CRITICAL korunur.
CDH monitor da bu power fault'u raporlar; bilinmeyen ölçümü `<2.5 V` kanıtı sayıp
CRITICAL zorlamaz, toparlanma yetkisi vermez. `EPS_CalculateSoC` sonlu olmayan
girdide NaN döndürür; bunu %100 veya %0 diye göstermez.

Bu kodlar aynı power-fault kaydını kullanır. Mevcut revision-guarded owner clear
politikası değişmedi: yalnız sonlu `V >= 8.5` ve örnekten önce yakalanmış aynı
revision ile EPS kendi E102 nedenini çözebilir. Düzelen ölçüm global NOMINAL
yazmaz; gerçek fault-clear sonrası açık bir işletim isteği gerekir. Yükler de
yerel SAFE → RECOVERY → NOMINAL adımlarından geçer.

Sonlu eşikler değiştirilmedi:

- CDH: `<2.5 V` güç CRITICAL; CRITICAL'dan `>6.0 V` yalnız SAFE.
- EPS monitor: `<3.3 V` critical E102 bildirimi.
- EPS manager: `<5.0 V` yük kesme; yerel SAFE'den `>=8.5 V` toparlanma izni,
  ancak global mod NOMINAL/DEGRADED ise. İzin kapısı zaten non-finite gerilimi reddeder.
- SoC demo modeli: `<=6.0 V` %0, `>=8.4 V` %100; ara değerler lineer.

EPS yük matrisi altı global × dört yerel × 12 gerilim = 288 durum + 28 özel
senaryodur. 5 ve 8.5 V'nin tam değer/ULP komşuları, NaN/±Inf, gerçek E102
fault-clear ve explicit restart/chatter, NULL komut ve 3001 noktalı SoC monotonluğu
kapsanır. CDH matrisi 2.5/6.0 V komşularını ve altı moddaki üç non-finite değeri
de kapsar. EPS lock → MODE sırası, fault/log/pet'in kilit dışında olması ve
normal döngüye dönülmesi gerçek C koduyla kontrol edilir; RTOS/sensör/zaman/ağ mock'tur.

Normal `esp32dev` build 27.45 s; RAM 42596/327680 (aynı), flash 803233/1740800
(+224 bayt). SDK config SHA256 aynı:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Karta yükleme/reset yapılmadı. Fiziksel brownout, hedefte jitter/stack/heap,
gerçek kernel yarışları veya tüm-source/fatal recovery doğrulanmış sayılmaz.
Önceki Python/graf/profile/hardware paketleri bu adımda yeniden çalıştırılmadı.
Solar recovery boost modeli ve farklı tarihsel gerilim eşikleri yeniden
kalibre edilmedi; bu testler fiziksel batarya modeli doğruluğu iddiası değildir.

## 2026-10-16: Mevcut recovery kapılarının birlikte doğrulaması

Alt servis fixture'ında 12 yeni süreç, gerçek deployment/EPS/FDIR/MODE/ADCS/payload
akışını birlikte çalıştırır. E102 owner-clear'dan sonra global SAFE, ayrı yer
NOMINAL/DEGRADED isteği, yerel EPS SAFE→RECOVERY→NOMINAL ve açık alt servis restart
ayrı kapılar olarak doğrulandı. Completion/ACK/payload RESET diğer critical/fatal
nedeni veya uncertainty'yi temizlemez. Pre-deployment clear sıraları, relapse ve
PDU kapısı da kapsandı. Bunlar yeni transition yetkileri değildir.

Pre-init fault floor'u korunurken yanlış INITIAL_BOOT basan init diagnostic'i
effective init-mode snapshot'ıyla düzeltildi. State/gate politikası değişmedi.
Alt servis grubu 183; tam 43 C/47 header/372 Python, build/ELF/metadata/59 profil
geçti. RAM 42884, flash 809289; SDK config aynı. Yeni imaj karta yüklenmedi;
genel/fatal/uncertainty recovery ve fiziksel lifecycle doğrulaması açık.
[Süreçler, ilk başarısız regression, hash ve sınırlar](subsystem_safety.md#2026-10-16-birlikte-boot-safe-ve-owner-recovery-akışı).
