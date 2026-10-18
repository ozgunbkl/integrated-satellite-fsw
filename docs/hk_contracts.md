# HK telemetri ve raporlama sözleşmesi

2026-10-15: bilgisayarda doğrulanan kapsam. Fiziksel sensör veya uçuş yeterliliği iddiası değildir.

## Birbirinden farklı üç veri biçimi

| Yol | Boy / içerik | Zaman / birim |
| --- | --- | --- |
| `HK_Serialize` | Beş adet BE uint32 değer + birer validity, toplam 25 bayt | Timestamp içermez; değerlerin birimi çağırana aittir. |
| `HK_GenerateReport` | 28 bayt: uptime, EPS voltaj/SOC, tablo sıcaklığı, ADCS gyro xyz | Uptime ms; V, %, °C, rad/s; BE IEEE-754 float alanları. Mevcut gönderici APID 5 korunur. |
| `vTelemetryGeneratorTask` → arşiv kuyruğu | Mevcut packed `HK_Telemetry_t`, GCC sözleşmesinde 20 bayt | Timestamp FreeRTOS tick; bus_voltage **sabit demo 3.3 V**, EPS ölçümü değildir. |

Son yol portable bir wire format değildir. Bitfield/packing mevcut GCC düzenine
bağlıdır. Paket tamamen temizlenir; sequence uint16 olarak ilerler, sistem modu
doldurulur. Kullanılmayan bayraklar, sıcaklık ve iç checksum sıfırdır; checksum
hesaplanmış sayılmaz. Arşiv servisinin dış kayıt CRC'si ayrı bir mekanizmadır.
Kuyruk dolsa da sıra sayacı ilerler ve sonraki denemeler devam eder.

## Parametre ve kilit politikası

`HK_Init()` görevler başlamadan bir kez çağrılır; entry/limit tablolarını temizler,
rapor periyodunu 5000 ms ve son deneme zamanını 0 yapar. Tek mutex oluşturulur.
16 Ekim'den itibaren `bool` döndürür: allocation, 10 ms ile sınırlı acquire veya
release hatası `false`; yalnız bütün adımlar başarılıysa `true`. Core helper
başarısız init'te sonraki servis/görevleri başlatmadan log + abort yapar; hazır
mesajı vermez. Allocation/acquire hatası veri/limit/timing'i değiştirmez;
release hatasında reset edilmiş state geri alınmaz, runtime retry yapılmaz.
Başarısız mutex oluşturma/alma halinde diğer API'ler başarısız olur veya iş yapmadan döner;
`HK_CheckLimits` geçersiz ID için -2, örnek yok/kilit yok için -1 döner.
Diğer hata dönüşleri mevcut int API'lerde -1'dir. Limitler dahilidir; varsayılan
[0,0] korunur. Negatif/taşan ID ve low > high değişiklik yapmaz.

`is_valid` yalnız örnek yazıldığını ifade eder; timestamp'ten otomatik yaş/expiry
hesaplanmaz. Sıcaklık örneği yoksa mevcut master formatında 0 °C gönderilir;
bu alanın ayrı quality biti yoktur. ADCS snapshot başarısızlığını da mevcut
void getter/format ayrı quality ile taşımaz. Bu sınırlamalar giderilmiş sayılmaz.

Tablo, limitler ve zamanlayıcı durumu HK mutex ile korunur. EPS voltaj/SOC aynı
EPS kilidi altında kopyalanır; erişilemiyorsa rapor atlanır. HK, EPS, ADCS ve
gönderici kilitleri iç içe tutulmaz; hiçbir HK/EPS kilidi TX veya log boyunca
tutulmaz. Bu, alt sistemlerin **aynı anda** örneklendiği anlamına gelmez.

## Zamanlama

- SET_INTERVAL 1..60 saniye kabul eder; fazı/son periyodik denemeyi sıfırlamaz.
- `HK_RunPeriodic` monotonic uint32 ms kullanır. Unsigned fark sayacın taşmasını
  karşılar; çağrılar arasında tam 2^32 ms veya daha fazla boşluk desteklenmez.
- İlk varsayılan deneme uptime 5000 ms'de uygundur; gecikme sonrası bir çağrı
  en fazla bir rapor dener. Geçmiş periyotları topluca göndermez.
- Due slot gönderimden önce ayrılır. Snapshot/TX hatası o slot'u tüketir;
  sık hata döngüsü oluşmaz. FORCE_REPORT periyodik son-deneme zamanını değiştirmez.
- TM görevi her döngü sonunda 1000 ms bekler; HK ve heartbeat her döngüde,
  legacy arşiv üretimi başlangıçta ve ardından en az 5000 ms aralıkla çalışır.
  Kuyruk yalnız NOMINAL modda doldurulur; diğer modlarda HK/pet devam eder.
  Bu relative-delay akışı kesin gerçek zaman deadline/jitter garantisi değildir.

## Kanıt ve sınırlar

İki yeni gerçek C fixture'ı: `tests/host/hk_service/check.c` ve
`tests/host/tm_gen/check.c`. İkincisinde tick özellikle 10 ms'dir; byte/timing
kontrolleri yanlış 1 tick = 1 ms varsayımına dayanmaz. RTOS, ağ ve diğer servis
sınırları mock'tur. Kilit davranışı ve reentrant due kontrolü, gerçek FreeRTOS
eşzamanlılık/öncelik/stress veya kart üzerindeki deadline ölçümü değildir.

30 C grubu ve 45 bağımsız header geçti. Component/metadata kontrolü 15 component,
59 doğrudan proje-header bağı ve 47 tekil kaynak için geçti. Normal ESP32 build
36.92 s; statik RAM 42596/327680 (+40), flash 803009/1740800 (+456).
Yeni HK mutex'i runtime heap de kullanır; bu build sayıları heap/stack ölçümü değildir.
SDK config SHA256: `DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Karta yükleme/reset yapılmadı; 14 Ekim'deki normal imaj ve per-boot RAM korunur.
YAMCS MDB/APID hizalaması, gerçek 1 s rapor gözlemi ve son ADCS değişikliklerinin
kart doğrulaması bu adımın kapsamı değildir. Önceki Python/graf/profile/hardware
test paketleri bu değişiklik için yeniden çalıştırılmadı.

### 2026-10-16: Required HK init sonucu

HK host fixture'ı altı süreçtir: önceki tüm servis kontrolleri ile ilk mutex
allocation/acquire/release hataları, seeded state korunması ve aynı mutex ile
başarılı pre-task reset. Gerçek app_main fixture'ı HK false ve repeat-init'i
ayrıca sınar; toplam 33 startup süreci ve yedi yeni process case geçti.
Tam 43 C/47 header/372 Python, normal build ve metadata/ELF/59 profil guard geçti.
Statik RAM 42884 (aynı), flash 809293 (+80); SDK config aynı. Wire/APID/birimler,
periyot/defaultlar ve task metadata'sı değişmedi. Yeni imaj karta yüklenmedi;
fiziksel init-failure/panic ve uzun soak bu sonuçla doğrulanmış sayılmaz.
[Başlangıç kanıtı ve hash](startup_policy.md#2026-10-16-hk-başlangıcında-sessiz-hata-kapısı).
