# EPS → FDIR → ADCS/payload: seçilen kart senaryosu

2026-10-17. **Ayrı kullanıcı onayı ve güncel RAM korumasıyla kart paketi geçti:
seçilen senaryo PASS, normal restore PASS, runner exit 0.**
Bu paket mevcut izin/owner davranışını sınar; yeni recovery politikası eklemez.
Önceki [183 gerçek-servis host süreci](subsystem_safety.md) gerçek FreeRTOS/kart
kanıtı yerine geçmez. Aşağıdaki sonuç yalnız seçilen gerçek RTOS zincirini kapatır;
genel recovery/fatal/uncertainty ve uzun çalışma maddeleri açık kalır.

## Girdi, gerçek yol ve beklenen sonuç

`esp32dev_mission_lifecycle` yalnız test imajıdır. Mevcut `SOLAR_HARV` görev
yuvasında kontrollü simüle bus gerilimi üretilir; gerçek EPS manager, FDIR/MODE,
deployment, ADCS manager, DATA_LOG/payload, RAM arşiv ve watchdog görevleri çalışır.
SDK ayarları, öncelikler ve görev stack bütçeleri değiştirilmez.

| Sıra | Girdi / işlem | Zorunlu gözlem |
| --- | --- | --- |
| Boot | Sağlıklı 8.5 V, gerçek deployment beklemesi sürüyor | Payload START ve ADCS pointing reddi; DETUMBLE izinli. Birinci gerçek NOMINAL TC reddedilir. |
| Çalışma | Gerçek deployment tamamlanır | Global/local EPS NOMINAL, ADCS pointing ve payload açık; DATA_LOG en az bir gerçek 64-byte payload kaydı yazar. |
| Hata | Yalnız simüle bus 4.9 V yapılır | Gerçek EPS E102 critical kaydı, global/EPS SAFE, payload/heater kapalı, COMMS korunmuş; ADCS/payload SAFE ve byte sayısı durur. İkinci NOMINAL TC reddedilir. |
| Sağlık | Yalnız bus 8.5 V yapılır | Gerçek EPS owner kendi E102 revision'ını çözer. En az 2 s global/local SAFE, servis inhibit ve byte geçmişi korunur. |
| Yetki | Üçüncü gerçek NOMINAL TC | CRC/router/FDIR sonucu ve YAMCS Sent/history doğrulanır. EPS toparlansa bile ADCS/payload kendiliğinden başlamaz. |
| Yeniden başlatma | Test sürücüsü açık ADCS DETUMBLE ve payload INIT/START verir | Yeni veri üretilir, payload STOP ile RAM tüketimi sınırlanır; önceki payload kaydı aynı içerik/CRC ile okunur; üç son durum ve stack sınırı doğrulanır. |

4.9 V, EPS manager'ın 5 V sınırının altında, diğer 3.3/2.5 V monitor yollarının
üstündedir; tek seçilen E102 yolu hedeflenir. Simüle gerilim, fiziksel besleme
gerilimi değildir; USB/gerçek güç kesilmez veya undervoltage uygulanmaz.

## Test izolasyonu ve kanıt sınırı

- Linker wrapper yalnız simüle harvest görevini ve legacy demo injector görevini
  değiştirir. Demo injector bu imajda silinir; normal imajda değişmez. FDIR
  wrapper'ı yalnız beklenen üç exact `0x52, 0x02` komutunu gerçek servise geçirir;
  sonucu gözler, kendisi MODE değiştirmez. Diğer FDIR komutu testi başarısız yapar.
- Driver fault raporlamaz/resolve etmez, MODE set/transition yapmaz, PDU veya yerel
  EPS durumunu yazmaz. Arşive test kaydı eklemez, silmez veya transfer onaylamaz;
  gerçek DATA_LOG kaydını `Archive_ReadRecord` ile CRC doğrulayarak karşılaştırır.
- ADCS/payload rearm komutları bu sınırlı senaryoda doğrudan servis API'sindendir;
  bu komutların ground/router uçtan uca kanıtı iddia edilmez. Yalnız üç global
  NOMINAL isteği gerçek YAMCS → UDP → CRC → router → FDIR yolundan geçer.
- Gözlem API'leri farklı mutex'lerden ayrı snapshot alır; bütün sistemin tek atomik
  fotoğrafı değildir. SAFE sonrası kararlılık tekrar gözlenir. ADCS GetMode
  kendisi inhibit etmez; diğer normal HK gözlemleri/ADCS manager çalışmaya devam
  ettiğinden inhibit'in tek bir göreve atfedildiği iddia edilmez.
- Son yetki kontrolünü geçmiş bir payload işlemi tamamlanabilir. Byte sayısının
  durması, gerçek servisler SAFE olduktan sonra ölçülür; anlık donanım interlock'u
  veya güç kesme garantisi değildir.
- Senaryo bitince sürücü suspend olur; `SOLAR_HARV` handle'ı RES_MON için geçerli
  kalır. Hata halinde de otomatik reset/onarım/clear yoktur. Normal restore dış
  runner'ın sorumluluğudur. Driver stack minimumu IDF byte biriminde 768..4096
  olmalıdır; bu minimum bütün görevlerin stack/WCET kanıtı değildir.

Başka-source repair/resolve, fatal/uncertainty temizliği, persistent history,
reboot-loop politikası, boost/PDU-off matrisi, bütün yarışlar ve uzun süreli
dayanıklılık bu tek seçilmiş kart koşusuyla kapanmaz. [Kapanış listesi](release_checklist.md).

## Okuyucu, masaüstü ve derleme

`tools/mission_lifecycle_check.py`, timeout'la bölünen UART satırlarını birleştirir;
boot/config/core, altı durum, tek E102/owner-clear, iki reddedilen/bir kabul edilen
mode isteği, CRC/route, kayıt-retention ve stack kanıtını ister. Her gerçek TC için
mevcut allowlisted sender dry-run binary/link kontrolü, tek POST ve Sent/CommandHistory
binary kanıtını kullanır. POST tekrar edilmez; eksik kanıt/süre aşımı PASS değildir.
Üç request dışında komut veya archive clear/CONFIRM/CANCEL gönderilmez.

JSON yeni dosya olmalıdır. RUNNING ve final PASS/FAIL/INTERRUPTED fsync ile saklanır;
ham seri/HTTP/credential metni yazılmaz. Worker, başarısızlık dahil normal restore
başlamadan önce durdurulup join edilir. Okuyucunun 150 s üst sınırı bir soak değildir.

Masaüstünde driver'ın 25 süreç senaryosu API/scheduler modelini kullanır: onay
atlama, otomatik resume, yanlış owner/fatal/uncertainty, gözlem/kilit hatası,
eksik veri, CRC retention ve stack sınırı. Bunlar gerçek EPS/FDIR yerine mock
kullanır; gerçek-servis host entegrasyonunun yerini almaz. 16 Python testi fake
UART/clock/sender ile sıra, sonuç, disk checkpoint ve gönderim hatasını sınar.
Ortak runner hiçbir gerçek port/TC kullanmadan bunları keşfeder. 17 negatif
derleme guard'ı diğer test profilleriyle birleşmeyi ve geçersiz selector'ı reddeder.

```powershell
# Yalnız bilgisayar: iki firmware derlenir; karta dokunulmaz.
C:\.platformio\penv\Scripts\platformio.exe run -e esp32dev -e esp32dev_mission_lifecycle
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_checks.ps1 -Compiler C:\cygwin64\bin\gcc.exe -Python C:\.platformio\penv\Scripts\python.exe -BuildMetadata
```

## 16 Ekim hazırlık doğrulaması

Tam host koşusu **47 C grubu / 49 bağımsız header / 43 C fixture**, **22 graf/layout**
ve **32 runner-refusal** kontrolünden geçti. Son okuyucu tanısı eklendikten sonra
bütün Python tekrarlandı: **22 dosyada 424 test PASS**. Negatif profil sayıları
**59 watchdog + 16 startup + 17 lifecycle = 92**. Yeni driver grubu 25 süreçtir;
mevcut gerçek-servis güvenlik grubunun 183 süreci de tam koşuda geçti.

Normal ve test profili derlendi; SDK config hash'i önceki değerle aynı kaldı.
Normal ELF'de test define/hook/host yolu yok; 54 C kaynağı tek IDF build yolunda.
Test ELF'sinde üç wrapper var ve required-task tablosunun son iki function pointer'ı
gerçekten demo/harvest wrapper'larına bağlı. Driver object'i MODE set/transition,
fault report/resolve, archive write/erase/confirm veya watchdog feed çağrısı içermiyor.
Bu statik link/girdi kanıtıdır; gerçek UART/ground davranışının yerine geçmez.

| Diskteki derleme | Statik RAM / flash | firmware.bin SHA256 |
| --- | --- | --- |
| `esp32dev` | 42884 / 809269 byte | `C7F16A266CF42C5142736D97062528F4D0EE1C25EEEADE05790E7888237DB5BF` |
| `esp32dev_mission_lifecycle` | 42884 / 811809 byte | `BE12A442DEB47B260983437D7E56CC14F42119797905218E141B98016326CA34` |

İlk metadata freshness kontrolü normal ELF'nin guard-only kaynak değişikliğinden
eski kalmasını reddetti: SCons boş TU'yu yeniden derledi, fakat aynı içeriği
nedeniyle link'i atladı. Eski üretilmiş ELF kanıt dizinine taşınarak korundu;
normal link gerçekten tekrarlandı, sonra metadata/ELF ayrımı geçti. Guard
atlatılmadı, source timestamp'i değiştirilmedi. Bu build ayrıntısı kart hatası değildir.

Kanıt dizini: `.pio/host-tests/mission-prep-20261016-204838/`;
`host_checks.log`, `python_final.log`, `builds.log`, `normal_relink.log`,
`check_test_layout.log`, `check_component_dependencies.log` ve üç profil log'u.
**Upload, seri port açma, gerçek TC, RAM export/silme, fiziksel test veya GitHub
işlemi yapılmadı.** Kartta önceki [startup paketinin normal imajı](startup_target_tests.md)
kalır; yukarıdaki dosya hash'leri karttan okunmuş kimlik değildir.

## Her yeni kart koşusunda ayrı onay gerekir

Aşağıdaki komut onaylı 17 Ekim koşusunda çalıştırıldı. Tekrar koşmak yeni RAM
koruması ve yeni reset/komut onayı gerektirir.

Önce güncel kart RAM arşivi taze HK/boot/pending kimliği kontrol edilerek dışa
aktarılmalı; CRC/digest/fsync/read-back ve retained tekrar okuma doğrulanmalıdır.
Eski disk export'u yeni RAM'i korumuş sayılmaz. Ardından upload/reset ve **üç
gerçek NOMINAL TC** açıkça onaylanmalıdır. Runner arşiv koruması yapmaz; iki izin
bayrağı korumanın otomatik kanıtı değildir. Her upload/reader reset RAM arşivini,
deployment ve fault durumunu kaybettirir.

```powershell
# SADECE güncel RAM korunduktan ve paket ayrıca onaylandıktan sonra:
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_watchdog_system.ps1 -MissionLifecycle -AllowRamReset -AllowModeCommands -Port COM7 -UploadBaud 57600
```

İki imaj önce derlenir; normal 90 s baseline, lifecycle okuyucusu ve finally
normal upload/90 s doğrulaması yapılır. Konservatif upload başarısızsa mevcut
standard PIO normal-restore fallback'ı kullanılır; önceki kesilme kök nedeninin
çözüldüğü iddia edilmez. Her koşu yeni `.pio/host-tests/mission-lifecycle-<UTC>/`
altında saklanır. Uzun test, son README veya GitHub yayını başlatılmaz.

## 17 Ekim gerçek kart sonucu

Mevcut YAMCS kapalıydı; aynı runtime dizini, derlenmiş sınıflar ve paketli 5.12.0
jar'larıyla arka planda başlatıldı. `dataDir`/MDB/link/credential değiştirilmedi,
Maven clean veya veritabanı temizliği yapılmadı. Kaynak/kurulu/runtime XTCE
hash'leri eşleşti. Native extraction/postprocess/repeat kontrolleri, normal ELF
ayrımı ve 16 lifecycle okuyucu testi tekrar geçti. Kaynak kod değişmediği için
önceki tam 47 C/49 header/424 Python koşusu tekrarlanmadı.

İlk GET-only HK kontrolü sunucu açıldıktan hemen sonra eski arşiv örneklerini
`stale/future` diye reddetti. Üç yeni HK geldikten sonra kontrol PASS oldu;
yaş sınırı veya server/kart saati değiştirilmedi. Native/test kontrolleri bu ilk
başarısız canlı denemeyi PASS saymadı.

### Korunan arşiv

Başlangıçta PENDING yoktu. Yeni BEGIN ile 30 kayıt indirildi, tamamı CRC/digest/
fsync/read-back ile doğrulandı; aynı pending prefix'in 30 kaydı tekrar okunup
ikinci dosya kaydedildi. Son PENDING aynı manifest'i doğruladı. CONFIRM/CANCEL/
WIPE gönderilmedi.

- `local/archive/pre-mission-tests-20261017.json`
- `local/archive/pre-mission-tests-20261017-retained.json`

İki dosyanın SHA256'sı:
`631E8D0D65EF15D98EC6FDBE3C662CDFC8E68DE3A3D21C7F84130E4C961165B1`.
Manifest: boot `13431462940791532080`, transfer `1`, request `2597444394`,
digest `2267418254`, count `30`. Bunlar resetten önceki boot'un disk kopyalarıdır;
paket içindeki baseline/test RAM'i onaylı resetlerde kayboldu. Restore sonrası
yeni RAM'in bu dosyalarla korunduğu iddia edilmez.

### Seçilen zincir

Normal baseline **95.8 s / 15 NOMINAL örneği PASS**. Lifecycle okuyucusu **24.0 s**
ve **tek boot** içinde geçti; tek EPS E102 critical kaydı, revision `1`, tek gerçek
EPS owner-clear, üç CRC/router/FDIR sonucu ve üç YAMCS Sent/history doğrulandı.

| Gözlem zamanı (reset sonrası okuyucu saati) | Global / EPS | ADCS / payload | Üretilmiş byte / aktif neden |
| --- | --- | --- | --- |
| 5.797 s, deployment bitmemiş | BOOT / NOMINAL | DETUMBLE / STANDBY | 0 / 0; birinci NOMINAL isteği reddedildi |
| 17.750 s, gerçek veri | NOMINAL / NOMINAL | NOMINAL / ACTIVE | 64 / 0 |
| 18.250 s, 4.9 V sonrası | SAFE / SAFE | SAFE / SAFE | 64 / 1; PDU yalnız COMMS, ikinci istek reddedildi |
| 20.563 s, owner-clear sonrası | SAFE / SAFE | SAFE / SAFE | 64 / 0; yeniden başlama yok |
| 20.891 s, üçüncü istek sonrası | NOMINAL / NOMINAL | SAFE / SAFE | 64 / 0; ayrı servis rearm'ı hâlâ gerekli |
| 21.969–23.985 s, açık rearm ve STOP sonrası | NOMINAL / NOMINAL | NOMINAL / STANDBY | 128 / 0; üç kararlı son örnek |

Önceki gerçek payload kaydı aynı içerik/CRC ile okunabildi. Driver stack minimumu
**956 byte**; seçilen 768-byte alt sınırının üstünde, evrensel stack payı kanıtı
değil. Ground sayacı arşiv koruması sonrasında **64**, üç mode TC sonrasında
**67** oldu. ADCS/payload rearm doğrudan servis API'sindendir; bu paket onları
ground TC gibi göstermez. B-Dot ilk örneği/boost veya başka neden/fatal çözümü
bu kart koşusuyla doğrulanmış sayılmaz.

### Normal firmware'e dönüş ve sınır

Normal/test/restore flash aktarımında dört bölgenin hash kontrolü geçti; bu
koşuda üç aktarım da 57600 baud'da ilk denemede tamamlandı, fallback gerekmedi.
Önceki seri aktarım kesintilerinin kök nedeni bu tek başarılı koşuyla çözülmüş
sayılmaz. Son normal gözlem **95.7 s / 15 NOMINAL örneği**, **RESTORE PASS** ve
runner **exit 0**; kartta normal `esp32dev` bırakıldı. Dosya hash'leri yukarıdaki
hazırlık değerleriyle aynı; karttan build-ID okunmadı.

Son GET-only YAMCS kontrolü `2026-10-17T08:34:44.006Z`: üç güncel HK, yedi alan/
birim, 20 s pencerede 28 INFO olay, duplicate wire 0, repeat pair 0; **TC 67→67**.
Bu bounded pencere uzun-repeat veya uzun çalışma doğrulaması değildir.

Kanıtlar:

- `.pio/host-tests/mission-lifecycle-20261017-082544-1735/`: upload/build logları,
  `mission_lifecycle_proof.json`, okuyucu sonucu ve normal baseline/restore logları.
- `.pio/host-tests/mission-preflight-20261017/`: `archive_protection.json`/log,
  `native_mdb.log`, `live_before.json`, `live_after.json` ve bütün paket log'u.

Yalnız seçilen kart zinciri kapandı. Diğer-source/fatal/uncertainty recovery,
kalıcı history/restart/reboot-loop politikası ve kalan ölçülmemiş watchdog sınırları
[kapanış listesinde](release_checklist.md) açık. Uzun test, nihai README, temiz
klon veya GitHub yayını yapılmadı.
