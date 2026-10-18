# Güncel kapanış ve yayın listesi

2026-10-17 kullanıcı kararı: **yeni hata senaryosu/profil eklenmeyecek**. Planlanan
watchdog, startup, tek-PANIC ve üç-abort BOOT_LOOP doğrulamaları kendi belirtilen
kapsamlarında tamamlandı. Ölçülmemiş her ihtimal yeni zorunlu görev değildir;
genel cache-off/debug/JTAG/diğer reset varyasyonları bilinen sınır olarak yazılır.
Somut hata/toparlanma düzeltmeleri mevcut testlerle doğrulanır. Bu karar geçmiş
FAIL'leri veya gerçek seri aktarım/geçmiş kaybı bulgusunu başarılı saymaz.

## Sabit kapanış listesi

- [x] Kullanıcının seçtiği **mevcut güvenli arşiv bakım yolunu** kodla/doğrula: açık
  WIPE sonrasında doğrulanmış boş arşiv, yalnız eski nonfatal CRC revision'ının
  çözülmesi; diğer hatalar/kalıcı geçmiş ve mod korunur. Otomatik silme veya ayrı
  karantina/salvage mekanizması bu sürüme eklenmeyecek. Kartta WIPE izni verilmedi;
  gerçek silme/CRC enjeksiyonu denenmedi. Mevcut host vakası ve tam regresyon geçti.
- [x] Eski aktarım/geçmiş kaybını mevcut loglarla ayır; güvenilir yükleme ve NVS
  koruma kapılarını gözden geçir. ROM `0x0105` yanıtı Windows USB kopması değildir;
  no-stub yolu koruma gerektiren runner'da kapatıldı. **Kök neden kesinleşmedi**;
  eski kayıp bulgusu çözülmüş sayılmaz. [İnceleme/riski](recovery_target.md).
- [x] Güncel normal imajı arşiv/reset izniyle yükle ve olağan çalışmasını
  kontrol et: boot12→13/history98, iki TC, 91.015 s PASS. WIPE/enjeksiyon yok.
  [Bakım, koruma ve normal çalışma kanıtı](maintenance_closeout.md).
- [x] 1 saatlik uzun çalışma ve sonuç değerlendirmesi: **3600 s / 120 rapor PASS**,
  720 SAFE örneği, aynı boot13/history98. All-NOMINAL yük kanıtı değildir;
  bu koşuda düzeltme gerektiren sınır ihlali görülmedi. [Sonuç](long_run_target.md).
- [x] Kullanıcının sonradan istediği **aktif NOMINAL** saatlik koşuyu uygula ve
  değerlendir:3600.2 s/120 rapor/720 NOMINAL örneği PASS.867 çift dosya/4286 kayıt,
  STOP/SAFE ve tail EMPTY18 Ekim'de tekrar doğrulandı. İlk başlangıcın FAIL'i
  korunur; saatlik PASS onu silmez. [Sonuç](nominal_soak_target.md).
- [ ] Nihai README, temiz klon/kurulum/test, credential/yayın güvenliği ve
  **kullanıcı onayıyla en son GitHub yayını**.
  - [x] Sonuçları ve sınırları README/roadmap/profil/demo belgelerine işle.
  - [x] Özel ayarlar/eski build olmadan389 dosyalık yerel kaynak snapshot'ından
    build ve tam52 C/53 header/535 Python+metadata/ELF/native MDB kontrolü PASS.
    Bağımsız temiz makine veya yayınlanmış repo clone'u değildir.
  - [ ] Hâlâ kullanılan tarihsel credential'ı kullanıcı değiştirsin; geçmiş/yayın
    kararı ayrıca verilsin. Router veya Git geçmişi otomatik değiştirilmedi.
  - [ ] Ayrı son onayla commit/push ve yayımlanan repo/kurulum doğrulaması.
  [Tamamlanan teknik kontrol ve kalan yayın kapısı](release_preflight.md).

Aşağıdaki ayrıntılar bu sabit listenin kanıtlarıdır; eski geniş başlıklar yeni bir
senaryo kuyruğu oluşturmaz. Esas geçmiş ve teknik kapsam [roadmap'tedir](roadmap.md).

## Kesin kapanış sırası

1. Kalan uygulamaları ve gerekli kısa kart doğrulamalarını bitir.
2. Test edilecek güncel normal firmware ile uzun çalışma testini yap.
3. Sonuçları kabul ölçütleriyle değerlendir; sorun varsa düzelt ve etkilenen
   testleri, gerekiyorsa uzun koşuyu tekrarla. Tek başına açık kalması PASS değildir.
4. Sonuçlara göre **nihai README** ve bilinen sınırları tamamla; temiz klon ve
   yayın güvenliği kontrollerini yap.
5. Kullanıcı onayıyla **GitHub yayını en son** yapılır; yayımlanan dosya/linkleri kontrol et.

Yerel araç/belge taslakları önceden hazırlanabilir; bu, son README veya yayın
adımının tamamlandığı anlamına gelmez.18 Ekim'de kullanıcı normal commit/push
için açık onay verdi; yayın sonucu ayrıca doğrulanır. Geçmiş rewrite/force-push yok.

## Son kontrol noktası

| Konu | Kanıt / sınır |
| --- | --- |
| Kaynak/masaüstü |18 Ekim özel ayarsız kaynak snapshot'ında tam52 C/53 header/535 Python(27 dosya),22 graph-layout,49 refusal/39 workflow-artifact/97 invalid-profile + metadata/normal ELF PASS. Native MDB16 komut PASS. İlk uzun-path mesaj-format test FAIL'i giderildi ve korundu. Bilgisayar testleri yeni fiziksel flash/USB kanıtı değildir. |
| Startup / lifecycle | 58 app_main ve 183 alt servis host süreci; üç startup hata/panic/ikinci boot yolu ve seçilen EPS→FDIR→ADCS/payload RTOS zinciri kartta geçti. Genel lifecycle matrisi değildir. |
| Diskteki güncel normal build | D36E80…: RAM42972, flash816129;57 C kaynağı tek IDF yolunda, normal ELF'de test hook'u yok. SDK aynı. Onaylı yükleme yapıldı; sonraki ground düzeltmesi firmware değiştirmedi. [Hazırlık](nominal_soak_preparation.md). |
| Kartta son doğrulanan imaj |17 Ekim D36E80…; upload/NVS eşit, boot13→14/history98. Kısa açılış90.984 s ve sonraki NOMINAL3600.2 s PASS. Son STOP/SAFE/held0/ready1/storage1/stable1/history98. Bugün yeni boot/kart kontrolü yapılmadı. [Güncel kanıt](nominal_soak_target.md). |
| Yeni kalıcı recovery | Tek-PANIC paketi 78.422 + 78.797 s; sonraki üç-PANIC BOOT_LOOP paketi 79.610 + 79.734 s, exit 0/RESTORE PASS. Son sayaç7→8→9→10→11→12; FAULT/BAD_RESET/BOOT_LOOP korundu, iki upload boyunca NVS eşitliği doğrulandı. Her çalışan fazda yeni sağlık/onay→SAFE→ayrı NOMINAL geçti. Eski FAIL/kök neden ve kalan matris açık. |
| Önceki lifecycle paketi | E102/SAFE/inhibit, gerçek owner-clear sonrası SAFE'in korunması, iki reddedilen/bir kabul edilen NOMINAL TC, ayrı rearm/veri/kayıt-retention: 24.0 s PASS. Kendi imajı için [tarihsel kanıt](mission_lifecycle_target.md). |
| Son yer istasyonu | Source/runtime MDB yedekli güncellendi; mevcut payload RESET handler'ıyla16 komut, native21 postprocessed/10 range reddi geçti. Görev örneği restart; DB/dataDir silinmedi. İlk aktif başlangıçtaki archive-full/DEGRADED olayları korunur. |
| GitHub / temiz kurulum | Özel dosyasız389 dosyalık yerel kaynak snapshot build/test ve README kapanışı PASS. Push/bağımsız temiz makine doğrulaması yok. Eski credential hâlâ geçerli: kullanıcı rotasyonu, geçmiş/yayın kararı ve son onay bekliyor. [Kontrol](release_preflight.md). |
| Aktif NOMINAL uzun gözlem | D36E80… imajda3600.2 s/120 rapor/720 NOMINAL PASS; diğer mod/yeni archive-full/DEGRADED/reset marker0, heap150108–150500, ilk-son0, stack min864.867 çift export/4286 kayıt ve STOP/SAFE/EMPTY tekrar doğrulandı. Simüle payload ve örneklenen kaynak kapsamıdır. |
| Uzun gözlem | C0719A… normal imajda 3600 s / 120 rapor PASS; 720 SAFE örneği, boot13/history98 korundu. Heap son30 dakika150272 baytta sabit, en düşük stack868 bayt. Yeni ciddi hata/reset gözlenmedi. All-NOMINAL veya eski upload kök nedeni kapanışı değildir. [Kanıt](long_run_target.md). |
| Son NOMINAL hazırlık doğrulaması | Firmware52 C/53 header/533 Python ilk regresyonu; stop marker sonrası535 Python/27 dosya. Filtreli client ve16 komutlu MDB ile535 Python tekrar geçti; native21 postprocessed,49 refusal/39 runner mock/97 negatif profil önceki doğrulamalar. Bilgisayar testleri saatlik kart PASS'i değildir. |

Son yüklenip gözlenen normal `firmware.bin` SHA256:
`D36E800D53AE174BF94468FD99981E8E364A93C37DAECCCBF41398870C5422C4`.
Bu değer karttan okunmuş kimlik değildir.
[Güncel bakım/normal yükleme kanıtı](maintenance_closeout.md);
[önceki BOOT_LOOP/restore](recovery_boot_loop_target.md).

Önceki startup paketinin normal imajı/hash'i ve ilk restore aktarımı kesintisi
[tarihsel kayıtta](startup_target_tests.md) korunur. Son lifecycle koşusunda fallback
gerekmedi; bu, önceki seri kesilme kök nedenini kapatmaz.

## Uzun testten önceki teknik liste

- [x] Recovery öncesi normal imajın kart smoke doğrulaması: 30 kayıt taze export/read-back
  ile korundu; ayrı onaylı baseline ve sonunda normal restore/görev ilerlemesi geçti.
- [x] HK caller init failure ve kısmi required-task başlangıcının seçilen gerçek
  RTOS senaryoları: HK false / CMD_PROC 3/12 / SOLAR_HARV 11/12, SDK PANIC=4 ve
  ikinci boot. Gerçek heap exhaustion veya bütün init konumları değildir.
  [Başlangıç sınırı](startup_policy.md), [kart kanıtı](startup_target_tests.md).
- [x] Mevcut boot/EPS/FDIR/ADCS/payload owner/izin zincirinin seçilen kart
  senaryosunu doğrula: tek E102/revision/owner-clear, SAFE'in korunması, iki NACK/
  bir ACK, gerçek yeni payload verisi ve eski kaydın retention'ı geçti. Ayrı rearm
  servis API'sindendir. [24.0 s seçilen fiziksel koşu](mission_lifecycle_target.md)
  bütün 12 host lifecycle sürecinin kart karşılığı veya genel recovery kanıtı değildir.
- [x] Kullanıcının onayladığı reset sonrası SAFE/ayrı yer onayı, unfinished-boot
  sayacı ve küçük kalıcı fault/reset özeti politikasını kodla; bounded yazma,
  fatal/uncertainty/bozuk kayıt/no-auto-erase sınırlarını masaüstünde doğrula.
  Geçmiş yapışkandır, ciddi olaydan sonraki her boot yeniden onay ister.
  [Uygulama ve kesin sınırlar](recovery_policy.md).
- [x] Kalıcı-durum farkında, orijinal RAM için iki tam retained export gerektiren
  kısa target runner hazırlandı. 55+15+11+16 Python, 15 erken-ret ve 39 workflow/artifact
  mock kontrolü var; baseline ve her upload öncesi/sonrası NVS kapıları bağlıdır.
  Normal/HK-panic imajları derlenip yüklendi. Eski reset runner'ı
  engelli kalır; otomatik NOMINAL oracle'ı kullanılmaz. [Paket ve yan etkiler](recovery_target.md).
- [x] Ayrı onayla yeni normal imajı/MDB'yi kur; orijinal 30 RAM kaydını iki canlı
  retained export ile koru, son normal boot ve canlı HK/event akışını kontrol et.
  İlk paketin başarısızlığı korunur; son geçmiş-farkında tam paket **geçti**.
- [x] Seçilen tam PANIC + strict normal-restore/korunan-geçmiş paketini doğrula:
  30 kayıt iki canlı kopya, sayaç4→5→6→7, FAULT/BAD_RESET retention, iki upload'da
  NVS byte eşitliği ve yeni boot onayı/SAFE→ayrı NOMINAL; exit 0/RESTORE PASS.
  Bir başarılı koşu diğer kalıcılık veya aralıklı aktarım yollarını kapatmaz.
- [x] Seçilen üç ardışık HK caller abort/SDK PANIC/BOOT_LOOP eşiğini ve ardından
  normal restore'u kartta doğrula: boot7→8→9→10→11→12, failed0→1→2→3,
  flags35→35→35→99, 79.610 + 79.734 s / exit 0/RESTORE PASS. Yeni boot onayı
  geçmişi silmez. [Kanıt ve sınırlı kapsam](recovery_boot_loop_target.md).
- [ ] Tekrarlayan seri yükleme kesintisinin kök nedenini ayır. 57600/stub,
  115200/ROM ve uncompressed ROM başarısızlıkları, son 460800/stub başarısıyla
  kapanmaz. Windows aygıt durumu/okunan olaylar fiziksel kopmayı kanıtlamıyor.
  Ek tekli dört normal yazımda kesilme olmadı; runner varsayılanı bu başarılı
  460800/stub yoluyla eşitlendi, kök neden düzeltmesi diye işaretlenmedi.
- [ ] Önceki tam upload sonrası boot sayacı/BAD_RESET geçmişi kaybının kök nedenini
  ayır ve gider. Son tam dört-dosyalı PANIC+restore paketi bunu tekrarlamadı;
  probe'lar artık otomatik fazlara bağlı ve her upload boyunca NVS eşitliği var.
  Bu yeni gözlem eski kaybın nedenini kanıtlamaz; eski fallback boot2 sonucu
  retention kanıtı olarak kullanılmaz. Mevcut FAULT geçmişi için ayrı onay sürer.
- [x] Kalıcılık doğrulamasının sürüm sınırını kaydet: seçilen PANIC/normal-restore
  ve üç-abort BOOT_LOOP yolları geçti; bozuk NVS/diğer reset/başarısız-start ve
  ölçülmemiş yazma-zamanlama varyasyonları kullanıcı kararıyla yeni senaryo işi
  değildir. Bunların gerçek kartta geçtiği iddia edilmez.
- [x] Arşiv CRC için seçilen mevcut bakım/owner-clear yolu kodlandı ve mevcut
  host vakasında doğrulandı; kartta WIPE/CRC enjeksiyonu denenmedi. Ayrı salvage,
  otomatik fatal/uncertainty onarımı ve genel diğer-source repair bu sürüme
  eklenmez; konservatif SAFE/EMERGENCY, kalıcı geçmiş ve ayrı yer onayı korunur.
  [FDIR](fdir_faults.md), [watchdog](watchdog_policy.md).
- [x] Planlanmış watchdog işlerini tamamlandı olarak ayır: heartbeat loss/resume,
  supervisor stall, CPU0/CPU1 idle/IWDT ve çift-core starvation yolları geçti.
  Genel deadlock/debug/cache-off/IRQ varyasyonları ve kesin WCET garantisi
  ölçülmemiş sınırdır; yeni zorunlu test/özellik listesi değildir.

Önceki ilk üç kart alt adımı kendi imaj/sınırlarıyla kapandı; yeni recovery imajının
normal smoke'u ve seçilen upload kalıcılığı/restore paketi geçti; kalan matris açıktır.
Genel init üst başlığının kapsam/sınırları roadmap'te ayrı kalır; bütün SDK/HAL
allocation yolları doğrulanmış
sayılmaz. Recovery fiziksel doğrulaması/yerinde onarım ve watchdog kapsam kapanışı
birbirinden ayrıdır. Genel başlıkları sessizce geleceğe taşıyıp
yalnız soak kalmış gibi işaretlemiyoruz.

## Tamamlanan uzun gözlem: SAFE kaynak/kararlılık kapsamı

- [x] Kullanıcının ertelediği uzun süreli kaynak gözlemi ve sonuç değerlendirmesi.
  3600 s / 120 tam rapor / 720 SAFE örneği PASS; giriş hash'leri ve boot/geçmiş
  eşleşti. Kaynak ölçümleri geçerli; tüm mission yükleri/temiz kurulum iddiası yok.
  İlk plan aşağıda korunur: uzun süreli çalışma, kaynak tüketimi ve
  tekrar üretilebilirlik. İlk öneri 1 saat kesintisiz gözlemdir; iş yükü/mod,
  firmware ve kabul ölçütleri koşudan önce kaydedilmelidir.
  17 Ekim 19:33:34–20:33:36 Türkiye saati koşusu tamamlandı/değerlendirildi.
  Mevcut mod korundu. All-NOMINAL yük iddiası yoktur.
  [Başlangıç, kabul ölçütleri ve sonuçlar](long_run_target.md).
  Önceki 600 s SAFE ölçümü bu işin yerine geçmez.
- [x] Kaynak okuyucusu 300..7200 s, 45 s rapor kesintisi sınırı, örneklenen mod
  ayrımı ve opsiyonel `--require-mode` ile hazırlandı. Yeni JSON dosyasında
  RUNNING checkpoint, PASS/FAIL/INTERRUPTED ve tamamlanmış ölçümler korunur.
  37 okuyucu testi / toplam 390 Python geçti; bir saatlik sahte-saat testi gerçek
  kart dayanıklılık kanıtı değildir. [Koşu ve sınırlar](resource_target.md#uzun-koşu-için-okuyucu-hazırlığı).

Gerçek sensör, AI ve fiilî STM32 port'u kullanıcının isteğiyle aktif kapsamda
değildir; yukarıya yeni zorunlu iş olarak eklenmez. Flight qualification veya
evrensel deadlock/WCET garantisi bu projenin tamamlanma ölçütü değildir.

## Uzun test ve değerlendirme sonrasında

- [ ] Nihai README'yi gerçek uzun test sonuçları, uygulanan davranışlar ve bilinen
  sınırlarla tamamla; hata varsa yayın öncesi düzelt/test döngüsünü bitir.
- [ ] Temiz klonda kurulum/derleme/test/demoyu yeniden üret; yerel hazır toolchain,
  ground jar/class/config ve gizli ayarlar yeni makinede otomatik mevcut değildir.
- [ ] Credential/dosya/Git-geçmişi incelemesi ve gerekiyorsa rotasyon;
  onaylı dosya seçimi, commit/push ve yayımlanan repo bağlantılarının kontrolü.
  Tüm kirli çalışma ağacını otomatik ekleme veya history rewrite yapılmaz.

## Yerel demo hazırlığı / yayın ayrımı

- [x] Kopyalanabilir derleme/test/native/live dry-run akışı ve veri kaybı sınırları
  [demo rehberinde](demo_walkthrough.md) bir araya getirildi.
- [x] Bu makinede native YAMCS, mevcut kart/YAMCS için GET-only HK/event kontrolü
  ve komut üretim önizlemesi tekrar geçti. Kaynak/runtime MDB hash'leri eşleşti.
- [ ] Yukarıdaki rehberin temiz makine/klon testi. GitHub yayını, uzun test ve
  sonuç değerlendirmesinden sonraki son adımdır; yerel demo sonucu bunu kapatmaz.

Yerel `.pio/` logları ve `local/archive/` export'ları Git'ten ignore edilir;
bu tek başına yedek veya yayın değildir. RAM, yer istasyonu veritabanı ve bilgisayar
export'u [ayrı veri alanlarıdır](yamcs_data_safety.md).
