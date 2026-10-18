# Component bağımlılıkları ve tek derleme yolu

2026-10-17 güncel kontrol: recovery component'i ve uygulama adapter'ı eklendi.
**16 component / 63 doğrudan proje-header bağı / 57 production C** için kaynak ve
taze normal build metadata/ELF kontrolleri geçti; proje grafı döngüsüz, her kaynak
tek IDF compilation yolunda. Recovery'nin MODE/ortak komut bağı private'dır;
CDH ve main recovery'yi private kullanır. Bu runtime kilit döngüsü veya fiziksel
NVS doğrulaması değildir. [Yeni davranışın sınırları](recovery_policy.md).
Aşağıdaki önceki sayılar kendi tarihli taşıma adımlarının kanıtlarıdır.

2026-10-14: 11 proje component'i ve `main` incelendi. Kayıtlı C kaynaklarında
doğrudan kullanılan proje header'ları için eksik 14 component bağı eklendi:

| Consumer | Eklenen private bağımlılıklar |
| --- | --- |
| adcs | cdh |
| archive | cdh, time |
| cdh | adcs, archive, eps, payload, time |
| comms | cdh, time |
| eps | adcs |
| payload | cdh |
| main | cdh, eps |

Mevcut public `REQUIRES` bildirimleri korunur; yeni bağlar C gövdelerinde
kullanıldığı için `PRIV_REQUIRES` olarak bildirildi. Çalışma mantığı, görev
metadata'sı, SDK config, ağ veya kart imajı değiştirilmedi.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\check_component_dependencies.ps1
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev -t compiledb
```

Kaynak kontrolü **12 component / 48 doğrudan proje-header bağı** için geçti.
Normal profilin CMake yapılandırması ve compilation database üretimi **29.66 s**
ile geçti; firmware compile/link veya upload yapılmadı. Root
`compile_commands.json` üretim çıktısıdır ve Git'ten ignore edilir.

Kontrol betiği mevcut literal `SRCS`/`.` include düzenini esas alır, SDK ve
linker-only bağımlılıklarını doğrulamaz. Aynı klasördeki header önce seçilir;
iki farklı `utils.h` bu nedenle yanlış sahiplenilmez. Conditional test kaynakları
da muhafazakâr şekilde taranır. Bu tam CMake/C preprocessor parser'ı değildir.

İlk 14 Ekim adımında global `-I` flag'leri ve `lib_extra_dirs` korunmuştu.
15 Ekim'deki devam adımı aşağıdadır. Karşılıklı component bağları hâlâ vardır;
yalnız doğru bildirmek onları bağımsız katmanlara ayırmaz. Ortak tip/komutların
CDH'den ayrılması, kalan gereksiz public bağlar ve tüm header'ların bağımsız
derlenmesi genel component/header sahipliği maddesinde açık kalır. Aşağıdaki
ikinci 15 Ekim adımı yalnız belirtilen beş header'ı ve ADCS public bağlarını kapatır.

## 2026-10-15: global include ve çift derleme yolunun kaldırılması

- `platformio.ini` içindeki `lib_extra_dirs = components` ve 11 genel proje
  `-I` yolu kaldırıldı. ESP-IDF `components/` dizinini CMake kayıtlarıyla bulur;
  aynı kaynaklar ikinci kez PlatformIO LDF kütüphanesi yapılmaz.
- `-Wl,--wrap=printf`, normal/test profil mirası, seri hızlar, partition ve SDK
  config dosyası korundu. Private `local/wifi_credentials.h` include'u yalnız
  `main` CMake hedefinde kalır; içerik okunmadı/yazdırılmadı/değiştirilmedi.
- HK'nin `arpa/inet.h` kullanımı için doğrudan `PRIV_REQUIRES lwip` eklendi.
  ESP-IDF 5.5 `tools/cmake/build.cmake` içindeki ortak gereksinimler incelendi:
  `freertos`, `esp_hw_support`, `log`, `esp_common`, `esp_system` gibi SDK temel
  component'leri bütün IDF hedeflerine zaten sağlanır. Bu, proje bağımlılıklarını
  genel `-I` ile gizlemekle aynı mekanizma değildir.

Tek normal `esp32dev` firmware compile/link **175.08 s** ile geçti. Ardından
yalnız üretilmiş root compilation database yenilendi (**3.71 s**, firmware yeniden
derlenmedi). `No dependencies` mesajı yalnız PlatformIO LDF içindir; ESP-IDF'nin
proje/SDK component bağımlılıklarının olmadığı anlamına gelmez.

```powershell
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\check_component_dependencies.ps1 -BuildMetadata
```

Kontrol **12 component / 48 doğrudan proje bağı** için geçti. SDK'nin güncel
`.pio/build/esp32dev/compile_commands.json` çıktısında **47 kayıtlı proje C dosyası
birer kez** bulunuyor. Güncel `Integrated_Satellite_FSW.map` içinde eski LDF
`lib<hash>` arşivleri yok. Normal ELF'te watchdog enjeksiyon hook'u yok;
watchdog/log/time/CRC yollarındaki sekiz gerekli entry tek tanımlı.

| Normal imaj | Statik RAM | Flash |
| --- | ---: | ---: |
| Önceki çift yol | 42556 | 799997 |
| Yalnız ESP-IDF | 42564 | 800053 |
| Fark | +8 | +56 |

Statik RAM tam runtime heap/stack bütçesi değildir. Binary-identical imaj veya
boyut tasarrufu iddiası yoktur. C çalışma mantığı, görev stack/öncelikleri ve
protokol kaynakları değiştirilmedi. Mevcut ADCS/FDIR unused-variable ve HAL
type-limits uyarıları ayrı iş olarak kaldı.

SDK config SHA256 yine
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Karta yükleme/reset veya RAM arşivi silme yapılmadı; kartta önceki doğrulanmış
normal imaj kalır. Yeni imajın fiziksel testi, tüm test profillerinin derlemesi,
23 C/122 Python regresyon paketi bu adımda yeniden çalıştırılmadı.

## 2026-10-15: public header sınırları ve bilinen uyarılar

- ADCS servis başlığı FDIR'ı dışarı taşımıyor; kendi `bool` gereksinimini
  `<stdbool.h>` ile sağlıyor. ADCS'nin public başlıkları yalnız standard/ADCS
  tiplerini kullandığından altı component bağı `PRIV_REQUIRES` oldu.
- CDH'nin EPS/TC task prototipleri gereksiz RTOS/frame include'larını taşımıyor.
  Kullanıcısı ve implementasyonu olmayan `vTC_SetSystemMode` prototipi kaldırıldı;
  gerçek mod geçişi API'si değişmedi. EPS telemetri include guard'ı bütün dosyayı
  kapsıyor; public semaphore tipi nedeniyle RTOS bağı burada korunuyor.
- HAL GPIO kayıt sabitlerinin tek sahibi `hal_config.h`. 8 bit adres API'si
  korunuyor: 0..255 aralığının tamamı geçerli, 256 kayıt şartı `_Static_assert`
  ile doğrulanıyor. Ulaşılamayan sınır/hata dalları kaldırıldı; daha büyük bir
  adres mevcut API'de temsil edilemez. Banka boyutu değiştirilirse derleme durur.
- Kullanılmayan ADCS `world_mag_field`, FDIR `heater_fault_active` ve eski tekrar
  eşiği kaldırıldı. Gerçek manyetik alan simülasyonu, warning eşiği, mod/güç
  korumaları ve task stack/öncelikleri değiştirilmedi.

24 C grubu (yeni mock HAL dahil) ve 5 ayrı header translation unit'i geçti.
Host gruplarında `-Wno-error=unused-variable` istisnası artık yok. Python kaynakları
değişmedi ve önceki 122 Python kontrolü bu adımda yeniden çalıştırılmadı.
Kalan ortak tip/komut sahipliği, diğer component'lerin public/private bağları,
tam header kapsamı ve mimari döngüler açık; bu adım tüm 3. aşamayı kapatmaz.

Önce 25.80 s'lik incremental C build geçti; aşağıdaki eskilik kontrolü bunun eski
CMake metadata'sını kullandığını yakaladı. Yeniden configure edilmiş normal
compile/link **25.72 s** ile geçti; yakalanan `warning:`/`error:` satırı yoktu.
Statik RAM **42556**, flash **800053** bayt (önceki tek-IDF imajına göre -8/0);
bu küçük statik fark için runtime tasarrufu veya binary-identical iddiası yok.
SDK config SHA256 yine yukarıdakiyle aynı. Karta upload/reset yapılmadı; yeni imaj
fiziksel test edilmedi, kartta 14 Ekim'deki doğrulanmış normal firmware kalır.
Son `-BuildMetadata` tekrar kontrolü 12 component/48 bağ, 47 kaynağın birer kez
yer alması ve LDF arşivi olmaması için geçti. Normal ELF'te watchdog test hook'u
yok. VS Code root compilation database güncel configure sonrası yenilendi (3.73 s).

### PlatformIO component-CMake önbelleği

Bu makinedeki ESP-IDF builder, yeniden configure gereksinimini kontrol ederken
root ve `main/CMakeLists.txt` zamanlarına bakıyor; alt component CMake değişikliği
tek başına yeniden configure başlatmayabilir. `compiledb` hedefi de bu eski
metadata'yı kullanabilir. Başarılı C derlemesi yeni bağımlılıkların alındığını
tek başına kanıtlamaz; `-BuildMetadata` eskilik kontrolü bu durumu reddeder.

Bu adımda yalnız üretilmiş `CMakeCache.txt`, aynı build dizininde
`CMakeCache.before_header_scope.txt` adına taşınarak korunmuş ve normal build ile
yeniden configure edilmiştir. Kaynak/SDK config veya kart belleği silinmedi.
Genel tekrar yolu aşağıdadır; `clean` yalnız ilgili profilin üretilmiş build
çıktılarını temizler ve sonraki derlemeyi uzatır, karta yükleme/reset yapmaz:

```powershell
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev -t clean
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\check_component_dependencies.ps1 -BuildMetadata
```

## 2026-10-15: ortak mission sözleşmelerinin CDH'den ayrılması

Yeni `components/fsw_common` yalnız header içeren bir ESP-IDF component'idir.
Runtime kodu, RTOS/SDK veya servis include'u içermez; genel yardımcı fonksiyon
deposu değildir. Mod kararları `system_mode`, hata işleme FDIR, watchdog/CDH ve
paket üretme/yönlendirme kodu comms/CDH sahipliğinde kalır.

| Tek tanım sahibi | İçerik |
| --- | --- |
| `fsw_common/satellite_types.h` | Mevcut TC/HK veri tipleri, frame sınırları ve görev/durum kimlikleri |
| `fsw_common/commands.h` | Mevcut alt sistem komut numaraları ve router fault kodları |
| `fsw_common/mission_ids.h` | Yönlendiricinin kullandığı sekiz APID |

İlk iki header CDH'den taşındı; eski dizinde forwarder/kopya bırakılmadı.
`ccsds_packet.h` APID'leri yeni sahibinden include ederek mevcut kullanıcılarına
sunmaya devam eder. `ADCS_CMD_SET_MODE` artık yalnız `commands.h` içinde tanımlı;
komut numarası isteyen ADCS API kullanıcısı bunu doğrudan include etmelidir.
ADCS manager/EPS commands gövdelerindeki kullanılmayan satellite-types include'ları kaldırıldı.

CDH ve comms public header'ları bu tanımları dışarı sunduğu için
`REQUIRES fsw_common` bildirir. Diğer tüketiciler yalnız C gövdelerinde kullandığı için
private bağ bildirir. Archive/HK/payload'un tanım amaçlı doğrudan CDH bağları
kaldırıldı; ADCS/EPS/FDIR'nin watchdog ve comms'un router bağları hâlâ gerçektir.
Diğer public bağlar ve karşılıklı runtime component döngüleri kapatılmış sayılmaz.

Komut başlığındaki eski HK/archive/FDIR/payload/CDH APID yorumları gerçek
yönlendirme değerleriyle uyuşmuyordu; yalnız yorumlar düzeltildi. FDIR ACK'in
warning-only etkisi ve mode komutunun `[0x52, mode]` açıklaması mevcut kodla
eşitlendi. APID/komut numaraları, paket/CRC biçimi ve komut davranışı değiştirilmedi.

Yeni `mission_contract` kontrolü eski ESP32 ABI'sine karşı taşıma öncesinde ve
sonrasında geçti; ayrıca host'ta CDH/RTOS/SDK include yolları olmadan çalışır.
25 C grubu ve 9 ayrı header kontrolü geçti. Arşiv read/downlink fixture'ları
CDH include yolundan da ayrıldı. Watchdog profil kontrolünün include yolları
yeni public bağı yansıtacak şekilde güncellendi. Python testleri değişmedi ve
önceki 122 Python kontrolü bu adımda yeniden çalıştırılmadı.

Normal `esp32dev` yeniden configure/compile/link **41.81 s** ile geçti; yakalanan
`warning:`/`error:` satırı yoktu. Root CMake'e yeni katmanın sahipliği açıklandı;
bu değişiklik PlatformIO'nun bu kez yeniden configure çalıştırmasını da sağladı.
Alt component-CMake değişikliklerinin önbellek sınırı genel olarak devam eder;
metadata kontrolü artık root CMake zamanını da denetler.
Statik RAM/flash **42556/800053** bayt olarak kaldı; binary-identical veya runtime
heap tasarrufu iddiası yok. SDK config hash'i aynı; karta upload/reset yapılmadı.
Yeni firmware'in fiziksel testi ve bütün target test profillerinin yeniden
derlenmesi bu adımda yapılmadı.

Son kaynak/build metadata denetimi **13 component / 54 doğrudan proje-header
bağı**, **47 C kaynağının birer kez** derleme metadata'sında bulunması ve LDF
arşivi olmaması için geçti. Üç uyumsuz watchdog profil birleşimi beklenen
diagnostic ile reddedildi; normal ELF'te test hook'u yok. Root compilation
database yeni ortak include yollarıyla yenilendi (**3.72 s**, firmware build değil).

## 2026-10-15: public/private sınırları ve proje component döngülerinin ayrılması

Yalnız `REQUIRES` → `PRIV_REQUIRES` değiştirmek döngüyü çözmüyordu. Servisleri
birleştiren mevcut task/adapter kodu aşağıdaki sahipliklere ayrıldı; algoritma,
task isimleri/periyotları/stack/öncelikleri ve init sırası değiştirilmedi.

| Yeni sahip | Taşınan mevcut kod | Kaldırılan geri bağ |
| --- | --- | --- |
| `components/watchdog` | Üç watchdog C kaynağı ve üç public/port/test başlığı | ADCS/EPS'nin heartbeat için CDH'ye bağı |
| `main/fdir_task.c` + `fdir_task.h` | FDIR processing + heartbeat task adaptörü | FDIR servisinin watchdog/CDH'ye bağı |
| `main/eps_harvest.c` + `simulation_tasks.h` | Mevcut güneş/termal/orbit ortam task'ı | EPS servisinin ADCS simülasyonuna bağı |
| `components/radio_link` | `comms_frame.c/.h`, `tm_manager.c/.h` | Temel comms'un görev yönlendiricisi CDH'ye bağı |

`radio_link` eski demo framing/yönlendirme adaptörüdür; yeni radyo sürücüsü veya
aktif UDP hattına yeni bir giriş değildir. İşlev/header isimleri korunur, yalnız
component include yolu değişir. `vFDIRTask` bildirimi artık uygulama adaptörünün
`main/fdir_task.h` başlığındadır; FDIR core API'sinin parçası değildir.

Yalnız C gövdelerinin kullandığı proje/SDK bağları private yapıldı. Proje public
bağları artık yalnız comms/watchdog → fsw_common ve EPS/FDIR → system_mode'dur.
EPS'in public semaphore API'si ve main'in SDK/RTOS public tipleri nedeniyle
gereken SDK public bağları tutulur. Diğer temel SDK ortak bağımlılıkları IDF'nin
kendi mekanizmasıyla sağlanır; projeye global `-I` yolu geri eklenmedi.

Salt-okuma audit **15 component / 59 doğrudan proje-header bağı** için geçti.
Gereksiz public proje bağı ve public/private birlikte ele alınan bildirilen
proje grafındaki her döngü artık hata üretir. Sekiz PowerShell graf testi
empty/leaf/chain/diamond sıralamasını ve self/private/consumer cycle ile
bilinmeyen düğüm reddini doğruladı. Topolojik çıktı bir çağrı zinciri değil,
bağımlılıkların tüketiciden önce geldiği geçerli bir sıralamadır.

26 C grubu ve `components/*`/`main` tarafından dışarı açılan **44 top-level
header** izolasyon kontrolü geçti. Eski dokuz sabit header fixture'ı ortak bir
translation-unit şablonuna taşındı; her header ayrı derlenir ve yalnız public
include yollarını alır. Taşınan FDIR task'ının 73, watchdog'un 70 ve SDK port'unun
13 senaryosu, injection grupları ve startup'ın 28 senaryosu da yeni sahiplikle
çalışır. Yeni radio-link testi mevcut demo çerçeve/CRC/dispatch ve sınırlandırılmış
telemetri akışını test eder; gerçek router/radyo ve geçersiz TM boyutlarının bütün
güvenlik matrisi kapsam dışıdır. Python testleri değişmedi ve yeniden çalıştırılmadı.

Bu sonuç **bildirilen proje CMake/direct-include grafı** içindir. SDK'nin implicit
ortak/link-only bağları (global printf wrap dahil), runtime çağrı döngüleri,
mutex kilit sırası ve bütün mimari coupling'in yokluğu kanıtlanmış değildir.
HAL/SDK sınırları, simülasyon birimleri ve tam README yayın temizliği açık kalır.

Normal `esp32dev` configure/compile/link, SDK/bootloader yeniden derlemesi dahil
**184.74 s** ile geçti; yakalanan `warning:`/`error:` satırı yoktu. Statik RAM
**42556**, flash **799997** bayt (önceki imaja göre 0/-56); binary-identical imaj
veya runtime heap tasarrufu iddiası yoktur. SDK config SHA256 yine aynı.
Karta upload/reset, ağ/ground ayarı veya flash/NVS silme yapılmadı; kartta
14 Ekim'deki doğrulanmış normal imaj kalır. Yeni imaj henüz kartta test edilmedi.
Tam target profil matrisi veya uzun süreli mission/recovery testi çalıştırılmadı.

Son `-BuildMetadata` kontrolü **47 kaynağın birer kez** kayıtlı olması, güncel
map'te LDF arşivi olmaması ve 15 düğümlü bildirilen proje grafının döngüsüzlüğü
için geçti. Üç geçersiz watchdog profil birleşimi reddedildi, normal ELF'te
watchdog test hook'u yok. Root compilation database yeni kaynak/include yollarıyla
yenilendi (**3.97 s**, firmware yeniden derlemesi değil).
