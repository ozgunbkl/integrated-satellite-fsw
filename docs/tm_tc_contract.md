# Etkin TM/TC paket sözleşmesi

2026-10-15. Bu profil, projenin mevcut UDP yolunu tanımlar; bütün CCSDS/PUS
özelliklerinin veya uçuş yeterliliğinin implementasyonu değildir.

## UDP komut ve iç kuyruk aynı paket değildir

UDP TC: `6 bayt primary + 1..48 bayt komut` = **7..54 bayt**.
İç kuyruk: `6 primary + 8 sıfır placeholder + 1..48 komut + 2 CRC` = **17..64 bayt**.
Yönlendiriciye verilen komut verisi, yalnız aynı 1..48 bayttır; CRC ve header verilmez.

Wire primary big-endian'dır: version 0, type TC=1, secondary flag=0,
unsegmented sequence flags=3, 11-bit APID ve 14-bit sequence. Length alanı
`UDP boyu - 7` olmalıdır. Type TM=0 ve length=data-field-size−1 kuralları
[CCSDS 133.0-B-2, §4.1.3](https://ccsds.org/Pubs/133x0b2e2.pdf) ile uyumludur.
Secondary header'lı veya segmented **wire TC** bu proje profilinde desteklenmez.
APID 0x7FF idle komut olarak reddedilir; diğer bilinmeyen APID'ler mevcut router
warning politikasına gider. Sequence kontrolü replay koruması sağlamaz.

Alıcı 55 baytlık buffer kullanır. 54 bayt sınırını aşan datagram, socket tarafından
kırpılsa bile 55 bayt olarak görünür ve reddedilir. Eksik/fazla byte, yanlış
version/type/flag/length, boş komut ve taşan length kuyruğa girmeden reddedilir.

Adaptör iç header'ın secondary flag ve length alanlarını iç paket boyuna göre
yeniler, sonra CRC-16/CCITT-FALSE hesaplar (poly 0x1021, init 0xFFFF, no reflection,
xorout 0; `123456789` → 0x29B1). CRC, iç header ve komutun tamamını kapsar;
trailer BE yazılır, `packet_crc` kopyasıyla birlikte tutulur. İşleyici hesaplanan
CRC, trailer ve kopyanın üçünü karşılaştırır. Header 8 baytındaki sıfırlar gerçek
zaman ölçümü değildir; iç header wire'a geri gönderilmez.

**Wire'da CRC veya kimlik doğrulama yoktur.** Alıcı veriyi aldıktan sonra hesaplanan
CRC ağdaki bozulmayı/kötü niyetli komutu doğrulamaz; yalnız sonraki iç aktarımın
yerel bütünlük kontrolüdür. UDP send başarısı teslim/komut icra onayı değildir.
Kuyruk dolunca paket bırakılır; otomatik resend veya deduplication yoktur.

## Komut APID'leri

| Alt sistem | TC APID (hex / decimal) | Komut kimlikleri |
| --- | --- | --- |
| ADCS | 0x010 / 16 | 0x01 mode, 0x05 estimation reset |
| EPS | 0x020 / 32 | 0x10 heater, 0x20 bus reset; mevcut hardware işlemleri stub |
| FDIR | 0x030 / 48 | 0x50 warning ACK, 0x52 guarded mode isteği |
| CDHS | 0x040 / 64 | 0xFF reboot, 0x70 sync time; mevcut işlemler stub |
| HK | 0x050 / 80 | 0x01 interval (1..60 s), 0x02 force report |
| Arşiv | 0x060 / 96 | 0x30 + BE uint16 index; 0x3F + 0xAA wipe (yıkıcı) |
| Payload | 0x070 / 112 | 0x00 init, 0x01 start, 0x02 stop, 0x03 rate, 0x04 reset |

Bu adım gerçek karta komut yollamaz; stub kaydı gerçek reboot/heater/RTC eylemi
kanıtı değildir. `cmd_inject.c` eski iç-kuyruk demosudur; wire builder değildir,
normal core tarafından `CMD_INJECT` görevi olarak hâlâ başlatılır: 5 s sonra
NOMINAL isteği, 10 s sonra stub heater komutu ve 10 s sonra yerel readout bayrağı
gönderir. Ardından kendini siler. Wire builder değildir; örnek mode ID'si gerçek
FDIR_SET_MODE sözleşmesi yerine kullanılamaz. Yerel readout, ground downlink/ACK
veya arşiv silme işlemi değildir.

## Telemetri ve eski demo yolu

Etkin `YAMCS_SendTelemetry`: `6 primary + 1..506 payload`, type TM=0, secondary=0,
unsegmented, BE length, APID başına 14-bit sıra. Boş/null payload ve 11 bit dışı
APID reddedilir; yanlış APID sessizce maskelenip başka hedefe çevrilmez.

Etkin TM: master HK **APID 5**, 28 bayt BE uptime/float (gyro rad/s); archive
**APID 96**, sürümlü metadata ve payload CRC ([ayrıntı](archive_downlink.md)); event
**APID 2000**, 64 bayt metin. Yeni transfer yanıtı **APID 97**, 33..161 bayt gövde
([ayrıntı](archive_transfer_wire.md)); transfer komutları APID 96'da kalır.
HK komutunun APID 80, TM'nin APID 5 olması iki ayrı
yönün mevcut sözleşmesidir; bu adım numaraları değiştirmez.

Eski radyo demo envelope'u `6 primary + 8 BE mission-ms + data` ve ayrı frame
CRC'sidir; etkin UDP yolu değildir. Buradaki yanlış type=TC biti düzeltildi
(0x1800 → 0x0800). Demo sequence hâlâ sıfırdır ve caller output kapasitesini
sağlamalıdır; tam protokol/CCSDS zaman kodu uyumluluğu iddiası yoktur.

## Yer istasyonu ve kanıt

Yerel `C:/yamcs-ground/my-mission-control/src/main/yamcs/mdb/xtce.xml` salt okunarak
incelendi: ARCHIVE_GET_BY_INDEX (96), HK_SetInterval (80), PAYLOAD_SetRate (112),
FDIR_SetMode (48) sabitleri firmware ile eşleşir; MasterHKContainer=5 ve Event=2000.
Eski Spacecraft TM=100 ve MyProjectPacket/simülatör komutları TC=101 farklıdır.
Bu ilk incelemeden sonra maintained profilde legacy simülatör ayrıldı ve archive
TM container eklendi; ardından beş transfer komutu ve APID 97 container kaynak
ground'a yedekli kuruldu. [Güncel native/kurulum kanıtı](yamcs_profile.md).
Java/YAML ve çalışan YAMCS değişmedi; live link sonrası encoding ayrıca açıktır.

Yeni `tests/host/tc_ingress/check.c` gerçek receiver/CRC/processor/router ile
85 offline girdi, tüm 7..54 boylar, 55..80 kırpılma, header/length hataları,
queue full, altı alt-sistem rotası ve unknown APID'yi kontrol eder; 55 komut
kuyruktan işlenir. Alt servisler/RTOS/socket mock'tur, işlem yapılmaz.
Golden iç paket: `1860c007000c00000000000000003012345ac3` (CRC bağımsız Python
`binascii.crc_hqx(..., 0xFFFF)` ile hesaplandı). Üç ek Python testi wire builder'ı
doğrular. Mevcut frame testi body/trailer/metadata bozulmasını ayrı ayrı dener.

31 C/45 header, 125 Python/9 dosya, 22 PowerShell ve 3 negatif profile kontrolü
geçti. Normal build 31.47 s, RAM 42596/327680 (aynı), flash 803509/1740800 (+276).
SDK config SHA256 aynı: `DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Upload/reset veya ağ gönderimi yapılmadı. Kartın eski imajı korunur; yeni sıkı
wire reddi/CRC davranışının fiziksel doğrulaması henüz yapılmadı.
