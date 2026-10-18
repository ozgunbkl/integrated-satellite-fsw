# RAM arşivi: doluluk ve eşzamanlılık sözleşmesi

15 Ekim 2026. Bu adım arşiv formatını, APID'leri veya YAMCS komutlarını değiştirmez.
Kalıcı flash veya wire ground ACK eklemez. Sonraki
[aktarim çekirdeği paketi](archive_transfer.md) ayrı servis API'leri ekler;
bunların TC/YAMCS bağlantısı bu ilk adımda henüz yapılmamıştı; güncel bağlantı ve
kanıtlar aktarım belgesindedir.

## İşlem sınırı ve kilit

`Archive_Init()` yalnız başlangıçta, görevlerden önce çağrılır. Gerekli mutex
oluşturulamazsa `false` döner; core başlangıcı görev açmadan abort eder.
Başarılı init tekrarlandığında kayıtlar silinmez. Runtime temizliği ayrı ve
yıkıcı `Archive_Reset()` işlemidir.

Yazma, okuma snapshot'ı, kullanım sorgusu ve reset aynı arşiv mutex'ini kullanır.
Runtime kilit bekleme sınırı `pdMS_TO_TICKS(10)`'dur; scheduler/tick çözünürlüğü
ve işlemin kendi süresi ayrıca geçerlidir. Bu, her API'nin duvar saatinde kesin
10 ms içinde tamamlanması garantisi değildir.

- Yazma özel bir kayıt oluşturur; kayıt baytları ve kullanılan alan birlikte
  kilit altında yayımlanır. Doluysa yeni kayıt reddedilir; eski kayıtlar değişmez.
- Okuma indeks kontrolünü ve kayıt kopyasını aynı kilit altında yapar. CRC, özel
  kopya üzerinde kilit bırakıldıktan sonra doğrulanır. Başarısız okumada çağıranın
  çıktı kaydı değişmez. Başarılı sonuç, kopyalandığı andaki tutarlı kayıttır.
- Reset kullanılan alanı ve belleği tek korumalı işlemde sıfırlar. Reset ile
  yarışan yazma, kilidi alma sırasına göre temizliğin öncesine veya sonrasına düşer.
- Kilit alınamazsa yazma/okuma/reset `ARCHIVE_ERR_UNAVAILABLE` döner, o işlem
  kayıtları değiştirmez. Kullanım sorgusu sıfır yerine
  `ARCHIVE_USAGE_UNAVAILABLE` (`UINT16_MAX`) döner; bu değer boş arşiv değildir.

Arşiv kilidi tutulurken zaman/log/TX veya `FDIR_ReportFault` çağrısı yapılmaz.
17 Ekim bakım düzeltmesinin dar istisnası: açık `Archive_Reset()` içinde yalnız
`FDIR_TryGetFaultRevision` ve `FDIR_TryResolveCriticalFault` ile MODE kilidine
tek sıfır-bekleme denemesi yapılır (arşiv → MODE, bekleme yok). Callback, flash,
ağ ve warning kilidi yoktur; sorgu/resolve başarısızsa hata korunur.
Payload gibi çağıranların kendi kilitleri bu ifadeden ayrı kalır.
GET telemetrisi gönderilmeden önce arşiv kilidi bırakılır; yavaş ağ gönderimi
arşiv kilidini tutmaz. Gönderim sırasında reset olsa bile gönderilen özel kayıt
kopyası bozulmaz. Ancak aynı indeks reset sonrasında farklı bir kaydı gösterebilir:
indeks kalıcı kimlik/epoch değildir. Bu yüzden tek GET veya UDP send başarısı
otomatik silme izni sayılmaz; ACK/güvenli toplu boşaltma maddesi açık kalır.

## Doluluk ve kuyruk politikası

4096 bayt RAM'de sabit boy `ArchiveRecord_t` kullanılır. Mevcut host/ESP32
düzeninde kayıt 134 bayt; kapasite 30 kayıt, kullanılabilir toplam 4020 bayttır.
Kalan 76 bayta yarım kayıt yazılmaz. Küçük payload da bir tam slot tüketir.

TM üreticisi 10 elemanlık HK kuyruğuna beklemeden tek gönderim dener. Kuyruk
doluysa yeni paket düşer; daha önce kabul edilen paketler korunur, sonsuz retry
yoktur. Sequence sayacı denemeyle ilerlediğinden kayıp bir sıra boşluğu olabilir.
HK canlı raporlama ve watchdog döngüsü devam eder.

Data Logger kuyruktan aldığı HK örneğini bir kez arşivlemeyi dener. Dolu arşiv
veya kilit timeout'unda o örnek düşer; yeniden kuyruğa koyulmaz, sonraki örnek
işlenebilir. Kuyruk boşsa 100 ms sınırlı receive beklenir. Payload güncellemesi ve
watchdog heartbeat her iki yolda da sürer. Bu politika lossless değildir.

Arşiv doluluğu mevcut `FAULT_ARC_FULL` warning'ini **her başarısız dolu yazma
denemesinde** bildirir. FDIR'nin mevcut warning/escalation politikası değişmedi;
burada fault dedup veya otomatik mode recovery eklenmedi. Payload'ın arşiv
reddinde ERROR'a geçme davranışı da korunur. Reset arşiv alanını açar; diğer
servislerin mod/fault/ERROR durumunu otomatik çözmez. Tek owner-clear istisnası:
açık reset öncesinde yakalanan nonfatal arşiv CRC revision'ı, boş bellek ve
aktarim durumları kilit altında geri okunup doğrulanınca çözülebilir. Yeni revision,
fatal veya kilit başarısızlığı korunur; mod, diğer nedenler ve kalıcı geçmiş
değişmez. Bu veri onarımı veya otomatik silme değildir.

`WIPE_ALL` yalnız tam iki bayt ve `0xAA` anahtar ile aynı reset yolunu kullanır.
Yanlış boy/anahtar veya kilit timeout'u kayıt silmez. Bu anahtar ağ kimlik
doğrulaması değildir. Host testleri yalnız test sürecinin RAM'ini silmiştir;
karta WIPE gönderilmemiştir.

## Doğrulama ve sınırlar

Yeni `archive_sync_check`, gerçek arşiv/CRC kodunu pthread mutex ile çalıştırır:
init tahsis hatası/yeniden deneme, tekrar init'te veri koruma, tüm API'lerde
enjekte ve gerçek contention timeout'u, değişmeyen hatalı çıktı, wipe anahtar/boy,
30 slot ve tekrarlı full denemeleri, FDIR/TX içinden tekrar arşiv erişimi ve
gönderim sırasında reset/yeni kayıt sınanır. Dört paralel üreticinin 60 denemesinde
30 farklı kayıt korunur. Ardından üç thread toplam 3000 okuma/yazma/reset
çağrısında gözlenen başarılı snapshot'ların iç tutarlılığını denetler.

`data_logger_check` gerçek görev gövdesinde başarı/full/unavailable/boş kuyruk
yollarını, tek append denemesini ve payload/watchdog ilerlemesini kontrol eder.
Mevcut TM üretici ve TC ingress testleri queue-full sonrası ilerleme/kayıp
politikasını korur. Eski arşiv bounds/downlink ve gerçek FDIR CRC testleri yeni
mutex API'sine uyarlandı. Ortak core testine archive-init hata/tekrar senaryoları
eklendi (28 → 30).

Bellek bozan hook yalnız `FSW_TEST_ARCHIVE_CORRUPTION` tanımlı test derlemesinde
vardır; normal metadata bu define'ı, ELF kontrolü hook sembolünü reddeder.
Bu kontroller exhaustive interleaving/race-detector veya ESP32 scheduler/lock
latency ölçümü değildir. Kartta yeni firmware henüz çalıştırılmadı.

Normal build 32.38 s: RAM 42604 (+8), flash 803889 (+380) bayt. SDK config hash'i
değişmedi. Yeni mutex'in runtime heap maliyeti bu statik RAM sayısına dahil
ölçülmüş değildir. Upload/reset, canlı YAMCS reload veya ağ ayarı yapılmadı.
Component CMake değişikliğinin eski metadata ile kalmasını yakalayan kontrol
sonrası yalnız generated CMake cache yedeklenip configure tekrarlandı (39.51 s);
imaj boyları aynı kaldı. Son güncel metadata 15 component/59 proje-header bağı/
47 tekil üretim kaynağı ve normal ELF test ayrımı kontrolünden geçti.
Tam regresyon: 33 C grubu, 45 bağımsız header, 22 PowerShell ve 135 Python testi;
ayrıca 3 geçersiz watchdog profili reddi geçti. Native YAMCS testi değişmeyen
MDB/ground kaynakları için bu arşiv adımında yeniden çalıştırılmadı.
