# Aktif FDIR hataları ve toparlanma engeli

Güncelleme: 2026-10-17. Kritik/fatal nedenlerin RAM kaydı ve normal çalışmaya dönüş
kapısı uygulanmıştır. Ayrı sampled kalıcı özet/reset sonrası kilit kod/masaüstü/
build'de eklendi; henüz kartta doğrulanmadı. [Recovery politikası](recovery_policy.md).
Gerçek donanım iyileşmesi, flight-qualified güvenlik veya bütün recovery yollarının
tamamlanması iddiası değildir.

## Kayıt ve sahiplik

FDIR, geçerli critical/fatal raporları `(source, fault_code)` anahtarıyla tutar.
Sabit kapasite 16 aktif kayıttır; yeni heap tahsisi, arşiv yazımı veya flash/NVS
yazımı yoktur. Aynı kaynaktaki farklı kodlar ve farklı kaynaklardaki aynı kodlar
ayrı kayıtlardır. Aynı anahtarın raporlanması yeni slot açmaz: ilk/son MET zamanı,
saturating report count ve yeni revision tutulur. Critical → fatal yükselir;
sonraki critical raporu fatal'ı düşürmez.

Kayıt, mode guard ve anlık güvenli mod kararı **aynı MODE mutex'i altında** commit
edilir. Bu yüzden kayıt yapıldıktan sonra mod engelinin ayrıca yayınlanmasını
bekleyen bir pencere yoktur. Fiziksel storage `system_mode` içindedir; FDIR kendi
snapshot/revision/resolve API'leriyle kullanır. `system_fault.h` yalnız platformdan
bağımsız tipleri içerir; MODE → FDIR bağımlılığı veya ters mutex alma yoktur.
Warning counter mutex'i ayrı kalır; önceki warning task/politikası korunur.

`FDIR_GetActiveFaults()` yalnız critical/fatal kayıtların bounded snapshot'ını
verir; warning sayaçlarını içermez. `count`, `overflow`, `recording_uncertain`
alanları da değerlendirilmelidir. Başlatılmamış/kilit alınamayan serviste `false`
döner ve çıktıyı değiştirmez; bu sonuç boş veya sağlıklı bir kayıt tablosu değildir.
`FDIR_HasActiveFaults()` artık warning **veya** critical/fatal/overflow/uncertainty
varsa true döndürür. Sorgu hatasında konservatif true'dur; fiziksel bütün hataların
tespiti veya atomik warning+ledger snapshot garantisi değildir.

Tekrar FDIR/MODE init kayıtları temizlemez; mevcut once-only init reddi sürer.
Aktif ledger sadece bu boot'un RAM'indedir. Reset bu kayıtları ve RAM arşivini
kaybettirir. 17 Ekim'de ayrı düşük öncelikli observer'ın 48 baytlık kalıcı özeti
eklendi; her raporu kalıcı kaydetmez, aktif ledger'ı sonraki boot'ta yeniden
oynatmaz. FDIR report/ISR içine flash I/O eklenmedi. Ciddi geçmiş sonraki her
boot'ta sağlık/yer onayı gerektiren bağımsız çalışma kilididir; root-cause repair
yerine geçmez. Bu son uygulama adımında kart/reset/NVS erişimi yapılmadı.

## Merkezi mod politikası

| Aktif neden | İzin verilen hedef modlar | Engellenen hedefler |
| --- | --- | --- |
| Critical, fatal yok | SAFE / CRITICAL / EMERGENCY | INITIAL_BOOT / NOMINAL / DEGRADED |
| Fatal | Yalnız EMERGENCY | Diğer beş mod |
| Recovery boot kilidi veya recovery kalıcı-IO hatası, aktif fatal yok | SAFE / CRITICAL / EMERGENCY | INITIAL_BOOT / NOMINAL / DEGRADED |
| Kayıt yok, overflow/uncertainty yok | Mevcut manuel mode yetkisi | Deployment bitmeden NOMINAL/DEGRADED hâlâ yasak |

BOOT da engellenir; çünkü mevcut ADCS detumble yolunun BOOT izni vardır. Böylece
aktif hatadan sonra BOOT seçerek operating-mode engelinin çevresinden dolaşılamaz.
Fatal kaydın SAFE/CRITICAL komutuyla daha zayıf bir moda düşürülmesi de reddedilir.

Critical rapor, BOOT/NOMINAL/DEGRADED'dan SAFE oluşturur; mevcut SAFE/CRITICAL/
EMERGENCY kararları korunur. Artık **EPS dışındaki critical raporlar da**, örneğin
arşiv CRC hatası, kayıt ve koruma üretir. Fatal rapor EMERGENCY oluşturur.
Record API quiet'tır; FDIR rapor logunu kilit bırakıldıktan sonra üretir.

`Set`, `Transition`, `TransitionQuiet` ve deployment completion bu kapıyı kullanır.
Yer mode komutu eski ACK/NACK protokolünü kullanır; engellenmiş isteğe başarı ACK'i
vermez. Completion yine kaydedilebilir fakat aktif nedenleri silmez veya NOMINAL
sağlamaz. Bu aktif-ledger mekanizması HK paketine alan eklemez. Ayrı recovery
komutu `FDIR_RearmBoot` yalnız kendi boot kapısını hedefler; fault resolve veya
NOMINAL komutu değildir ve kurulu ground'a henüz eklenmedi.

`Get` ve `WithCurrent` konservatif effective mode verir. Böylece mevcut EPS/ADCS/
payload izin callback'leri de aynı korumayı görür. Önceden geçerli izinle başlamış
tek işlem geriye dönük iptal edilmez; yeni karar sonraki izin kontrolünü engeller.
Fiziksel yük kesme/hard interrupt interlock veya kanıtlanmış tepki süresi değildir.
Ledger boşken bütün modlar için yeni bir genel severity/transition grafiği eklenmedi.

## Taşma ve kayıt belirsizliği

16 slot doluyken farklı bir neden gelirse mevcut kayıtlar üzerine yazılmaz.
`overflow` latch'i konur; normal çalışma engeli korunur. Eksik neden fatal ise
EMERGENCY floor'u da korunur. Bilinen kayıtların owner tarafından çözülmesi bile
bu belirsizliği temizlemez. 32-bit revision tükenmesi de aynı konservatif yoldur;
revision sessizce wrap edip eski bir clear token'ını geçerli kılamaz.

Record sırasında MODE kilidi 10 ms'de alınamazsa veya servis yoksa bağımsız C11
atomic uncertainty latch'i set edilir. Eksik kaydın anahtarını yeniden oluşturduğumuz
iddia edilmez; `recording_uncertain=true` ve konservatif SAFE/EMERGENCY effective
mode işletilir. ACK, sonraki başarılı rapor, resolve veya tekrar init bunu silmez.
Bu bilinmeyen neden/overflow için bu boot'ta normal çalışmaya dönüş yolu yoktur;
genel yerinde belirsizlik repair'i ayrı iştir. Otomatik reboot eklenmedi. Kullanıcı
onaylı reset-sonrası review kapısı bu latch'i aynı boot'ta temizlemez; yeni boot'ta
geçmiş korunur, manuel onay otomatik root-cause doğrulaması sayılmaz.

Normal snapshot/query/resolve kilit hatası yeni uncertainty üretmez: sorgu başarısız
sayılır veya mevcut kayıt korunur. Atomic fallback, report/permission'ın gerçek
scheduler üzerinde ne kadar süreceğini kanıtlamaz. Gerçek RTOS iki-core yarış ve
priority inheritance bu host testlerinin yerine geçmez.

## EPS düşük voltajının çözülmesi

EPS manager yalnız `(SRC_EPS, FAULT_EPS_LOW_VOLTAGE)` critical kaydını hedefler.
16 Ekim'de watchdog da yalnız kendi task-timeout kayıtları için owner resolver
oldu; [ayrı liveness koşulları](watchdog_recovery.md). Mevcut 8.5 V eşiği değişmedi:

1. Kaydın revision'ı **ölçümden önce** quiet MODE sorgusuyla alınır.
2. EPS data mutex'i alınır; mevcut simüle bus voltage okunur.
3. Yalnız `isfinite(V) && V >= 8.5` ve bilinen, sıfır olmayan revision varsa resolve
   denenir. NaN, ±infinity, düşük voltaj veya başarısız sorgu/lock temizleme yapmaz.
4. Clear yalnız aynı kaynak/kod/revision ve hâlâ critical olan kayıt için commit
   edilir. Daha yeni bir rapor/promotion varsa eski token reddedilir.
5. Resolve mode değiştirmez. Global SAFE ve yük kesme korunur; deployment ve diğer
   nedenler uygun olunca ayrı yer komutu gerekir. EPS ardından mevcut RECOVERY →
   NOMINAL yerel akışını kullanır; ADCS/payload explicit restart koşulları aynıdır.

Revision değişmezse bile EPS'nin live değeri kendi mutex'i altında doğrulanır ve
quiet resolve bu mutex bırakılmadan yapılır. Kilit sırası EPS → MODE'dur; resolver
FDIR warning mutex'ini, başka kilidi veya I/O'yu kullanmaz. MODE callback içinden
FDIR çağrısı yapılmaz. Clear sonucu logu ancak EPS kilidi bırakılınca üretilir.

Geçerli bir clear'dan sonra yeni low-voltage raporu gelirse yeni revision ile
tekrar latch edilir. Önceki örnekten gecikmiş bir rapor da konservatif yeniden
latch oluşturabilir; sonraki geçerli owner gözlemi çözebilir. Source sample
generation/freshness protokolü bütün raporlayıcılara eklenmedi. Fiziksel gerilim
değişimi kaydedilmiş yeni hata değildir; tespit yine mevcut periyodik ölçümlerle
yapılır. Tek finite örnek simülasyon health kuralıdır; dwell/filter/sensör kalibrasyonu
ve fiziksel batarya kararlılığı kanıtı değildir. CDH'nin >6 V CRITICAL → SAFE yolu
bu kaydı temizlemez ve NOMINAL izni vermez.

## ACK, arşiv ve fatal sınırı

Ground ACK_FAULT yalnız warning sayaçlarını temizler; mesaj artık critical/fatal
kayıtların korunduğunu açıkça söyler. Arşivden geçerli başka kayıt okumak CRC kaydını
çözmez; EPS toparlanması başka bir arşiv/fatal/taşma nedenini temizlemez.
17 Ekim bakım düzeltmesi mevcut açık WIPE/reset yoluna dar bir owner-clear ekler:
reset öncesi CRC revision'ı alınır, boş arşiv ve aktarım durumları doğrulanır,
yalnız aynı nonfatal revision çözülebilir. Diğer hatalar, mod ve kalıcı geçmiş
korunur. Otomatik silme, veri onarım/quarantine veya fatal yeniden başlatma
politikası eklenmedi. Fatal kayıt generic resolver tarafından da reddedilir.

`FDIR_ResolveCriticalFault(source, code, revision)` trusted owner servis API'sidir:
ayrı health doğrulaması **caller'ın sorumluluğudur**, capability/authentication veya
kendi içinde bütün sensörleri doğrulayan mekanizma değildir. Ground handler buna
bağlı değildir. `TryGetFaultRevision` / `TryResolveCriticalFault` aynı validasyonla
tek sıfır-bekleme MODE mutex denemesi yapar; busy/query failure çıktı değiştirmez,
resolve failure kaydı korur ve yeni uncertainty üretmez. Mevcut 10 ms API'leri aynıdır.
Üretimde arşiv CRC owner'ı artık bu açık bakım yoludur. Arşiv kilidi altında
yalnız sessiz/sıfır-bekleme Try API'leri kullanılır. Busy, fatal veya değişmiş
revision çözülmez; `DEFERRED` bakım sonucu bildirilebilir. Kartta WIPE denenmedi.

## Doğrulama ve açık işler

`tests/host/fdir_faults/check.c`: gerçek MODE/FDIR/EPS/Archive/CRC kaynaklarıyla 117
ayrı süreç. 48 API/target/severity gate, 12 kaynak/severity, sekiz operation izni,
10 voltage health ve 39 init/record/overflow/revision/ACK/EPS-race/CRC/completion yolu.
CRC bozulması yalnız host process'in özel RAM'inde; kartta fault komutu yoktur.
RTOS/mutex/time/YAMCS sınırları deterministik taklit edilir; canlı TCP/UDP veya
fiziksel fault/scheduler/heap testi değildir. Revision/report-count son değerleri
4 milyar kez döngü yapılarak test edilmedi; tükenme/saturasyon kod koşullarıdır.

Eski testler kapıyı atlamaz: gerçek LOW state seed'inin operating/BOOT mode isteği
artık NACK bekler; EPS monitor yarışında operating bypass reddi ve ayrı geçerli
CRITICAL writer ile CAS reddi doğrulanır. Önceki assertion'lar sessizce kaldırılıp
fault tabloları resetlenmedi. Warning grubunda non-EPS critical ve ACK beklentileri
yeni kayıt anlamına göre güçlendirildi; warning/task sınırı hâlâ ayrı 73 senaryodur.

2026-10-14: toplam 19 C host grubu ve mevcut 79 Python kontrolü, normal `esp32dev`
derlemesi geçti. Statik RAM 42524/327680, flash 797241/1740800 bayt; önceki imaja
göre +336 statik RAM baytı. SDK config SHA256 aynı; compiler uyarıları topluca
temizlenmedi. Karta yükleme/reset, arşiv silme, Wi-Fi/router/firewall/YAMCS/credential
değişikliği yoktur. Karta en son yüklenen imaj önceki normal firmware'dir;
o ilk registry ekleme adımında değişiklikler yalnız derlendi, karta yüklenmedi.

Sonraki [software watchdog adımı](watchdog_policy.md) task timeout'larını bu kayıt
kapısına bağladı; tek heartbeat'in yeniden gelmesi veya ACK bu nedenleri çözmez.
16 Ekim owner politikası yalnız aynı task-timeout revision'ını yeni, en az 30 s
kararlı liveness penceresinden sonra çözer. Mode, diğer nedenler ve belirsizlik
korunur; 54 watchdog ve 10 Try API senaryosu eklendi (132/127 toplam).
Host/normal build sonrasında TM_GEN için iki gerçek timeout/clear, SAFE'in
korunması ve ayrı mode komutları [kartta doğrulandı](watchdog_recovery_target.md).
Diğer-source/fatal/uncertainty ve domain repair bu sonuçla doğrulanmış sayılmaz.
SDK/supervisor-stall politikası daha sonra kullanıcı onayıyla eklendi: shared
15 s/idle/panic, yalnız completed check sonrası feed. Reset bu per-boot tabloyu
kaybettirir; reset verified repair değildir. Sonraki kontrollü kart paketi tek
`SRC_CDH/0xC200` heartbeat kaybında SAFE/tek neden korunmasını ve gerçek SDK
supervisor timeout/TASK_WDT reset reason 6 yolunu doğruladı; EPS/fatal/CRC/clear
matrisinin tamamı fiziksel olarak test edildi demek değildir.
[Kart kanıtı](watchdog_target_tests.md).
Sonraki kart adımları CPU0/CPU1 idle/IWDT, çift-core starvation ve seçilen kalıcı
recovery yollarını doğruladı; yukarıdaki ilk aşama notları tarihsel kanıttır.
17 Ekim kullanıcı kararıyla yeni hata senaryosu eklenmez. Genel fatal/uncertainty
onarımı, tüm olası interleaving'ler ve kesin WCET doğrulanmış değildir;
bunlar yeni zorunlu özellik kuyruğu değildir. Güncel kapanış listesi ve eski
seri aktarım/geçmiş kaybı riski [ayrı tutulur](release_checklist.md).

Bakım düzeltmesi mevcut `real_archive_crc` host vakasını genişletir: hatalı
WIPE boyu/anahtarı veriyi korur; geçerli WIPE yalnız CRC'yi çözerken diğer CDH
critical ve SAFE korunur; fatal CRC silinmez. Yeni kart enjeksiyonu/profil yoktur.
Tam regresyon: 51 C grubu, 52 header, 522 Python; normal build/ELF kontrolü geçti.
