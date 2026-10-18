# HAL, simülasyon ve birim sözleşmesi

2026-10-15. Bu proje bir geliştirme/simülasyon platformudur; flight-qualified
kontrol veya fiziksel olarak kalibre edilmiş uydu modeli değildir.

17 Ekim NOMINAL hazırlığında EPS ortamı bounded20 Wh/0..9 V enerji proxy'sine
geçti;24 W/60 s güneş ve60 s gölge varsayımı, ölçülen dt, tutarlı SOC ve solar
girdinin tek sahibi eklendi. Eski nominal1 s gerilim/sıcaklık proxy'si aşağıda
tarihsel kapsamdır. [Yeni varsayımlar ve kart/build ayrımı](nominal_soak_preparation.md).

## Sınırlar ve sahiplik

- `sat_hal`: RAM kayıt bankasıdır; ESP32/STM32 GPIO/I2C/UART/SPI sürücüsü değildir.
  I2C/UART yalnız ilk baytı işler. Aktif uygulama bu mock'u sensör sürücüsü olarak
  kullanmaz. Zaman/watchdog gerçek SDK adaptörleri üzerinden çalışır; bunlar ayrı
  platform port'larıdır. Gerçek sensör/AI entegrasyonu aktif kapsamda yoktur.
- `adcs_sim.c`: RTOS/SDK içermeyen, tek sahipli ham model. `SIM_*` API'leri
  thread-safe değildir. Runtime okumaları ve mutasyonları ADCS servisinin state
  mutex'i altında yapılır. Raw setter'lar yalnız görevlerden önceki startup veya
  seri host test setup'ı içindir; startup'ın `(10,-5,0.5)` rad/s değeri korunur.
- ADCS manager ve solar task, yükseklik/yakıtı artık kilitli servis getter'larıyla
  okur. Kilit alınamazsa yükseklik `NAN`, yakıt konservatif `0` döner. Manager
  bilinmeyen yüksekliği NOMINAL diye etiketlemez. State kilidi alınamayan physics
  adımı atlanır; başka task state'i tutarken kilitsiz pasif ilerleme yapılmaz.
- ADCS → EPS → system_mode actuation izin/kilit sırası korunur. Bir ölçüm veya
  snapshot, boost/control yetkisi değildir; commit anındaki izin tekrar kontrol edilir.

## Birimler

| Alan/API | Birim ve sözleşme |
| --- | --- |
| Gyro, `angular_velocity`, spin setter, PD `omega` | rad/s; gyro gürültüsü her eksende en çok ±0.01 rad/s |
| Magnetometre, B-Dot girişleri | µT; yaklaşık 45 µT synthetic alan, eksen başına ±0.5 µT gürültü |
| `dt`, B-Dot türev aralığı | saniye; nominal task 100 ms, model adımı 0.1 s |
| Yükseklik ve boost | km; yükseklik en az 0, 10 km boost maliyeti 1.5 yüzde puan yakıt |
| Yakıt, EPS SOC | yüzde; model yakıtı 0..100, kilogram veya joule değil |
| Quaternion | boyutsuz, scalar-first `(w,x,y,z)`; attitude setter normalize eder |
| B-Dot/PD/plant effort | keyfi demo kontrol birimi; kalibre A·m² veya N·m değildir |
| EPS/HK güç, gerilim, sıcaklık | W, V, °C; solar task gerilimi/sıcaklığı basit nominal 1 s step proxy'siyle değiştirir |
| MasterHK uptime / fault timestamp / demo CCSDS secondary time | sırasıyla ms / s / ms; mevcut HK queue timestamp'i ayrıca FreeRTOS tick'idir |

`adcs_units.h` cycle ve rate sınırlarının tek sahibidir. Niyet edilen eksen başına
3000 deg/s saturation sınırı artık rad/s'e çevrilir: yaklaşık **52.3599 rad/s**.
DETUMBLE ve NOMINAL bu sınırı uygular; eşik üstü SAFE/FDIR warning, tam sınır
kabul edilir. Stability norm eşiği **0.25 rad/s ≈14.3239 deg/s** olarak korundu.
Gyro/HK float taşıma biçimi veya paket layout'u değiştirilmedi; gyro zaten rad/s
üretiyordu. Ground ekranı/MDB etiketlerinin hizalanması ayrı TM/TC kapsamıdır.

## Model zamanı, işaret ve geçersiz giriş

Eski model `dt`'yi kullanmıyordu. Yeni adım `steps = dt / 0.1` ile ölçeklenir:
angular hız `(omega - 0.2 * braking * steps) * 0.95^steps`, yükseklik azalması
`0.05 * dt` km'dir. Eski 0.1 s nominal decay/damping/braking kazancı korunur.
Bu sayılar orbit/atalet/enerji kalibrasyonu değildir. Pasif damping'in aynı
toplam sürede tutarlılığı test edilir; forced adım hâlâ basit ayrık modeldir.

PD torque işaretinin legacy plant'in çıkardığı braking işaretine dönüşümü servis
adaptöründe yapılır. Identity attitude/pozitif spin senaryosunda bir nominal
step'in spin'i artırmadığı test edilir; bu, bütün modlarda kapalı çevrim
kararlılık kanıtı değildir. B-Dot synthetic alan/effort modelidir; fiziksel
`m × B`, atalet matrisi veya magnetorquer kalibrasyonu uygulanmaz.

Magnetometre okumak artık alanı döndürmez; yalnız physics step state'i ilerletir.
Okumalar yine noise örneği alır. Attitude quaternion'u açık setter değiştirmedikçe
sabit kalır; rijit cisim quaternion kinematiği eklenmedi. Nominal `vTaskDelay`
üzerinden sabit model zamanı kullanılır; scheduler jitter'ı gerçek ölçülen dt'ye
çevrilmez. Gerçek zamanlı plant doğruluğu iddiası yoktur.

Nonpositive/nonfinite dt, nonfinite effort veya taşan/nonfinite aday state tüm
adımı mutasyonsuz reddeder. Nonfinite gyro setter ve geçersiz/zero attitude
setter mevcut state'i korur. Boost; nonfinite/taşan, temsil edilemeyecek kadar
küçük, sıfır/negatif veya yakıtı yetmeyen mesafeyi reddeder; kısmi yakıt borcu/
ücretsiz son manevra yoktur. Başarıda yükseklik ve yakıt birlikte commit edilir.

## Matematik ve doğrulama sınırı

Vector/quaternion normalizasyonu finite büyük değerlerde ölçekli norm kullanır;
norm ≤1e-6 veya nonfinite giriş değişmeden kalır. Null pointer no-op'tur; bu
utility davranışı sensor fault recovery politikası değildir. Hamilton product
iki operanddan biriyle aynı output adresini destekler. Önceden yalnız deklarasyon
olan rotation matrix fonksiyonu finite unit quaternion için sağ-elli active
rotation üretir; caller önce normalize etmelidir. `q`/`-q` aynı matrisi verir.

İki yeni saf-C test grubu math ve modeli SDK/RTOS olmadan çalıştırır. Alt servis
güvenlik grubu **164→171** senaryo oldu: rate below/at/above/negative, NOMINAL
saturation/damping ve invalid dt eklendi; state-lock/snapshot failure ve
yetersiz yakıt senaryosu güncellendi. Tüm **28 C grubu / 45 public top-level
header** geçti. Bu; fiziksel sensör/aktüatör, genel kapalı çevrim kararlılık,
gerçek scheduler/heap/stack bütçesi veya bütün geçersiz girişlerin formal kanıtı değildir.

Normal `esp32dev` compile/link **21.60 s** ile geçti; yakalanan `warning:`/`error:`
satırı yoktu. Statik RAM **42556**, flash **802553** bayt (önceki imaja göre
0/+2556). Runtime stack/heap maliyeti ölçülmedi. SDK config SHA256 değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Build metadata kontrolü 15 component/59 doğrudan bağ, döngüsüz bildirilen proje
grafı ve 47 C kaynağının birer kez derlenme kaydı için geçti. Python testleri ve
bütün target profil matrisi yeniden çalıştırılmadı. Karta upload/reset, flash/NVS
silme veya ground/network ayarı yapılmadı; karttaki önceki normal imaj korunur.
Yeni kontrol/model davranışı fiziksel kartta henüz doğrulanmadı.
