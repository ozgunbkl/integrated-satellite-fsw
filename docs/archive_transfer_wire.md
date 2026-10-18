# Arşiv aktarımı: komut, TM ve YAMCS kaynak bağlantısı

15 Ekim 2026. [Çekirdek oturum sözleşmesi](archive_transfer.md) artık gerçek komut
dispatcher'ına bağlıdır. Bu adım host/native kodlama ve kaynak kurulum kanıtıdır;
karta upload/komut, çalışan YAMCS reload veya canlı aktarım yapılmadı.

## Wire komutları

TC APID **96 (0x060)** korunur. Yeni yanıtlar TM APID **97 (0x061)** kullanır;
eski tek-kayıt GET/TM APID 96 değişmez. İkisi de mevcut 6 bayt primary/no-secondary/
unsegmented profilini kullanır. Bütün çok-baytlı tamsayılar big-endian'dır.

Manifest `M` 22 bayttır: `boot:u64 | transfer:u32 | request:u32 | digest:u32 | count:u16`.
Bu alanların tamamı aktif oturumla eşleşmelidir; C struct doğrudan gönderilmez.

| Komut / XTCE adı | Komut gövdesi | Toplam wire TC |
| --- | --- | ---: |
| ARCHIVE_BEGIN_TRANSFER | `31 request:u32` | 11 bayt |
| ARCHIVE_PENDING_TRANSFER | `32` | 7 bayt |
| ARCHIVE_READ_TRANSFER | `33 M index:u16` | 31 bayt |
| ARCHIVE_CONFIRM_TRANSFER | `34 M A5` | 30 bayt |
| ARCHIVE_CANCEL_TRANSFER | `35 M` | 29 bayt |

Her boy **tam** eşleşir. Bilinen transfer komutunun yanlış boyu status PARAM
üretir; API çağrılmaz, kayıt/oturum değiştirilmez. Null/boş girdi mevcut dispatcher'da
atlanır. Bilinmeyen opcode'un eski fault politikası korunur. 31 baytlık en büyük
wire TC, mevcut 54 bayt ingress ve 64 bayt private queue sınırlarının içindedir.
Her TC en fazla bir yanıt üretir; kart içinde 30 kayıtlık network-send döngüsü yoktur.

## TM yanıtı

Gövde 33..161, primary dahil paket **39..167 bayt**; header length=`toplam-7`.
Gövde içi konumlar:

| Bayt | Alan |
| --- | --- |
| 0 | sürüm 1 |
| 1 | istenen opcode (31..35) |
| 2 | status |
| 3..24 | `M`: boot 3..10, transfer 11..14, request 15..18, digest 19..22, count 23..24 |
| 25..26 | kayıt indeksi; kayıt dışı yanıtta FFFF |
| 27..28 | record ID |
| 29..30 | payload uzunluğu |
| 31..32 | payload CRC16 |
| 33.. | tam olarak length bayt payload |

Status sabitleri: OK=0, FULL=1, CRC=2, PARAM=3, NOT_FOUND=4, EMPTY=5,
UNAVAILABLE=6, BUSY=7, STALE=8, NOT_READY=9, EXHAUSTED=10.
Yalnız başarılı READ kayıt içerir. Diğer yanıtlarda record ID/length/CRC sıfırdır;
başarılı boş kayıt ise length=0, CRC=FFFF'tir. READ hatasında istenen indeks,
tam boy geçerliyse echo edilir. BEGIN hatası request'i echo eder, diğer manifest
alanları sıfır kalır; pending-not-found'da manifest sıfırdır. Hata alanları aktif
manifest veya başarı kanıtı olarak kullanılmaz.

Yanıt göndermeden önce servis kilitleri bırakılır. READ TX hatasında kayıt
silinmez. **Geçerli CONFIRM commit edildikten sonra yanıt kaybolursa prefix zaten
çıkarılmıştır**; send hatası rollback veya veri korunumu anlamına gelmez. Aynı
CONFIRM tekrarlandığında çekirdeğin receipt'i ikinci silmeyi engeller. Ground,
yalnız YAMCS `Sent OK` değil doğru opcode/status/manifest ile gelen yanıtı kontrol
etmelidir. FDIR CRC fault'u veya UDP send başarısı disk kalıcılığı kanıtı değildir.

## YAMCS ve offline yardımcı

Maintained `config/yamcs/xtce.xml` toplam **30 parametre, 6 TM container ve 9 somut
ve 1 abstract komut** içerir. `ArchiveTransferContainer` APID 97'yi alanlara ayırır;
TransferPayload boyu önceki TransferLength ×8 bittir. MDB CRC/digest'i otomatik
doğrulamaz; sürüm/boy/CRC doğrulaması ayrıca Python decoder'dadır.

CONFIRM'de public `ConfirmKey` zorunludur ve yalnız **165 (0xA5)** kabul edilir;
default değer yoktur. Manuel YAMCS komutu istemcinin dosya kaydetmesini bağımsız
ispatlayamaz. Anahtar/MAC/kimlik doğrulaması değildir; güvenilir LAN/istemci
sınırı ve boot/RNG/CRC sınırlamaları çekirdek belgesinde geçerlidir.

YAMCS 5.12, XML integer-range attribute'larını signed Java long olarak okur;
unsigned 64-bit üst sınır profile'da `maxInclusive="-1"` bit gösterimiyle yazılır.
Unsigned checker'ın bunu `2^64-1` yorumladığı native builder ile doğrulandı:
max değer tam 8 FF baytı üretir, 0 ve 2^64 reddedilir; TM'nin high-bit'li uint64'ü
de kayıpsız çıkarılır. Bu pinned-reader uyumluluğudur, farklı XTCE okuyucusuna
taşınırken otomatik uyumluluk iddiası değildir. API uint64 için decimal **string**
kullanılmalıdır; JavaScript Number üzerinden hassasiyet kaybına izin verilmez.

`tools/archive_transfer_wire.py` ağ açmaz. Begin/pending/read/cancel ve exact
CCSDS TC üretir; TM primary/sürüm/op/status/boy ve READ CRC16'sını çözer.
`confirm_command()` yalnız `SavedTransfer` + `confirm_clear=True` alır; dosyayı
yeniden doğrular ve receipt manifest'iyle eşleşmeden komut üretmez. Bu hâlâ
güvenilir kod prosedürüdür, uzaktaki kartın disk durumunu ölçmesi değildir.
YAMCS HTTP üzerinden alımı/yazmayı/retry'ı yöneten [kontrollü CLI](yamcs_archive_client.md)
sonraki Python-only adımında eklendi; gerçek server/kart doğrulaması yapılmadı. Mevcut
read-only tek-kayıt kontrol betiği yeni CONFIRM/WIPE göndermez.

## Kanıt ve kurulum

- Gerçek C dispatcher/core/mutex: 235 yanlış boy, golden manifest, pending/empty,
  0/9/128 payload, key/read-set/kimlik/timeout/CRC, başarısız READ TX, kayıp CONFIRM
  receipt tekrarı, tail koruma ve cancel sınandı. Sockets/RTOS/TX host sınırıdır.
- Native YAMCS: legacy 14 + yeni 5 postprocessed komut, 8 + 11 geçersiz/missing-key
  reddi; HK/event/archive ve transfer dynamic payload extraction geçti. Ağ/board yok.
- Tam regresyon: **36 C grubu, 46 header, 22 PowerShell, 162 Python/12 dosya** geçti.
  15 component/59 header bağı/49 tekil production C kaynağı ve normal ELF test
  ayrımı geçti. Transfer API'leri artık normal ELF'te gerçekten bağlıdır.
- Normal configure/build **55.38 s**: RAM **42668** (+48), flash **806217** (+2232).
  Runtime mutex wait/stack/heap/jitter ve fiziksel aktarım bu ölçüm değildir.
  SDK config hash'i `DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087` aynı.

Onaylı installer yalnız mevcut ground SOURCE
`C:/yamcs-ground/my-mission-control/src/main/yamcs/mdb/xtce.xml` dosyasını değiştirdi.
Yeni backup: `xtce.xml.before-fsw-20261015-190353-2117.bak` (D2A4010E... profilinin
tam kopyası). İlk orijinal simulator backup'ı da korunur. Repo ve kurulu kaynak SHA256:
`AC52E1E4191002D8F6430B790A339A47EA3EDFE394D745E0C7C9B33D594B5EE3`.
Kurulu SOURCE dosyası native kontrolden ayrıca geçti. Ground Java/YAML,
`target/yamcs`, çalışan sunucu ve kart değişmedi. Sonraki kontrollü ground CLI
fake API/host testleriyle eklendi; runtime reload ve yeni firmware upload/reset
ayrı, RAM kaybı açıklanan fiziksel doğrulama adımıdır. Stage 5 son maddesi uygulama/
host-test sınırında kapandı; gerçek end-to-end stage 6'da açık kalır.
Sonraki [ilk fiziksel kontrol noktasında](target_yamcs_smoke.md) yeni normal
firmware yüklendi ve ground runtime yeni MDB ile açıldı. Temel HK/event bağlantısı
ve arşiv komutları için dryRun geçti. Ardından 17 kayıtlık retained download,
CRC16/tam CRC32/fsync/readback ve PENDING/READ(0) retention kontrolü gerçek kartta
geçti; APID 97 yanıtı runtime container'ıyla tanındı. 16 Ekim'de yeni boot'ta
24+6 kayıtla CONFIRM/tail/receipt/CANCEL/resume ve kontrollü HTTP yanıt kaybı/retry
gerçek kartta geçti. Gerçek UDP/TM kaybı oluşturulmadı; ayrıntılar ve sınırlar
[arşiv işlem belgesindedir](archive_target_tests.md).
