# Canlı HK alan/birim ve olay işleme kontrolü

2026-10-16, 08:07–08:08 Türkiye saati. Mevcut ESP32 normal firmware'i ve mevcut
`C:/yamcs-ground/my-mission-control` projesinin YAMCS 5.12.0 runtime'ı kullanıldı.
Yeni ground projesi, firmware upload, seri port açma veya kart reseti yoktur.

## HK düzeltmesi ve karşılaştırma

Sayısal kodlama/offset doğruydu; eski XML'de HK ölçü birimleri eksikti. Yalnız
HK'ye özel beş tip eklendi, yedi parametre bunlara bağlandı. Generic `uint32_t`
tipi ve transfer kimlikleri birimsiz kalır. Kalibrasyon, wire, komut, C/SDK/ELF,
limit/alarm veya fiziksel sensör entegrasyonu değiştirilmedi.

| Parametre | Paket başından bit offset | Kodlama | Birim |
| --- | --- | --- | --- |
| HK_UptimeMs | 48 | BE uint32 | ms |
| HK_BatteryVoltage | 80 | BE float32 | V |
| HK_BatterySOC | 112 | BE float32 | % |
| HK_BatteryTempC | 144 | BE float32 | degC |
| HK_GyroX | 176 | BE float32 | rad/s |
| HK_GyroY | 208 | BE float32 | rad/s |
| HK_GyroZ | 240 | BE float32 | rad/s |

Üç ayrı APID 5/34 bayt paket bağımsız `!I6f` çözümlemesiyle karşılaştırıldı.
YAMCS'in aynı arşiv paketi için verdiği raw/engineering değerleri, bit konumu ve
boyu kontrol edildi; float32 yeniden paketleme bit eşitliği kullanıldı. Processor
cache'inin farklı zamanlarda güncellenen alanları birbirine karıştırılmadı.
Yedi birim ayrıca canlı MDB API'sinden okundu.

Son örneklerin UTC generation time değerleri `05:07:46.847`, `05:07:51.857`,
`05:07:56.850`; uptime değerleri 1680280, 1685280, 1690280 ms. Uptime boot-relative
uint32'dir; mutlak zaman değildir, taşar. Batarya/EPS ve gyro değerleri mevcut
simülasyondan gelir; fiziksel doğruluk/kalibrasyon kanıtı değildir. Sıcaklıkta
`0`, HK girdisi ayarlanmamışsa placeholder'dır; pakette validity biti yoktur.
`ACQUIRED` görünmesi gerçek bir sıcaklık sensörü okunduğu anlamına gelmez.

[YAMCS UnitSet tanımı](https://docs.yamcs.org/yamcs-server-manual/mdb/data-types/)
ve [belirli arşiv paketini mevcut MDB ile çıkarma API'si](https://docs.yamcs.org/yamcs-http-api/packets/extract-packet/)
kullanıldı. Wire zaman damgası içermediğinden generation time ground saatidir.

## Olaylar: mesaj tekrarı ile çift işleme ayrımı

Son 20 saniyelik pencere UTC `05:07:36.476`–`05:07:56.476`:

- 235 geçerli APID 2000/70 bayt paket, 235 `FSW_EVENT` ile birebir eşleşti.
  Kaynak `MyPacketPreprocessor`, bu doğal penceredeki severity değerleri INFO idi.
- 96 art arda aynı metin çifti ham paketlerde de vardı; aynı wire paketinin
  birebir tekrar sayısı 0 idi. Bu çiftler YAMCS'in fazladan event üretmesi değildir.
  Logların neden aynı metni ürettiği veya 63 bayt kesmenin etkisi bu testle
  belirlenmedi; firmware log politikası değiştirilmedi.
- Ground generation time farkı en fazla 250 ms eşleştirme toleransıyla kontrol
  edildi; iç pencerede eşleşmeyen ek olay yoktu. Üç EventMessage XTCE çıkarımı da
  ham UTF-8/NUL-padded metin ve 48/512 bit offset/boyuyla karşılaştırıldı.
- API'nin `event/events` ve `packet/packets` alias listeleri iki kez sayılmadı.
  Sayfalama filtreleri korunur; 10 sayfa ve toplam 40 saniye sınırı vardır.

Mevcut preprocessor'un taban sınıfı, pinned 5.12.0'da 10 saniyelik repeated-event
reduction kullanır. Gerçek kurulu sınıf mock event kuyruğuyla ayrıca sınandı:

- INFO → INFO, WARN/WARNING → WARNING, ERROR/CRITICAL → ERROR; `CRITICAL`
  prefix'i mevcut uygulamada YAMCS CRITICAL seviyesi değildir.
- NUL padding temizlenir; boş olay yayımlanmaz.
- Aynı metin iki kez geldiğinde ilk olay hemen, tek ilave tekrar flush sırasında
  değişmeden yayımlanır. Dolayısıyla iki giriş iki aynı metinli event olabilir.
- Üç kez geldiğinde ilk olay + `Repeated 2 times: ...` özeti oluşur. Farklı mesaj
  gelmesiyle buffer flush edilerek bekleme gerektirmeden test edildi.
- Aynı wire paketinin tekrar verilmesi de repeat sayısına girer; mevcut sınıf
  packet deduplication veya exactly-once teslim garantisi sunmaz.

Uzun tekrar özeti ve WARNING/ERROR senaryoları native fixture'ta doğrulandı;
canlı karta yapay hata gönderilmedi. Canlı araç üç ve üzeri aynı mesaj koşusunu
1:1 varsaymaz, açık hata ile durur; genel timer/UDP kayıp/duplicate/reboot kanıtı
olarak kullanılmamalıdır. Malformed paketin Java event katmanında tümden
reddedildiği de iddia edilmez. Bu adım event yorumlama ve bounded tekrar davranışı
kapsamını kapatır; yeni suppression/deduplication politikası eklemez.

## Tekrarlama ve kanıt dosyaları

```powershell
# Yalnız GET; opsiyonel --output yeni bir rapor dosyası oluşturur, üzerine yazmaz.
& C:\.platformio\penv\Scripts\python.exe tools/check_yamcs_telemetry.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check_yamcs_mdb.ps1 -GroundProject C:\yamcs-ground\my-mission-control
```

Canlı araç komut, link kontrolü, reset, firmware, seri port veya API POST yolu
kullanmaz. Son rapor `.pio/live-check/hk-events-20261016-final.json`:
SHA256 `E62B39A83D9EE49B3545C38AE4359A32D853641023EAA2FFBFC9BFBDCFA00335`.
Rapor sayısal HK ve toplu olay sayıları/metin hash'i içerir; ham event metni içermez.
İlk başarılı ayrı pencerede de 231 olay/92 çift eşleşmişti.

Yeni araç 23, XML profili 17 host testi içerir. Toplam **16 dosyada 273 Python
testi** geçti. Native MDB/command/preprocessor kontrolü; normal metadata/ELF,
dependency grafı ve üç negatif test-profili guard'ı tekrar geçti. C/header tam
regresyonu ve firmware derlemesi, C/SDK kaynakları değişmediğinden tekrarlanmadı.
Cygwin sandbox erişim hatası sonrası negatif guard'lar onayla sandbox dışında
çalıştırıldı; kart testi değildir.

Repo/ground SOURCE/runtime XML aynı SHA256:
`7D3C4D84FB1C555069051BDB6CB1DEED32410589A40695E18449444A94C13749`.
Önceki `AC52E1...` profil iki yerde geri alınabilir şekilde korundu:

- SOURCE: `src/main/yamcs/mdb/xtce.xml.before-fsw-20261016-050453-3947.bak`
- Runtime: `target/yamcs/mdb/xtce.xml.before-units-20261016-050505-2908.bak`

Yalnız `myproject` instance'ı
[restart API'siyle](https://docs.yamcs.org/yamcs-http-api/instances/restart-instance/)
MDB reload için yeniden başlatıldı. Ground bağlantısı kısa süre kesildi; o aralık
kesintisiz capture/teslim kanıtı değildir. Ground link sayaçları bu yüzden 79'dan
0'a döndü; eski komut/veri arşivi silinmedi. Son salt-okunur kontrol boyunca TC
dataOutCount 0 → 0 kaldı. Kart uptime artmaya devam etti, reset yapılmadı.
Firmware SHA256 `A3DC0D97B6AAD65B4D17F7C6CC6CF1EDE6FAE82679F5DA55564054B67E4D1440`
ve SDK SHA256 `DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`
değişmedi. Önceki arşiv export/proof dosyaları korunur.

Sonraki ayrı adımda [kaynak/stack/heap/queue/zamanlama baseline'ı](resource_target.md)
600 s SAFE koşusuyla tamamlandı. Geniş watchdog ve çeşitli yüklerde uzun süreli
çalışma kontrolleri hâlâ açıktır. Tüm stage 6 veya proje tamamlanmadı.
