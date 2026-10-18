# Gerçek kart/YAMCS arşiv işlem testleri

2026-10-16, yaklaşık 07:41–07:52 (+03:00). Mevcut YAMCS 5.12.0 projesi çevrimdışı
`yamcs:run` ile açıldı; source/Java/YAML değiştirilmedi, arşivler temizlenmedi.
COM7'deki ESP32'ye yeniden firmware yüklenmedi ve kasıtlı reset yapılmadı.
Son yüklenen normal imaj, 15 Ekim FDIR stack düzeltmeli imajdır; yerel `.bin`
SHA256 hâlâ `A3DC0D97B6AAD65B4D17F7C6CC6CF1EDE6FAE82679F5DA55564054B67E4D1440`.
Repo/ground SOURCE MDB hash'i aynı: `AC52E1E4191002D8F6430B790A339A47EA3EDFE394D745E0C7C9B33D594B5EE3`.
Bu oturumda flash read-back hash ölçümü yapılmadı.

## Sonuç: PASS

Yeni boot'ta BEGIN ile **24 kayıtlık** prefix seçildi ve bütün kayıtlar gerçek
YAMCS → ESP32 → YAMCS yoluyla CRC16/tam CRC32/fsync/readback doğrulanarak kaydedildi.
Bu snapshot açıldıktan sonra normal üretici arşive **6 kayıt** daha ekledi.
Kapasite 30 olduğundan bu append-tail, silme deneyi için gerçek test verisi oldu.

`tools/archive_target_check.py` şu adımları gerçek kartta geçti:

1. PENDING ile kaydedilmiş prefix/boot eşleşmesi doğrulandı. Tail, global
   GET_BY_INDEX 24..29 ile CRC-checked okundu ve ayrı proof dosyasına sync/readback
   ile kaydedildi. Bu proof bir CONFIRM yetkisi/SavedTransfer değildir.
2. READ için ilk kabul edilmiş HTTP yanıtı istemcide atıldı; aynı manifest/indeks
   ile retry geçti. İki ayrı command ID'nin exact binary + Sent OK kaydı doğrulandı.
3. Kaydedilmiş 24 kaydın CONFIRM yanıtı aynı modelle bir kez atıldı; retry
   eşleşen kart receipt'iyle geçti. Sonraki global GET 0..5, altı tail kaydının
   ID/length/CRC/payload değerlerini silme öncesiyle **byte-byte aynı** buldu.
4. Tail için yeni oturum açılıp altı kayıt ayrıca kaydedildi. Eski prefix'in
   CONFIRM'i bu yeni oturum sürerken tekrarlandı: yeni PENDING ve altı READ
   değişmedi. Böylece duplicate receipt ikinci bir silme yapmadı.
5. Tail oturumu CANCEL ve duplicate CANCEL ile kapatıldı; veri silinmedi.
   Yeni BEGIN'in ilk kabul edilmiş HTTP yanıtı atıldı ve aynı request retry geçti.
   READ(0) sonrasında aktarım kasıtlı bırakıldı; PENDING ile `resume_pending=True`
   akışı yeni BEGIN yapmadan aynı snapshot'ın altı kaydını indirip doğruladı.
6. Bu son dosyaya ait altı kayıt CONFIRM ile silindi; duplicate CONFIRM geçti.
   Daha eski prefix receipt'i STALE olarak reddedildi. Yeni BEGIN EMPTY ve
   PENDING NOT_FOUND döndürdü. Test sonunda RAM arşivi boş, pending oturum yoktu.

Toplam 79 gerçek TC linkten geçti: 6 BEGIN, 45 READ, 8 PENDING, 6 CONFIRM,
2 CANCEL ve 12 legacy GET. DryRun'lar buna dahil değildir. Son link durumu
udp-in/out OK, sayaçlar 1336/79; son HK uptime 725260 ms
(`2026-10-16T04:51:51.829Z`). YAMCS açık bırakıldı: <http://localhost:8090>.
Başlatıcı PID 10112 yalnız bu oturumun kaydıdır; sonraki kullanımda yeniden doğrulanmalıdır.

## Saklanan veriler

Hepsi Git-ignored `local/archive/` altında. Ortak dosya adı tabanı
`esp32-transfer-20261016-074204`:

| Sonek | İçerik |
| --- | --- |
| `.json` | Transfer 1, ilk 24 kaydın verified export'u |
| `.tail-before.json` | CONFIRM öncesindeki altı append-tail kaydının ayrı proof'u |
| `.tail.json` | Transfer 2, aynı altı kaydın iptal öncesi export'u |
| `.resumed.json` | Transfer 3, aynı altı kaydın resume ile doğrulanmış export'u |
| `.target-report.json` | PASS, manifest'ler, kayıp modeli ve artifact yolları |

Prefix boot_id `17680455678307417585`, transfer_id `1`, request_id `3100688573`,
digest `3392740186`, count `24`. Son tail transfer_id `3`, request_id `3932188893`,
digest `3891186478`, count `6`; aynı boot.

24 kayıtlık dosya SHA256:
`DD6E2D922CB9DF8DB1301D36FCE7A6FC10F5417DBC46CC9B35575C3EDE7301AC`.
Altı kayıtlık resumed dosya SHA256:
`6C63BC4A073F7C79F18FF45522EFB969F3DF19F35D32E112ADFA00F409BEA022`.
Son rapor SHA256:
`65491A12158B8F15D21DF316A702E920E2FC140F8E0872A5BFDE1B3D96261544`.
15 Ekim'in 17 ve 27 kayıtlık disk kopyaları da eski hash'leriyle korunuyor.
**30 RAM kaydı kontrollü CONFIRM ile silindi; bilgisayardaki kopyalar silinmedi.**
fsync, OS'ye kalıcılık isteğidir; fiziksel güç-kesintisi testi yapılmadı.

## Tekrarlama ve güvenlik sınırı

Test için aynı boot'ta hâlâ PENDING olan, önce kaydedilmiş bir prefix ve daha sonra
eklenmiş tail gerekir. İndirme/fixture hazırlama ayrı adımdır; bu araç reset,
mode değişikliği veya yapay onboard write/fault injection ile fixture oluşturmaz.
Mevcut dosyalar tekrar kullanılarak bu başarılı test doğrudan tekrarlanamaz:
oturum kapanmıştır, proof/export dosyalarının üzerine yazılması da reddedilir.

```powershell
# Yalnız mevcut MDB'nin dryRun kontrolü; dosya/board mutation yok:
& C:\.platformio\penv\Scripts\python.exe -B tools/archive_target_check.py --export local/archive/NEW_PREFIX.json --output-dir local/archive

# Yalnız yeni ve eşleşen fixture ile; kaydedilmiş prefix/tail RAM'den silinir:
& C:\.platformio\penv\Scripts\python.exe -B -u tools/archive_target_check.py --export local/archive/NEW_PREFIX.json --output-dir local/archive --total-records 30 --send --confirm-clear
```

`--send` ve `--confirm-clear` birlikte zorunludur. Default dry-run'dır. Her gerçek
komutta link/hedef/binary/history/yanıt kontrolü mevcut istemciden kullanılır.
Legacy proof okuması yalnız indeks 0..29 ve APID 96 GET 0x30'a izin verir; eski
aynı-time reply'lar baseline ile ayrılır. Proof path'leri önceden kontrol edilir,
dosya üzerine yazılmaz. Hata halinde otomatik CANCEL/başka CONFIRM/WIPE/reset yoktur.
Başarısız koşuyu körlemesine yeniden başlatmayın; dosyalar/sonuç/pending incelenmelidir.
CONFIRM sonrası belirsizlikte yalnız aynı doğrulanmış dosyanın onayı tekrar edilir.

Kayıp modeli, gerçek sunucunun kabul ettiği bir HTTP IssueCommand yanıtının
istemcide bir kez atılmasıdır. Gerçek UDP/TM paket kaybı, Wi-Fi kesintisi veya
sunucu çökmesi oluşturulmadı. Resume, aynı boot'ta READ sonrası kasıtlı durdurma
modelidir; reboot sonrası RAM restore özelliği değildir. CRC/boot identity,
kriptografik kimlik doğrulama veya tüm failure interleaving'lerinin kanıtı değildir.

## Regresyon ve açık işler

Yeni test aracının 19 host testi dahil **15 bağımsız dosyada 247 Python testi**
geçti. Default/no-send/no-clear/overwrite/bad-pending/CRC/binary/stale-reply/sync
hatası ve export hatasında başka veri silmeme kontrolleri modele dayalıdır;
onboard uygulama/RTOS/UDP bu host modelinde gerçek değildir.
Normal ELF/metadata ayrımı tekrar geçti. Firmware/CMake/SDK/MDB/Java/YAML
değişmedi; C/header/native/firmware build ve önceki Wi-Fi/watchdog soak'ları
bu Python/physical archive adımında tekrar çalıştırılmadı.

Seri/UDP raw bypass, NVS/flash erase, link/queue enable, router/firewall ayarı
ve mode komutu kullanılmadı. Yeni araç common host runner'da gerçek donanıma
bağlanmaz; yalnız `tests/host/archive_target/test_target.py` fake API modeli keşfedilir.
Loglar `.pio/live-check/archive-transfer-start-20261016.log` ve
`.pio/live-check/archive-target-20261016.log`.
Arşiv işlem kapsamı bu koşullarla kapandı. Sonraki ayrı adımda
[tüm HK alan/birim ve bounded event işleme/tekrar kontrolü](hk_event_target.md)
de tamamlandı. Ardından [kaynak baseline'ı](resource_target.md) 600 s SAFE
koşusuyla ölçüldü. Genel/çeşitli-yük uzun süreli sistem doğrulaması hâlâ açıktır.
