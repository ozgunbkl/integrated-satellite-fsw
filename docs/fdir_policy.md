# FDIR uyarı işleme sözleşmesi

Güncelleme: 2026-10-14. Bu adım yalnız biriken warning'lerin periyodik işlenmesini
kapatır. Sonraki [aktif hata adımı](fdir_faults.md) critical/fatal RAM kaydını,
normal çalışmaya dönüş kapısını ve EPS owner resolve yolunu ekledi. Watchdog ve
diğer kaynaklar için verified recovery açık; uçuş güvenliği iddiası yoktur.

## Çalışma yolu

`app_main()` mevcut 11 göreve ek olarak required `FDIR` görevini oluşturur:
`vFDIRTask`, 2048 bayt stack, priority 4. Core servisleri/kuyruklar/EPS mutex'i
bu aşamadan önce hazırdır. Task-create başarısızlığı mevcut required-task hata
politikasıyla log + abort üretir; sessizce warning processing olmadan devam edilmez.
Mevcut görevlerin stack/priority değerleri ve core/network başlangıç sırası değişmez.

FDIR görevi ilk değerlendirmeyi başlayınca yapar, sonra `vTaskDelayUntil()` ile
1000 ms nominal periyot kullanır. Bağımsız görev seçimi warning processing'i EPS
ölçümüne, telemetri üretimine veya Wi-Fi bağlantısına bağımlı bırakmaz. Eski
watchdog döngüsüne warning processing sorumluluğu yüklenmez.

Bu, scheduler/WCET veya kesin bir saniyelik tepki garantisi değildir. Görev
gecikebilir; delay-until kaçırılmış periyotlarda hemen dönebilir. Console log
süresi de ölçülmedi. Yeni görevin 2048 bayt stack'i ve TCB'si **runtime heap**
maliyetidir; firmware statik RAM raporu bu dinamik maliyeti göstermez. Target stack
high-water/heap/uzun süre çalışma henüz ölçülmedi. FDIR görevi mevcut proje
watchdog'una sonraki adımda `WDT_TASK_FDIR` ID'siyle kaydedildi;
[watchdog sözleşmesi](watchdog_policy.md) supervisor/SDK açık sınırını anlatır.

## Warning kararı

Mevcut politika korunur: her `FaultSource_t` için ayrı sayaç, eşik üç warning,
üçte saturasyon. Sayaç fault-code bazlı değildir; aynı kaynaktaki farklı warning
kodları birlikte sayılır. Zaman penceresi, otomatik sayaç eskimesi ve filtre yoktur.

Örneğin mevcut RAM arşivi dolduktan sonra reddedilen yazmalar `SRC_ARCHIVE`
warning'i üretir. Üçü birikince NOMINAL → DEGRADED artık gerçekten çalışır;
warning kaynağı bu adımda susturulmadı ve arşiv kayıtları silinmedi. Mevcut TM
generator yalnız NOMINAL'da arşiv kuyruğuna paket üretir; DEGRADED'da bu yol bekler,
mode dışından çalışan `HK_RunPeriodic()` canlı rapor yolu devam eder. DEGRADED'a
geçiş Wi-Fi bağlantısının koptuğu anlamına gelmez; bütün DEGRADED mission davranışı
bu warning adımında yeniden tasarlanmadı.

| İşlem anındaki mod | Eşik dolmuş warning için davranış |
| --- | --- |
| NOMINAL | Korumalı NOMINAL → DEGRADED geçişi; başarıda ilgili sayaçlar tüketilir |
| DEGRADED | DEGRADED → DEGRADED doğrulamasıyla batch işlenir; ilgili sayaçlar tüketilir |
| INITIAL_BOOT / SAFE / CRITICAL / EMERGENCY | Mod aynen korunur; eşik dolmuş sayaçlar bekler |
| Mode servisi yok / mod kilidi alınamadı | Başarı iddiası ve sayaç tüketimi yok; sonraki çağrı yeniden değerlendirir |

Bir ve iki warning işlenip silinmez; sonraki warning ile eşiğe ulaşabilir.
Tek kararla bütün eşik dolmuş kaynaklar işlenir; eşik altındaki kaynaklar korunur.
Bekleyen warning, uygun moda gerçekten dönülürse sonraki çağrıda DEGRADED kararı
oluşturabilir. Otomatik SAFE/CRITICAL/EMERGENCY → NOMINAL veya DEGRADED eklenmedi.

`FDIR_Process()` counter mutex'ini 10 ms istekle alır. Alamazsa mode API çağırmadan
döner. Mutex altında pending kaynakları belirler; sırasıyla korumalı
NOMINAL → DEGRADED veya DEGRADED → DEGRADED dener. Her mod kilidi isteği 10 ms'dir;
tek çağrıda en fazla iki deneme vardır, spin/sınırsız retry yoktur. İkisi de
başarısızsa sayaçlar aynen kalır. Bu istek süreleri toplam task latency sınırı değildir.

Yeni `SYSTEM_MODE_TransitionQuiet()` normal Transition ile aynı enum/expected-mode/
deployment kontrollerini kullanır; yalnız log basmaz. Böylece **FDIR counters →
global mode** kilit sırası korunurken sayaç işlemi ile mode kararı serileşir ve
mutex altında I/O yapılmaz. Normal `SYSTEM_MODE_Transition()` bu primitive'i
kullanıp kendi mevcut logunu üretmeye devam eder. Deployment kapısı atlanamaz.
Ters yönde FDIR mutex'i alan mode callback yoktur.

Sayaçlar yalnız geçiş/DEGRADED doğrulaması başarılıysa tüketilir. Sonra FDIR
mutex'i bırakılır, işlenen kaynaklar için karar loglanır. Log o committed işlemi
anlatır; o andan sonra modun değişmeyeceğini veya hatanın fiziksel olarak
giderildiğini söylemez. Yeni bir güvenli mod kararı hiçbir koşulsuz mode yazımıyla
ezilmez. Expected-mode kontrolü geçmiş/sürüm takibi değildir; ABA garantisi yoktur.

## Yerden uyarı onayı ve eşzamanlılık

`FDIR_CMD_ACK_FAULT` aynı counter mutex'i altında bütün warning sayaçlarını temizler.
ACK transaction'ı önce kazanırsa sonraki işlem eski bir snapshot'a dayanarak
DEGRADED kararı veremez. Processing önce kazanmışsa ACK bitmiş mode kararını geri
almaz; yalnız sayaçları temizler. Aynı anda gelen komutun önce işleneceği garantisi
yoktur; mutex transaction sırası belirleyicidir.

İşlem bitince gelen yeni warning yeni sayaca eklenir; eski batch'in temizliği onu
silemez. ACK başarısızsa counters korunur ve mevcut `CMD_NACK` yolu çalışır. Başarılı
ACK logu yalnız warning temizliğini ve critical/fatal kayıtların korunmasını söyler;
yeni komut/ACK-event protokolü eklenmedi. NULL/boş komut
pointer'ı dereference edilmez.

ACK **hatayı tamir etmez, moda dönüş sağlamaz, deployment'ı tamamlamaz**. Mevcut
`FDIR_HasActiveFaults()` artık warning sayaçlarına ek olarak active critical/fatal,
overflow ve recording uncertainty'yi sorgular; sorgu hatasında konservatif true'dur.
ACK bu yeni kayıtlara dokunmaz. False, fiziksel bütün hataların giderildiği kanıtı değildir.

## Açık kalan güvenlik işleri

- Critical/fatal neden kaydı ve safety floor, sonraki adımda aynı MODE mutex'i
  altında birleştirildi; non-EPS critical artık kayıt + SAFE koruması üretir.
  [Aktif hata sözleşmesi](fdir_faults.md) operating gate ve uncertainty davranışını açıklar.
- EPS dışındaki kaynaklar için verified repair/resolve, fatal/uncertainty recovery
  ve bütün mod/mission politikası açık. Bu warning task'ı genel repair servisi değildir.
- Sonraki adımda software heartbeat koruması ve görev bazlı critical timeout FDIR
  kaydı eklendi. Daha sonra onaylı SDK supervisor/idle-stall reset politikası
  uygulandı: shared 15 s/panic, completed check sonrası feed. Öncelikler aynı;
  target reset/stall deneyi ve verified task recovery açık. Reset RAM fault/arşiv
  geçmişini kaybettirir. Arşiv mutex/kalıcı depolama ve fiziksel HAL işleri de açık.

## Doğrulama

`tests/host/fdir/check.c` gerçek FDIR service/task ve mode service ile **73** ayrı
süreç senaryosu çalıştırır: altı mod × 0..4 warning (30), altı kaynak (6), ayrıca
37 init/kilit/retry/task/ACK/eşzamanlı karar yolu. Altı gerçek task senaryosunda üçer
döngü çalıştırılır; tick/delay, mutex ve YAMCS sınırları taklit edilir. Log/event/
delay sırasında mutex tutulmadığı, lock sırası, güvenli modların korunması,
başarısızlıkta sayaç saklanması, ACK öncesi/sonrası transaction sırası ve eski
batch sonrası yeni warning korunması kontrol edilir. RTOS thread scheduling,
priority inheritance, donanım hata enjeksiyonu veya canlı yer istasyonu testi değildir.

Ortak başlangıç grubuna `task_FDIR` hata yolu eklendi: **25** süreç senaryosu,
12 required task'ın function/name/stack/priority/sırası ve her create hatası.
2026-10-14: **18 C host grubu**, mevcut **79 Python kontrolü** ve normal `esp32dev`
derlemesi geçti. Statik RAM 42188/327680, flash 796085/1740800 bayt; SDK config
SHA256 değişmedi. Mevcut compiler uyarıları temizlenmedi. Karta yükleme/reset,
RAM arşivi silme, Wi-Fi/YAMCS/router/firewall/kimlik bilgisi değişikliği yapılmadı.
