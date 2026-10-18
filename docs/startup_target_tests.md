# Zorunlu başlangıç hataları: kart doğrulaması

2026-10-16. Kod/host hazırlığından sonra kullanıcı ayrı onay verdi ve güncel
RAM korunarak kart paketi tamamlandı. **Üç test PASS, normal restore PASS,
runner exit 0.** İlk restore aktarımının kesilmesi ve başarılı fallback aşağıda
ayrı kaydedildi; bütün yüklemelerin ilk denemede geçtiği iddia edilmez.
Amaç, [mevcut fail-stop politikasını](startup_policy.md) seçilen üç hata
noktasında gerçek FreeRTOS/SDK panic/ikinci boot ile doğrulamaktır. Yeni üretim
recovery, otomatik görev onarımı veya kalıcı reboot-loop inhibit'i eklemez.

## Üç ayrı test imajı

| PlatformIO environment | Enjeksiyon | Beklenen ilk boot sonucu |
| --- | --- | --- |
| `esp32dev_startup_hk_failure` | `HK_Init` yerine bir kez `false`; gerçek HK init çağrılmaz | `Failed to initialize HK service`, görev/kaynak başlangıcına devam etmeden abort |
| `esp32dev_startup_task_early` | Yalnız `CMD_PROC` oluşturmasına `-1`; gerçek create çağrılmaz | Stack 4096 / priority 5 / `required_created=3/12`, abort |
| `esp32dev_startup_task_late` | Yalnız `SOLAR_HARV` oluşturmasına `-1`; gerçek create çağrılmaz | Stack 4096 / priority 1 / `required_created=11/12`, abort |

Bellek tüketilmez/bozulmaz; global SDK tahsisleri başarısız yapılmaz. HK testinde
gerçek mutex allocation/acquire/release yolu değil, caller'ın false-return
sözleşmesi sınanır; bu HK iç yollarının host kanıtı [ayrıdır](startup_policy.md#2026-10-16-hk-başlangıcında-sessiz-hata-kapısı).
`required_created` sayısı önceki başarılı required-create dönüşleridir, çalışan
veya sağlıklı görev sayısı değildir. Önceden oluşturulan görevler hata öncesinde
çalışabilir; atomik startup, cleanup/rollback veya görev durdurma iddiası yoktur.

Pinned IDF 5.5'te `xTaskCreate`, `xTaskCreatePinnedToCore` çağıran always-inline
fonksiyondur. Linker wrapper ikincisine bağlanır; önce function pointer, ardından
isim/stack/priority/NULL argument/NULL output handle/NO_AFFINITY doğrulanır.
Diğer uygulama/Wi-Fi/SDK task çağrıları parametreleri ve sonucu aynen geçirilir.

Test hook'u HK çağrısında bir kez hazırlanır. SDK `ESP_RST_PANIC` bildirirse
yalnız test enjeksiyonu ikinci boot'ta bastırılır; normal init/create gerçekten
çağrılır. Bu, üretim reboot-loop politikası değildir ve NVS/RTC kayıt yazmaz.
İlk görülen boot zaten PANIC ise okuyucu bunu başarı kabul etmez: önce armed
profil, seçilen enjeksiyon, exact error ve gerçek abort görülmelidir. Farklı
reset nedeni, ekstra boot, başka panic/critical/fatal veya test profili reddedilir.

## Okuyucu ve runner

`tools/startup_required_check.py`, timeout'la bölünen UART satırlarını birleştirir.
İlk boot'ta sahte core-ready mesajını reddeder; ikinci boot'ta SDK 15 s / iki idle /
panic ayarını, gerçek reset nedeni **4**, HK/kaynak/arm/core sırasını, task profillerinde
gerçek başarılı hedef oluşturmayı ve en az üç güncel NOMINAL örneğini ister.
Sonrasında supervisor başlangıcından itibaren varsayılan 45 s gözlem yapılır.
Erken oluşturulmuş WDT görevinin ilk abort öncesinde çalışmış olması şart değildir.
NOMINAL işi core-ready logundan hemen önce başlayabilir; scheduler böyle bir
preemption yaparsa yalnız bu yüzden hata verilmez.

JSON dosyası yeni olmalıdır. RUNNING/final PASS/FAIL/INTERRUPTED ve yalnız sabit
tanılar/sayısal kanıt saklanır; ham seri/credentials veya PC/backtrace saklanmaz.
Disk/USB hatası, eksik kanıt veya süre dolması PASS değildir. `--reset` verilmeden
port açılmaz; okuyucu firmware yüklemez veya arşiv korumaz.

Mevcut `test_watchdog_system.ps1 -StartupProfiles` paketi aynı doğrulanmış upload /
finally-normal-restore yolunu kullanır. Dört imaj önce derlenir; normal baseline,
üç ayrı test ve sonunda normal firmware/90 s restore gözlemi zorunludur. Enjeksiyon
profilini kartta bırakmaz; restore başarısızsa paket başarısız kalır. Eski loglar
korunur, her koşu `.pio/host-tests/startup-<UTC>/` altında ayrı dizin kullanır.

## Uygulama önkoşulu

Bu komut onaylı koşuda çalıştırıldı. Her yeni koşudan önce güncel RAM arşivi bilgisayara indirilmeli,
CRC/digest/fsync/read-back ve retention doğrulanmalı; ardından kullanıcı upload /
reset paketine açıkça izin vermelidir. SDK panic ve upload/reader resetleri kartın
arşiv/fault/deployment RAM'ini kaybettirir. Eski disk export'u, yeni RAM kayıtlarının
korunduğu anlamına gelmez. Runner export yapmaz; `-AllowRamReset` bunun otomatik
kanıtı değildir. Gerçek TC veya mode değiştirme bu başlangıç paketinde yoktur.

```powershell
# YALNIZ güncel arşiv korunup resetli paket ayrıca onaylandıktan sonra:
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_watchdog_system.ps1 -StartupProfiles -AllowRamReset -UploadBaud 57600
```

Geçmişte bir seri upload kesilmesi nedeniyle düşük baud seçilmiştir; sorunun
kök nedeni çözülmüş sayılmaz. Seri portun başka monitor'de açık olmaması gerekir.
Bir test başarısız olursa normal restore denemesi yine yapılır; yalnız restore'un
geçmesi test paketinin başarısızlığını gizlemez. Uzun test ve GitHub yayını bu
paketin parçası değildir; [kapanış sırası](release_checklist.md) değişmez.

## Masaüstü sınırı

Gerçek wrapper C kaynakları üç grupta 5 + 15 + 15 süreçle; okuyucu 18 Python
testiyle sınanır. Başka task'ın metadata/return/handle passthrough'u, yanlış
metadata/order/retry reddi, PANIC suppression, yanlış reset'te armed kalma,
seçilen hata/abort sırası, ikinci boot ve eksik/yanlış kanıtlar kapsanır.
Sahte seri port ve saat kullanılır; bu testler gerçek panic/reset kanıtı değildir.
Profil selector aralığı ve 13 diğer test flag'iyle birleşimi 16 negatif derleyici
kontrolüyle reddedilir. Normal ELF'de iki yeni wrapper'ın yokluğu ayrıca denetlenir.

Yerel build/regresyon logları: `.pio/host-tests/startup-target-prep-20261016/`.

## 2026-10-16: Onaylı koşu ve veri koruması

Kullanıcı üç kısa başlangıç testini, güncel RAM'in korunmasını ve sonunda normal
firmware restore/gözlemini onayladı; uzun test veya GitHub yayını bu izinle başlatılmadı.
Usage kesintisinde yalnız derlemeler tamamlanmıştı; karta upload/test yapılmamıştı.
Devam öncesi COM7, üç güncel HK'nin raw/extracted değerleri ve yedi birimi doğrulandı.
Canlı pending manifest'i, eski iki doğrulanmış export ile aynı boot/transfer/request/
digest/count değerlerini verdi. Oturumun 30 kaydı yeniden indirildi, her CRC16,
toplam digest ve exclusive write/fsync/read-back geçti. Taze dosyanın SHA256'sı
önceki iki kopyayla birebir eşleşti:

- `local/archive/pre-startup-tests-20261016.json`
- `local/archive/pre-startup-tests-20261016-retained.json`
- `local/archive/pre-startup-execution-20261016.json`

Üçünün SHA256'sı:
`B5E0482F666338EB217EF7CBD44639BFCA9B042FA1FDA4CECBE7BDD393B37DD1`.
Manifest: boot `13927922775562330963`, transfer `1`, request `1795840004`,
digest `1999430223`, count `30`. CONFIRM/CANCEL/WIPE gönderilmedi. Sonraki onaylı
upload/resetler kartın bu boot'una ait RAM kopyasını kaybettirir; disk export'ları korunur.

İlk canlı HK/event kontrolü uzun bir ardışık tekrar dizisinde bounded 1:1
karşılaştırmanın dışında kaldı ve **başarısız** sayıldı; PASS'e çevrilmedi.
Reset önkoşulu ayrı, scope'u açık bir HK freshness/extraction/unit + pending/boot
kontrolüyle sağlandı. Native pinned YAMCS severity/NUL/empty ve uzun repeat-reduction
fixture'ı yeniden geçti; bu sonuç canlı uzun dizide birebir event eşleşmesi iddiası değildir.
Yerel önkoşul/outer log: `.pio/host-tests/startup-execution-preflight-20261016/`.

Paket dizini: `.pio/host-tests/startup-20261016-201502-6803/`.
Normal baseline 95.8 s geçti. Üç seçilen testin tamamında ilk boot armed,
beklenen return/error, gerçek abort, ikinci boot'ta SDK **ESP_RST_PANIC=4** ve
test enjeksiyonunun bastırılması doğrulandı. İkinci boot'ta normal HK init,
kaynaklar/arm/core ve mission ilerlemesi görüldü; task profillerinde hedef görevin
gerçek create çağrısının `pdPASS` dönüşü ayrıca istendi.

| Test | İlk boot'ta görülen hata | Toplam gözlem | İkinci boot NOMINAL örnekleri |
| --- | --- | --- | --- |
| HK init | False return → HK init error → abort | 51.406 s | 6 |
| Erken görev | CMD_PROC, 4096 / priority 5 / 3/12 → abort | 56.563 s | 7 |
| Geç görev | SOLAR_HARV, 4096 / priority 1 / 11/12 → abort | 56.547 s | 6 |

Her testte ikinci boot supervisor başlangıcından itibaren 45 s koşulu sağlandı;
bu sürenin tamamının NOMINAL/payload-active yükü olduğu iddia edilmez. Tablo
duvar-saati seri gözlemidir, IRQ/panic latency veya kesin reboot deadline ölçümü
değildir. HK hata→abort yaklaşık aynı seri anında; erken/geç görevde 15–31 ms
aralığındaki görünüm CPU WCET olarak yorumlanmaz.

### Normal restore ve kapanış

İlk normal restore, 57600 baud'da uygulama yazımının **%77** noktasında
`The chip stopped responding` / `StopIteration` ile kesildi. Başarısız aktarım
`wdt_restore_upload.log` içinde tutuldu. Hazır fallback normal PIO uploader'ını
460800 baud ile çalıştırdı; dört bölgenin hash doğrulaması ve upload geçti.
Bu tek başarılı fallback, USB/seri kesintisinin kök nedeninin çözüldüğünün veya
düşük/yüksek baud'un her koşulda güvenilir olduğunun kanıtı değildir. Driver,
USB güç/router/firewall veya firmware kontrol eşikleri değiştirilmedi.

Son normal gözlem **95.7 s / 15 NOMINAL döngüsü**, network/log mirror
connected/started ve **RESTORE PASS** ile tamamlandı. Beklenmeyen panic/reset,
critical/fatal veya queue-drop gözlenmedi. Başlangıç baseline'ı da 95.8 s / 15
NOMINAL döngüsüydü. Runner exit **0**; kartta normal `esp32dev` bırakıldı, test
imajı yok. SDK configuration hash'i değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.

Yüklenen normal dosyanın SHA256'sı:
`2ACAA915A14E6EF24A0748E9490548E65B9DD69119EE40D706ADB4150633C62C`.
RAM **42884**, flash **809269**; normal ELF'de startup/watchdog injection hook'u
yok. Dosya hash'i karttan okunmuş build-ID telemetrisi değildir; uploader hash
kontrolü ve normal çalışma gözlemi ayrı kanıtlardır.

Restore sonrası GET-only kontrol ayrıca geçti: üç güncel HK (uptime
105270/110270/115270 ms), yedi alan/birim ve 20 s pencerede **28 INFO olay**;
duplicate wire ve ardışık tekrar çifti 0. TC sayacı **402→402** kaldı. Bu pencere
canlı uzun repeat-reduction kanıtı değildir; önceki başarısız preflight ile native
fixture'ın farklı kapsamları yukarıda korunur. Rapor `live_after.json` dosyasıdır.

Hazırlıkta 46 C/48 header/408 Python (21 dosya), 22 graf/layout, 22 runner refusal
ve 59 watchdog + 16 startup negatif profil geçmişti. Bu kart adımında firmware /
okuyucu kaynakları değiştirilmedi; dört cache'li build tekrar kontrol edildi,
test ELF wrapper bağlantıları ve normal metadata/ELF ayrımı doğrulandı. Tam
C/Python regresyonu yeniden çalıştırılmadı. Native YAMCS kontrolü ayrıca geçti.

Seçilen HK caller-failure ve iki partial-create/panic/ikinci boot yolu kapanır.
Gerçek heap exhaustion, bütün task konumları/SDK/HAL init hataları, genel verified
repair/fatal/uncertainty/persistent reboot-loop veya EPS/ADCS/payload lifecycle
kart matrisi bu paketle kapatılmaz. Uzun test başlatılmadı; son README ve GitHub
yayını kullanıcının belirlediği sırada en sonda kalır.
