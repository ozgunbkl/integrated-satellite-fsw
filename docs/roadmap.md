# Aktif roadmap

Güncelleme: 2026-10-18. Bu belge, mevcut kapsamı ve açık işleri takip eder.
Eski plandaki aşama numaraları yalnız referanstır; o numaraya ait bütün işlerin
tamamlandığını veya önceki aşamaların kapandığını göstermez.

## 2026-10-18 güncel: NOMINAL saatlik test ve değerlendirme PASS

17 Ekim D36E80… imajda **3600.2 s /120 rapor /720 NOMINAL örneği PASS**;
yeni archive-full/DEGRADED/reset marker0. Heap150108–150500, ilk-son fark0,
en düşük lifetime boş stack864 bayt.867 export çifti/4286 kayıt18 Ekim'de
yeniden CRC/manifest/hash ile doğrulandı:3554 sentetik64-byte payload +732 HK.
Sayılar tüm üretim/drain/kapanış aralığına aittir; fotoğraf veya mod sayısı değil.
STOP/SAFE, tail EMPTY ve boot14/history98/ready1 korundu. Kanıtlar `.pio/` dışında
saklandı. [Sonuç ve ilk denemenin korunan FAIL'i](nominal_soak_target.md).

Seçilen uzun çalışma artık açık iş değil. Nihai README ve özel ayarlar/eski build
çıktıları olmadan389 dosyalık yerel kaynak snapshot build/test kontrolü de geçti:
52 C/53 header/535 Python+normal metadata/ELF ve native16 komutlu MDB PASS.
Uzun-path PowerShell hata metni formatına bağlı mevcut test düzeltildi; firmware
mantığına/yeni kart senaryosuna dokunulmadı. Teknik kapanış tamamlandı.
Platform sürümü test edilen6.12.0'a sabitlendi; firmware mantığı değiştirilmedi,
yeni kart/reset/test senaryosu yok. Bağımsız temiz makine/remote clone iddiası yok.

Güncel kaynaklarda eşleşen gerçek Wi-Fi credential literal'ı yok; **eski commit'te
var**. Parola rotasyonu kullanıcıya aittir, doğrulanmadı; router/geçmiş otomatik
değiştirilmez.18 Ekim'de kullanıcı normal GitHub commit/push için açık onay verdi;
force-push/geçmiş temizliği veya yeni kart işlemi yok. [Yayın kontrolü](release_preflight.md).

## Önceki kontrol noktası: NOMINAL uygulaması ve tek tekrar

D36E80… normal imaj yüklendi; iki canlı RAM kopyası ve NVS byte eşitliği korundu.
Boot14/history98, sağlık→ayrı yer onayı→SAFE→ayrı NOMINAL90.984 s PASS. İlk aktif
başlangıçta yavaş tüm-paket sorgusu arşivin dolmasına yol açtı; payload ERROR ve
DEGRADED sonrası SAFE'e alındı. Bu deneme FAIL; **saatlik NOMINAL PASS yok**.
30 yeni kayıt çift doğrulanmış dosyada, doluyken reddedilen yazımlar ayrıca
başarılı sayılmaz. Kaynak sorgusu yalnız transfer indeksine daraltıldı; consumer
önce açılır,45 s hesabı gözlem başlangıcına bağlanır.535 Python/27 dosya ve native
16 komutlu MDB geçti. Aynı boot'ta yerel payload RESET + guarded NOMINAL içeren
tek tekrar kullanıcı onayıyla21:37:16'da başlatıldı; yeni ESP reset/upload yok.
Yerel toparlanma/guarded NOMINAL ve ADCS/payload başlangıcı geçti. Saatlik gözlem
21:38:33'te başladı;21:39:48 kontrolünde3 rapor/14 NOMINAL örneği, yeni archive-full0
ve DEGRADED0. Gözlem/kapanış yaklaşık22:39'da; **henüz sonuç bekleniyor**.
[Uygulama, başarısızlık ve takip dosyaları](nominal_soak_target.md).

Sonuç değerlendirmesi → nihai README/temiz kurulum/yayın güvenliği → açık kullanıcı
onayıyla GitHub en son. Yeni hata senaryosu açılmadı; eski upload riski açık.

## Önceki kontrol noktası: aktif NOMINAL koşusu hazırlığı

Kullanıcı SAFE sonucu sonrasında bir saat NOMINAL yükünü de istedi ve hazırlığı
onayladı. Bounded EPS enerji modeli, gerçek payload rate sınırı, iki doğrulanmış
dosyadan sonra exact-prefix CONFIRM yapan ground drain ve mevcut handler'lar için
ADCS/payload XTCE komutları hazırlandı. **52 C/53 header/533 Python (27 dosya)**
ve normal build/ELF; native Yamcs15 komutlu profil doğrulaması geçti. Yeni normal
disk imajı **D36E80…**, RAM42972/flash816129; SDK aynı.
[Kod, varsayımlar, testler ve kart fazı](nominal_soak_preparation.md).

Henüz yeni upload/reset, gerçek drain/CONFIRM veya ground MDB kurulum/restart
yapılmadı. Kartta en son C0719A… normal imaj var; yeni NOMINAL saatlik koşu
başlamadı. RAM/NVS korumalı yükleme ve yedekli ground güncellemesi sonraki
onaylı uygulama adımıdır. Eski SAFE sonucu yeni imajın uzun test kanıtı değildir.
Yeni fault-injection senaryosu/profil açılmadı; GitHub en son kalır.

## Önceki kontrol noktası: bir saatlik SAFE kaynak gözlemi PASS

19:33:34–20:33:36 Türkiye saati, C0719A… normal imajda **3600 s / 120 tam rapor**;
720 SAFE örneği, NOMINAL0. Mod zorlanmadı; upload/reset/TC/silme yok. Son pasif
kontrolde boot13/history98 korundu, held0/storage1/stable1, ready1. Yeni ciddi
hata/reset veya ölçüm sınırı ihlali görülmedi. Heap 150020–150576 bayt; son 30 dakika
150272 baytta sabit, en düşük lifetime boş stack YLOG_TX: 868 bayt. Araç/imaj/SDK
hash'leri aynı; 120 örnek/119 aralık/14 rol ve yedi ilerleyen heartbeat incelendi.
[Kabul ölçütleri, sonuç ve SAFE sınırı](long_run_target.md).

Bu seçilen uzun kaynak gözlemi ve değerlendirmesi tamamlandı; tüm alt sistemlerin
bir saat NOMINAL görev yükü veya eski aktarım hatasının kök nedeni çözümü değildir.
Yeni senaryo eklenmiyor. Sırada nihai README, temiz kurulum/yayın güvenliği ve
kullanıcı onayıyla en son GitHub var; henüz yayın yapılmadı.

## Önceki kontrol noktası: bakım düzeltmesi ve normal yükleme PASS

Mevcut açık WIPE bakım yolu tamamlandı: doğrulanmış boş arşiv sonrası yalnız
eski nonfatal CRC revision'ı çözülebilir; diğer nedenler/SAFE/geçmiş korunur.
Kartta WIPE veya yeni hata senaryosu yok. Tam 51 C/52 header/522 Python ve normal
build/ELF geçti; ROM reddiyle değişen 15/39 runner kontrolü tekrar geçti.
Normal C0719A… imaj, 30 kaydın iki canlı kopyası korunduktan sonra yüklendi;
NVS byte eşitliği ve boot12→13/history98 korundu. Yeni sağlık→yer onayı→SAFE→
ayrı NOMINAL, üç mission örneği: **91.015 s PASS**. YAMCS üç HK/yedi birim/29 INFO,
duplicate0, GET-only TC414→414. [Kanıt ve sınırlar](maintenance_closeout.md).

Eski aktarım/geçmiş kaybı logları incelendi; ROM yanıtı USB kopması kanıtı değil.
Koruma gerektiren runner'da no-stub yolu kapatıldı. **Kök neden çözülmüş değildir**;
yeni başarılı upload eski bulguyu kapatmaz. Yeni senaryo açmadan kalan sıra:
1 saat uzun çalışma → değerlendirme → nihai README/temiz kurulum/güvenlik →
kullanıcı onayıyla en son GitHub. Yayın değerlendirmesinde eski risk korunur.

## Önceki kontrol noktası: seçilen BOOT_LOOP paketi de PASS

**Son kapsam kararı:** kullanıcı daha fazla senaryo istemiyor. Watchdog'un
planlanan işleri tamamlandı; geçmişteki geniş init/watchdog/reset matrisleri
yeni zorunlu senaryo kuyruğu olarak sürdürülmeyecek. Ölçülmeyen varyasyonlar
bilinen sınırdır, PASS iddiası değildir. Arşiv CRC için ayrı karantina/salvage
yerine mevcut açık-WIPE bakım yolu tamamlandı; gerçek kartta veri silme izni
verilmedi. Somut bakım düzeltmesi ve normal imaj kontrolü yukarıdadır; kalan
eski aktarım riski, uzun test ve yayın hazırlığı ayrı tutulur.
[Sabit kapanış listesi](release_checklist.md#sabit-kapanış-listesi).

Kullanıcının ayrı onayıyla üç HK caller false/gerçek abort ve SDK PANIC=4 yolu
gerçek kartta doğrulandı. Baseline **boot7/history34**, ardından **8→9→10→11**,
failed_boots **0→1→2→3**, flags **35→35→35→99**; dördüncü boot'ta enjeksiyon
durdu, BOOT_LOOP/SAFE korundu. Seçilen faz **79.610 s**, normal restore **79.734 s**,
**exit 0 / RESTORE PASS**. Normal boot12 de yeni sağlık ve yer onayı istedi;
her fazda ayrı onay→SAFE→ayrı NOMINAL ve beşer mission örneği, stack min 3668 bayt.
Güncel 30 kayıt iki tam canlı export + iki ek yerel kopyada; NVS erase/bozma yok.
İki tam upload ilk denemede geçti ve her upload boyunca NVS hash'i korundu.

Son normal imaj **6B8D11…**, boot12/held0/ready1/stable1/history98/storage1;
GET-only üç HK/yedi birim/29 INFO olay/duplicate0/**TC349→349 PASS**.
Üretim recovery/MODE davranışı ve SDK ayarları değişmedi; normal imaj yeniden
derlendi, test hook'u içermediği ELF/metadata ile doğrulandı. Tam masaüstü
**51 C / 52 header / 522 Python (26 dosya)**, 22 graf/layout, 48 erken-ret,
39 workflow/artifact; ayrıca 97 negatif profil geçti.
[Yeni BOOT_LOOP kanıtı ve kesin sınırlar](recovery_boot_loop_target.md).

Bu seçilen ardışık-failure yolu kapandı. Eski aralıklı aktarım/geçmiş kaybı kök
nedeni, bozuk kayıt/diğer reset yolları, yerinde repair ve watchdog sınırlarının
kapanışı açık. Uzun test, sonuç değerlendirmesi, nihai README/temiz klon ve
**en son GitHub** sırası sürer; uzun test/yayın başlatılmadı.

## Önceki 2026-10-17 kontrol noktası: seçilen tek-PANIC tam paket PASS

Geçmiş farkında runner ve her upload'ın öncesi/sonrasındaki NVS probe kapıları
tamamlandı. Son tam masaüstü **50 C / 52 header / 506 Python (25 dosya)**,
22 graf/layout, 46 erken-ret, 92 negatif profil ve 33 workflow/artifact geçti.
Boş SAFE arşivi ilk denemede reset/flash öncesi doğru biçimde reddedildi; ayrı
iki-bootstrap-TC onayından sonra 30 kayıt üretildi ve iki tam retained export
ile korundu. Yeni gerçek paket **exit 0 / RESTORE PASS**: PANIC fazı **78.422 s**,
normal restore **78.797 s**, sayaç **4→5→6→7**, eski FAULT ve yeni BAD_RESET
geçmişi korundu. İki tam upload ilk denemede geçti; her upload boyunca NVS byte
hash'i değişmedi. Her iki fazda yeni sağlık/yer onayı→SAFE→ayrı NOMINAL ve beşer
mission örneği; recovery stack min **3668 bayt**. Onaylı toplam 10 recovery/mod
TC sınırı korundu; erase veya arşiv silme yapılmadı.

Kart normal **153DF1…** imajda; son passive boot7/held0/ready1/history34/storage1,
GET-only YAMCS kontrolü üç HK/yedi birim/**29 INFO olay**, duplicate=0 ve
**TC278→278 PASS**. [Fazlar, hash'ler, önceki FAIL'ler ve sınırlar](recovery_target.md).
Bu koşuda eski kesilme/geçmiş kaybı tekrarlanmadı; **kök nedenler kapanmadı**.
Diğer kalıcılık/boot-loop/bozuk kayıt, yerinde repair ve watchdog kapsam kapanışı
açık; henüz yalnız uzun test kalmış değil. Sonra uzun test/değerlendirme/nihai
README/temiz klon ve **en son GitHub**; uzun test/yayın başlatılmadı.

## Önceki 2026-10-17 kontrol noktası: kısa recovery kart koşusu kısmi

Son ek teşhis: güncel 30 kayıt iki yeni export ile korundu. Yeni salt-okunur loader
probe'u gerçek tablo/MAC ve çift NVS okumasını doğruladı; yalnız SHA256'lar raporlandı,
ham Wi-Fi/NVS kaydedilmedi. Normal imajın partition/bootloader/OTA/app parçaları
460800/stub ile **tek tek** yazıldı; her adımda NVS byte özeti aynı kaldı, sayaç
2→3 ve eski FAULT geçmişi korundu. Bu koşuda önceki kesilme/kayıp tekrarlanmadı;
**kök nedenler kapanmadı**. Dar PANIC reader eski FAULT geçmişini reddetti (FAIL
korunuyor); sonra ayrı kullanıcı onayıyla reset yapmadan iki TC/COMPLETE→SAFE→
ayrı NOMINAL ve üç örnek **64.391 s PASS**, stack min 3672 bayt. Kart normal 153DF1…
imajda. Son tam masaüstü **50 C/52 header/492 Python (25 dosya)**; 45 refusal,
92 negatif profil, 20 workflow/artifact. Runner varsayılanı başarılı normal yolun
460800/stub seçimiyle eşitlendi; bu bir seri kök neden düzeltmesi değildir.
Yeni probe henüz otomatik fazlara bağlanmadı; seçilen tam PANIC/restore oracle'ının
mevcut geçmişe uygun/açık onaylı hazırlanması ve doğrulanması açık.
[Ek kanıt, sınırlar ve eski FAIL kayıtları](recovery_target.md).

Aşağıdaki aynı gün önceki kontrol noktası kendi kısmi sonuçlarıyla korunur:

Yeni 11 komutlu MDB mevcut ground source/runtime'a yedekli kuruldu; instance reload,
native kontrol ve canlı preview geçti. Orijinal 30 RAM kaydı iki tam retained
export/read-back ve ek yerel kopyalarla korundu. Gerçek HK false→PANIC=4→SAFE,
sağlık/stale-token/ayrı yer-onayı→SAFE→ayrı NOMINAL fazı **79.391 s PASS**.

**Bütün paket FAIL/açık:** test ve restore yüklemelerinde tekrarlayan seri yanıt
kesilmeleri var; farklı baud/ROM denemeleri çözüm kanıtı olmadı. Tam upload sonrası
kalıcı sayaç/BAD_RESET geçmişi kaybı da gözlendi; kök neden ayrılmadı. Sonunda normal
153DF1… imaj standart PIO ile yüklendi; ayrı fallback **47.734 s**, altı NOMINAL
örneği ve recovery stack min **3676 bayt** geçti. Son canlı GET-only kontrolde üç
HK/yedi birim/31 INFO olay, duplicate=0, TC132→132. Normalin çalışması yükleme
boyunca kalıcılık testini PASS'e çevirmiyor. [Ayrıntı ve açık iki sorun](recovery_target.md).

Son tam masaüstü **50 C / 52 header / 477 Python (24 dosya)**, 22 graf/layout,
45 refusal, 92 negatif profil ve 20 workflow/artifact kontrolü geçti. Üretim
firmware/SDK/ağ ayarları bu fiziksel koşuda değişmedi; okuyucu yerel iki demo TC'sini
ayrı ilişkilendirecek şekilde düzeltildi. Önce bu aktarım/kalıcılık sorunları ve
kalan kısa kapsam kapanışı, sonra uzun test/değerlendirme/nihai README/temiz klon/
**en son GitHub**. Uzun test ve yayın başlatılmadı; yalnız uzun test kalmış değil.

## Önceki 2026-10-17 kontrol noktası: kalıcı recovery uygulaması/hazırlığı

Kullanıcı onayıyla kötü reset sonrası SAFE çalışma kilidi, üç başarısız açılış
takibi, 48 bayt CRC'li/bounded NVS özeti ve ayrı `FDIR_RearmBoot` komutu kodlandı.
Geçmiş silinmez: ciddi olay sonrası her boot yeni sağlık penceresi/yer onayı ister;
onay yalnız kilidi açar, SAFE'i NOMINAL'e çevirmez. Fatal/uncertainty ve bozuk
arşiv otomatik temizlenmez. [Politika, uygulama ve kanıt](recovery_policy.md).

Tam **50 C / 52 header / 425 Python (22 dosya)**, 22 graf/layout, 34 runner-refusal
ve 92 negatif profil geçti. Yeni recovery grupları 47+12+31 mock süreçtir.
Normal build RAM 42964, flash 813525; 57 production C/16 component/63 header bağı,
taze ELF'de test hook'u yok. Native YAMCS 11 komutlu repo MDB'sini doğruladı.
**Yeni imaj karta yüklenmedi, MDB ground'a kurulmadı, gerçek NVS test edilmedi.**
Kartta son fiziksel restore'u geçen C7F16A… normal imajı var; önceki fiziksel
sonuçlar bu yeni davranışın kanıtı değildir. Eski reset runner'ı yeni politikayı
bilmediğinden artık build/flash öncesi durur. Önce persistent-state-aware runner,
sonra ayrı arşiv/NVS/reset onaylı kısa kart testleri gerekir. Genel yerinde domain
repair ve watchdog kapsam kapanışı ayrıca açıktır; henüz yalnız soak kalmadı.
Uzun test/son değerlendirme/nihai README/temiz klon/**en son GitHub** sırası değişmedi.

Aynı gün sonraki hazırlıkta [kalıcı-durum farkında kısa target paketi](recovery_target.md)
tamamlandı: iki tam canlı retained export kapısı, ayrı onay/yanıt/history takibi,
PANIC sonrası ve normal restore sonrası yeni SAFE kilidi; hash-bound imajlar ve
finally restore. **Fiziksel koşu hâlâ yapılmadı.** Tam 50 C/52 header/473 Python
(24 dosya), 45 erken-ret ve 92 profil geçti; son araç değişikliğinde yeni 48 Python/
19 workflow ve gerçek ELF/imaj ayrımı ayrıca tekrarlandı. İki imaj derlendi, SDK
config/normal firmware hash'i değişmedi. NVS/seri port/canlı API/ground kurulumu
ve GitHub işlemi yok. Kart testi, diğer recovery yolları ve uzun test açık.

## Kapsam

Hedef, ESP32 üzerinde çalışan, test edilebilir bir CubeSat FSW geliştirme ve
simülasyon platformudur. Flight-qualified veya uçuşa hazır sistem iddiası yoktur.

Gerçek sensör entegrasyonu ve embedded AI, kullanıcının isteğiyle şimdilik aktif
roadmap'ten çıkarılmıştır. Bunlar bekleyen zorunlu işler değildir; ancak kullanıcı
yeniden isterse ayrı kapsam olarak ele alınır. Mevcut simülasyonlar ve platform
arayüzleri korunur. STM32'ye fiilen taşıma da şu an yapılacak bir iş değildir.

## Doğrulanmış kazanımlar

- [x] ESP32 firmware derlemesi, yükleme ve başlangıç gözlemleri yapıldı.
- [x] Ortak sistem modu servisi ve başlangıç kaynak oluşturma kontrolleri eklendi.
  Bu, bütün başlangıç/mod senaryolarının tamamlandığı anlamına gelmez.
- [x] TC boy sınırları, iç CRC kontrolü ve router'a CRC'siz komut verisi aktarımı test edildi.
- [x] Arşiv indeks/uzunluk sınırları ve gerçek CRC16 hesabı test edildi.
- [x] YAMCS göndericisi mutex ile korundu; eşzamanlı gönderim ve hata yolları host'ta test edildi.
- [x] 51 C host grubu (recovery politika/port/app mock'larında 47+12+31 ve BOOT_LOOP enjeksiyonunda 16; kaynak izleme, arşiv wire/aktarim/port/eşzamanlılık/logger, TC ingress, HK servis/görev, saf ADCS math/SIM, demo radyo/mission sözleşmesi/mock HAL dahil; lifecycle driver mock'unda 25, startup test hook'larında 5+15+15, watchdog test hook'larında 11 + 8 + 4×10 + 15 global + 17 recovery, watchdog'da 132,
  ESP32 watchdog port'unda 13, aktif FDIR'de 127, FDIR warning'de 74, deployment'ta 212,
  alt servis güvenliğinde 183, EPS yüklerinde 316, güç/mod grubunda 78, core başlangıcında 58,
  Wi-Fi servisinde 25 senaryo), 52 bağımsız header, 8 graf + 14 layout PowerShell kontrolü geçti.
  26 dosyadaki 522 Python, 34 eski + 14 yeni runner-refusal ve 59 watchdog + 21 startup + 17 lifecycle negatif profil güncel masaüstü koşusunda
  geçti. Tam C/header/native kontrolleri önceki wire adımında da yapılmıştı.
  Son FDIR stack düzeltmesinde yalnız
  ilgili iki C grubu ve tüm Python tekrar geçti. Native YAMCS MDB
  kontrolü de eklendi. Wire adımı tam host regresyonunu tekrar geçirdi;
  güncel repo ve kurulu ground kaynağı native YAMCS kontrolünden ayrıca geçti.
  Ortak Wi-Fi kart testi ve normal firmware'e dönüş daha önce tamamlandı.
  Son fiziksel arşiv adımında tüm Python + normal ELF/metadata tekrar geçti;
  C/header/native/build, kaynakları değişmediği için tekrar çalıştırılmadı.
  Son HK birim/olay adımında tüm Python, native YAMCS/preprocessor, metadata ve
  negatif profil guard'ları tekrar geçti; C/header tam regresyonu/build yok.
  Ardından kaynak ölçüm adımında tam 37 C/47 header/292 Python ve normal build,
  metadata/ELF, native YAMCS ve negatif profil kontrolleri geçti.
  Son CPU0 watchdog paketinin masaüstü adımında 39 C/47 header/312 Python,
  iki yeni hook grubunun tekrar kontrolü, 30 negatif profil, normal build ve
  güncel ELF/metadata/component kontrolleri geçti. Kartta iki gerçek reset nedeni,
  ikinci boot mission ilerlemesi ve normal restore de doğrulandı.
  CPU1 hazırlığında tam 41 C/47 header/324 Python, 36 negatif compile guard ve
  5 runner-refusal kontrolü geçti. Kullanıcının tercihiyle target build/upload/
  reset yapılmadı; normal ELF/metadata bu yeni kaynaklarla yeniden doğrulanmadı.
  Ardından ayrı izinle CPU1'in normal ve iki test imajı derlendi; güncel normal
  ELF/metadata/component ve 36 guard geçti. 30 kayıt disk export ile korundu;
  CPU1 iki gerçek reset nedeni 6/5, ikinci boot mission ilerlemesi ve normal
  restore doğrulandı. Paket exit 0 / RESTORE PASS, son normal gözlem 95.8 s.
  Ardından global pakette 42 C/47 header/341 Python, 9 runner güvenlik ve 47
  compile guard geçti. İlk okuyucunun üç failed subscriber beklentisi SDK tur
  bayraklarıyla uyuşmadı; koşu başarısız sayılıp normal restore edildi. Firmware
  değişmeden okuyucuya iki CPU'nun worker snapshot'ı zorunluluğu eklendi;
  346 Python tekrar geçti. Yeni 30 kayıt korunup tam paket tekrarlandı: gerçek
  TASK_WDT reason 6 / ikinci boot mission / normal restore ve exit 0 doğrulandı.
  Sonraki kaynak-helper tekrar giriş ve watchdog liveness-recovery adımlarında
  normal build ve tam 42 C/47 header/346 Python/47 profil kontrolleri tekrar geçti.
  Watchdog'da 54 recovery, FDIR'de 10 zero-wait API senaryosu eklendi. Bu yeni
  davranışlar o ilk adımda karta yüklenmedi. Ardından recovery hook/reader ile
  43 C/47 header/372 Python, 59 profil ve 15 runner güvenlik kontrolü geçti.
  Güncel RAM arşivi her tekrar öncesi korunup gerçek iki C200/revision 1–2,
  stable-clear/SAFE ve ayrı yer komutları doğrulandı. İlk iki reader-oracle ve
  üçüncü aktarım hatası başarısız koşu olarak tutuldu; düşük hızlı tam paket
  exit 0 / normal restore 95.8 s ile geçti. Recovery paketinin normal imajı kartta.
  Ardından HK required-init sessiz hatası kapatıldı: bool allocation/acquire/release
  sonucu ve core fail-stop; 5 HK + 2 app_main süreç senaryosu eklendi. Tam host,
  normal build/metadata/ELF/59 profil tekrar geçti. RAM 42884, flash 809293 (+80).
  Bu son HK değişikliği henüz karta yüklenmedi; önceki per-boot RAM korunuyor.
  Sonraki kısmi-task başlangıç modelinde 25 yeni süreç (toplam core 58), aynı
  metadata'lı const görev tablosu ve `required_created=N/12` tanısı eklendi.
  Tam host/build/metadata/ELF/59 profil tekrar geçti; RAM/flash 42884/809293 aynı.
  Bu yeni startup imajı da yüklenmedi; gerçek RTOS partial-startup/reset açık.
  Ardından 12 bütünleşik boot/EPS/FDIR/ADCS/payload lifecycle süreci eklendi
  (alt servis toplam 183). Pre-init fault floor'unda yanlış INITIAL_BOOT basan
  diagnostic düzeltildi; koruma kararı zaten doğruydu. Tam host/build/ELF/metadata/
  59 profil geçti. RAM 42884, flash 809289; SDK config aynı; bu imaj yüklenmedi.
  Son uzun-koşu okuyucu hazırlığında yalnız bilgisayar aracı değişti: 18 ek test,
  37 kaynak okuyucu / toplam 390 Python geçti. 1 saat sahte saatle sınandı;
  C/firmware değişmedi, build veya fiziksel uzun koşu tekrar yapılmadı.
  Ardından üç opt-in startup return-code profili, SDK'nin gerçek inlined create
  çağrı noktasında wrapper, abort/PANIC/ikinci boot okuyucusu ve mevcut restore
  runner'ına ayrı seçim eklendi. Tam 46 C/48 header/408 Python, 22 graf/layout,
  22 refusal/75 negatif profil geçti; normal metadata'da 53 production C kaynağı.
  Sonra ayrı kullanıcı onayıyla üç gerçek startup hata/abort/PANIC=4/ikinci boot
  testi geçti. 30 kayıt aynı hash'li taze export ile korundu. Normal baseline
  95.8 s ve son normal restore 95.7 s / exit 0; kartta güncel normal imaj var.
  İlk restore %77'de seri aktarım hatası verdi, PIO fallback geçti; kök neden
  çözülmüş sayılmaz. Restore sonrası üç HK/yedi birim/28 olay ve TC 402→402 geçti.
  RAM 42884, flash 809269, SDK config aynı. [Kanıt ve sınırlar](startup_target_tests.md).
  Sonraki adımda seçilen EPS/FDIR/ADCS/payload kart zincirinin izole test imajı,
  üç allowlisted ground isteği ve finally-normal-restore paketi hazırlandı.
  Test yalnız simüle bus girdisini değiştirir; MODE/owner-clear üretim politikası
  değişmedi. **Fiziksel koşu henüz yapılmadı; kart maddesi açık.**
  47 C/49 header/424 Python, 32 refusal/92 negatif profil, normal/test build ve
  güncel normal metadata/ELF ayrımı geçti; 54 C kaynağı tek IDF yolunda. Kart
  resetlenmedi, önceki RAM ve normal startup imajı yerinde kaldı.
  [Hazırlık, ayrı onay ve sınırlar](mission_lifecycle_target.md).
  17 Ekim'de ayrı onayla 30 güncel RAM kaydı iki tam export/read-back ile korundu;
  seçilen gerçek EPS/FDIR/ADCS/payload zinciri 24.0 s / tek boot içinde geçti.
  Tek E102/revision 1 ve owner-clear, SAFE'in korunması, iki reddedilen/bir kabul
  edilen gerçek NOMINAL TC, açık servis rearm'ı, 64→128 byte ve eski kayıt CRC/
  retention doğrulandı. Driver stack minimumu 956 byte. Normal baseline 95.8 s ve
  restore 95.7 s / 15'er NOMINAL örneği, RESTORE PASS / exit 0; üç aktarım ilk
  denemede geçti. Son canlı HK/yedi birim/28 olay ve TC 67→67, native MDB ve güncel
  normal ELF kontrolü de geçti. Kod değişmedi; tam host tekrarlanmadı. Kartta
  normal C7F16A… imajı var. Genel recovery ve uzun test açık; GitHub yayını yok.
- [x] YAMCS → ESP32 → YAMCS tek kayıt okuması gerçek kartta doğrulandı; aynı kayıt
  iki kez alınarak payload CRC16'sı ve kaydın silinmediği kontrol edildi.

Son arşiv/komut doğrulamasının kapsamı ve sınırları [downlink belgesindedir](archive_downlink.md).
Bir okuma komutunun çalışması bütün komutların veya bütün sistemin doğrulandığı anlamına gelmez.

## Açık işleri kapatma sırası

[Tek sayfalık güncel kapanış listesi](release_checklist.md) kalan kart testlerini,
genel recovery kararlarını, uzun testi ve yayın işlerini ayrı gösterir.
Henüz yalnız uzun test kalmış değildir; [yerel demo rehberi](demo_walkthrough.md)
yayın/temiz klon doğrulaması yerine sayılmaz.

Kullanıcının kesinleştirdiği son sıra: **kalan uygulama/kısa kart testleri →
uzun çalışma → sonuç değerlendirmesi ve gerekirse düzeltme/tekrar → nihai README
ve temiz klon/yayın güvenliği kontrolleri → GitHub yayını**. GitHub en sondadır;
yerel taslak/araç hazırlığı yayın sayılmaz. Uzun koşunun kabulü önceki açık
işlerin veya sınanmamış hata yollarının yerine geçmez.

Güncel konum: **6. başlık, ESP32 ve çalışan YAMCS üzerinde sistem doğrulaması**.
TM/TC/arşiv uygulama + host/native kodlama test kapsamı kapandı; yeni firmware ve
runtime MDB eşleşmesi/açılış/canlı HK-event bağlantısı ve retained arşiv aktarımı
(ilk 17, FDIR düzeltmesinden sonra 27 kayıt) doğrulandı. 16 Ekim'de yeni boot'ta
24+6 kayıt ile CONFIRM/tail/receipt/CANCEL/resume ve kontrollü HTTP yanıt kaybı/retry
gerçek kart/YAMCS'te geçti. Ardından tüm HK alan/birim, bounded event ve kaynak
ölçüm alt adımları tamamlandı. Son kaynak koşusu 600 s SAFE yüküdür; ilk
NOMINAL/DEGRADED bölümü simüle EPS düşük gerilim bildirimiyle durdu ve başarı
sayılmadı. CPU0 ve CPU1 watchdog kart paketleri ayrı ayrı tamamlandı; ikisinde
de arşiv korunup normal firmware restore/gözlemi geçti. IRQ-açık priority-24
çift-worker global starvation koşusu da ayrı SDK CPU snapshot'ları/gerçek
TASK_WDT reseti ve normal restore ile kapandı. Genel recovery/diğer reset
yolları açık; uzun süreli çalışma kullanıcının isteğiyle akşama bırakıldı,
başlatılmadı. [Son kısa test kanıtı](watchdog_global_target.md).
Ardından kullanıcı seçimiyle >=30 s kararlı heartbeat sonrası yalnız ilgili
timeout revision'ını çözen owner politikası kod/host/normal build'de eklendi;
SAFE ve diğer fault/deployment kapıları korunur. Sonraki onaylı kart paketinde
iki gerçek timeout/clear, bir reddedilen/iki kabul edilen ayrı NOMINAL isteği ve
normal restore doğrulandı. [Recovery target kanıtı](watchdog_recovery_target.md)
yalnız bu bounded yolu kapatır; genel repair/fatal/uncertainty ve soak hâlâ açık.
Son startup kart paketinde de seçilen HK false-return / CMD_PROC 3/12 /
SOLAR_HARV 11/12 caller hataları, gerçek PANIC reseti ve ikinci boot ilerlemesi
kapandı. İlk restore aktarımı kesildi; fallback ve normal son kontrol geçti,
paket exit 0. Bu, genel init/HAL/servis recovery başlıklarını veya uzun testi
kendiliğinden kapatmaz. [Güncel kart kontrol noktası](startup_target_tests.md).
Arayüz/build maddeleri belirtilen proje/host/normal build
sınırlarında kapandı. Başlangıç/mod ve FDIR/watchdog'un
temel korumaları uygulanmış ve belirtilen test kapsamlarında doğrulanmıştır;
genel recovery ve geniş target matrisi açık kalır. Maddeler iç içe/örtüşen işler
içerdiği için checkbox sayısı ilerleme yüzdesi değildir.

Önceki FDIR stack taşması blokajı, 4096 bayt bütçe ve gerçek doluluk/uyarı yolunda
305.8 s tek-boot kontrolüyle kapatıldı; minimum boş stack 1636 bayt. Yeni 27 kayıtlık
pending oturum bu kontrolü geçip korundu. Ertesi gün yeni boot'ta arşiv işlem
testleri tamamlandı: 30 kayıt kopyaları doğrulandıktan sonra kontrollü silindi,
test sonunda RAM EMPTY/pending yok. [Güncel kanıt ve sınırlar](archive_target_tests.md).

Yeni özellik eklemek yerine önce önceki aşamalardaki açık temel işler kapatılacak.
Çalışan haberleşme hattı ve testler korunacak. Her madde küçük alt adımlara ayrılacak;
uygulama için kullanıcı onayı beklenecek. Testler yalnız en sondaki iş değil,
değişiklik yapılan her alt adımın parçasıdır.

### 1. Başlangıç ve sistem modu — eski aşamalar 1–2

- [x] RAM arşivini `app_main()` içinde, uygulama görevlerinden önce bir kez hazırla;
  Data Logger'daki reset çağrısını kaldır ve `main` için arşiv bağımlılığını açıkça tanımla.
  2026-10-12: derleme/yükleme, dört C ve 12 Python kontrolü geçti. Kart açılışında
  hazırlık mesajı TC PROC ve DATA LOGGER'dan önce gözlendi; YAMCS üzerinden yeni
  indeks 0 iki kez alındı (20 bayt, CRC16 `0x4F96`, TM sıra 0 → 1), kayıt korundu.
  Yükleme ve açıkça istenen açılış testi eski RAM kayıtlarını sıfırladı.
- [x] `HK_Init()` ve `Payload_Init()` çağrılarını arşivden sonra, uygulama görevlerinden
  önce ekle; `main` için `hk`/`payload` bağımlılıklarını açıkça bildir. Init davranışları
  değiştirilmedi; payload otomatik çalıştırılmıyor. Yeni C host kontrolünde önceden
  doldurulmuş HK tablosunun temizliği ve payload telemetrisinin `OFF`/sıfır durumuna
  dönmesi doğrulandı. Bu, HK limit/rapor zamanlaması veya runtime recovery doğrulaması değildir.
  2026-10-12: beş C ve 12 Python kontrolü, ESP32 derleme/yükleme ve `--services`
  açılış kontrolü geçti. Hazırlık bir kez TC PROC, DATA LOGGER ve TM Generator'dan
  önce gözlendi; gerçek payload durumu `OFF` (`0`), ardından NOMINAL döngüsü görüldü.
  YAMCS üzerinden yeni indeks 0 iki kez alındı (20 bayt, CRC16 `0x01DB`, TM sıra
  0 → 1); kayıt korundu. Yükleme/reset eski RAM kayıtlarını sildi; geri alınamaz.
- [x] `ADCS_Init()` ve ilk simülasyon dönme ayarını `app_main()` içinde, uygulama
  görevlerinden önce hazırla; ADCS görevindeki tekrar çağrıları kaldır ve `main`
  için `adcs` bağımlılığını açıkça tanımla. Mevcut `DETUMBLE` başlangıcı, simüle
  actuator bayrağı ve dönme değerleri `(10, -5, 0.5)` korundu; matematik/mod kuralları değişmedi.
  Yeni C host kontrolü gerçek ADCS servis/görev/simülasyon/control/math kaynaklarıyla
  erken `NOMINAL` komutunun ilk görev döngüsünde korunduğunu, init/dönme ayarının
  tekrarlanmadığını ve watchdog/100 ms delay yolunu doğruladı; karta ADCS komutu gönderilmedi.
  2026-10-12: altı C ve 12 Python kontrolü, ESP32 derleme/yükleme ve `--adcs` açılış
  kontrolü geçti. ADCS bir kez `DETUMBLE` (`1`) olarak dört tüketici görevden önce
  hazırlandı; ardından NOMINAL döngüsü görüldü. YAMCS üzerinden indeks 0 iki kez
  alındı (20 bayt, CRC16 `0x4F96`, TM sıra 0 → 1); kayıt korundu. Yükleme/reset eski
  RAM kayıtlarını sildi; geri alınamaz. Bu, runtime eşzamanlılık, recovery veya global
  mod politikasının kapanışı değildir; mevcut iki unused-variable uyarısı açık kaldı.
- [x] İki zorunlu kuyruk ve EPS mutex oluşturmayı `main/startup_resources.c/.h`
  yardımcısına ayır; üçü de hazır olduktan sonra global handle'ları ata ve Wi-Fi/log
  başlat. Kapasiteler 10/5, eleman tipleri ve mutex türü korundu. Kaynak oluşturma
  hataları artık açık mesaj + `abort()` ile mevcut zorunlu görev politikasını izliyor;
  Wi-Fi sonuç/log init hatalarında devam dalları değiştirilmedi. SDK Wi-Fi init hata
  yollarının tamamının non-critical olduğu veya doğrulandığı iddia edilmez.
  2026-10-13: yeni C host kontrolünde üç başarısız tahsisatın her biri ayrı denendi;
  doğru kaynak mesajı/tek abort, sonraki tahsisat ve çağıran koda devamın olmaması
  doğrulandı. Başarılı handle'lar/kapasite/eleman boyları ve NULL çıktı koruması geçti.
  Yedi C ve 12 Python kontrolü, ESP32 derleme/yükleme ve `--resources` normal açılış
  kontrolü geçti; kaynak hazır mesajı Wi-Fi IP ve dört tüketici görevden önce gözlendi.
  Kapalı YAMCS mevcut ayarlarla tekrar başlatıldı; iki indeks 0 okuması aynı 20 bayt
  kaydı verdi (CRC16 `0xE6EF`, TM sıra 0 → 1). Kartta bellek-hatası enjekte edilmedi;
  target hata/panic/reboot senaryoları, retry/cleanup veya recovery bu adımda doğrulanmadı.
  Önceki tahsisatlar fatal hata anında panic/reset'e bırakılır; mevcut reboot ayarında
  kalıcı kaynak yetersizliği reboot döngüsü oluşturabilir. Yükleme/reset eski RAM
  kayıtlarını sildi; geri alınamaz. Derlemede mevcut unused-variable uyarıları kaldı.
- [x] Ağ/log başlangıç sonucunu çekirdek görev oluşturma sonucundan ayır; mesajlar
  artık `Network at startup`/`log mirror` durumlarını bildirir. Bunlar başlangıç
  anlık görüntüsüdür, sürekli link veya bütün sistemlerin sağlıklı olduğuna dair iddia değildir.
  `esp32dev` varsayılan kaldı; açık `esp32dev_wifi_timeout` test profili aynı SDK
  ayarları/partition/flags ile RAM'de geçici rastgele SSID/test parolası kullanır.
  2026-10-13: 7 C ve 22 Python kontrolü, normal/test derlemeleri geçti. Kartta gerçek
  yaklaşık 15 saniyelik bağlantı timeout'u, IP alınmaması, log mirror'ın atlanması,
  çekirdek görev mesajları ve en az 9 saniyeye yayılan 3 NOMINAL döngüsü gözlendi;
  bu kısa gözlemde reboot/panic/watchdog/queue hatası görülmedi. Betik normal
  firmware'i geri yükledi; bağlı açılışta network/log durumları `connected`/`started`
  ve IP `192.168.1.5` doğrulandı. Son iki YAMCS okumasında indeks 0 aynı 20 bayt
  kaydı verdi (CRC16 `0x60F8`, TM sıra 0 → 1); kayıt korundu. Ortak SDK config hash'i
  değişmedi. Gerçek Wi-Fi bilgileri/router/YAMCS/firewall ayarları değiştirilmedi;
  geçici bağlantı ayarları NVS'ye kaydedilmedi. Bu sıfır flash/NVS yazısı iddiası değildir.
  NVS/sürücü init hataları, uzun süreli offline/recovery ve geç bağlanınca log mirror
  başlatılması bu adımda düzeltilmedi/doğrulanmadı. Yükleme/resetler önceki RAM
  kayıtlarını sildi; geri alınamaz. Kartta test profili bırakılmadı.
- [x] Ağ altyapısı hazırlığını Wi-Fi bağlantısından ayır; varsayılan kapalı kontrolü
  yalnız `esp_netif_init()` başarılı döndükten sonra aç. Mevcut gönderici mutex'i
  hazırlık bilgisini de korur; ek kaynak tahsisi yoktur. Hazır değilken gerçek TM/olay
  göndericisi soket kullanmadan `false` döner, UDP TC görevi soket açmadan sonlanır.
  `cdh` için doğrudan `comms` bağımlılığı eklendi. Hazır durumu tek yönlü startup
  bilgisidir; Wi-Fi timeout/kopmasında temizlenmez. Atlanan TC görevi kendiliğinden
  yeniden başlatılmaz; runtime stack teardown/recovery bu adımın kapsamı değildir.
  2026-10-13: yeni C kontrolü gerçek sender/TC kaynaklarıyla kapalı durumda sıfır
  socket/bind/receive/send, hazırlık yayınlama/okuma hatalarının güvenli reddi ve
  hazır durumda IP kontrolü olmadan dinleme yolunu doğruladı. Sekiz C ve 25 Python
  kontrolü, normal/test firmware derlemeleri ve kart timeout testi geçti. IP yokken
  stack hazır kaldı, TC UDP 10025'i dinledi ve en az 9 saniyeye yayılan üç NOMINAL
  döngüsü görüldü; kısa gözlemde reboot/panic/watchdog/queue hatası yoktu. Normal
  `esp32dev` geri yüklendi; hazırlık mesajı IP ve görevlerden önce, ardından
  `connected`/`started`, NOMINAL ve IP `192.168.1.5` doğrulandı. YAMCS üzerinden
  indeks 0 iki kez aynı 20 bayt kaydı verdi (CRC16 `0x75D6`, TM sıra 0 → 1,
  TC sıra 1 → 2); kayıt korunuyor. Ortak SDK config hash'i değişmedi; gerçek Wi-Fi
  bilgileri/router/firewall/YAMCS kaynak ayarları değiştirilmedi. YAMCS kapalıydı,
  mevcut ayarlarla başlatılıp çalışır bırakıldı. Yükleme/reset eski RAM kayıtlarını
  sildi; geri alınamaz. Mevcut compiler uyarıları açık kaldı. Bu koruma NVS/sürücü/SDK
  init assert/abort yollarını kaldırmaz; fiziksel erken init hatası enjekte edilmedi.
- [x] NVS başlatmada otomatik tam bölüm silme/retry ve uygulama abort'unu kaldır.
  `STARTUP_InitNvs()` tek SDK çağrısından sonra sonucu aynen döndürür; hata adını
  ve Wi-Fi'nin atlandığını bildirir. NVS hatasında ağ/sürücü/arayüz başlatılmadan
  dönülür; soket hazırlığı kapalı kalır, log mirror atlanır ve temel görev oluşturma
  devam eder. TC kendini soket açmadan sonlandırır; gönderici `false` döner.
  Gerçek NVS'nin onarımı veya otomatik network retry/recovery eklenmedi.
  2026-10-13: yeni C host kontrolünde ESP_OK ve altı hata sonucu tek çağrı, sıfır
  erase/retry/abort ile doğrulandı; dokuz C ve 39 Python kontrolü, normal derleme
  geçti. Fiziksel hata profili gerçek init'i çağırmadan NO_FREE_PAGES taklit eder;
  açık erase/ikinci init denemesi test trap'idir. Gerçek NVS bilerek silinmez/bozulmaz.
  SDK iç recovery/normal Wi-Fi/PHY ve yüklemeler flash yazabilir; sıfır flash yazısı
  iddiası yoktur. Gerçek heap/flash arızası, SDK iç assert/abort, sürücü cleanup veya
  diğer init hata yolları bu değişiklikle doğrulanmış sayılmaz. Gerçek NO_MEM daha
  sonra zorunlu task tahsisinde fatal hata doğurabilir; o politika korunur.
  Normal/test derlemeleri ve karttaki taklit NO_FREE_PAGES testi geçti: ağ hazırlığı
  açılmadı, IP alınmadı, log mirror/TC soketi atlandı; en az 9 saniyeye yayılan üç
  NOMINAL döngüsü görüldü, kısa gözlemde reboot/panic/watchdog/queue hatası yoktu.
  Betik çıkış kodu 0 ile normal `esp32dev` geri yüklendi; `--network` başlangıç
  kontrolü, IP `192.168.1.5` ve `connected`/`started` doğrulandı. ELF sembol kontrolü
  normal firmware'de NVS test wrapper'larının olmadığını, test imajında init
  wrapper'ının bağlı olduğunu doğruladı. YAMCS'ten indeks 0 iki kez aynı 20 bayt
  kaydı verdi (CRC16 `0x96C1`, TM sıra 0 → 1, TC sıra 3 → 4); kayıt korunuyor.
  SDK config hash'i değişmedi; gerçek Wi-Fi bilgileri/router/firewall/YAMCS kaynak
  ayarları değiştirilmedi. Test gerçek NVS'yi bilerek silmedi/bozmadı; yükleme/reset
  eski RAM arşiv kayıtlarını sildi, geri alınamaz. Kartta normal firmware bırakıldı;
  mevcut compiler uyarıları açık kaldı. Sürüm uyumsuzluğu ve diğer NVS hata kodları
  host'ta test edildi; fiziksel NVS bozulması veya gerçek bellek yetersizliği enjekte edilmedi.
- [x] `esp_netif_init()` hata kodunda uygulama abort'u yerine Wi-Fi başlangıcını
  atla. `STARTUP_InitNetworkStack()` tek SDK çağrısı yapar; hata adını bildirir ve
  sonucu aynen döndürür. Soket hazırlığı yalnız ESP_OK sonrasında yayınlanır;
  hata halinde TC soketi/log mirror atlanır ve temel görev oluşturma devam eder.
  Başarıdaki hazır mesajı ve bu bilginin yayınlama hatasındaki önceki fatal politika
  korundu. Retry veya `esp_netif_deinit()` eklenmedi: SDK tam lwIP deinit desteklemiyor;
  içeride kısmi kaynaklar kalabilir ve hepsinin geri alındığı iddia edilmez.
  2026-10-13: mevcut network C kontrolü gerçek başlangıç yardımcısıyla genişletildi;
  dört SDK hata kodu, tek çağrı, sıfır deinit/soket erişimi, kapalı hazırlık ve doğru
  hata logu doğrulandı. Başarı hazırlığı açar; yayınlama hatasında mevcut abort yakalandı.
  Dokuz C ve 49 Python kontrolü, normal firmware derlemesi geçti. SDK iç assert/abort,
  gerçek heap arızası/kısmi kaynak tüketimi veya sonraki event-loop/Wi-Fi sürücü/arayüz
  hataları bu adımda çözülmüş/doğrulanmış sayılmaz. Hata profili gerçek SDK init'i
  çağırmadan ESP_FAIL taklit eder; retry/deinit/yanlış hazır yayını/event-loop denemeleri
  test trap'idir. Normal firmware'de bu wrapper'lar yoktur, test yalnız açık profil ile seçilir.
  Normal/test derlemeleri ve karttaki taklit ESP_FAIL testi geçti: hazır bilgisi
  açılmadı, IP alınmadı, TC/log mirror atlandı ve en az 9 saniyeye yayılan üç NOMINAL
  döngüsü görüldü; kısa gözlemde reboot/panic/watchdog/queue hatası yoktu. Betik
  çıkış kodu 0 ile normal `esp32dev` geri yüklendi; `--network` açılışı, IP
  `192.168.1.5` ve network/log `connected`/`started` doğrulandı. YAMCS'ten indeks 0
  iki kez aynı 20 bayt kaydı verdi (CRC16 `0xFAC8`, TM sıra 0 → 1, TC sıra 5 → 6);
  kayıt korundu. ELF kontrolünde wrapper'lar normal imajda yok, init/ready/event-loop
  guard'ları test imajında bağlıydı. Ortak SDK config hash'i değişmedi; gerçek Wi-Fi
  bilgileri/router/firewall/YAMCS kaynak ayarları değiştirilmedi. Yükleme/reset eski
  RAM kayıtlarını sildi; geri alınamaz. Kartta normal firmware bırakıldı; mevcut
  compiler uyarıları açık kaldı. Bu gözlem, gerçek SDK iç hata/kısmi kaynak recovery
  veya bellek yetersizliği doğrulaması değildir.
- [x] Wi-Fi default event-loop oluşturma hata kodunda uygulamayı durdurmak yerine
  arayüz/sürücü başlangıcını atla. `STARTUP_CreateWifiEventLoop()` tek SDK çağrısından
  sonra sonucu aynen döndürür ve hata adını bildirir. Hazır TCP/IP bilgisi korunur;
  temel görevler oluşturulur, log mirror atlanır, TC IP olmadan soket dinleyebilir.
  ESP_ERR_INVALID_STATE başarı sayılmaz; başka bileşenin mevcut loop'u sahiplenilmez,
  silinmez veya yeniden oluşturulmaz. Otomatik retry/delete eklenmedi. Başarıdaki
  yeni hazır mesajı interface/driver başlangıcından öncedir.
  2026-10-13: mevcut network C kontrolü ESP_OK ve dört event hata sonucuyla genişletildi;
  tek create, sıfır delete/retry/abort, doğru log ve hazır bilgisinin korunması doğrulandı.
  Dokuz C ve 61 Python kontrolü, normal firmware derlemesi geçti. Yeni hata profili
  gerçek NVS/TCP-IP başlatıp yalnız event-loop create sonucunu NO_MEM taklit eder;
  ikinci create/delete/interface/driver denemeleri test trap'idir. Gerçek SDK heap
  yetersizliği/partial cleanup, internal assert/abort veya sonraki sürücü/arayüz
  hataları çözülmüş/doğrulanmış sayılmaz. Normal imajda wrapper yoktur; test profili açıktır.
  Normal/test derlemeleri ve karttaki taklit NO_MEM event testi geçti: gerçek stack
  hazır kaldı, TC IP olmadan UDP 10025'i dinledi, Wi-Fi/log mirror atlandı ve en az
  9 saniyeye yayılan üç NOMINAL döngüsünde reboot/panic/watchdog/queue hatası yoktu.
  Betik çıkış kodu 0 ile normal `esp32dev` geri yüklendi; `--event-loop` kontrolünde
  yeni hazır mesajı stack'ten sonra, IP ve görevlerden önce bir kez geldi. IP
  `192.168.1.5`, network/log `connected`/`started` ve NOMINAL doğrulandı. YAMCS'ten
  indeks 0 iki kez aynı 20 bayt kayıt alındı (CRC16 `0x816C`, TM sıra 0 → 1,
  TC sıra 7 → 8); kayıt korundu. ELF'te create/interface/driver guard'ları yalnız
  test imajında bağlı, normal imajda hata wrapper'ları yoktu. Ortak SDK config hash'i
  değişmedi; gerçek Wi-Fi bilgileri/router/firewall/YAMCS kaynak ayarları değiştirilmedi.
  Kalıcı veri/gerçek heap arızası enjekte edilmedi; normal NVS/SDK ve upload flash
  yazabilir. Eski RAM kayıtları yükleme/resetle silindi; geri alınamaz. Kartta normal
  firmware bırakıldı. Mevcut compiler uyarıları açık; SDK iç cleanup veya gerçek
  bellek yetersizliği bu kısa gözlemle doğrulanmış sayılmaz.
- [x] Init bağımlılıkları ve critical/non-critical hata politikasının kapanış kontrol
  listesini [başlangıç belgesine](startup_policy.md) kaydet. Liste oluşturuldu;
  listedeki bütün uygulama/target açıklarının kapandığı anlamına gelmez.
- [x] Ortak servis başlangıcını `main/startup_core.c/.h` yardımcısına ayır;
  `app_main()` resources/network/task öncesi bir kez çağırır. TIME, INITIAL_BOOT,
  FDIR, ortak sender, Archive/HK/Payload/ADCS ve `(10,-5,0.5)` SIM sırası/defaultları,
  getter snapshot'ları ve readiness mesajları korunur. Üç mevcut fatal servis
  politikası değişmedi; tekrar giriş TIME/arşiv dahil hiçbir servise dokunmadan abort edilir.
  Main'in `time` component bağımlılığı açıkça bildirildi. 2026-10-14: gerçek main/core/
  resource kaynakları 24 host senaryosuyla test edildi: üç servis/üç resource hatası,
  11 task-create hatası, opsiyonel devam ve başarı/üç hata sonrası tekrar init reddi.
  13 C grubu, 79 Python kontrolü, normal firmware derlemesi geçti. RAM 42188/327680,
  flash 792953/1740800 bayt; SDK config değişmedi. Bu adımda upload/reset veya fiziksel
  panic/heap/task-failure testi yapılmadı; mevcut uyarılar korunmuştur.
- [x] Required resource helper'ında tekrar tahsisi engelle. 2026-10-16: ilk girişim
  boot boyunca kilitlenir; başarı, NULL veya tahsis hatası sonrası tekrar çağrı
  çıktı alanını değiştirmeden/yeni kaynak ayırmadan log + abort üretir. 12 kaynak
  ve 31 app_main senaryosu, ardından 42 C/47 header/346 Python ve bütün normal
  metadata/ELF/negatif profil kontrolleri geçti. Normal build RAM 42796, flash
  808501 bayt; SDK config aynı. Karta yükleme/reset yapılmadı; önceki normal
  imaj kartta. [Sözleşme ve sınırlar](startup_policy.md).
- [ ] Eksik init sırası, kaynak oluşturma hata yolu ve yanıltıcı başlangıç mesajlarını düzelt.
  Queue/EPS mutex ve HK required-init hata politikası alt adımları tamamlandı;
  HK init başarısızken payload/ADCS/resource/network/task ve "HK hazır" mesajı yok.
  Altı HK / 33 app_main süreci; tam 43 C/47 header/372 Python, build/ELF/metadata
  ve 59 profil geçti. İlk host adımında HK imajı karta yüklenmemişti; sonraki ayrı
  onaylı üç startup kart testi aşağıda kapandı. Genel başlangıç mesajları/kalan
  init/HAL hata yollarının üst kapsamı açık; seçilen iki task konumu bütün
  kernel/allocation hata yollarının fiziksel kanıtı değildir.
  Servis/resource/task hata sıraları gerçek app_main ile host'ta kapsandı;
  bu, target allocation/panic/recovery doğrulaması değildir.
- [x] Kısmi required-task başlangıcının host modelini genişlet ve hata konumunu
  açık raporla. Aynı metadata/sırayla tek const task tablosu; `required_created=N/12`
  yalnız başarılı required oluşturma sayısıdır. Yeni gate/cleanup/retry yok.
  Önceki stub görevlerin create dönmeden ve hata logunda ilerlemesi; 12 hata
  konumu + tekrar-init reddi + erken başarı için 25 yeni süreç (core toplam 58).
  Exact hata tanısı, canary/handle korunması, sonrakilere devam ve sahte core-ready
  yokluğu geçti. Tam 43 C/47 header/372 Python, build/ELF/metadata ve 59 profil
  tekrar geçti; RAM/flash aynı. Bu host adımında fiziksel partial-startup/panic
  yapılmadı; seçilen kart sonuçları sonraki maddede, üst genel init kapsamı
  ayrı kalır. [Model ve sınırlar](startup_policy.md#2026-10-16-kısmi-görev-başlangıcının-host-modeli).
- [x] Seçilen gerçek RTOS başlangıç hata/panic/ikinci boot yollarını doğrula:
  HK caller false, CMD_PROC 3/12 ve SOLAR_HARV 11/12; SDK PANIC=4 ve normal
  ikinci boot mission ilerlemesi geçti. 30 RAM kaydı üç aynı-hash'li disk
  export'unda korunur; karttaki eski boot/pending/fault durumu onaylı resetlerle
  kayboldu. Baseline 95.8 s, normal restore 95.7 s, exit 0 / RESTORE PASS.
  İlk restore %77'de chip stopped responding; PIO fallback 460800 baud/hash
  doğrulaması geçti, USB/seri kök nedeni hâlâ bilinmiyor. Güncel normal imaj
  kartta; GET-only üç HK/yedi birim/28 olay ve TC 402→402 doğrulandı. Genel
  init/HAL/full-heap/fatal recovery veya uzun soak iddiası değildir.
  [Fiziksel kapsam ve loglar](startup_target_tests.md).
- [x] Mevcut Wi-Fi başlangıç dönüş/hata yollarında core'a devam politikası ve sahipli
  cleanup uygula/doğrula. `main/wifi_service.c` abort eden default STA helper yerine
  checked netif/attach/default-handler çağrıları kullanır; group/driver/storage/handler/
  mode/config/start/manager-task sonuçları kontrol edilir. Mevcut arayüz sahiplenilmez;
  cleanup hatası primary error'u değiştirmez, belirsiz kaynak serbest bırakılmaz.
  TCP/IP/event-loop korunur; ready yayınlama hatası artık ESP_FAIL, abort değildir.
  25 Wi-Fi host senaryosu ve kartta taklit netif-allocation dönüş hatası geçti;
  IP/log yokken üç NOMINAL döngüsü görüldü. Gerçek heap çökmesi/SDK iç assert'leri,
  bütün zorunlu task tahsis hataları veya tüm kernel/radio arızaları doğrulanmış sayılmaz.
- [x] Kopma/IP kaybında canlı bağlı bilgisini kapat; 1..30 saniye backoff ve 20 saniye
  pending timeout ile reconnect yap. İlk IP geç gelse de tek log sender başlar;
  log allocation hatası 30 saniyede retry edilir. Offline/full loglar düşürülür.
- [x] UDP/YAMCS socket hata toparlanmasını ekle: sender 250 ms timeout ve hatada close;
  receiver 1 saniye receive timeout, setup retry ve fatal receive hatasında rebind.
  Host'ta timeout/EINTR/EIO, offline socket engeli ve eşzamanlı link değişimi geçti.
  UDP gönderimi ground ACK değildir; yeni güvenilir downlink/otomatik silme eklenmedi.
- [x] Gerçek Wi-Fi credential'larını sürüm kontrolüne girecek kaynaklardan çıkar;
  ignored `local/wifi_credentials.h`, boş config ile offline core ve değerleri
  yazdırmayan sızıntı kontrolünü ekle. Config her profilde RAM storage kullanır.
  Eski HEAD credential içerir; geçmiş/remote değiştirilmedi. Kullanıcı parolayı
  değiştirmelidir; binary/yerel header/eski NVS şifreli veya temizlenmiş sayılmaz.
- [x] Ortak fiziksel Wi-Fi testi: 2026-10-13, 600 saniye; gerçek geç IP/log start,
  üç gerçek disconnect/reconnect, dört IP acquisition, 116 core döngüsü (58 NOMINAL,
  ayrıca INITIAL_BOOT/SAFE bekleme), reboot/panic/watchdog/queue hatası yok.
  Free heap 164600..215500 bayt, minimum heap 160796, manager minimum stack headroom
  1616 bayt; 250 saniye sonrası bağlı heap ilk/son farkı 228 bayt.
  İlk testin sürekli NOMINAL şartı simülasyonun SAFE geçişi nedeniyle yanlıştı;
  monitor düzeltildi/18 offline testi geçti ve fiziksel test baştan tekrarlandı.
  Enerji/mod eşikleri veya mevcut injector değiştirilmedi; heartbeat boşluğu üst sınırı 12 saniyedir.
  Soak sonrası aynı arşiv kaydı iki kez alındı (20 bayt, CRC16 0xDB3F, TC 9→10,
  TM 0→1, udp-out 8→10). Script exit 0 ve RESTORE PASS: normal boot/IP .5 doğrulandı.
  Normal firmware'de ayrıca iki okuma geçti (CRC16 0x3865, TC 11→12, TM 0→1,
  udp-out 10→12). Normal ELF'te SDK fault wrapper yok; SDK config hash'i değişmedi.
  Normal build RAM 42180/327680, flash 792797/1740800 bayt. Eski RAM kayıtları
  upload/reset ile geri alınamaz biçimde kayboldu. Router/firewall/YAMCS kaynak
  ayarları ve gerçek NVS silinmedi/bozulmadı; SDK/upload normal flash yazabilir.
  Saatlik/günlük dayanıklılık veya her ortam/SDK iç arızası garantisi değildir.
  Mevcut compiler uyarıları korunmuştur. Ayrıntılar [Wi-Fi belgesindedir](wifi.md).
- [x] CDH güç monitoründe koşulsuz mod yazmalarını koşullu/atomik geçişe çevir;
  FDIR bildirimi sonrası yeni snapshot al, EMERGENCY'yi koru ve güç toparlanmasında
  CRITICAL → SAFE kullan. Eşikler `< 2.5 V` / `> 6.0 V`, watchdog ve 1000 ms döngü
  korundu. Başarısız geçiş artık başarı logu basmaz; sonraki döngüde yeniden değerlendirilir.
  2026-10-14: gerçek monitor/mod/FDIR ile 36 yeni host senaryosu; toplam 14 C grubu,
  79 Python kontrolü ve ESP32 derlemesi geçti. RAM 42188, flash 793309 bayt; SDK
  config hash'i değişmedi. Kart yüklenmedi/resetlenmedi; RAM arşivini silen işlem yapılmadı.
  FDIR logunda yalnız timestamp format cast'i taşınabilir hâle getirildi; fatal/
  manuel komut politikası aynı kaldı. Bu alt adım global SAFE davranışını, bütün
  recovery/boot kurallarını veya target eşzamanlılığı kapatmaz. [Mod belgesi](system_modes.md).
- [x] EPS yük kesme/geri açmayı global moda bağla: SAFE/CRITICAL/EMERGENCY'de
  PAYLOAD/HEATER bitleri kesilir; COMMS/diğer bitler korunur. Yerel SAFE'den geri
  açma yalnız global NOMINAL/DEGRADED ve `V >= 8.5 V` altında, mod kontrolüyle aynı
  kilit bölgesinde olur. `< 5.0 V` tespitinde aynı döngüde kesme; RECOVERY relapse
  da kesilir. EPS modu private, getter mutex korumalı; kilit sırası EPS → global
  mode, FDIR/log dışarıda. INITIAL_BOOT sağlıklı mevcut yük defaultları korunur,
  boot'ta recovery yük açmaz; global SAFE → NOMINAL eklenmez. Heater komutu stub
  kalır, logu artık gerçek işlem iddiası taşımaz. EPS component doğrudan freertos/
  time/system_mode bağımlılığını bildirir. 2026-10-14: gerçek EPS/mod/FDIR/commands
  ile 144 yeni host senaryosu, toplam 15 C grubu ve 79 Python kontrolü, ESP32
  derlemesi geçti (RAM 42188, flash 793361 bayt). SDK config hash'i aynı;
  yükleme/reset/arşiv silme yok. Yeni mod kararı geçerli açma işleminden sonra
  gelirse sonraki başarılı manager döngüsünde kesilir; anlık donanım interlock'u
  veya kanıtlanmış 100 ms worst-case tepki değildir. ADCS/payload davranışı ve
  genel recovery/boot politikası açık. Ayrıntılar [mod belgesindedir](system_modes.md).
- [x] Deployment simülasyonu tamamlanmadan NOMINAL/DEGRADED seçilmesini merkezi
  mode service'te engelle. Completion ayrı/monoton per-boot RAM bilgisi; init/Set/
  Transition ve gerçek FDIR yer komutu kapıyı atlayamaz. İlk completion + BOOT→NOMINAL
  aynı mutex altında; SAFE/CRITICAL/EMERGENCY korunur. 10 + 2 s simülasyon beklemeleri
  aynı, 100 ms retry yalnız kayıt içindir; başarılı kayıt olmadan task delete/log
  yok. Mesajlar gerçek burn-wire/anten iddiası taşımaz. 2026-10-14: 212 yeni host
  senaryosu, toplam 17 C grubu/79 Python kontrolü ve normal derleme geçti; RAM
  42188, flash 795377 bayt, SDK config hash'i aynı. Yükleme/reset/arşiv silme yok.
  Eski operating test fixture'ları completion API'siyle post-deployment hazırlanır;
  timer kanıtı ayrı gerçek task host grubundadır. [Deployment belgesi](deployment_policy.md).
- [ ] Genel boot/recovery ve izin verilen mod geçişlerini critical/fatal aktif neden
  takibiyle kapat; deployment completion veya warning ACK'i fault-clear sayma.
- [x] Mevcut EPS/FDIR/ADCS/payload zincirinin seçilen gerçek RTOS/kart senaryosu:
  17 Ekim, 24.0 s / tek boot; E102/SAFE/inhibit, owner-clear sonrası SAFE ve
  servislerin durmasının korunması, ayrı gerçek mode TC ve açık servis rearm'ı,
  veri üretimi/CRC retention geçti. 30 önceki kayıt diskte korundu, normal restore
  95.7 s / exit 0. Bu seçim bütün host lifecycle matrisi, boost, başka-owner/fatal/
  uncertainty veya kalıcı reboot-loop politikasını kapatmaz.
  [Gerçek kart sonucu](mission_lifecycle_target.md).
- [x] Mevcut owner/izin politikasının bütünleşik boot→deployment→EPS fault→SAFE→
  owner-clear→ayrı ground mode→yerel EPS recovery→açık ADCS/payload restart yolunu
  host'ta doğrula. 12 yeni süreç/183 alt servis senaryosu; completion/ACK/payload
  RESET ile diğer critical/fatal veya uncertainty bypass yok. Pre-deployment
  clear sıraları, relapse, DEGRADED ve PDU kapısı kapsandı. Pre-init safety floor
  doğru kalırken yanıltıcı init logu düzeltildi. Tam 43 C/47 header/372 Python,
  build/ELF/metadata/59 profil geçti. Yeni imaj yüklenmedi; genel recovery ve
  gerçek kart lifecycle kapsamı açık. [Kanıt/sınırlar](subsystem_safety.md#2026-10-16-birlikte-boot-safe-ve-owner-recovery-akışı).
- [x] Mevcut EPS/ADCS/payload güvenli mod yollarını global moda bağla ve host'ta
  mod/permission/kilit/hata yollarını doğrula. ADCS actuator/dipole/torque kapatma,
  actual physics/boost authorization ve açık DETUMBLE recovery; payload START/
  üretim/PDU gate, SAFE sonrası INIT/START, prepare sırasında sample discard
  eklendi. Yeni servis mutex'leri command/task/getter state'ini korur; zorunlu
  init tahsis/lock hatası abort'tur. Lock sırası service → EPS → global mode;
  FDIR/log state lock dışında, archive submission EPS/mode lock dışındadır.
  2026-10-14: 164 yeni senaryo, toplam 16 C grubu ve 79 Python kontrolü, normal
  ESP32 derlemesi geçti (RAM 42188, flash 795061 bayt). SDK config hash'i aynı;
  yükleme/reset/arşiv silme yok. Son izin kontrolünü geçmiş tek payload kaydı
  tamamlanabilir; sonraki Update yeni üretmez. İki yeni mutex'in dinamik heap'i
  ve target scheduler/actuator/fault davranışı ölçülmedi. Gain'ler korunurken
  duplicate PD hesaplaması kaldırıldı; ADCS unit/sign/kararlılık doğrulaması açık.
  Genel boot/deployment/latched-fault recovery ve phase 1 bütünü kapanmış değildir.
  Ayrıntılar [alt servis sözleşmesindedir](subsystem_safety.md).

### 2. FDIR ve watchdog — eski aşama 3

- [x] `FDIR_Process()` için bağımsız required `FDIR` görevi ekle: 2048 bayt stack,
  priority 4, ilk kontrol + 1000 ms nominal delay-until periyodu. Mevcut görevler/
  ağ davranışı korunur; oluşturma hatası mevcut abort politikasıyla ele alınır.
- [x] Warning threshold işlemesinde başarısız geçiş/BOOT/safety modlarında sayaçları
  koru. FDIR → mode kilit sırası, log basmayan guarded geçiş ile ACK ve yeni warning
  işlemleri serileşir; tüketim yalnız NOMINAL→DEGRADED veya zaten DEGRADED kararı
  başarıyla commit edilmişse yapılır. Yer ACK'i mode/fault-clear değildir.
  2026-10-14: 73 yeni FDIR host senaryosu, core grubunda 25/12 task, toplam 18 C
  grubu ve 79 Python kontrolü, normal derleme geçti. Statik RAM 42188, flash 796085
  bayt; SDK config hash'i aynı. Karta yükleme/reset yok. Runtime yeni task heap/
  stack/scheduler kanıtı yok; watchdog kaydı henüz eklenmedi. [FDIR belgesi](fdir_policy.md).
- [x] Critical/fatal nedenleri 16-slot `(source, code)` RAM tablosunda kaydet; aynı
  MODE mutex'inde normal çalışmaya dönüş/BOOT bypass engeli ve safety floor'u uygula.
  ACK warning-only; fatal promotion geri alınmaz. Full/recording uncertainty
  mevcut kayıtları silmeden bu boot'ta konservatif inhibit bırakır. EPS owner yalnız
  finite `V >= 8.5` + aynı revision ile kendi düşük voltaj critical nedenini çözer;
  global NOMINAL yazmaz, başka kaynak/fatal kayıtlar korunur. 2026-10-14: 117 yeni
  host senaryosu, toplam 19 C grubu/79 Python kontrolü ve normal derleme geçti;
  statik RAM 42524 (+336), flash 797241, SDK config hash'i aynı. Yeni heap/task/flash
  write, yükleme/reset/arşiv silme yok. [Aktif hata sözleşmesi](fdir_faults.md).
- [ ] EPS dışındaki kaynaklar için verified repair/resolve; fatal/uncertainty
  recovery, persistent fault history ve bütün mode/mission grafiğini ayrıca doğrula.
  Task-timeout owner liveness çözümü aşağıda kod/host/build ve bounded iki-episode
  kart kapsamında tamamlandı; arşiv CRC gibi domain repair ve geniş fault matrisi açık.
- [x] 17 Ekim: kullanıcı onaylı konservatif reset/boot-loop/kalıcı özet/ayrı yer
  onayı politikasını kod/host/normal build/native YAMCS sınırında uygula.
  Geçmiş her boot için review kapısıdır, fatal/uncertainty yerinde onarımı değildir.
  [Kesin davranış ve bounded yazma](recovery_policy.md).
- [x] Yeni kalıcı recovery için seçilmiş PANIC + normal-reset, tam orijinal RAM
  koruma ve finally restore'lu güvenli runner hazırla/test et; eski runner engellidir.
  Bu yalnız araç/mock/build sonucudur. [Paket ve açık matris](recovery_target.md).
- [x] Ayrı onaylı seçilen gerçek PANIC/NVS/yer onayı/normal restore paketini doğrula:
  30 kayıt iki kopya, baseline4→boot5→PANIC boot6→normal boot7, iki upload boyunca
  NVS eşitliği, her boot'ta yeni onay ve SAFE→ayrı NOMINAL; exit 0/RESTORE PASS.
  [78.422 s + 78.797 s, kesin kapsam](recovery_target.md).
- [x] Ayrı onaylı üç ardışık HK caller abort/SDK PANIC/BOOT_LOOP eşiği ve normal
  restore yolunu gerçek kartta doğrula: 79.610 + 79.734 s / exit 0/RESTORE PASS,
  boot7→8→9→10→11→12; BOOT_LOOP geçmişi silinmez, yeni boot ayrı onay ister.
  [Seçilen fiziksel kapsam](recovery_boot_loop_target.md).
- [ ] Aralıklı aktarım/geçmiş kaybının somut incelemesini kapat; seçilen iki paket
  bütün kalıcılık matrisi değildir. Ek bozuk-kayıt/reset senaryoları kullanıcı
  kararıyla yeni zorunlu iş değildir; bu sınır belgede korunur.
- [x] Software heartbeat supervisor'ın timestamp/latch verisini koru; invalid ID
  kontrolü, init öncesi/arm öncesi davranış ve startup epoch düzeltmesi. Core içinde
  required WATCHDOG_Init, network/log denemesinden sonra ilk task-create öncesi
  once-only arm; erken pet monitor başlangıcında ezilmez. Mevcut 15 s strict `>`
  timeout/5 s tarama, yedi görev (EPS_MGMT + FDIR eklendi), task bazlı `SRC_CDH`
  critical neden ve ayrı state-unavailable nedeni. ACK/resumed pet/init/arm auto
  clear yok, reset/SDK ayarı/öncelik değişmedi. 2026-10-14: 61 yeni 100 Hz host
  senaryosu; core 28, toplam 20 C grubu/79 Python ve normal derleme geçti. Statik
  RAM 42548 (+24), flash 798229; SDK config hash'i aynı. Yeni mutex runtime heap/
  stack maliyeti ve target scheduling ölçülmedi; karta yükleme/reset yok.
  [Watchdog belgesi](watchdog_policy.md).
- [x] Kullanıcı onayıyla SDK supervisor/idle-stall recovery politikası uygula:
  ESP32 port'u hazır TWDT'yi reconfigure eder, ortak 15 s/iki idle/panic=true.
  WDT_MON kendisini subscribe eder, yalnız completed true check sonrası feed;
  false/bloke check/report/log beslemez. Tek mission-task timeout'u FDIR/SAFE,
  kendi başına reset isteği değil; SDK API failure required abort. Interrupt WDT,
  SDK init/deinit, task öncelikleri ve ağ kodu değişmedi. 2026-10-14: watchdog 70,
  yeni port 13, toplam 21 C grubu/79 Python ve normal derleme geçti. Statik RAM
  42556 (+8), flash 799881 (+1652); sdkconfig dosyası aynı, runtime TWDT farklı.
  Karta yükleme/reset yok; yeni SDK aboneliğinin heap/stack maliyeti ölçülmedi.
  [Politika, RAM kaybı ve test sınırları](watchdog_policy.md).
- [x] Yeni normal imaj, tek heartbeat kaybı ve kontrollü supervisor self-stall/SDK
  timeout/reset reason hedefli kartta doğrulandı. Task-loss: 65.7 s, SAFE/tek C200
  nedeni, dokuz SDK-fed snapshot ve reboot yok. Supervisor: 15.00 s stall-to-timeout,
  gerçek TASK_WDT reason 6, ikinci boot'ta 45 s mission gözlemi. Test-only stack
  ölçümüyle WDT_MON 4096 bayt budget'ı doğrulandı; minimum boşluk 1588 bayt.
  Bu, yalnız bu bounded yolun stack ölçümüdür; bütün task/fault bütçesi değildir.
  Paket/kapsam/restore kanıtı [kart belgesindedir](watchdog_target_tests.md).
- [x] CPU0/CPU1 idle-starvation/IWDT ve IRQ-açık çift-worker starvation'ın
  belirtilen target yollarını doğrula; gerçek reset nedeni ve normal restore
  kanıtları mevcut. Bu kapsam bütün deadlock'ları temsil etmez.
- [x] Kullanıcı tercihiyle kararlı-heartbeat task-timeout owner recovery'sini
  kod/host/normal build düzeyinde ekle: token sonrası >=30 s / >=3 farklı-tick
  pet / <=15 s gap / <=5 s freshness ve exact revision. Yalnız ilgili critical
  timeout çözülüp task latch'i rearm edilir; global mode, diğer nedenler, fatal,
  overflow/uncertainty ve C20F korunur. Heartbeat → MODE quiet zero-wait Try
  API'leri busy olduğunda beklemez. 54 yeni watchdog + 10 FDIR senaryosu;
  toplam watchdog 132 / aktif FDIR 127. Tam 42 C/47 header/346 Python, graf/layout,
  runner-refusal, 47 negatif profil ve güncel normal metadata/ELF kontrolleri geçti.
  Normal RAM 42884, flash 809193 bayt; SDK config aynı. Yeni heap/task/mutex yok.
  Upload/reset yapılmadı. [Politika, kanıt ve sınırlar](watchdog_recovery.md).
- [x] Yeni timeout recovery politikasının TM_GEN dropout/resume/clear,
  tekrar dropout ve ayrı mode komutu yolunu gerçek kartta doğrula. Güncel RAM
  kopyaları her tekrar öncesi korundu; upload/reset onaylıydı. İki gerçek C200,
  revision 1–2, resume-clear 40.016/35.000 s, SAFE'in korunması ve bir NACK/iki
  ACK geçti. Recovery 135.8 s; normal baseline/restore 95.8'er s; exit 0 /
  RESTORE PASS. WDT_MON test-only minimum boş stack 1588 bayt. Üç güncel HK/28
  olayın salt okunur YAMCS kontrolü de geçti. [Kanıt/sınırlar](watchdog_recovery_target.md).
- [x] Watchdog işlevlerini ve sürümün test sınırını ayır: planlanan task/supervisor,
  CPU0/CPU1/global ve heartbeat recovery yolları tamamlandı. Genel deadlock/
  cache-off/debug/JTAG/farklı IRQ etkileri ölçülmemiştir ve yeni senaryo işi
  değildir. Reset/rearm/BOOT_LOOP politikası ve seçilen fiziksel yollar geçti;
  evrensel fault/recovery veya WCET garantisi verilmez.

### 3. Arayüzler ve build — eski aşama 4

- [x] Component bağımlılıklarını/header sahipliğini tamamla; global include'ların gizlediği bağları kaldır.
  2026-10-14 ilk bildirim kontrolünde 7 CMake dosyasına eksik 14 private proje bağı
  eklendi. Read-only audit 12 component/48 doğrudan header bağı için geçti;
  normal CMake yapılandırması/compilation database üretimi 29.66 s ile geçti.
  Runtime kaynaklar, global include/LDF flag'leri ve kart imajı değiştirilmedi.
  Bu ilk adımda full firmware compile/link yapılmadı; SDK/header sahipliği ve döngüler açık kaldı.
  [Kapsam ve tekrar kontrol komutları](component_dependencies.md).
  2026-10-15 devamında genel proje `-I` yolları ve ikinci LDF derleme yolu
  kaldırıldı; HK'nin SDK lwIP bağı doğrudan bildirildi. Tek normal compile/link
  175.08 s ile geçti; 47 proje C dosyası birer kez derleme metadata'sında, eski
  LDF arşivleri link map'inde yok. Statik RAM 42564, flash 800053 bayt; SDK config
  aynı. Karta yükleme/reset yapılmadı. Bu alt adım tamam; ortak tip/komut sahipliği,
  kalan gereksiz public bağlar, tüm header'ların bağımsız derlenmesi ve döngülerin ayrıştırılması açık.
  İkinci 15 Ekim adımında ADCS public FDIR sızıntısı ve CDH task header'larının
  gereksiz RTOS/frame include'ları ayrıldı; ADCS'nin altı bağı private oldu.
  EPS include guard'ı ve HAL sabitlerinin tek sahipliği düzeltildi. 5 standalone
  header ve 24 C grubu geçti. PlatformIO'nun alt component CMake önbelleği
  yakalanıp yeniden configure edildi; güncel normal compile/link 25.72 s ile geçti.
  Statik RAM 42556, flash 800053 bayt; SDK config aynı, upload/reset yapılmadı.
  Üçüncü 15 Ekim adımında ortak tip/komut/APID tanımları header-only `fsw_common`
  component'ine ayrıldı; arşiv/HK/payload'un yalnız tanımlar için taşıdığı doğrudan
  CDH bağı kaldırıldı. ADCS komut ID'sinin ikinci tanımı kaldırıldı, hatalı APID
  yorumları düzeltildi; çalışan numaralar ve layout değişmedi. 25 C/9 header,
  taşıma öncesi/sonrası ESP32 ABI syntax kontrolü ve 41.81 s normal build geçti.
  Statik RAM/flash 42556/800053 olarak kaldı; SDK config aynı, upload/reset yok.
  Bu adımda diğer public/private bağlar, header kapsamı ve bildirilen component döngüleri açıktı.
  Son 15 Ekim paketinde watchdog ve eski radyo adaptörü ayrı component'lere,
  FDIR/solar task adaptörleri main'e taşındı. Gereksiz public bağlar private oldu;
  bildirilen proje CMake/direct-include grafı artık döngüsüz. 15 component/59 bağ,
  26 C/44 exported top-level header ve 8 graf kontrolü geçti. Tek normal
  configure/compile/link 184.74 s ile geçti; RAM 42556, flash 799997 bayt.
  Task metadata/algoritmalar/protokol korunur, SDK config aynı; upload/reset yok.
  Bu maddenin bildirilen proje/include kapsamı kapandı. SDK implicit/link-only
  bağları, runtime çağrı/kilit döngüleri, bütün target profilleri ve mimarinin
  her yönü için tam doğrulama iddiası yok; kapsam belgesinde sınırlar ayrıldı.
- [x] HAL, simülasyon ve servis sınırlarını, birimleri ve ortak tip/komut sahipliğini netleştir.
  Ortak mission tip/komut/APID sahipliği `fsw_common` altında toplandı. Gerçek
  servis/HAL/SDK sınırları ve ADCS birimleri [simülasyon sözleşmesinde](simulation_contracts.md)
  tanımlandı. rad/s gyro ve 3000 deg/s eşik dönüşümü, dt ölçekli model, nominal
  PD/braking işareti, yakıtı yetmeyen boost reddi, ölçümün plant'i ilerletmemesi
  ve kilitli runtime snapshot/step sahipliği düzeltildi. Overflow-safe norm,
  alias-safe Hamilton product ve eksik rotation matrix implementasyonu eklendi.
  28 C/45 header (alt servis grubunda 171 senaryo), 15 component/59 bağ/47 tekil
  kaynak metadata kontrolü ve normal build 21.60 s ile geçti. RAM 42556,
  flash 802553 (+2556), SDK config aynı; karta upload/reset yapılmadı.
  Bu birim/arayüz kapsamı kapanmıştır; bütün modlarda kapalı çevrim kararlılık,
  fiziksel model/aktüatör veya runtime stack/tepki süresi doğrulaması değildir.
- [x] Bilinen ADCS/FDIR unused-variable ve HAL type-limits uyarılarını işlevsel anlamlarıyla ele al.
  Kullanılmayan iki değişken/eski makro kaldırıldı. HAL 8 bit adres API'si
  korunup 256 kayıt şartı derleme zamanında doğrulandı; gerçek simülasyon ve fault
  davranışı değişmedi. Tüm C gruplarından unused-variable Werror istisnası kaldırıldı;
  24 C/5 header ve yeniden configure edilmiş normal build geçti. Bütün test
  profilleri yeniden derlenmedi; bu sonuç sonraki değişiklikler için uyarısızlık garantisi değildir.

### 4. Test kapsamını genişletme — eski aşama 5, tüm işlere paralel

- [x] HK, mod geçişi, FDIR escalation, EPS threshold/hysteresis ve ADCS matematik kontrollerini tamamla.
  15 Ekim HK alt paketi tamamlandı: limit/reset/serialization, 1..60 s komutlar,
  force/periodic ayrımı, zaman taşması, kilit/TX hataları ve gerçek TM görev
  gövdesi için iki grup eklendi. Başlatılmamış legacy paket alanları temizlendi;
  HK/EPS snapshot kilitleri eklendi, 1 s poll ile 5 s arşiv periyodu ayrıldı.
  30 C/45 header ve 15 component/59 bağ/47 tekil kaynak kontrolü geçti.
  Normal build 36.92 s: RAM 42596 (+40), flash 803009 (+456); SDK config aynı.
  Karta upload/reset yapılmadı; yeni davranışın fiziksel doğrulaması açık.
  [HK kapsamı](hk_contracts.md), age-based geçerlilik/target jitter ve ground
  sözleşmesi gibi henüz doğrulanmamış noktaları ayırır.
  Ardından EPS geçersiz gerilim/float sınır paketi tamamlandı: EPS yük grubu
  144 → 316, güç/mod grubu 36 → 78 süreç. NaN/±Inf yükleri kestirir ve mevcut
  E102 power fault kapısını kullanır; SoC sahte %100/%0 yerine NaN olur.
  Finite gerilim eşikleri/FDIR politikası korunur; geçersiz ölçüm fault'u da yalnız
  finite >=8.5 V + aynı revision ile çözülür. 5/8.5 V chatter, gerçek fault-clear
  ve explicit restart ile inclusive/exclusive eşik komşuları test edildi.
  30 C/45 header ve normal build 27.45 s geçti: RAM 42596, flash 803233 (+224).
  SDK config aynı; karta yükleme/reset yok. [EPS/mod sözleşmesi](system_modes.md)
  ve mevcut 73 FDIR escalation/117 aktif fault/212 deployment/ADCS math grupları
  birlikte bu temel host test maddesini kapatır; %100 coverage/target timing veya
  tüm-source recovery kanıtı değildir. Diğer test düzeni ve sistem maddeleri açık.
- [x] İlgili eski testleri mevcut API'ye uyarla; host/target ve production/test kaynaklarını ayrı tut.
  15 Ekim kapanış: 30 kalıcı C fixture / 30 grup / 45 header güncel API'lerle
  geçti. Kayıtlar tek `host_test_manifest.ps1` fonksiyonuna alındı; fixture
  yetimliği/eksik kaynak ve generated `.pio` input'u otomatik reddediliyor.
  Normal compile metadata/ELF'de host source/include, test define ve bilinen
  injection hook yok; kaynaklardan eski normal ELF reddediliyor. Profil guard'lı
  target injection dosyalarının kayıtlı ama normalde aktif olmadığı açıkça ayrıldı.
- [x] Hata enjeksiyonu ve başarı koşullarını otomatikleştir; ölçülmemiş coverage iddiası yapma.
  `tools/run_checks.ps1 -BuildMetadata` tek komutla 30 C/45 header, 8 graf + 14
  test-layout PowerShell kontrolü, 8 dosyada 122 Python ve 3 negatif profil
  kontrolünü geçirdi. İlk hata/sıfır Python testi başarısızlık üretir; normal
  metadata kontrolü isteğe bağlı atlanırsa SKIP yazılır. Grup seçimi/headers
  atlama kısmi regresyon diye etiketlenir; unknown grup derlemeden önce reddedilir.
  `-List` derleyicisiz ve `hk_service_check -SkipHeaders` seçimi ayrıca kontrol edildi.
  Firmware/SDK config değişmedi; normal build tekrarlanmadı, upload/reset/ağ/kart
  testi yapılmadı. Son normal derleme 42596 RAM/803233 flash olarak kalır;
  karttaki eski normal imaj yenilenmedi.
  Bu kapanış mevcut otomatik host/profil sözleşmesi içindir; stage 6 fiziksel
  doğrulama, tüm-source recovery ve ölçülmemiş coverage açık kalır.

### 5. TM/TC ve arşiv akışını tamamlama — eski aşama 6

- [x] Firmware–YAMCS komut/APID/boy/byte-order/CRC sözleşmesini hizala; yanıltıcı yorumları düzelt.
  15 Ekim ilk alt paket: wire TC 7..54 bayt/version 0/type TC/no secondary/
  unsegmented/exact length kontrolü, 55 bayt sentinel buffer ile oversized
  datagram reddi eklendi. İç header genişletilen boyu/secondary flag'i yansıtır;
  işlemci body CRC + trailer + cache'i birlikte kontrol eder. Legacy demo TM
  type biti 0x1800 → 0x0800 düzeltildi; active TM boş payload/taşan APID'yi reddeder.
  Yeni gerçek-C ingress grubu 85 offline girdi/55 işlenen komut ve Python wire
  builder testleri geçti. 31 C/45 header/125 Python/22 PowerShell/3 negatif profil;
  normal build 31.47 s: RAM 42596, flash 803509 (+276), SDK config aynı.
  Ground XTCE yalnız okundu: mevcut yeni komutların APID'leri/HK=5/event=2000
  eşleşir; legacy simulator TC=101/TM=100 ve archive TM container işi açık.
  Karta upload/reset/komut veya ground dosyası değişikliği yapılmadı.
  [Etkin wire/iç paket sözleşmesi](tm_tc_contract.md), yerel CRC'nin ağ kimlik
  doğrulaması olmadığını ve henüz yapılmamış fiziksel doğrulamayı ayırır.
  Ardından `config/yamcs/xtce.xml` maintained ground profili eklendi; 4 somut
  komut ve HK/event/archive TM actual YAMCS 5.12.0 parser/builder/postprocessor
  ile aynı wire sözleşmesinde doğrulandı. CRC alanı gösterilir, native MDB otomatik
  payload CRC-valid kalite üretmez. Kaynak ground dosyasına yedekli/hash-guarded
  kurulum yapıldı; runtime reload/canlı veri doğrulaması stage 6'da hâlâ açık.
- [x] Simülatör örnek komutlarını gerçek kart komutlarından ayır ve gerçek komutları güvenli biçimde doğrula.
  Source profile'dan Spacecraft TM=100, TC=101/Reboot/batarya örnekleri ayrıldı;
  orijinal ground dosyası byte-identical backup'ta korundu. CmdId'ler sabit,
  CmdParam tipleri gerçek firmware aralıklarıyla sınırlı. 14 gerçek builder →
  postprocessor çıktısı ve 8 geçersiz değer offline kontrol edildi; değiştiren
  komutlar karta gönderilmedi, fiziksel proof kapsamı genişletilmedi.
- [x] Arşiv TM alanlarını YAMCS'te ayrı ayrı gösterecek container tanımını ekle.
  APID 96 için sürüm/index/record ID/length/payload CRC/dynamic binary parametreleri
  eklendi. Native extractor 0/1/9/128 bayt payload'ı, HK/event'i ve 5 yanlış header
  reddini doğruladı. 10 yeni static guard ile toplam Python 135/10 dosya geçti.
  Kurulu kaynak dosyası native testten tekrar geçti; C/SDK config/ELF değişmedi,
  kart ve çalışan YAMCS süreci yeniden başlatılmadı. [Ayrıntı/yedek/reload](yamcs_profile.md).
- [x] Queue full/backpressure, dolu arşiv ve arşiv okuma/yazma/silme eşzamanlılığını tanımla/test et.
  Arşiv mutex'i eklendi; init hatası görevlerden önce durdurulur, tekrar init
  kayıt silmez. Runtime lock timeout'u değişiklik yapmadan UNAVAILABLE döner;
  kullanım sorgusu hatayı boş arşiv diye göstermez. Read snapshot/CRC ve TX/FDIR
  kilit dışında; wipe aynı korumalı reset yolunu kullanır. Mevcut full warning
  ve drop-new politikası korunur. 60 paralel yazmada 30 kayıt korundu; üç thread
  3000 karışık read/write/reset çağrısındaki snapshot tutarlılığını kontrol etti.
  Logger tek deneme/timeout/full sonrası payload-watchdog ilerlemesi test edildi.
  33 C/45 header/135 Python/22 PowerShell ve 3 negatif profil geçti; memory
  corruption hook normal ELF'den ayrıldı. Normal build RAM 42604/flash 803889,
  SDK config aynı. Karta yükleme/reset yapılmadı; target latency/fairness ve
  canlı ground doğrulaması açık. [Sözleşme ve sınırlar](archive_concurrency.md).
- [x] Toplu aktarım ve yer istasyonu onayıyla güvenli boşaltma davranışını tanımla/test et.
  Tek kayıt GET sonucundan otomatik silme veya ground ACK varmış sonucu çıkarma.
  İlk çekirdek paketi: prefix manifest/kimlik/digest/read-set ve cancel/CONFIRM
  servis API'leri eklendi. Doğru CONFIRM yalnız snapshot prefix'ini çıkarır;
  aradaki append ve tekrarlı/eski ACK veri silmez. Offline ground yardımcısı
  verify/exclusive-write/fsync/readback bitmeden synced-save receipt üretmez.
  İlk çekirdek paketi 35 C/46 header/147 Python ile geçti.
  Normal build RAM 42620/flash 803985, SDK config aynı; metadata 48 tekil C
  kaynağı doğrular. O ilk imajda API'ler wire'a bağlı olmadığı için linkten
  elenmişti; aşağıdaki sonraki wire paketi artık onları bağlar. Upload yok.
  [Çekirdek kanıtı ve boot/RNG/CRC sınırları](archive_transfer.md).
  Ardından beş TC ve APID 97 yanıtı gerçek dispatcher'a bağlandı; eski GET/WIPE
  korundu. 235 malformed-length reddi ve key/read-set/CRC/timeout/receipt/tail
  host testleri eklendi. YAMCS 5.12 loader/builder/postprocessor 19 komut ve
  19 geçersiz/missing-key girdiyi offline doğruladı. 9 somut komut/30 parametre/
  6 container kaynak profili mevcut ground projesine yedekli/hash-guarded kuruldu;
  runtime reload yok. 36 C/46 header/162 Python ve normal build 55.38 s geçti:
  RAM 42668, flash 806217, SDK config aynı; 49 production C dosyası tekil.
  [Wire sözleşmesi ve kurulum kanıtı](archive_transfer_wire.md). Ağ istemcisi,
  kontrollü clear akışı o wire adımında henüz açık kaldı; sonraki istemci adımı aşağıdadır.
  Kontrollü YAMCS HTTP istemcisi eklendi: varsayılan dryRun; --send ile verified
  export/retain, ayrıca açık --confirm-clear; saved-file confirm retry, pending
  resume ve non-destructive cancel. Same-timestamp eski arşiv yanıtı baseline ile
  ayrılır; exact Sent binary + matching board reply birlikte gerekir. 51 fake
  API/state istemci ve üç ek export/resync testiyle 216 Python/13 dosya geçti.
  C/SDK/MDB/Java/ELF değişmedi; normal metadata ayrımı yeniden geçti. C/native/
  target runner bu Python-only adımında tekrarlanmadı. Bu madde uygulama/host-test
  sınırında kapandı; gerçek HTTP/runtime/ESP32 end-to-end stage 6'ya taşınır,
  başarılmış sayılmaz. [Kullanım, sınırlar ve test kanıtı](yamcs_archive_client.md).

Arşiv şu anda RAM'dedir; resetle kaybolur. Kalıcı flash arşivi mevcut bir özellik
değildir ve bu belge onu tamamlanmış göstermez. Yeni bir kalıcı depolama backend'i
istenirse kapsamı ayrıca kararlaştırılır.

### 6. ESP32 üzerinde sistem doğrulaması — eski aşama 7

- [x] Arşiv doluluk uyarılarında görülen FDIR stack taşmasını temsilî target
  yolunda düzelt/doğrula: 2048 → 4096 bayt, 305.8 s tek boot; üç doluluk uyarısı
  işlenip DEGRADED kararı verildi. Panic/reset/kritik fault yok, minimum boş stack
  1636 bayt. Yeni 27 kayıtlık pending oturum eşik sonrasında da aynı manifest ve
  READ(26) ile doğrulandı; dosya/kart kayıtları korunuyor, CONFIRM yok.
  Bu bounded kontrol genel soak/diğer task stack'leri değildir.
  [Düzeltme ve güncel kontrol noktası](fdir_stack_target.md).
- [x] Yeni normal firmware'i karta yükle, maintained MDB'yi mevcut YAMCS runtime'ına
  al ve temel bağlantıyı doğrula. 15 Ekim: startup sırası/NOMINAL, canlı HK ve
  event container'ları, üç HK parameter ACQUIRED ve BEGIN/PENDING dryRun geçti.
  YAMCS üzerinden gerçek TC yok; RAM arşivi upload/reset ile kayboldu.
  [Kontrol noktası ve bırakılan durum](target_yamcs_smoke.md).
- [x] Gerçek kart/YAMCS ile retained export: 17 kayıt CRC16/tam CRC32,
  fsync/readback ile kaydedildi. Sonraki PENDING aynı manifest'i ve READ(0)
  hâlâ okunabilir kaydı verdi; hiçbir CONFIRM/CANCEL/WIPE/reset yapılmadı.
  [Dosya/manifest ve sınırlar](target_yamcs_smoke.md).
- [x] Açık izinle gerçek kart/YAMCS üzerinde kontrollü arşiv işlemlerini doğrula:
  24 kayıtlık kaydedilmiş prefix onayı 6 append-tail kaydını byte-byte korudu;
  eski receipt yeni aktif transfer sırasında no-op kaldı. CANCEL/duplicate,
  PENDING ile resume, son receipt tekrarı/eski STALE ve EMPTY geçti.
  BEGIN/READ/CONFIRM retry, ilk kabul edilmiş HTTP yanıtını istemcide atma modeliyle
  sınandı; gerçek UDP/TM kaybı veya reboot sonrası RAM restore iddiası yok.
  30 kayıt disk kopyaları korunarak silindi; firmware/reset/mode/WIPE yok.
  [79 gerçek TC ve artifact kanıtı](archive_target_tests.md).
- [x] Tüm HK alan/birim karşılaştırması ve event servisinin bounded işleme/tekrar
  davranışını kontrol et: üç canlı pakette yedi alan raw/engineering bit eşitliği,
  offset/boy ve canlı MDB birimleri geçti. Eksik birimler yedekli kurulum/reload
  ile eklendi. 20 saniyede 235 event/96 aynı metin çifti ham paketlerle eşleşti;
  aynı wire tekrarı 0, ground'da eşleşmeyen ek event yok. Severity/boş metin ve
  üçlü tekrar özeti gerçek preprocessor ile native fixture'ta geçti; exactly-once
  veya tüm malformed/UDP kayıp yolları iddiası yok. Kart upload/reset yok.
  [Kanıt, sınırlar ve tekrarlama](hk_event_target.md).
- [x] Temsilî kaynak/zamanlama baseline'ını ölç: 13 kalıcı task stack'i,
  8-bit heap/minimum alt sınır/en büyük blok, iki mission kuyruğunun örneklenen
  doluluğu ve 7 heartbeat aralığı. Ayrı 600.0 s SAFE koşusunda 20 rapor geçti:
  örneklenen boş heap 157088, minimum toplamı 141532, en büyük blok minimumu
  110592 bayt; en düşük boş stack EPS_MON 1076 bayt. Üç PENDING client turu
  279–456 ms. İlk koşunun EPS düşük gerilim/SAFE nedeniyle durması ayrıca
  belgelendi; all-NOMINAL/WCET/CPU/exact queue peak/uzun soak iddiası yok.
  Onaylı normal upload öncesi EMPTY; yeni 30 kayıt disk ve kartta korunuyor.
  [Ölçüm, güç geçişi, artifact ve sınırlar](resource_target.md).
- [x] Mevcut Wi-Fi yokluğu/geç bağlantı/kopma/yeniden bağlanma kapsamının bounded
  kart doğrulaması tamamlandı (600 saniye); yukarıdaki sonuç/sınırlamalar geçerlidir.
- [x] Kontrollü heartbeat kaybı ve supervisor SDK TASK_WDT reset nedeni kartta doğrulandı.
- [x] CPU0 idle starvation (supervisor hâlâ SDK'yı beslerken) ve CPU0 interrupt
  mask testlerinde gerçek TWDT/IWDT panic/reset nedeni 6/5 ve ikinci boot'ta
  mission ilerlemesi doğrulandı. Seri işaret–tanı gecikmeleri 15.000 s / 0.297 s;
  kesin deadline garantisi değildir. Önceki 30 kayıt disk export ile korundu;
  paket exit 0 / RESTORE PASS, son normal gözlem 95.8 s, kartta normal firmware.
  [Kanıt ve sınırlar](watchdog_starvation_target.md).
- [x] CPU1 idle-starvation/IRQ-mask opt-in profillerini ve çekirdeğe özel seri
  kanıt kontrolünü hazırla; 2×10 yeni C ve 12 yeni Python kontrolü, tam host
  regresyonu ve 36 compile guard geçti. Karta dokunulmadı.
- [x] CPU1'in güncel normal/test imajlarını derle; arşiv korunup ayrı uygulama
  izniyle iki gerçek reset ve normal restore paketini çalıştır. Idle1 / CPU1 IRQ
  panic'i ve reset nedeni 6/5, ikinci boot'ta mission ilerlemesi geçti; seri
  gecikmeler 15.000 s / 0.297 s, supervisor boş stack minimumu 1616 bayt.
  30 kayıt disk kopyası korunuyor; exit 0 / RESTORE PASS ve 95.8 s son normal
  gözlem geçti. Üç HK / yedi alan-birim ve 31 event ayrıca doğrulandı; TC 104→104.
  Kartta normal firmware, test imajı yok. [Kanıt ve sınırlar](watchdog_starvation_target.md).
- [x] İki çekirdekte IRQ-açık priority-24 çift-worker starvation koşusunu doğrula:
  SDK CPU0/WDT_G0 + CPU1/WDT_G1 ve failed supervisor, 15.000 s timeout, gerçek
  reset nedeni 6 ve ikinci boot mission ilerlemesi geçti. İlk okuyucu hatası
  başarısız koşu olarak belgelendi; iki koşunun 30'ar RAM kaydı ayrı disk
  dosyalarında korundu. Yeniden deneme exit 0 / RESTORE PASS, son normal 95.8 s;
  üç HK / yedi alan-birim, 28 event ve TC 170→170 doğrulandı. Kartta normal
  firmware. [İlk bulgu, düzeltme ve sınırlar](watchdog_global_target.md).
- [x] Watchdog/reset kanıtının sınırlarını belirt: tamamlanmış seçilen yollar,
  bütün deadlock/debug/IRQ/reset varyasyonlarını kanıtlamaz. Kullanıcının son
  kararıyla ek fiziksel senaryo açılmayacak; bu bir eksik watchdog özelliği değil,
  belgelenmiş doğrulama sınırıdır.
- [ ] Uzun süreli çalışma, kaynak tüketimi ve simülasyonun tekrar üretilebilirliğini kontrol et.
  Son kullanıcı isteği: yarın sabaha bırakıldı; uzun test henüz başlatılmadı.
- [x] Uzun gözlem için bilgisayar okuyucusunu hazırla: 300..7200 s, rapor
  kesintisinde 45 s sınırı, timeout-split seri satır birleştirme, heartbeat sayacı
  ilerlemesi, mod örnekleri ve opsiyonel mod şartı, RUNNING/final JSON kanıtı.
  37 okuyucu / toplam 390 Python geçti; bir saatlik 120 rapor/720 mod örneği
  **sahte saat/port** ile sınandı. Gerçek port/TC/upload/reset yok; yukarıdaki
  uzun çalışma maddesi açık kalır. [Araç ve sınırlar](resource_target.md#uzun-koşu-için-okuyucu-hazırlığı).

### 7. Dokümantasyon ve tekrarlanabilir demo — eski aşama 10

- [x] Ana README'deki eski/simüle edilmiş akışları ve "flight-ready" iddialarını gerçek durumla düzelt.
  Doğrulanmamış mission timeline/benchmark ve flash archive/gerçek burn-wire/otomatik
  ground ACK iddiaları çıkarıldı; gerçek/sim/stub/yerel demo ayrımı yapıldı.
- [x] Mimari, startup, task tablosu, mod/FDIR ve TM/TC akışlarını belgele.
  15 uygulama görevinin öncelik/stack/beklemesi ve one-shot injector davranışı
  README'de açıklandı; detay sözleşmelere bağlandı. GitHub'a push yapılmadı.
- [ ] Build/test talimatlarını, gerçek ölçümleri ve bilinen sınırlamaları yayımla.
  Nihai README ve GitHub yayını, uzun koşu ve sonuç değerlendirmesi/düzeltmelerden
  sonradır. GitHub en son adımdır; mevcut yerel taslaklar yayın değildir.
- [x] Yerel tekrarlanabilir demo ve güncel kapanış listesini hazırla; derleme/test,
  native YAMCS ve gerçek TC göndermeyen canlı HK/event/dry-run akışlarını ayır.
  Native test, üç HK/yedi birim/233 olay/82 tekrar çifti ve BEGIN/PENDING dry-run
  mevcut kurulumda geçti; TC sayacı 308'de kaldı. Repo/ground source/runtime MDB
  hash'leri eşleşti. Kart/sunucu reset, upload, arşiv silme veya GitHub push yok.
  Kaynak kod değişmedi; son tam host/build kanıtı lifecycle adımına aittir.
  Temiz klon/credential-geçmiş incelemesi ve yayımlama hâlâ açık.
  [Demo](demo_walkthrough.md), [kalanlar](release_checklist.md).
- [x] YAMCS kaynakları/üretilmiş dosyalar ayrımını ve arşivi silebilen temizlik komutlarını açıkça belirt.
  2026-10-16: [dosya sahipliği/veri güvenliği rehberi](yamcs_data_safety.md) eklendi;
  kart RAM'i, ground veritabanı ve bilgisayar export'u ayrıldı. Mevcut ground
  kaynak/runtime `dataDir` ve üç MDB hash'i salt okunur kontrol edildi;
  kurulum betiğinin değişikliksiz varsayılanı ve belge bağlantıları doğrulandı.
  Kart/sunucu resetlenmedi, veri silinmedi. Yedekten geri yükleme/temiz kurulum
  veya GitHub yayını yapılmış sayılmaz; yukarıdaki yayın maddesi açık kalır.
- [x] Masaüstü test rehberini güncel manifest/kontrol sonuçlarıyla eşleştir:
  42 C, 47 header, 40 C fixture, 51 production C kaynağı ve 19 dosyada 346 Python;
  CPU0/CPU1/global watchdog fixture'ları ve 47 profil guard'ı açıklandı.
  Python bytecode/cache `.gitignore` kuralları eklendi; test kaynaklarının ignore
  edilmediği kontrol edildi. Dosya silme veya GitHub'a push yapılmadı.

## Bir aşamayı kapatma koşulu

İlgili açık maddeler uygulanmış, uygun test/ölçüm yapılmış, sonuç ve kalan sınırlar
kaydedilmiş olmalıdır. "Kod eklendi", "log basıldı" veya sonraki aşamada bir demo
çalıştı diye önceki aşamalar tamamlanmış sayılmaz.

Mevcut beş Wi-Fi başlığı ortak çalışma kapsamında kapatıldı; yeni Wi-Fi özellikleri
planlanmıyor. Başlangıç kapanış listesi ve ortak servis orchestration/host testi eklendi.
CDH güç kaynaklı CRITICAL/SAFE geçişinin sınırlı düzeltmesi (78 host senaryosu) ve
EPS simüle yük politikası (316 host senaryosu) ve mevcut ADCS/payload izin bağları
(171 host senaryosu), deployment startup kapısı (212 host senaryosu) ve FDIR warning
processing (73 host senaryosu) ve aktif critical/fatal RAM kaydı/operating gate/EPS
owner resolve (117 host senaryosu) ve software watchdog heartbeat/FDIR timeout
bağı (artık 70 host senaryosu) ve SDK supervisor/idle-stall recovery politikası
(13 adapter senaryosu) tamamlandı. SDK reset davranışı RAM kaybı açıklanıp kullanıcı
onayıyla eklendi. Kontrollü kart paketi, kullanıcı kartı bağlayıp uygulama onayı
verdikten sonra çalıştırılır; her upload/reset RAM arşivini kaybettirir. Paket,
test-only hook'ları, stack düzeltmesi ve sonuç sınırları
[kart belgesindedir](watchdog_target_tests.md). Diğer-source/fatal/uncertainty verified recovery açık
kalır; completion veya warning ACK'i bir fault-clear izni değildir.
FDIR/watchdog ve diğer roadmap işleri açık kalır; bu alt adım bütün başlangıç/mod
aşamasını kapatmaz. 2026-10-14 normal `esp32dev` artık core/güç/EPS/ADCS/payload/
deployment/FDIR warning/aktif neden/software watchdog/SDK recovery değişiklikleriyle
karta yüklendi. Kartta test profili bırakılmaması ayrıca restore kontrolüyle doğrulanır.
Kontrollü paket son normal restore/gözlem (95.8 s), exit 0 ve RESTORE PASS ile
tamamlandı; kartta normal imaj bırakıldı. Bir sonraki uygulama adımı component
bağımlılıkları/header sahipliği incelemesidir; ardından build/uyarı ve README
temizliği mevcut doğrulanmış davranışları koruyarak ele alınır. Daha geniş
watchdog/mission/arşiv test maddeleri yukarıda açık tutulmuştur.
İlk dependency bildirim düzeltmesi ve normal configure kontrolü tamamlandı;
15 Ekim'de genel include/çift build yolu kaldırılıp tek normal compile/link ile
doğrulandı. Ardından beş public header/ADCS private bağı ve bilinen compiler
uyarıları temizlendi. Ortak mission tip/komut/APID sahipliği de `fsw_common`
altında ayrıldı. Ardından watchdog/radyo/task sahipliği ayrılarak bildirilen
proje component döngüleri kapatıldı; 26 C/44 header/8 graf ve güncel normal build
geçti. Son birim/model paketinde saf ADCS math/SIM grupları ve yedi ek güvenlik
senaryosu eklendi; rad/s/dt/işaret/yakıt/ownership düzeltmeleriyle 28 C/45 header
ve normal build geçti. Arayüz/build kapsamı kapandı. Ardından HK limit/rapor
zamanlaması ve TM görev paketi 30 C/45 header/normal build ile tamamlandı;
EPS float sınır/geçersiz ölçüm paketiyle temel HK/mod/FDIR/EPS/ADCS host test
maddesi kapandı. Kalıcı test/API ve production/test ayrımı da tek host doğrulama
komutu ve korumalarıyla kapandı; stage 4 tamamlandı. Stage 5'te wire TC giriş,
iç CRC ve TM type alt paketi tamamlandı. Legacy demo ayrımı ve native doğrulanmış
ground archive container kaynak profili de yedekli kuruldu. Ardından arşiv kuyruk/
eşzamanlılık sözleşmesi, mutex ve host testleri tamamlandı. Toplu aktarım/ACK
çekirdeği ve dosyalama yardımcısı host'ta doğrulandı; ardından wire komut/yanıt ve
YAMCS kaynak bağlantısı native testlerle tamamlandı. Kontrollü YAMCS HTTP istemcisi
de fake API/state/file-failure testleriyle eklendi; stage 5 uygulama/host-native
sınırında kapandı. Sırada stage 6 gerçek firmware/runtime/HTTP/kart doğrulaması var.
Ground runtime reload ve yeni normal firmware'in temel açılış/canlı HK-event
kontrolü tamamlandı. Ardından 17 kayıt silinmeden indirildi ve pending/yeniden
okuma ile retention kontrol edildi. FDIR stack düzeltmesi ve 305.8 s kart koşusu
ardından 16 Ekim'de yeni boot'ta kontrollü CONFIRM/tail/receipt/CANCEL/resume ve
HTTP yanıt kaybı/retry kapsamı kapandı. Ardından yedi HK alan/birim ve bounded
event işleme/tekrar kontrolü tamamlandı; native severity/özet testi de geçti.
Ardından kaynak/zamanlama baseline'ı 600 s SAFE koşusu ve 30 kayıt retained
export ile tamamlandı; normal build + tam host regresyonu geçti. İlk gözlemdeki
EPS düşük gerilim/SAFE bildirimi gizlenmedi; koruma eşikleri değiştirilmedi.
Ardından CPU0 idle-starvation ve IRQ-mask paketinde gerçek reset nedenleri 6/5,
ikinci boot'ta mission ilerlemesi ve normal restore doğrulandı. Paket exit 0 /
RESTORE PASS ile kapandı; önceki 30 RAM kaydı disk export ile korundu. Ardından
IRQ-açık çift-worker global starvation koşusu da geçti. İlk oracle/SDK tur-bayrağı
uyumsuzluğu ayrı başarısız koşu olarak tutuldu; düzeltilen okuyucu iki CPU'nun
worker snapshot'ını zorunlu tuttu. Aynı firmware ile tekrar, gerçek reason 6 /
ikinci boot mission / normal restore ve exit 0 ile kapandı. Diğer genel recovery/
reset yolları açık; uzun süreli test kullanıcı isteğiyle akşama kaldı ve başlatılmadı.
README/mimari/task belgesi de güncellendi. Son kısa kontrol noktası
[global test belgesindedir](watchdog_global_target.md). Tek çekirdek sonuçları
[watchdog belgesindedir](watchdog_starvation_target.md); CPU1'in yalnız-host
hazırlığını ayrı izinli gerçek kart paketi izledi. 30 kayıt disk kopyası korunarak
iki gerçek reset nedeni 6/5 ve normal restore doğrulandı; paket exit 0 / RESTORE
PASS ile kapandı, kartta normal firmware bırakıldı. Kaynak ölçümleri
[önceki belgededir](resource_target.md).
Sonraki timeout owner-recovery paketi de RAM export'ları korunarak tamamlandı:
iki gerçek C200/clear, SAFE'in korunması, ayrı ground NOMINAL komutları ve
normal restore geçti. [Güncel bounded sonuç](watchdog_recovery_target.md).
Diğer-source/domain/fatal/uncertainty recovery ve uzun soak açık kalır.
Runtime çağrı/kilit döngülerinin
tamamı veya bütün target profilleri bu sonuçla doğrulanmış sayılmaz.
Aynı watchdog/Wi-Fi kart paketi her metadata düzeltmesinde tekrar
çalıştırılmaz; ilgili koruma davranışı değişirse hedefli regresyon yeniden gerekir.
