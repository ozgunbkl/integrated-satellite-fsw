# Kalıcı özet ve reset sonrası çalışma kilidi

2026-10-17. Kullanıcının onayladığı konservatif politika kodlandı; masaüstü,
normal ESP32 derlemesi ve YAMCS kontrolleri geçti. Ayrı onaylı son fiziksel koşuda
**seçilen PANIC ve üç-abort BOOT_LOOP + normal-restore/korunan-geçmiş paketleri de geçti**.
Bu belge uçuş yeterliliği, donanım onarımı veya bütün recovery yollarının kapanışı
değildir. [Güncel kapanış listesi](release_checklist.md).

## Davranış

- PANIC, task/interrupt watchdog, brownout ve bilinmeyen reset nedeni sonraki
  açılışı SAFE çalışma kilidiyle başlatır. Yalnız SDK POWERON / EXT / SW nedenleri
  tek başına hata sayılmaz; mevcut geçmiş ve yarım açılış sayacı yine kontrol edilir.
- Core init'ten önce kalıcı `OPEN_BOOT` işareti yazılır. Önceki açılış bu işareti
  kapatmamışsa veya mevcut reset nedeni kötüyse başarısız açılış sayacı bir artar
  (iki koşul birlikteyken iki değil). Üç ardışık başarısız açılış `BOOT_LOOP` bırakır.
- Bozuk/sürümü bilinmeyen/okunamayan kayıt, mutex/init/yazma/read-back hatası veya
  boot sayacının tükenmesi bu boot'ta kaldırılamayan konservatif kilit oluşturur.
  Otomatik erase, retry/reboot döngüsü veya bozuk kayıt üzerine yazma yapılmaz.
  Daha önce hiç oluşturulmamış namespace/key ise ilk kurulum olarak kabul edilir.
- Ciddi geçmiş bayrakları **yapışkandır**: fault, fatal, uncertainty, overflow,
  kötü reset veya boot-loop görülmüşse sonraki **her boot** yeni sağlık penceresi
  ve yeni yer onayı ister. Önceki boot'un onayı bunu kalıcı olarak devre dışı bırakmaz.
- Önceki boot'un kayıtları yeni boot'ta aktif FDIR nedenleri gibi yeniden oynatılmaz.
  Bunun yerine ayrı çalışma kilidi, geçmişi incelemeden göreve dönülmesini engeller.
  Bu davranış asıl arızanın giderildiğinin kanıtı değildir.

MODE içindeki bağımsız kapı, `Set`/`Transition`/deployment ve servis izinlerinin
gördüğü effective mode üzerinde uygulanır. Kilitliyken INITIAL_BOOT, NOMINAL ve
DEGRADED seçilemez. Aktif fatal hâlâ EMERGENCY floor'udur. Eski `FDIR_SetMode`
ve warning ACK komutu bu kapıyı veya fatal/uncertainty kaydını kaldıramaz.

## Sağlık penceresi ve ayrı yer onayı

Yeni düşük öncelikli `RECOVERY` görevi yaklaşık saniyede bir gözlem alır. Şunların
birlikte sağlanması gerekir:

- Arşivde mevcut kayıtlar salt-okunur doğrulanabilmeli; CRC/lock/okuma hatası olmamalı.
  Otomatik veri silinmez. Mevcut açık WIPE bakımından sonra boş arşiv doğrulanırsa
  yalnız yakalanan nonfatal CRC revision'ı çözülebilir; diğer aktif nedenler,
  SAFE ve kalıcı geçmiş korunur. Bu, bozuk veriyi onarma/quarantine değildir;
  normal yükleme onayı kartta WIPE izni anlamına gelmez.
- FDIR snapshot'ı alınabilmeli; aktif critical/fatal, overflow ve recording uncertainty
  olmamalı. Sorgu hatası boş ledger veya uydurulmuş yeni uncertainty sayılmaz.
- Deployment tamamlanmış olmalı; mode BOOT/CRITICAL/EMERGENCY olmamalı.
- EPS mutex'i alınabilip bırakılmalı, bus finite ve kritik alt eşiğin (5 V) altında
  olmamalı; payload ERROR olmamalı. Bu, EPS owner-clear için mevcut 8.5 V koşulunu
  gevşetmez; aktif EPS nedeni ayrıca pencereyi engeller.
- İzlenen yedi heartbeat rolünün en az bir pet'i olmalı ve son pet yaşı mevcut
  `WATCHDOG_TIMEOUT_MS` sınırını aşmamalı.

Pencere en az 30 saniye kesintisiz, en az üç örnekle ilerler; iki saniyeyi aşan
örnek boşluğu veya sağlıksız örnek pencereyi sıfırlar. Kararlı açılış bir kez
`OPEN_BOOT` işaretini ve ardışık başarısız sayacını kapatır; **geçmişi veya mevcut
çalışma kilidini açmaz**. Sağlıklı ve geçmişsiz normal ilk boot'ta mevcut deployment
→ NOMINAL akışı sürer; bu pencere bütün açılışlara zorunlu yeni gecikme değildir.

Bakımı yapılan MDB'deki yeni `FDIR_RearmBoot` komutu:

```text
APID 0x030; komut gövdesi tam 6 bayt:
[0x53] [BootCounter: uint32, big-endian] [ConfirmKey: 0xA5]
```

İki argüman da açıkça girilir; BootCounter 1..0xffffffff, ConfirmKey yalnız 165'tir.
Güncel boot sayacı ve ready/storage/held durumu seri `RECOVERY:` tanılarındadır;
HK paketine yeni alan eklenmedi. Sayaç ve sabit anahtar **kimlik doğrulama değildir**;
mevcut UDP hattının güvenilir yer istasyonu varsayımı korunur.

Komut bağlamı yalnız istek kuyruğa alır; flash yazmaz. En son sağlık örneği
1.5 saniyeden eskiyse, token yanlışsa veya başka istek varsa reddedilir.
`queued; pending is not completion` başarı onayı değildir. Worker sağlığı yeniden
kontrol eder, onayı kalıcı yazıp read-back doğrular, MODE kapısını gerçek ledger/
deployment/permanent-failure koşullarıyla tekrar denetler. Yalnız
`Ground rearm COMPLETE` kapının açıldığını bildirir. İşlem **SAFE'te bırakır**;
NOMINAL için ayrı mevcut yer mode komutu, ADCS/payload için mevcut açık servis
rearm koşulları sürer. Eski bir komut sonraki boot'ta kullanılamaz.

Aktif fatal/uncertainty/overflow aynı boot içinde bu komutla temizlenmez. Arşiv CRC
nedeni de kendiliğinden çözülmez; arşiv silme/quarantine/salvage veya genel
other-source verified repair henüz uygulanmış değildir. [Aktif FDIR sözleşmesi](fdir_faults.md).

## Kalıcı kayıt ve yazma bütçesi

ESP32 adapter'ı mevcut NVS partition'ında yalnız `fsw_recovery` namespace'inin
`summary` anahtarını kullanır. Shared NVS init ilk sonucu boot boyunca cache'ler;
recovery önce, Wi-Fi sonra aynı sonucu kullanır. NVS erase/deinit eklenmedi.
Politika/BE serileştirme `components/recovery/recovery.c`, ESP32 NVS/mutex/reset
eşlemesi `recovery_port_esp32.c`, uygulama gözlemi `main/recovery_app.c` içindedir.

Sabit 48 bayt, sürüm 1 kaydı:

| Offset | Alan |
| --- | --- |
| 0..7 | `FSR1`, sürüm=1, uzunluk=48, iki sıfır reserved bayt |
| 8, 12, 16, 20 | BE uint32 boot sayacı, ardışık başarısız açılış, bayraklar, SDK reset nedeni |
| 24, 28, 32, 36 | BE uint32 son özetin boot'u, hata gözlenen boot sayısı, hata MET'i, onay yazılan boot |
| 40..43 | kaynak uint8, kod BE uint16, sıfır reserved bayt |
| 44..47 | İlk 44 baytın CRC32/ISO-HDLC değeri, BE uint32 |

Bayraklar: OPEN_BOOT=1, FAULT=2, FATAL=4, UNCERTAIN=8, OVERFLOW=16,
BAD_RESET=32, BOOT_LOOP=64. Sayaçlar sessiz wrap etmez. Onayın diske yazılması,
son MODE release kontrolünün de geçtiği anlamına gelmez; son worker sonucu esastır.

En fazla **boot başına yedi uygulama commit girişimi**: başlangıçta bir, kararlı
açılışta bir, o boot'ta ilk kez görülen en fazla dört hata sınıfında dört, yer
onayında bir. Sürekli loop, aynı sınıfın tekrar raporu veya tekrar reddedilen onay
yeni yazı oluşturmaz. Yazma hatasından sonra bu boot'ta retry yapılmaz. Bu sayı
NVS'nin dahili garbage collection/sector erase/flash yazma sayısı değildir;
flash ömür veya güç kesintisi dayanımı sertifikası verilmez.

FDIR raporu, ISR veya panic handler flash yazmaz. Özet saniyelik örnekleri birleştirir;
her olayın kalıcı günlüğü değildir. Arada gelip çözülen hata veya poll/commit öncesi
güç kesilmesi hata detayını kaybettirebilir. `fault_boots` rapor sayısı değil,
hata gözlenen boot sayısıdır; source/code son yeni sınıf gözleminin bounded özetidir.
OPEN_BOOT yalnız kararlı açılışa kadar unfinished-start takibidir; bundan sonraki
normal güç kaybını eksiksiz saydığı iddia edilmez. SDK kötü reset nedeni ayrı kapıdır.
CRC/read-back kimlik doğrulama, anti-rollback veya tüm NVS güç-kesintisi kanıtı değildir.
Namespace/full-flash silinmesi geçmişi kaldırabilir; otomatik bakım komutu yoktur.

Görev priority 1, stack bütçesi **6144 bayt**tır; 12 required mission task tablosundan
önce, watchdog arm'dan sonra oluşturulur. Oluşturma/tekrar başlatma hatası kalıcı
per-boot MODE kilidi bırakır. Observer yedi watchdog rolüne eklenmedi; durursa
onay tazeliği geçersizleşir, ancak ayrı observer watchdog/reset garantisi yoktur.
Senkron NVS I/O yalnız startup/worker'dadır; gerçek cache-off etkisi ve latency
henüz ölçülmedi. Seçilen kısa fiziksel PANIC ve normal fallback
ilk gözlemlerinde recovery stack minimum 3676 bayttı; son tek-PANIC/BOOT_LOOP
paketlerinde 3668 bayt ölçüldü. Bunlar bütün iş yükleri veya uzun koşu
ölçümü değildir. Statik RAM raporu bu dinamik stack/TCB/heap'i içermez.

## Doğrulanan ve henüz yapılmayanlar

İlk 2026-10-17 uygulama hazırlığı masaüstü: **50 C grubu / 52 header / 425 Python (22 dosya)**,
22 graf/layout, 34 güvenli runner-refusal ve 59+16+17 negatif profil geçti.
Yeni recovery grupları 47 politika + 12 ESP32-port mock + 31 uygulama mock
senaryosudur: kalıcılık/CRC/bozuk kayıt, boot-loop, bounded writes, stale token/
sağlık/rollover, save/release yarışı, fatal/uncertainty/deployment ve init hataları.
TC frame/ingress gerçek router zincirine yeni komut için test eklendi.
NVS init yedi ayrı süreçte, her süreçte üç çağrı/tek SDK init ile doğrulandı.

Çevrimdışı gerçek YAMCS 5.12 loader/builder/postprocessor **11 komutu** yükledi;
üç unsigned boot counter'ın BE binary'si ve yedi geçersiz/eksik argüman reddi geçti.
Bu ilk hazırlıkta ground source/runtime kurulumu/reload/TC yoktu. Sonraki ayrı
onaylı fiziksel koşuda **11 komutlu MDB kuruldu ve instance reload edildi**;
canlı preview ve dört gerçek recovery/mode isteği de doğrulandı.

Normal build: statik RAM **42964/327680**, flash **813525/1740800** bayt;
57 production C, 16 component/63 doğrudan proje-header bağı; taze normal ELF'de
test hook'u yok. Firmware SHA256:
`153DF1197889F6028F70E06054E91969335EC0EC334D660A8DAC3061FFA7D998`.
SDK config SHA256 değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Yerel kanıt: `.pio/host-tests/recovery-prep-20261017/`; ilk başarısız hazırlık
logları da tutulur, son tam koşu `host_verified.log`, build `build_verified.log`.

İlk hazırlık sonunda kartta normal **C7F16A…** kalmıştı. O adımda seri port/reset,
gerçek NVS okuma-yazma veya güncel RAM arşivine erişim yapılmadı. Önceki iki export
aynı SHA256 ile durur; bunlar mevcut boot RAM'inin yeni yedeği sayılmaz.

Eski `test_watchdog_system.ps1` artık recovery kaynakları mevcutken **build/flash
öncesi durur**: eski testlerin resetten sonra otomatik NOMINAL oracle'ı yeni
politikayla uyumsuzdur. 34 refusal testi bu korumayı cihazsız doğrular. Eski
fiziksel sonuçlar kendi imajları için tarihsel kanıttır; yeni politika için PASS değildir.
Yeni resource okuyucu da RECOVERY rolünü bekler; eski kartın 13 rollü çıktısı
yeni 14 rollü sözleşmeyle karıştırılmamalıdır.

Sonraki adımda kalıcı-durum farkında, iki tam retained export ve finally-normal
restore gerektiren [ayrı kısa target paketi](recovery_target.md) hazırlandı.
NVS yan etkileri ve geçici test RAM'i kaybı ayrıca açıklandı; eski runner engeli
kaldırılmadı. Hazırlığın ardından ayrı kullanıcı onaylı fiziksel koşuda gerçek
PANIC/SAFE/sağlık/stale-token/yer onayı fazı 79.391 s geçti. Ancak restore aktarımı
kesildi ve tam upload sonrası boot sayacı/BAD_RESET geçmişi kaybı gözlendi: **paket
bütünü ve upload boyunca kalıcılık PASS değildir**. Normal 153DF1… imaj sonradan
PIO ile geri döndürüldü; ayrı 47.734 s fallback ve canlı YAMCS kontrolü geçti.
Son tam masaüstü 50 C/52 header/477 Python/24 dosya; 45 refusal, 92 negatif profil,
20 workflow/artifact. [Sonuçlar ve açık sorunların tam kaydı](recovery_target.md).
Sonraki ek loader-probe/tekli normal yazım teşhisinde NVS hash'i her adımda korundu,
sayaç 2→3 ve FAULT geçmişi doğrulandı. Ayrı kullanıcı onayıyla bu boot için iki TC/
SAFE→NOMINAL akışı 64.391 s geçti; eski aktarım/kayıp kök nedeni kapanmadı.
Güncel masaüstü toplamı 50 C/52 header/**492 Python/25 dosya**; yeni probe 15 testtir.
Gerçek NVS bozma/erase, fatal onarım veya arşiv silme bu hazırlığın örtük yetkisi
değildir. Uzun test ve GitHub yayını yapılmadı; [sıra değişmedi](release_checklist.md).

Sonraki geçmiş-farkında runner adımında passive baseline bağlama, açık FAULT-history
onayı ve iki upload'ın öncesi/sonrası NVS probe kapıları eklendi. Son tam masaüstü
**50 C/52 header/506 Python (25 dosya)**; 46 refusal, 92 negatif profil,
33 workflow/artifact geçti. Normal C/SDK/imaj değişmedi. İlk boş SAFE arşivi
flash öncesi reddedildi; kullanıcı onaylı iki bootstrap komutu ve 30 kaydın iki
canlı retained kopyası sonrası tam paket **exit 0 / RESTORE PASS** tamamlandı:
PANIC **78.422 s**, normal restore **78.797 s**, sayaç **4→5→6→7** ve
FAULT/BAD_RESET geçmişi korundu. İki tam upload ilk denemede geçti, her biri
boyunca NVS byte hash'i aynı kaldı; arada app'in journal yazıları beklendiği gibi
özeti değiştirdi. Her fazda yeni sağlık/yer onayı→SAFE→ayrı NOMINAL ve beşer
mission örneği; stack min **3668 bayt**. Son boot7/held0/ready1/stable1/history34,
YAMCS üç HK/yedi birim/29 INFO/TC278→278 PASS. Geçmiş veya arşiv silinmedi,
ham NVS kaydedilmedi. Önceki aralıklı aktarım/kayıp kök nedeni ve diğer kalıcılık/
boot-loop/bozuk kayıt/repair yolları hâlâ açık; bu tek koşu bunları kapatmaz.
[Faz kanıtları ve kesin sınırlar](recovery_target.md).

Ardından ayrı kullanıcı onayıyla gerçek üç ardışık HK caller abort/SDK PANIC
koşusu **79.610 s**, normal restore **79.734 s / exit 0/RESTORE PASS** geçti.
Baseline7/history34→boot8/9/10/11, failed0/1/2/3 ve flags35/35/35/99 doğrulandı.
Son normal boot12/failed0/held1/flags99 yeniden sağlık ve ayrı yer onayı istedi;
son passive held0/ready1/stable1/history98. BOOT_LOOP bayrağı silinmedi. Güncel
30 kayıt iki tam canlı retained export + iki ek yerel kopyada korundu; her iki
upload boyunca NVS byte hash'i aynı kaldı. Son normal imaj **6B8D11…** yeniden
derlendi; üretim politika/SDK değişmedi. Tam masaüstü **51 C/52 header/522 Python
(26 dosya)**, 48 refusal, 97 negatif profil, 39 workflow/artifact geçti.
Bozuk NVS/diğer reset türleri/yerinde repair bu sonuçla kapanmaz.
[BOOT_LOOP kart kanıtı ve sınırlar](recovery_boot_loop_target.md).
