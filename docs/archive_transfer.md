# Toplu arşiv aktarımı — çekirdek sözleşmesi

15 Ekim 2026. İlk adım servis API'sini ve offline ground doğrulama/dosyalama
yardımcısını ekledi. Ardından [TC/TM ve maintained YAMCS kaynak bağlantısı](archive_transfer_wire.md)
host/native doğrulamayla eklendi. Ardından [kontrollü HTTP istemcisi](yamcs_archive_client.md)
fake API/host testleriyle eklendi; canlı kart doğrulaması henüz yok.
Eski GET/WIPE değişmedi. Bu belge mevcut kartta otomatik aktarım/onay/silme
çalıştığını söylemez. Stage 5 yazılım/host kapsamı kapandı; gerçek end-to-end stage 6'da açık.

Hedef çağrı sırası:

`BEGIN → oturum kayıtlarını al → tamamını doğrula → dosyala/fsync/geri oku → CONFIRM`

Her hata yolunda veri tutulur. CANCEL yalnız oturumu kapatır; kayıtları silmez.

## Servis sözleşmesi

`Archive_BeginTransfer(request_id, out)` mevcut kayıtların sabit bir **prefix**'ini
adlandırır. Ek 4 KB snapshot buffer veya yeni mutex yoktur. Tek aktif oturum vardır;
başka request BUSY alır, aynı request'in tekrarı aynı manifest'i döndürür. Sonradan
append edilen kayıtlar manifest'e katılmaz. Boş arşiv EMPTY verir, oturum açılmaz.
`Archive_GetPendingTransfer()` kaybolan BEGIN yanıtından sonra manifest'i salt
okunur sorgular; başka oturum açmaz, veri/receipt değiştirmez.

Manifest: `boot_id:uint64`, `transfer_id:uint32`, `request_id:uint32`,
`digest:uint32`, `record_count:uint16`. Bunlar mantıksal alanlardır; C struct
padding'i/endianness'i wire formatı değildir. Wire kodlama ayrı belgede tanımlıdır.

`Archive_ReadTransferRecord()` tüm manifest alanlarının aktif oturumla eşleşmesini
ve indeksin snapshot içinde olmasını ister. CRC16 doğrulanan özel kopyayı döndürür;
hangi indekslerin API üzerinden okunduğunu bitmap'te tutar. Tekrarlı veya sırasız
okuma mümkündür. Legacy GET bu bitmap'i doldurmaz. Okuma/dosyalama/send başarısı
tek başına silmez; API'den okundu bilgisi **paketin ground'a ulaştığı kanıtı değildir**.

`Archive_ConfirmTransfer()` aynı manifest'i, bütün indekslerin oturum API'sinden
okunmuş olmasını ve mevcut prefix'in yeniden hesaplanan digest'ini ister.
Eşleşirse yalnız o prefix tek kilitli işlemde çıkarılır; arada eklenen tail kayıtları
aynı sırayla korunur. İşlemden sonra canlı legacy indeksler yer değiştirebilir.
En son tamamlanmış CONFIRM receipt'i tutulur: kayıp yanıt nedeniyle aynı onay
tekrarlanırsa ikinci kez silmez; daha yeni bir oturum aktifken de onu değiştirmez.
Receipt geçmişi sınırsız değildir; başka tamamlanma/cancel eski receipt'i aşınca
eski onay STALE döner, hiçbir kayıt silmez. Son kapanmış request'in BEGIN tekrarı
STALE'dir; eski request tekrar yeni oturum açsa da yeni transfer ID gerekir.

`Archive_CancelTransfer()` eşleşen oturumu kayıtları değiştirmeden kapatır;
tekrar cancel idempotent'tir. Reset/WIPE aktif oturumu ve receipt'i geçersizleştirir,
aynı boot içinde transfer sayacını geri sarmaz. Sayaç `UINT32_MAX`'a ulaşırsa yeni
oturum EXHAUSTED ile reddedilir; ID wrap yoktur. Bu uç sayaç dalı 2^32 oturum
çalıştırılarak test edilmedi.

Eksik okuma NOT_READY, kimlik uyuşmazlığı STALE, kilit hatası UNAVAILABLE,
bozuk kayıt/digest CRC hatası verir. Başarısız çıktı parametreleri değişmez.
Yeni API'ler aynı 10 ms-tick mutex bekleme politikasını kullanır; digest/record CRC
kontrolleri ve compaction bu kilidin içindedir (en fazla 30 × 128 payload baytı).
FDIR/zaman/log/TX kilit dışındadır. Tam API çalışma süresi/ESP32 lock jitter'ı
ölçülmedi; 10 ms toplam yürütme süresi garantisi yoktur. CRC hatası mevcut FDIR
critical kaydını bildirir; cancel/reset otomatik fault-clear veya mode recovery değildir.

## Kontrol özeti ve boot kimliği sınırı

CRC-32/ISO-HDLC (reflected poly `0xEDB88320`, init/xorout `0xFFFFFFFF`) şu kanonik
baytlardan hesaplanır: önce BE `count:u16`; her kayıt için BE `index/id/length/crc`
(dört uint16), ardından tam payload. Böylece metadata da özete girer. Kayıt CRC16'sı
ayrıca payload üzerinde CCITT-FALSE'tur. Test vektörü:

`0002000012340000ffff0001abcd000929b1313233343536373839 → 0x417EE9BC`

CRC'ler kriptografik hash/MAC veya komut kimlik doğrulaması değildir. Firmware,
ground'un gerçekten dosya kaydettiğini bağımsız ispatlayamaz; güvenilir istemci
prosedürü ve ileride açık kullanıcı clear onayı gerekir. Kötü niyetli istemciye
karşı veri silme güvenliği iddiası yoktur; legacy anahtarlı WIPE da hâlâ vardır.

Boot discriminator platform adaptörü `archive_port_esp32.c`'dedir; servis kodu ESP
RNG API'sine bağlanmaz. RNG'den nonzero uint64 istenir, dört sıfır sonuçta init
başarısızdır. NVS yazılmaz, ADC/radyo ayarı değiştirilmez. ESP-IDF 5.5'in kurulu
`esp_random.h` notuna göre Wi-Fi/Bluetooth veya ek entropy koşulları yokken true
random garantisi yoktur; başlangıçta Wi-Fi henüz açılmamıştır. Dolayısıyla bu
discriminator **kalıcı/çakışmaz boot counter veya kriptografik nonce değildir**.
Reboot sonrası boot kimliği çakışmasının mutlak önlenmesi doğrulanmış bir özellik
değildir. Boot ID alanı farklıysa eski ACK reddedilir; resetle RAM yine kaybolur.

## Ground dosyası yardımcısı

`tools/archive_transfer.py` ağ/seri port açmaz ve komut göndermez. Eksiksiz
0..count-1 indeks kümesini, payload boyunu/CRC16'sını ve tüm prefix digest'ini
denetler; alan tipleri/range, duplicate indeks ve JSON alanları da kontrol edilir.

`save_verified_export()` yeni dosyayı exclusive-create ile açar; eski dosyanın
üstüne yazmaz. Write/flush/fsync, kapatma ve yeniden doğrulama başarıyla bitince
immutable `SavedTransfer` receipt'i döner. Salt `load_verified_export()` yalnız
doğrulanmış manifest döndürür; synced-save receipt'i üretmez ve tek başına clear
izni değildir. Hata halinde receipt yoktur; yarım veya sync edilememiş dosya
kalabilir, otomatik ACK verilmez. `fsync` OS seviyesinde istektir; depolama aygıtı,
directory durability veya güç kesilmesine karşı mutlak kalıcılık garantisi değildir.

## Kanıt ve kalan iş

`archive_transfer_check` gerçek servis/CRC/pthread mutex ile init/boot failure,
empty/busy/retry, tüm kimlik alanları, read-set, timeout, payload/metadata corruption,
reset/cancel/duplicate ACK, full 30 slot ve CONFIRM ile yarışan dört append'i sınar.
ESP adaptörü ayrı mock RNG kontrolünde null/nonzero/dört sıfır sınırını sınar.
12 yeni Python testi kanonik C vektörünü, eksik/bozuk kayıtları, 30 maksimum
payload'ı, overwrite engelini, short write/fsync/readback hata yollarını doğrular.
Gerçek güç kaybı/disk arızası veya exhaustive interleaving kanıtı değildir.

TC/yanıt kodlaması ve maintained YAMCS MDB komut/container'ları sonraki wire
paketinde eklendi. Verified-export sonrası açık onay isteyen ground istemcisi de
fake API/host testleriyle tamamlandı. Çalışan YAMCS/kartta kayıp/tekrar/append/
receipt doğrulaması ayrıca yapılmalıdır.
İlk çekirdek adımında API'ler production'a bağlı değildi; aşağıdaki ölçümler o
aşamanın tarihsel sonucudur. Güncel wire paketi API'leri bağlar ve kaynak ground
profilini yedekli kurar; güncel imaj/kurulum kanıtı wire belgesindedir.

Son doğrulama: 35 C grubu/46 header, 22 PowerShell ve 147 Python (11 dosya) geçti.
Son salt-okunur pending sorgusu ekinden sonra ilgili C grubu + 46 header ve 12
export testi yeniden geçti. Normal firmware configure/build 52.76 s; pending sorgu
build'i 28.86 s, son yorum sonrası zorlanmış relink 12.19 s. RAM 42620 (+16),
flash 803985 (+96) bayt; son üç geçersiz watchdog profil kontrolü de geçti. SDK config SHA256
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087` değişmedi.
Metadata/normal ELF ayrımı 15 component/59 proje-header bağı/48 tekil üretim C
kaynağı için geçti. `nm` yalnız yeni boot adapter'ını gösterdi; Begin/Confirm/
Pending henüz normal production yoluna bağlı olmadığı için linkten elenmiştir.
