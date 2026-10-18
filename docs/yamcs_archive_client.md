# Kontrollü YAMCS arşiv istemcisi

`tools/yamcs_archive_transfer.py` mevcut YAMCS HTTP API'si üzerinden çalışır.
Seri port/raw UDP açmaz; reset, legacy WIPE, mode değişimi, link açma veya command
queue/verification bypass yapmaz. Yeni firmware/MDB ile temel canlı bağlantı ve
istemcinin varsayılan dryRun kontrolü 15 Ekim'de geçti. Ardından gerçek karttan
17 kayıt `--send --output` ile CRC16/CRC32/fsync/readback doğrulanarak indirildi;
PENDING ve READ(0) ile aynı snapshot'ın kartta kaldığı görüldü. **16 Ekim'de gerçek
CONFIRM/tail/receipt/CANCEL/resume ve kontrollü HTTP yanıt kaybı/retry testleri de geçti.**
[Kart/YAMCS kontrol noktaları ve kaydedilen dosya](target_yamcs_smoke.md).

Önceki CONFIRM ön kontrolü pending oturumu bulamadı ve silme göndermedi: FDIR
stack taşması/PANIC, eski RAM oturumunu kaybettirmişti. Sonraki düzeltme 4096 bayt
bütçe ile gerçek doluluk yolunda 305.8 s tek boot/1636 bayt boş stack kontrolünü
geçti. Yeni boot'tan 27 kayıt indirildi ve pending/READ(26) ile korunduğu görüldü.
Eski 17 kayıtlık disk kopyası korunur fakat eski manifest yeni boot'ta kullanılamaz.
[FDIR düzeltme kanıtı](fdir_stack_target.md). Ertesi gün yeni boot'ta 24+6 kayıtla
kontrollü işlem testleri açık izinle tamamlandı; test sonunda RAM EMPTY/pending yok,
tüm kopyalar diskte. [Güncel dosyalar, koşullar ve sonuç](archive_target_tests.md).

## Ön koşul ve güvenli varsayılan

Canlı kullanımdan önce yeni firmware kartta, maintained MDB çalışan YAMCS'te
yüklü olmalıdır. Kaynak XTCE'nin kurulmuş olması runtime reload değildir.
Eski firmware'e yeni komut göndermek unsupported-command fault'u üretebilir.
Firmware upload/reset mevcut RAM arşivini kaybettirir; bu işlem ayrıca onaylanır.
YAMCS `udp-out` hedefi beklenen IPv4:10025, `udp-in`/`udp-out` durumu OK olmalıdır.
İstemci ağ veya firewall ayarı değiştirmez.

Varsayılan yalnız BEGIN/PENDING için YAMCS `dryRun` binary üretimini kontrol eder;
karta komut veya yerel export dosyası göndermez/yazmaz:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/yamcs_archive_transfer.py
```

HTTP adresi varsayılan `http://localhost:8090`; `--url`, `--instance`, `--processor`,
`--board-ip` açıkça seçilebilir. URL'de credentials/path/query/fragment kabul
edilmez, otomatik HTTP redirect/proxy kullanılmaz. Mevcut yerel auth'suz kurulum
hedeflenir; kullanıcı/rol yetkilendirmesi veya credential yönetimi eklenmedi.

## Kullanım modları

Önce çıktı klasörü hazırlanır. Mevcut dosyanın üstüne yazılmaz; yol/parent hatası
komuttan önce reddedilir. `local/` Git'ten ignore edilir:

```powershell
New-Item -ItemType Directory -Path local/archive -Force
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/yamcs_archive_transfer.py --send --output local/archive/pass1.json
```

Bu komut BEGIN, her indeks için READ, tam CRC/digest doğrulama ve exclusive
write/flush/fsync/readback yapar. **Kayıtlar ve oturum kartta kalır.** Yeni BEGIN
BUSY olabilir; başka oturum otomatik iptal edilmez. İlk request ID rastgele uint32
seçilir ve basılır; `--request-id 0x12345678` açıkça verilebilir. Retry aynı request'i
kullanır, yeni snapshot başlatmaz.

Kaydetme sonrasında aynı dosyayı açıkça onaylamak:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/yamcs_archive_transfer.py --send --confirm-export local/archive/pass1.json --confirm-clear
```

Dosya tekrar doğrulanır, byte'ları yeniden yazılmadan sync/readback yapılır;
yalnız aynı manifest ile CONFIRM üretilir. Alternatif olarak indirme komutuna
`--confirm-clear` eklenirse başarılı kaydetmeden hemen sonra onay gönderilir.
Salt bir manifest veya `load_verified_export()` sonucu synced-save receipt
yerine geçmez. CONFIRM actual POST öncesinde dosya yeniden kontrol edilir.

Veriyi silmeden sadece kayıtlı oturumu kapatmak:

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/yamcs_archive_transfer.py --send --cancel-export local/archive/pass1.json
```

CANCEL sonrasında aynı eski dosyayla CONFIRM STALE olur; yeni indirme yeni bir
oturumdur. Kaydetme yarım kaldıysa yeni bir dosya adı ile açık pending oturumdan
devam edilir (yeni BEGIN veya wipe yok):

```powershell
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/yamcs_archive_transfer.py --send --resume-pending --output local/archive/recovered.json
```

Yanlış/eksik kayıt, disk doluluğu, fsync/readback hatası veya tutarsız duplicate
durumunda CONFIRM ve otomatik CANCEL yapılmaz. Yarım dosya kalabilir; silinmez
ve üstüne yazılmaz. Operatör farklı bir çıktı yolu seçer. `--resume-pending`
mevcut tek oturumu açıkça benimser; sahiplik/kimlik doğrulaması sağlamaz.

## Gönderim, yanıt ve retry kanıtı

İstemci yalnız 31..35 opcode'larına izin verir. Her gönderim öncesinde link
türü/durumu/hedefi ve YAMCS dry-run binary'sinin APID/type/flags/size/body'si
kontrol edilir. UInt64 BootId HTTP JSON'a decimal **string** yazılır.

Sonra iki ayrı kanıt beklenir: Command History'de doğru command ID/name,
`Acknowledge_Sent_Status=OK` ve exact postprocessed binary; ayrıca arşivde doğru
link/APID/opcode/manifest/index/status ve CRC-checked kart yanıtı. Yalnız Sent OK
veya yalnız TM yeterli değildir. Çelişen duplicate yanıtlar hata olur.

Komuttan **önce**, en yeni generation-time slot'undaki mevcut arşiv paketleri
baseline'a alınır. Böylece aynı milisaniye zaman damgasındaki önceki bir yanıt
yeni kanıt diye kabul edilmez. Sonra IssueCommand generationTime inclusive alt
sınırıyla ascending paketler aranır. Baseline'ın ilerisinde geri giden server
clock'u reddedilir. Bu, kriptografik/per-attempt TC nonce garantisi değildir;
geç gelen paket ve güvenilir ground/boot/CRC sınırları wire sözleşmesinde kalır.

Sayfalama `continuationToken` → `next` kullanır ve her sayfada orijinal filtreleri
korur; deprecated `packet` ve yeni `packets` alanı desteklenir. Dönen/repeated
token ve çelişkili alias reddedilir. En fazla 10 ×100 kayıt taranır; yoğun arşivde
sessizce ilk sayfa başarı sayılmaz. HTTP JSON 1 MiB ile sınırlıdır; `read1` ile
parça parça okuma deadline'ı kontrol edilerek sürekli trickle da sınırlanır.

Varsayılan komut başına attempt deadline 8 s, retry 2 (toplam en fazla 3 deneme),
HTTP socket timeout en fazla 4 s; CLI sınırları timeout (0,30], retry 0..3'tür.
Socket/scheduler çağrıları nedeniyle bunlar hard real-time toplam süre garantisi
değildir. Retry yalnız aynı request/manifest/index'i kullanır. Fatal HTTP/auth,
yanlış binary/kimlik/CRC ve board rejection otomatik yeni bir akış başlatmaz.

**CONFIRM yanıtının kaybı verinin tutulduğu anlamına gelmez.** Prefix commit
edilmiş olabilir. İstemci `OutcomeUnknown` bildirir; aynı doğrulanmış dosyanın
onayı tekrar edilir. Yeni BEGIN/başka dosya/wipe ile sonuç tahmin edilmez.
Son completed receipt aşılmışsa STALE dönebilir; önceki temizleme hakkında
çıkarım yapılmaz. Bu araç fiziksel disk kalıcılığını/board kimliğini kriptografik
kanıtlamaz; CRC/MAC ve RAM/reset sınırları değişmedi.

API alanları resmî [Issue Command](https://docs.yamcs.org/yamcs-http-api/commands/issue-command/),
[Get Command](https://docs.yamcs.org/yamcs-http-api/commands/get-command/) ve
[List Packets](https://docs.yamcs.org/yamcs-http-api/packets/list-packets/) sözleşmeleriyle
karşılaştırıldı. Kurulu pinned 5.12.0 protobuf sınıflarında id/name/time/binary ve
packet/packets/continuationToken/next alanları ayrıca salt okunur doğrulandı.

## İstemci geliştirme adımının host doğrulaması

51 yeni istemci testi gerçek Python istemcisi ve HTTP request serialization'ını
**sahte YAMCS/board state modeli ve fake opener/clock** ile sınar: default dry-run,
retain/clear/tail, lost BEGIN/READ/CONFIRM HTTP/TM reply, retry bound, history/reply
ayrımı, wrong target/binary/identity, CRC, duplicate, pagination, eski aynı-time
yanıt, clock rollback, dosya hatası/son anda değişim, resume/cancel/stale, auth ve
trickle deadline. Gerçek soket, JVM/API server veya ESP32 end-to-end testi değildir.
Resync için üç ek test ile export grubu 12→15 oldu. Tüm **216 Python testi/13
dosya** geçti; normal metadata/ELF ayrımı tekrar geçti.

Firmware/CMake/SDK, repo/kurulu XTCE ve Java/YAML değişmedi. C/header/native/ESP32
build'i bu Python-only adımında tekrar çalıştırılmadı; önceki 36 C/46 header,
native 19 command/19 reject ve RAM 42668/flash 806217 kanıtı tarihsel kalır.
HTTP canlı sunucuya, seri porta veya karta bağlanılmadı. TM/TC/arşiv uygulama ve
host-test kapsamı kapandı; runtime reload/firmware upload ve gerçek retained
download/clear-receipt deneyi stage 6'da açıkça bekler.

Sonraki stage 6 kontrolünde gerçek HTTP/runtime/ESP32 ile retained download ve
pending/yeniden okuma geçti. Yukarıdaki paragraf Python-only geliştirme adımının
tarihsel kapsamıdır; güncel fiziksel kanıt [kontrol noktası belgesindedir](target_yamcs_smoke.md).
16 Ekim'de clear/tail/receipt/CANCEL/resume ve kontrollü HTTP kabul-yanıtı kaybı/retry
testleri gerçek kartta geçti. Gerçek UDP/TM kaybı oluşturulmadı; güncel koşullar
ve tekrarlanabilir ayrı target aracı [işlem belgesindedir](archive_target_tests.md).
