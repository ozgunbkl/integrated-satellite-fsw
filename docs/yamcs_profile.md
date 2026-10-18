# Bakımı yapılan YAMCS uydu profili

2026-10-18. Kaynak: `config/yamcs/xtce.xml`. ESP32 uydu FSW geliştirme profili;
uçuş yeterliliği veya yeni fiziksel kart doğrulaması iddiası değildir.

## Güncel16 komutlu profil

**15 somut +1 abstract komut**: mevcut HK/FDIR/legacy arşiv/beş transfer komutuna
ADCS_SetMode0..2 ve PAYLOAD_Init/Start/Stop/Reset eklendi. Bunlar mevcut firmware
handler'larıdır; yeni WIPE, fiziksel heater veya sahte ESP reboot komutu değildir.
Payload RESET APID112/body04 ile yalnız yerel durumu OFF'a ve error_counter'ı0'a
alır; genel fault/NVS/arşivi temizlemez. Boot onayı ile NOMINAL ayrı kalır.

17 Ekim source/runtime MDB yedekli kuruldu ve yalnız görev instance'ı reload
edildi; DB/dataDir silinmedi. Son kaynak/runtime hash'i:
`763BC61488BBBDB79838876F883471F12F6DFFBCF9B703C5DEBD97CDAE12C5EE`.
Native Yamcs5.12:21 postprocessed komut/10 range reddi, ayrıca3 recovery binary/
7 argüman reddi ve5 transfer binary/11 range-key reddi geçti; extraction/event
kontrolleri korundu. Canlı komutlar kart/Sent zinciriyle doğrulandı; aynı imajda
bir saatlik [NOMINAL koşusu PASS](nominal_soak_target.md).18 Ekim dokümantasyon
güncellemesi ground'u başlatmaz/değiştirmez.

## Tarihsel11 komutlu recovery kurulumu

Önceki kaynak değişikliği: `FDIR_RearmBoot` ile **10 somut +1 abstract =11 komut**.
APID 0x030 / CmdId 0x53, zorunlu BootCounter (BE uint32, 1..0xffffffff) ve zorunlu
ConfirmKey=165 içerir. Sağlık penceresi/boot kilidi içindir; fault clear/NOMINAL
komutu değildir. [Recovery sözleşmesi](recovery_policy.md). Yeni repo profili gerçek
YAMCS native kontrolden geçti; sonra ayrı onayla **ground source/runtime'a yedekli
kuruldu ve yalnız instance reload edildi**. Repo/source/runtime hash'i
`056799FF96A38B8097D8FFDFE87A115BF930CBA8AFC748E500FE141C2C61ADF2`.
Canlı iki preview ve dört gerçek recovery/mode isteği Sent/binary/kart-yanıtıyla
doğrulandı. Kartta yeni 153DF1… normal var; recovery restore paketi ise seri aktarım/
geçmiş kaybı nedeniyle **açık**, [kısmi fiziksel sonuç](recovery_target.md).
Aşağıdaki daha eski kurulum kanıtları kendi 10 komutlu profillerine aittir.

## Alanlar ve komutlar

Eski Spacecraft/APID 100 telemetrisi, simülatör APID 101 komutları, `Reboot`,
`SwitchVoltageOn/Off`, bunlara ait parametreler/algorithm aktif profil kaynağından
çıkarıldı. Orijinal ground dosyası aynen yedeklendi; ayrı simülatör uygulaması,
çalışan sunucu belleği ve eski arşivler silinmedi/değiştirilmedi.

Profilin önceki dört somut komutu mevcut isimleriyle korunur: ARCHIVE_GET_BY_INDEX,
HK_SetInterval, PAYLOAD_SetRate, FDIR_SetMode. CmdId artık kullanıcı argümanı
değil sabit bayttır. CmdParam sırasıyla 1..60, 1..10, 0..5 aralığıyla sınırlanır;
arşiv RecordIndex 0..65535 BE uint16'dır. Wire paketleri önceki geçerli komutlarla
aynıdır; link postprocessor'u length ve APID bazlı sequence alanlarını doldurur.
WIPE_ALL veya gerçek olmayan reboot/heater komutu eklenmedi. Sonraki wire paketi
beş gerçek transfer komutu ekledi: BEGIN/PENDING/READ/CONFIRM/CANCEL. Profil artık
9 somut + 1 abstract komut içeriyordu; recovery ile yukarıdaki 11'e çıktı. CONFIRM yıkıcıdır; tam manifest ve mandatory
ConfirmKey=165 gerekir. [Wire biçimi ve güvenlik sınırı](archive_transfer_wire.md).

| Arşiv parametresi | UDP paketindeki bit offset | Kodlama |
| --- | --- | --- |
| ArchiveVersion | 48 | uint8 |
| ArchiveIndex | 56 | BE uint16 |
| ArchiveRecordId | 72 | BE uint16 |
| ArchivePayloadLength | 88 | BE uint16, bayt |
| ArchivePayloadCRC | 104 | BE uint16, yalnız payload CRC'si |
| ArchivePayload | 120 | Binary, önceki length alanı × 8 bit |

Değişken binary uzunluk, [resmî YAMCS veri tipi sözleşmesindeki](https://docs.yamcs.org/yamcs-server-manual/mdb/data-types/#binary-data-type)
DynamicValue + LinearAdjustment yaklaşımıyla tanımlanır. 0/1/9/128 bayt payload
gerçek YAMCS 5.12.0 extractor ile çözüldü. **CRC alanını göstermek CRC'yi
doğrulamak değildir**; bu MDB otomatik CRC-valid quality/alarm üretmez. Mevcut
Python arşiv decoder'ı sürüm/boy/CRC kontrolünü ayrı yapar. Bilinmeyen sürüm,
malformed payload veya CRC'nin MDB tarafından doğrulandığı iddia edilmez.

Master HK APID 5 ve Event APID 2000 korunur. TelemetryPacket üst profili version 0,
TM, no-secondary, unsegmented ister; yanlış profil için archive alanları üretilmez.
Gövde yalnız raw archive payload olarak gösterilir; 20 bayt legacy HK kaydının
içindeki packed/bitfield alanlarının taşınabilir decode'u bu adım değildir.

## Testler

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\check_yamcs_mdb.ps1 -GroundProject C:\yamcs-ground\my-mission-control
```

JDK 17+ ve mevcut ground projesinin `target/bundle-tmp/lib` içindeki pinned
YAMCS 5.12.0 jar'ları, güncel `target/classes` postprocessor'u gerekir. Betik
bağımlılık indirmez; Java class/log/cache yalnız `.pio/yamcs-mdb-check` altındadır.
Yeni kaynak/eskimiş postprocessor class eşleşmesi reddedilir.

`YamcsMdbCheck.java` gerçek MDB loader/extractor, command builder ve ground'un
MyCommandPostprocessor sınıfını kullanır. İlk 17 parametre/5 container/4 somut
komut kanıtı korundu; güncel profil30 parametre/6 container/15 somut komuttur.
Dört legacy arşiv ve dört transfer payload boyu, HK BE uint32/float/rad/s,
event ve beş yanlış primary profil kontrolü korunur. Güncel command/range sayıları
yukarıdaki16 komutlu profil bölümündedir; önceki22/26 toplamı tarihsel profildi.
Unsigned64 max/high-bit kayıpsız kodlanır; pinned YAMCS range-reader
uyumluluğu wire belgesindedir. History publisher mock; sunucu/link/soket/kart yoktur.

`test_profile.py` artık 18 XML guard içerir. Wire entegrasyonunda 36 C/46 header,
22 PowerShell ve 162 Python/12 dosya geçti; normal ELF/host ayrımı güncel build
ile doğrulandı. Son RAM 42668, flash 806217; SDK config aynı. Fiziksel kart ve
runtime ground reload doğrulaması bu host/native kapsamına dahil değildir.

## Kurulum ve geri dönüş

[Kaynak/üretilmiş dosya ayrımı ve veri kaybı rehberi](yamcs_data_safety.md),
mevcut `target/yamcs/yamcs-data` riskini ve kart RAM/export farkını açıklar.
Üretilmiş runtime dosyalarını kalıcı kaynak veya güvenli temizlik hedefi sayma.

`tools/install_yamcs_mdb.ps1 -GroundProject ...` varsayılan dry-run'dır. Değişiklik
için `-Apply -ExpectedCurrentHash <mevcut-hash>` gerekir. Betik native doğrulamayı
çalıştırır; doğrulama sırasında hedef/kaynak değişirse durur. Önce byte-identical
backup, sonra doğrulanmış artifact kopyası oluşturur; hash başarısızlığında
yedekten geri döner. Aynı profil zaten kuruluysa NOOP'tur. Recursive silme/taşıma,
NVS veya ground arşivi temizliği yoktur.

Onaylı kaynak kurulum:
`C:/yamcs-ground/my-mission-control/src/main/yamcs/mdb/xtce.xml`.
Yedek aynı klasörde `xtce.xml.before-fsw-20261015-175733-2245.bak` dosyasıdır.
Orijinal hash `C6F1A4F4B91B07FB7D036DD3F64299BAC115D4BD1DEEDC9FB71334BA1634B757`,
ilk profil hash'i `D2A4010EC0FA4A20D4B73829FDECEEC66F265253E053D9D6630C5CA1BE745E99` idi;
backup hash'i orijinalle eşleşti. Son wire güncellemesinden önce ilk profil de
`xtce.xml.before-fsw-20261015-190353-2117.bak` olarak korundu. Wire adımındaki repo/kurulu
kaynak hash'i `AC52E1E4191002D8F6430B790A339A47EA3EDFE394D745E0C7C9B33D594B5EE3`;
kurulu SOURCE yeni native kontrolden ayrıca geçti. Her iki backup da korunur.

Yukarıdaki host/native kurulum sırasında runtime reload veya kart upload yapılmamıştı.
**Sonraki 15 Ekim kontrol noktasında** mevcut ground projesi `compile` + `yamcs:run`
ile açıldı; `target/yamcs` MDB hash'i kaynakla eşleşti. Yeni normal firmware karta
yüklendi; canlı HK/event container tanıma, üç HK alanının processor'da ACQUIRED
olması ve BEGIN/PENDING dryRun geçti. Ardından 17 kayıtlık gerçek retained
transfer ve APID 97 `/myproject/ArchiveTransferContainer` tanıma doğrulandı.
16 Ekim'de CONFIRM/tail/receipt/CANCEL/resume ve kontrollü HTTP yanıt kaybı/retry
gerçek kartta geçti. Ardından HK birimleri ms/V/%/degC/rad/s olarak eklendi;
üç canlı pakette yedi alan ve 235 olay/96 aynı metin çifti karşılaştırıldı.
Native fixture, mevcut preprocessor'un severity/boş metin/tekrar özetini doğruladı.
Güncel SOURCE/runtime/repo hash'i
`7D3C4D84FB1C555069051BDB6CB1DEED32410589A40695E18449444A94C13749`;
önceki AC52E1 profili iki yerde yedeklendi, instance reload yapıldı.
Bu son XML değişikliği kart firmware'ini değiştirmez.
[Güncel kanıt, yedekler ve sınırlar](hk_event_target.md).
