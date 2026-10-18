# FDIR stack taşması düzeltmesi ve kart kontrolü

2026-10-15. Önceki normal imajda arşiv doluluk uyarıları işlenirken FDIR
görevinde stack overflow ve PANIC/reset_reason=4 yakalandı. Önceki log ve kaybolan
RAM oturumunun ayrıntıları [üçüncü kontrol noktasındadır](target_yamcs_smoke.md).

## Değişiklik

- `FDIR_TASK_STACK_BYTES=4096U` tek bütçesi `main/fdir_task.h` içinde tanımlandı;
  `app_main` FDIR görevini önceki 2048 yerine bu bütçeyle oluşturur.
- FDIR görevi her 30 işleme döngüsünde (nominal periyot 1 s) lifetime stack
  high-water mark'ını bayt olarak raporlar. `sizeof(StackType_t)` ile dönüşüm
  açık yapılır; pinned ESP32 port'unda StackType_t uint8_t'dir.
- Uyarı eşiği, DEGRADED kararı, mutex sırası, heartbeat/watchdog besleme politikası
  değiştirilmedi. Yeni fault-injection profili veya sahte doluluk eklenmedi.
- Ek 2048 bayt task stack'i çalışma zamanı heap'inden ayrılır. Statik RAM
  raporunun aynı kalması bu ek dinamik bütçenin ücretsiz olduğu anlamına gelmez.

## Masaüstü ve build kanıtı

Seçili `fdir_check` (74 senaryo) ve `startup_core_check` (30 senaryo) geçti.
FDIR testine 65 döngüde iki stack raporu, kilit dışında ölçüm, korunmuş
işleme/heartbeat/periyot ve 4 baytlık mock StackType_t ile 400 → 1600 bayt
dönüşümü eklendi. Başlangıç testi gerçek app_main'in 4096 bayt ayırmasını denetler.
Yeni evidence parser'ın 12 testi dahil 14 bağımsız dosyadaki 228 Python testi
geçti. Tam C/header/native suite tekrarlanmadı.

Normal `esp32dev` build: 14.29 s, statik RAM 42668/327680, flash
806277/1740800 bayt. `.bin` 806688 bayt; SHA256:
`A3DC0D97B6AAD65B4D17F7C6CC6CF1EDE6FAE82679F5DA55564054B67E4D1440`.
Normal metadata/ELF/test ayrımı geçti. SDK config SHA256 değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.

## Fiziksel test

Kullanıcı onayıyla normal imaj COM7'ye 115200 baud ile yüklendi; yazılan hash'ler
doğrulandı. Uygulama yazımı 44.7 s. NVS/tam flash erase yok; upload ve test açılış
reseti önceki RAM arşivini kaybettirdi. Eski 17 kayıtlık JSON kopyası korunur.

```powershell
& C:\.platformio\penv\Scripts\python.exe -B -u tools/fdir_stack_check.py --port COM7 --duration 300 --reset
```

`--reset` açıkça zorunludur; bu komut RAM arşivini kaybettirir. Test 300..600 s
arasında sınırlandırılır. Normal watchdog evidence parser'ını kullanır: tek boot,
doğru SDK konfigürasyonu, startup/core ilerlemesi, en az üç NOMINAL döngüsü ve
beklenmedik kritik/fatal fault/panic/reset olmaması gerekir. Ayrıca gerçek
SRC_ARCHIVE/0xAF01 doluluk uyarıları, uyarı eşiğinin işlenmesi, bunun ardından
en az 60 s çalışma ve taze stack raporu gerekir. Minimum 512 bayt rezerv test
kriteridir; tüm çağrı yolları için evrensel yeterlilik iddiası değildir.

Log: `.pio/live-check/fdir-stack-check-20261015.log`. Gerçek kart kontrolü **PASS**:
305.8 s gözlem, tek boot, üç arşiv doluluk uyarısı, başarılı DEGRADED batch kararı,
panic/reset/kritik-fatal fault yok. Stack raporları 3140 → 1684 → **1636 bayt**;
uyarı işlendikten sonraki taze raporlarda minimum 1636 kaldı. Önceki 2048 bayt
bütçe yerine 4096 bayt ile bu temsilî yolun çalıştığı doğrulandı.

```text
[170] FDIR: Fault from Source 3 (Severity: 1) Code: 0xAF01
[175] FDIR: Fault from Source 3 (Severity: 1) Code: 0xAF01
[180] FDIR: Fault from Source 3 (Severity: 1) Code: 0xAF01
FDIR: Warning threshold handled for source 3; DEGRADED decision committed.
FDIR: stack_free_min_bytes=1636.
PASS: single boot; 3 archive-full warnings; DEGRADED batch handled; FDIR minimum free stack 1636 bytes; no panic/reset/critical fault (305.8s observation).
```

Sadece bu yolun doğrulanması uzun süreli soak, diğer task stack'leri veya tüm
kritik/fatal FDIR senaryolarını kapatmaz. Doluluk nedeniyle yeni kayıtların
düşürülmesi ve DEGRADED politikası değiştirilmedi; bu test doluluk uyarısını gizlemez.

## Aynı boot içinde retained export ve son durum

Gözlem devam ederken reset/silme yapmadan yeni imajdan 27 kayıt indirildi:
`local/archive/esp32-fdir-fixed-retained-20261015-232537.json`.
Her READ için Sent history + eşleşen kart yanıtı, CRC16/tam CRC32 ve
exclusive write/fsync/readback geçti. Log: `.pio/live-check/fdir-fixed-retained-20261015.log`.
Dosya SHA256: `F3D28AA1BE54FFBF725A44D19A627D800E29CDCF87BD0DE257CAECA09200B8EF`.

Manifest: boot_id `14627712901142658383`, transfer_id `1`, request_id
`3607974553`, digest `1130485649`, record_count `27`.
Testten sonra dosya tekrar offline doğrulandı; PENDING **aynı** manifest'i döndürdü
ve READ(26) CRC-valid kayıt verdi. Böylece bu oturumun doluluk/FDIR eşiğini geçip
reboot olmadan korunduğu da doğrulandı. Son HK uptime 330330 ms
(`2026-10-15T20:28:37.381Z`); YAMCS canlı kalıyor.

Kartta bu normal imaj ve pending oturum bırakıldı. CONFIRM/CANCEL/WIPE gönderilmedi;
27 kayıt kartta ve diskte duruyor. Eski 17 kayıtlık disk dosyası da aynı SHA256 ile
korundu, ancak eski boot manifest'i artık geçerli değil. Sonraki adım yeni export
için açık izinle kontrollü CONFIRM/tail/receipt/retry testidir. Sonraki operatör
önce pending/boot/dosya eşleşmesini yeniden kontrol etmeli; yeniden flash/reset
bu yeni RAM oturumunu da kaybettirir.

16 Ekim güncellemesi: kart yeni boot ile geldi; firmware yeniden yüklenmedi.
Yeni 24 kayıtlık prefix + 6 tail ile kontrollü arşiv işlem testleri geçti.
30 kayıt kopyaları doğrulandıktan sonra CONFIRM ile silindi; test sonunda RAM
EMPTY/pending yok. Yukarıdaki 27 kayıtlık dosya tarihsel kopyadır ve korunur.
[Güncel oturum ve sonuç](archive_target_tests.md).
