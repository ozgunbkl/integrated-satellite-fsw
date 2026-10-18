# ADCS ve payload güvenli mod sözleşmesi

Güncelleme: 2026-10-16. Mevcut simülasyon servislerinin global mod/EPS izin bağları
tamamlandı. Gerçek actuator/sensör, flight qualification, bütün mod grafiği veya
fiziksel kontrol kararlılığı doğrulaması değildir.

## Ortak izin ve kilit sırası

`eps_service.h` içindeki `EPS_RunIfPermitted` canlı EPS verisi ve global modla
kısa bir işlemi yetkilendirir. Her işlem için yerel EPS NOMINAL, sonlu bus voltajı
`>= 5.0 V` ve `safe_mode_active=0` gerekir. LOW_VOLTAGE, SAFE, RECOVERY,
başlatılmamış servis veya kilit hatası izin vermez.

| İşlem | İzin veren global mod | Ek koşul |
| --- | --- | --- |
| ADCS DETUMBLE | INITIAL_BOOT / NOMINAL / DEGRADED | Yerel ADCS DETUMBLE |
| ADCS pointing | NOMINAL / DEGRADED | Yerel ADCS NOMINAL |
| Orbit boost | Yalnız NOMINAL | Yerel ADCS NOMINAL, actuator etkin, irtifa `< 500 km`, pozitif/sonlu mesafe ve simüle yakıt |
| Payload START / üretim | NOMINAL / DEGRADED | PDU PAYLOAD biti açık; uygun yerel payload durumu |

SAFE/CRITICAL/EMERGENCY bütün bu işlemleri engeller. Healthy boot detumbling
başlangıcı korunur; boot'ta pointing/boost/science START yetkisi yoktur. Global
mod otomatik NOMINAL yapılmaz; yer komutunun mevcut manuel yetkisi ve EPS yük
toparlanmasının 8.5 V eşiği değişmedi.

Kilit sırası **ADCS veya payload state → EPS data → global mode**. Komut/görev/
getter durumu ayrı servis mutex'leriyle korunur. Callback kısa state/vector
güncellemesi veya sessiz simülasyon mutasyonudur; log/FDIR/ağ/arşiv/başka kilit
işlemi yapmaz. EPS/mode kilitleri her yolda bırakılır. FDIR ve servis diagnostics
state kilidi de bırakıldıktan sonra çalışır.

Yeni state mutex'leri startup-only `ADCS_Init()` / `Payload_Init()` içinde
oluşturulur. Zorunlu tahsis veya init lock hatası servis mesajı + abort'tur;
consumer task'lara devam edilmez. `void` init imzaları, core sıra/defaultları ve
core helper'ın tekrar init'i reddetmesi korunur. Doğrudan tekrar service init,
mevcut mutex'i yeniden tahsis etmez ama state'i resetler; runtime restart/rollback
API'si değildir. Panic/reset RAM arşivini kaybettirir. Gerçek cihazda bu hata yolu
veya iki yeni mutex'in dinamik heap maliyeti ölçülmedi.

## ADCS

- Local SAFE/izin kaybı actuator bayrağını kapatır; dipole, PD torque ve B-Dot
  geçmişi sıfırlanır. SAFE çıktısı eski komut döndüremez.
- `ADCS_Process()` hesaplama öncesi ve frame publish öncesi canlı izin kontrol
  eder. Publish izinle aynı kilit bölgesindedir. Mevcut DETUMBLE numeric saturation
  ve non-finite sensor/command local SAFE ve kilitler dışında warning üretir.
- `ADCS_StepSimulation()` actual physics komutunu uygularken tekrar yetkilendirir.
  İzin/state lock yoksa sıfır komutla pasif damping/irtifa değişimi sürer;
  simülasyon zamanı dondurulmaz.
- `ADCS_TryOrbitBoost()` authorization ile sessiz `SIM_ApplyBoost()` mutasyonunu
  aynı kilit altında yapar. Primitive bool döndürür; manager yalnız gerçek başarıda
  "Maintenance Complete" basar. Boş yakıt/izin reddi başarı değildir. Mevcut
  fuel-cost/clamp modeli gerçek propulsion modeli değildir.
- İzin geri gelince otomatik aktif moda dönüş yoktur. SAFE'ten yalnız izinli
  DETUMBLE kabul edilir; doğrudan NOMINAL reddedilir. İlk magnetometer örneği
  geçmişi hazırlar/sıfır B-Dot verir; eski türev veya torque yeniden oynatılmaz.
- `ADCS_SetMode()` bool döndürür; state lock varsa SAFE çalışma izni aramaz.
  Negatif/out-of-range ve uygulanmamış SUN_SAFE/PAYLOAD_POINT, state değişmeden
  reddedilir. NULL/eksik komut kontrol edilir. Reset-estimation hâlâ stub'dır;
  mesajı filter reseti yapıldığı iddiası taşımaz.
- Getter lock/permission kaybı konservatif SAFE/zero yoludur; fallback değerleri
  gerçek sensör ölçümü değildir. Dönen vector süresiz bir izin token'ı değildir;
  manager uygulama anında yeniden kontrol eder.

Control gain'leri ve numeric stability threshold aynı kaldı. Kullanılmayan ikinci
PD hesaplaması kaldırıldı; eski `torque_cmd` warning'i bu nedenle yoktur.
Bu ilk adımda ADCS birim/sign/kararlılık/model doğrulaması ve raw `SIM_*`
API'lerinin thread ownership'i ayrı bırakılmıştı. Birim/sign ve runtime erişim
sınırlarının 15 Ekim devamı [simülasyon sözleşmesinde](simulation_contracts.md)
açıklanır; bütün modellerde kapalı çevrim kararlılık hâlâ kanıtlanmış değildir.

## Payload

- OFF/ERROR/SAFE Update üretmez. ACTIVE Update önce global/EPS/PDU izni kontrol
  eder; izin yoksa SAFE olur, sample/arşiv yazımı yoktur.
- Hazırlama sırasında izin kaybolursa ikinci kontrolde sample atılır; archive/byte
  counter artmaz. Önceden yazılmış kayıtlar silinmez.
- Son izin kontrolünü **geçmiş** tek transaction, karar submission'dan hemen
  önce veya submission sırasında değişse bile tamamlanabilir.
  Archive_WriteRecord FDIR/log üretebildiğinden EPS/mode kilitleri altında çağrılmaz.
  Yeni güvenlik kararında sonraki Update yeni üretimi keser. Anlık interlock veya
  geri alınabilir archive transaction yoktur. START ACK'i de komut anına aittir.
- Güvenlik SAFE'i sonrası explicit `INIT → STANDBY`, izinli `START → ACTIVE`
  gerekir; otomatik resume yoktur.
- START reddi `PL_ERR_INHIBITED` (`4`), state lock kaybı `PL_ERR_UNAVAILABLE` (`5`)
  ve wrapper NACK üretir. Beklenen güvenlik reddi warning counter'ını artırmaz.
  Önceki status değerleri `0..3` aynıdır.
- INIT OFF/SAFE'ten, STOP ACTIVE'ten geçer. STOP/RESET çalışma izni aramaz. RESET
  OFF/error-clear yapar; archive'i veya generated-byte geçmişini silmez. SET_RATE
  `1..10` sınırı korunur.17 Ekim NOMINAL hazırlığında açık rate seçimi artık
  monotonic-ms üst hız sınırı uygular; catch-up burst yoktur. Seçilmemiş0 değeri
  legacy her-update davranışıdır. Bu kesin scheduler/deadline garantisi değildir.
  [Değişiklik ve doğrulama](nominal_soak_preparation.md). Geçersiz rate tek warning üretir.
- Archive hatası ERROR/error counter ve kilitler dışında warning üretir. NULL/
  boş wrapper NACK'tir. Getter lock hatasında SAFE/zero fallback gerçek sayaç
  snapshot'ı sayılmaz. Archive/FDIR backend kendi iç logunu, payload transaction
  sürerken fakat EPS/mode kilitleri dışında üretebilir.

## Doğrulama ve sınırlar

`tests/host/subsystem_safety/check.c` gerçek EPS manager/commands, global mod,
FDIR, ADCS service/manager/control/math/simulation ve payload service kaynaklarını
çalıştırır. Sensor/irtifa okumaları kontrollüdür; physics/boost gerçek simulation
fonksiyonuna forwarded edilmeden kilit/command koşulları kontrol edilir. Payload
generation/archive, RTOS/zaman/ağ sınırları taklit edilir. Gerçek archive CRC/record
kontrolleri ayrı mevcut testlerdir.

164 ayrı süreç: 96 global mod × yerel EPS × operation izin senaryosu; altı global
modda iki ADCS controller, payload START/Update ve boost için 30 senaryo; 38 özel
senaryoda canlı PDU/voltage/flag, NaN/Infinity, NULL/uninitialized/kilit hataları,
stale output temizliği, publish/physics/boost öncesi EMERGENCY, illegal mod,
saturation/non-finite frame, boot stability, manager heartbeat/boost/boş yakıt,
payload recovery/prepare/submit yarışları, power cut, archive failure, ACK/NACK,
STOP/RESET/rate, boost parametresi ve dört state-mutex init fatal yolu doğrulanır.

2026-10-14: 16 C host grubu, yedi dosyada mevcut 79 Python kontrolü ve normal
`esp32dev` derlemesi geçti. Statik RAM 42188/327680, flash 795061/1740800 bayt;
SDK config SHA256 aynı. Statik RAM dinamik mutex heap maliyetini içermez. Karta
yükleme/reset veya RAM arşivini silen işlem yapılmadı. Gerçek scheduler/priority
inheritance/tepki süreleri, physical actuator, runtime heap/stack, gerçek TC ile
yeni mod komutları veya cihaz fault/panic testi değildir. Wi-Fi/YAMCS/router/
firewall/credential ayarları değişmedi.

Sonraki adımda [deployment startup kapısı](deployment_policy.md) eklendi; bu
belgedeki operating fixture'lar artık completion API'siyle post-deployment state
hazırlar. [FDIR warning görevi](fdir_policy.md) de sonraki adımda eklendi.
Sonraki [aktif hata kapısı](fdir_faults.md) critical/fatal nedenleri operating/BOOT
modlarıyla atlamayı engeller; mevcut permission callback'leri effective safety mode
görür. LOW seed'in operating-mode isteği yeni gate nedeniyle NACK bekler. EPS
finite 8.5 V + revision ile kendi critical kaydını çözebilir; ADCS/payload otomatik
restart eklenmedi. Bütün severity/mission grafiği, EPS dışı verified/fatal recovery,
watchdog ve archive eşzamanlılığı hâlâ açıktır.

## 2026-10-15: birim, işaret ve model sahipliği devamı

Güvenlik grubu 171 senaryoya çıktı. rad/s'e çevrilmiş 3000 deg/s eşiğinin
below/at/above/negative ve NOMINAL saturation senaryoları, nominal damping
işareti ve invalid dt eklendi. ADCS state kilidi alınamıyorsa artık kilitsiz
pasif physics de yapılmaz. Altitude/fuel snapshot fallback'leri test edilir.
Manager'ın eski empty-fuel fixture'ı artık gerçekten yetersiz yakıtı sınar:
2.5% yakıtla 7.5% isteyen 50 km boost reddedilir, ücretsiz son manevra yoktur.
İzin tablosu, EPS/global mode kilit sırası ve stale-output/emergency korumaları
korunur. Tam plant/model/gerçek sensör veya runtime scheduler doğrulaması değildir.

## 2026-10-16: Birlikte boot, SAFE ve owner-recovery akışı

Güvenlik fixture'ına gerçek `deployment_service.c` de bağlandı. Yeni **12 süreç**
ile alt servis grubu **183** oldu. Mode/FDIR/EPS manager/ADCS/payload ve deployment
task C gövdeleri birlikte çalışır. RTOS delay'leri anında dönen kontrollü sınırdır;
10 s + 2 s bekleme argümanları ve tek task-delete doğrulanır, gerçek zaman beklenmez.
Sensor, payload veri üretimi, archive backend, zaman ve ağ hâlâ mock'tur.

Başarılı runtime yolu:

```text
BOOT → deployment → NOMINAL → EPS E102 → SAFE
→ sağlıklı örnekle EPS owner-clear (global SAFE kalır)
→ ayrı yer NOMINAL/DEGRADED komutu
→ yerel EPS RECOVERY → yerel EPS NOMINAL
→ açık ADCS DETUMBLE ve payload INIT/START → yeniden izinli çalışma
```

Kapsanan yeni süreçler:

- Healthy boot: completion öncesi NOMINAL/DEGRADED komutu, pointing/boost/payload
  reddedilir; yalnız mevcut boot DETUMBLE izni korunur. Gerçek deployment task
  completion'ından sonra payload üretimi açılabilir.
- Deployment'tan önce düşük voltaj: neden completion'dan önce veya sonra
  çözülebilir; iki sırada da global SAFE korunur. ACK ve completion fault-clear
  değildir; yalnız deployment eksikliği bile NOMINAL isteğini engeller.
- Runtime E102: gerçek EPS owner yalnız kendi revision'ını çözer. Sağlıklı voltaj
  ve kabul edilmiş global mode komutu tek başına yerel EPS SAFE/RECOVERY kapısını
  atlayamaz. ADCS/payload kendiliğinden resume etmez; açık restart gerekir.
- NOMINAL/DEGRADED toparlanması: ikisinde payload ve pointing izinli olabilir;
  orbit boost yalnız NOMINAL'dedir. Yeni DETUMBLE ilk örnekte eski B-Dot çıktısını
  yeniden kullanmaz. PAYLOAD PDU biti kapalıysa START yine reddedilir.
- Recovery sırasında yeniden düşük voltaj yeni E102 revision'ı ve yük kesme
  üretir; ikinci owner-clear ve ayrı mode kararı gerekir.
- Başka critical, fatal veya recording uncertainty: EPS E102 çözülse bile
  BOOT/NOMINAL/DEGRADED isteği reddedilir. Tekrar MODE/FDIR init, warning ACK,
  deployment completion ve payload RESET/INIT bu engeli temizleyemez. Diğer
  source/code/revision/fatal kaydı ve payload generated-byte geçmişi korunur.
- Pre-init critical/fatal: kayıt mutex'i henüz yokken oluşan uncertainty,
  MODE init/deployment/sağlıklı EPS döngüsü sonrası da SAFE/EMERGENCY floor'unu
  korur; boş ledger normal çalışmaya izin vermez.

### Yakalanan log tutarsızlığı

Pre-init fault halinde mode servisi doğru SAFE/EMERGENCY'de kalmasına rağmen
`SYSTEM_MODE_Init()` istenen INITIAL_BOOT'u logluyordu. Yeni regression önce bu
mesajda başarısız oldu; koruma kararı başarısız değildi. Diagnostic artık aynı
init transaction'ındaki effective mode snapshot'ını yazıyor. Yeni mode/repair
yetkisi veya uncertainty temizleme eklenmedi.

İlk hedefli başarısız koşu `lifecycle_before_fix.log` olarak korundu. Düzeltmeden
sonra 183 süreç ve tam **43 C/47 header/372 Python (20 dosya)**, **22 graf/layout**,
**15 runner-refusal**, **59 profil** ve güncel normal metadata/ELF geçti. Son
ledger/payload-retention assert'lerinden sonra ilgili 183 süreç ayrıca tekrar geçti.
52 production C kaynağı tek derlenir. Normal build 43.30 s; RAM **42884** (aynı),
flash **809289** (önceki startup build'inden **-4 bayt**). SDK config aynı.
Firmware SHA256:
`B224DB759C39366C8C72A239085E80AC5E5C7C83C3EE316EB89E6034D4EC6C1F`.
SDK config SHA256:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Yerel kanıt: `.pio/host-tests/mission-lifecycle-20261016/`.

Bu, belirtilen mevcut izin/owner akışının deterministik entegrasyon kanıtıdır;
gerçek scheduler/iki-core yarış, fiziksel sensör/actuator, kalıcı restart inhibit,
diğer-source domain repair veya fatal/uncertainty recovery uygulaması değildir.
Karta yükleme/reset/komut, arşiv silme veya ağ/MDB değişikliği yoktur; son startup
ve bu diagnostic değişikliği henüz karta yüklenmedi. Uzun test başlatılmadı.
Wire/ground değişmediği için native YAMCS/Java paketi tekrarlanmadı.

## 17 Ekim: seçilen gerçek kart senaryosu

Son startup paketinden sonraki izole EPS girdisi/gerçek-servis zinciri test imajı,
üç ground NOMINAL isteği ve restore paketi [ayrı belgede](mission_lifecycle_target.md)
anlatılır. 16 Ekim hazırlığından sonra 17 Ekim'de ayrı onay ve güncel 30 RAM
kaydının iki tam retained export'uyla gerçek kart paketi geçti: 24.0 s / tek boot,
E102 revision 1, owner-clear sonrası SAFE, iki NACK/bir ACK ve ayrı servis rearm'ı;
payload byte geçmişi 64'te durup açık yeniden başlatmayla 128'e çıktı, eski kayıt
aynı CRC/içerikle kaldı. Driver stack minimumu 956 byte. Normal restore 95.7 s /
15 NOMINAL örneği, RESTORE PASS / exit 0. Kartta normal imaj bırakıldı.

Bu seçilen gerçek RTOS sonucudur; yukarıdaki bütün host senaryolarının fiziksel
karşılığı veya genel recovery/fatal/uncertainty çözümü değildir. Gerçek servis
kodu değiştirilmedi. Uzun çalışma ve GitHub yayını yapılmadı.
