# ESP32 watchdog kart testleri

Bu paket iki ayrı korumayı sınar: tek görev heartbeat kaybının FDIR/SAFE sonucu
ve supervisor çalışmayı bıraktığında SDK TWDT'nin gerçek panic/reset yolu.
Normal firmware'de hata enjeksiyon hook'ları bulunmaz. Bu, uçuş yeterliliği veya
bütün deadlock/interrupt-starvation senaryolarının doğrulaması değildir.

CPU0 idle starvation ve interrupt-mask/IWDT için ayrı, açık RAM-reset onayı
isteyen paket [bu belgede](watchdog_starvation_target.md) açıklanır. Aşağıdaki
2026-10-14 ölçümleri önceki üç imajlı pakete aittir; yeni profiller için kanıt
yerine geçmez.

## Çalıştırma ve veri kaybı

Seri monitor kapalı, kart bağlı ve COM portu doğru olmalıdır. Paket bütün imajları
ilk yüklemeden önce derler; daha sonra normal → task-loss → supervisor-stall
sırasını izler. Yükleme başladıysa `finally`, başarı/hata durumunda normal imajı
geri yükleyip 90 saniyelik normal kontrolü yapar. Başarı için betik exit 0 **ve**
`RESTORE PASS` birlikte gerekir; yalnız restore mesajı testlerin geçtiği anlamına gelmez.
Paket PIO ile derler, aynı dört flash bölgesini SDK layout/size/path sınırlarını
kontrol ederek esptool ile 115200 baud'da yükler. NVS adresi bu listeye dahil değildir.
Normal restore bu yolda başarısız olursa standart PIO uploader ile bir kez denenir.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_watchdog_system.ps1 -Port COM7
```

Başarılı normal baseline'dan sonra yalnız upload transport hatasıyla yarım kalan
paket `-ContinueAfterBaseline` ile sürdürülebilir. Betik, normal baseline PASS
logunu ve normal binary'nin bu kanıttan sonra değişmediği dosya zamanını ister;
bu seçenek yeni/rebuilt firmware için kullanılamaz. Fault senaryoları tekrar
çalışır ve normal restore/gözlem hiçbir zaman atlanmaz. Timestamp guard tam bir
imzalı artifact/provenance sistemi değildir; temiz tekrarlama için varsayılan tam
paketi kullanın. Bu turda baseline başarıyla gözlendikten sonra bu devam yolu kullanıldı.

Her yükleme/reset önceki RAM arşivini, aktif hata nedenlerini, heartbeat latch'lerini
ve deployment-complete bilgisini siler; bunlar geri alınamaz. Gerçek NVS silme,
fault history flush veya SDK config dosyasını değiştirme yoktur. Bu, normal SDK/
NVS/PHY ve firmware yüklemesinin sıfır flash yazısı yaptığı iddiası değildir.
Wi-Fi/router/firewall/credential/YAMCS ayarları değiştirilmez; ground komutu gönderilmez.
Kablo kesilmesi veya sürecin zorla sonlandırılması geri dönüşü engelleyebilir.
Gerekirse normal firmware'i açıkça geri yükleyip doğrulayın:

```powershell
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev -t upload --upload-port COM7
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/watchdog_system_check.py --port COM7 --mode normal --duration 90 --reset
```

## Kanıt ve test-only davranış

| İmaj | Enjeksiyon | Gerekli kanıt |
| --- | --- | --- |
| `esp32dev` | Yok | SDK 15000 ms / idle mask 3 / panic=1, kaynak/arm/core sırası, aralıklı NOMINAL döngüleri, kritik/fatal hata veya reboot olmaması |
| `esp32dev_wdt_task_loss` | Yalnız task 0 (`TM_GEN`) pet çağrısını düşürür; görev çalışmaya devam eder | Gerçek `SRC_CDH/0xC200` critical kaydı, task-0 timeout, SAFE; tek neden/report, overflow/uncertainty yok; en az 25 saniyeye yayılan SDK-fed snapshot'lar, reboot yok |
| `esp32dev_wdt_supervisor_stall` | Altıncı başarılı check + SDK feed sonrasında supervisor kendini askıya alır | SDK timeout, **unserved subscriber** listesinde yalnız `WDT_MON`, gerçek panic, ikinci boot'ta `ESP_RST_TASK_WDT` (6), sonra normal mission ilerlemesi |

Heartbeat düşürme bir pet-path enjeksiyonudur, gerçek mission-task suspension veya
deadlock değildir. Supervisor kendini uygulama kilitleri bırakıldıktan sonra
askıya alır; diğer mission/idle task'ları ve interrupt'lar çalışır. Başka task/ISR/
timer onun yerine SDK'yı beslemez; `esp_restart()` ile beklenen reset taklit edilmez.

Supervisor test imajı SDK'nın gerçek reset nedeni 6 olduğunda yalnız **test
enjeksiyonunu** ikinci boot'ta bastırır. Bu RAM/NVS'ye kalıcı onarım kaydı yazmaz,
normal firmware'e recovery/verified repair veya reboot-loop inhibit eklemez.
Test profilleri birbiriyle veya ağ hata profilleriyle birlikte derlenemez.
`check_watchdog_profiles.ps1` artık 47 geçersiz flag/çekirdek bileşiminin doğru compile guard
ile reddedildiğini ve normal ELF'te `WATCHDOG_TEST_` sembolü olmadığını kontrol eder.

Gözlemci `tools/watchdog_system_check.py` yalnız bilinen kanıt satırlarını ve
sayısal panic tanısını gösterir; keyfi SDK/Wi-Fi loglarını dışarı aktarmaz.
Test imajları ayrıca current-task stack high-water değerini bayta çevirip raporlar;
512 bayttan az veya 4096 bayttan büyük değer başarısızlıktır. Bu, yalnız bu bounded
akışta gözlenmiş minimum alandır; bütün task'ların veya bütün fault yollarının WCET/
stack bütçesi değildir. Normal `WDT_MON` stack'i 4096 bayttır; eski 2048'e göre
ek 2048 bayt dinamik stack tahsisi vardır (PIO statik RAM sayısına dahil değildir).
Beklenmeyen reboot/panic, başka critical/fatal neden, başka SDK subscriber timeout'u,
yanlış sıra/reset nedeni veya eksik kanıt başarısızlıktır. Loglar
`.pio/host-tests/wdt_*` altındadır; üretilmiş/ignored çıktıdır, test kaynakları değildir.

## Masaüstü kontrolleri ve sınırlar

- Gerçek test hook'ları için 11 task-loss + 8 supervisor-stall süreç senaryosu.
  SDK/RTOS/ledger sınırları taklit edilir; fiziksel reset kanıtı değildir.
- Seri kanıt ayrıştırıcısı için 43 offline Python kontrolü; port açılmaz.
- Mevcut 70 watchdog, 13 ESP32 port ve 28 startup senaryosu korunur.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_host_tests.ps1
& 'C:\.platformio\penv\Scripts\python.exe' -B tests/host/watchdog_system/test_monitor.py
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\check_watchdog_profiles.ps1
```

TWDT paylaşılan timer'dır; ölçülen tek stall-to-trigger aralığı her görev için
kesin 15 saniyelik deadline veya üst tepki süresi garantisi değildir. Genel
idle/global starvation, interrupt masking/IWDT, debug/JTAG etkileri, task restart,
verified fault-clear, kalıcı arşiv/fault history ve uzun süreli soak bu paketin
kapsamı dışındadır. Politika: [watchdog sözleşmesi](watchdog_policy.md).

## Derleme maliyeti ve ilk kart bulgusu (2026-10-14)

| İmaj | Statik RAM (327680 bayt sınır) | Flash (1740800 bayt uygulama sınırı) |
| --- | ---: | ---: |
| Normal | 42556 | 799997 |
| Heartbeat kaybı | 42884 | 800893 |
| Supervisor durması | 42564 | 800573 |

Bu sayılar runtime heap/stack/TCB maliyeti değildir. Normal imajda yeni görev
veya mutex yoktur; `WDT_MON` stack tahsisi 2048'den 4096 bayta çıkarılmıştır.
Task-loss imajının salt-okunur snapshot'ı test-only statik RAM'dedir.

İlk task-loss denemesi, BOOT aşamasındaki ölçüm satırının `printf` yolunda
`StoreProhibited` panic ile durdu. Sayısal backtrace ESP32 ELF araçlarıyla
`WATCHDOG_TEST_AfterCheck → __wrap_printf → yamcs_printf_common → vprintf → console_write`
zincirine çözüldü. Test başarısız sayıldı; paket normal firmware'i geri yükledi.
4096 bayt stack ile aynı task-loss yolu başarıyla çalıştı; ölçülen minimum boş
alan 1588 bayttır (gözlenmiş kullanım 2508 bayt, eski 2048 baytı aşar). Böylece
bu yolun eski stack budget'ına sığmadığı target ölçümüyle doğrulandı. İlk başarısız
deneme bir heartbeat/SAFE veya SDK watchdog-reset başarısı olarak sayılmaz.
Sonraki yüklemede USB seri akışı kesildi; hata yine restore yolunu çalıştırdı,
test senaryosu başlamadı. Ortak `upload_speed` 460800'e düşürüldü; UART monitor
hızı 115200 ve Wi-Fi/network davranışı değişmedi.
460800'de de transfer kesilmesi görüldü; paket derlenmiş imajları aynı doğrulanmış
flash bölgelerine 115200 baud ile yükleyecek şekilde güncellendi. Bu yol task-loss
imajında hash doğrulamasıyla tamamlandı; normal PIO ayarı 460800 kalır.

## Bounded kart kanıtı (2026-10-14)

Normal baseline 95.8 saniyelik gözlemde SDK 15000 ms / idle mask 3 / panic=1,
kaynak/arm/core sırası ve NOMINAL döngülerini geçti. Kritik/fatal hata veya reboot
yoktu; mevcut ADCS warning `0x00A2` bu kontrolün başarısızlık koşulu değildir.

Task-loss 65.7 saniyelik gözlemde gerçek `SRC_CDH/0xC200` critical nedenini ve
SAFE'i üretti. Dokuz başarılı SDK-fed fault snapshot'ında `active=1, reports=1`,
`uncertain=0, overflow=0` korundu; başka critical/fatal neden veya reboot yoktu.
Bu enjeksiyon arm sonrası başladığı için BOOT'tan SAFE'e korumayı da kapsar;
normal baseline'ın NOMINAL sonucu bu testin NOMINAL'dan SAFE'e geçtiği iddiası değildir.

Supervisor-stall 96.5 saniyelik toplam gözlemde altıncı check/feed sonrasında
`WDT_MON` self-suspend yaptı. SDK **15.00 saniye** sonra timeout bildirdi; yalnız
`WDT_MON` unserved subscriber olarak listelendi. Gerçek panic/reboot sonrasında
reset nedeni **6 (`ESP_RST_TASK_WDT`)** okundu. Test reinjection ikinci boot'ta
bastırıldı; supervisor tekrar çalıştı, minimum stack boşluğu 1668 bayt ve
45 saniyelik post-supervisor-start gözlemde normal mission döngüleri doğrulandı.
Başka failed subscriber/critical/fatal hata veya üçüncü boot görülmedi.
15.00 saniye tek ölçümdür; paylaşılan SDK timer için deadline garantisi değildir.

Son normal restore, hash doğrulamasıyla tamamlandı. Test profili/hook mesajı
olmadan 95.8 saniyelik normal gözlem ve NOMINAL döngüleri geçti; kritik/fatal
hata veya reboot yoktu. Devam betiği exit 0 ve `RESTORE PASS` ile bitti.
**Kartta normal `esp32dev` bırakıldı.** Önceki RAM arşiv/fault/deployment state'i
yüklemeler/resetlerle silindi; geri alınamaz. Gerçek NVS erase ve ground komutu
gönderilmedi; SDK config SHA256
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087` aynı kaldı.
Normal ELF hook kontrolü ve üç negatif profile-guard kontrolü geçti.

Kanıtlar: `.pio/host-tests/wdt_normal_check.log`, `wdt_task_loss_check.log`,
`wdt_supervisor_check.log`, `wdt_restore_check.log` ve aynı dizindeki build/upload
logları. Bunlar bu makineye/çalıştırmaya ait ignored çıktılardır; GitHub için
tekrar üretme komutları ve bounded sonuç özeti bu belgede tutulur.
