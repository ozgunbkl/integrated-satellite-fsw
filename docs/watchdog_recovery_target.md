# Kontrollü timeout / resume / tekrar timeout kart paketi

2026-10-16. Yeni owner recovery'nin kart doğrulaması için ayrı profil ve reader.
Kontrollü iki timeout/toparlanma döngüsü ve normal restore geçti. Önceki üç
başarısız koşu aşağıda ayrı tutulur; yalnız son paket exit 0'dır.

## Yetki ve veri koruması

Kullanıcı güncel RAM arşivi korunarak upload/reset ve kontrollü kart testini
onayladı. Uzun soak bu paket değildir. Önceki normal boot'un 30 kaydı
`local/archive/pre-watchdog-recovery-20261016-1518.json` olarak CRC16/digest,
exclusive save/fsync/readback ve yeniden offline kontrolle korundu. Canlı
PENDING aynı manifest, READ(29) dosyadaki son kayıtla birebir eşleşti.

- BootId `18030524638729855582`, TransferId `1`, RequestId `3312577605`.
- Digest `1201240499`, count `30` (kapasite; snapshot dışında tail yok).
- Dosya SHA256 `9379EF51771E8E003CC4B9AD3D04485631081ABAC91848F4E4849AB809680917`.

CONFIRM/CANCEL/WIPE gönderilmedi. Upload/reset eski RAM/pending/per-boot hata
durumunu kaybettirir; bilgisayardaki kopya korunur, karta otomatik geri yüklenmez.

İlk reader denemesi sonrasında yeni normal boot'un güncel 30 kaydı da
`local/archive/pre-watchdog-recovery-retry-20261016.json` dosyasında korundu.
PENDING manifest ve READ(29) tekrar birebir doğrulandı; tail yoktu.
BootId `13038726505248059175`, TransferId `1`, RequestId `1375054142`,
digest `3716630078`, SHA256
`79AC4D1F561290E7AA64B23711C57EF33CBA6E212B091CAE9BDF94E36409E749`.
İki export da korunur; yeni test/restore sırasında oluşan simülasyon RAM'i
sonraki upload/reset ile kaybolur. Hiçbir export otomatik CONFIRM edilmedi.

İkinci reader denemesinden sonra üçüncü koşu öncesi güncel 30 kayıt
`local/archive/pre-watchdog-recovery-retry2-20261016.json` ile ayrıca korundu;
disk/PENDING/READ(29) eşleşti. BootId `11547961125680986904`, TransferId `1`,
RequestId `3121467997`, digest `261110087`, SHA256
`E4B319635AC64CCEE248781A00E9C35F55DE73C474F0034BB00936C3035B396F`.

Üçüncü koşunun seri aktarım hatasından ve başarılı normal restore'dan sonra,
düşük hızlı tek yeniden deneme öncesinde 30 kayıt ayrıca
`local/archive/pre-watchdog-recovery-slow-20261016.json` dosyasına kaydedildi.
Disk/PENDING/READ(29) eşleşti: BootId `288475432483083115`, TransferId `1`,
RequestId `1259252606`, digest `2876135594`; SHA256
`8996CD70842166BC40C3BD501113B4BBC9EE9478A9CE3906E536BA8085A548D5`.

## Profil ve çalıştırma

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_watchdog_system.ps1 -TaskRecovery -AllowRamReset -Port COM7
# Onaylı düşük hızlı tekrar: aynı komuta -UploadBaud 57600 eklenir.
```

Normal ve `esp32dev_wdt_task_recovery` imajları flash'tan önce derlenir. Paket:
normal upload + 90 s gözlem, kontrollü recovery imajı + en çok 260 s reader,
`finally` içinde normal restore + 90 s gözlem. Varsayılan 115200 baud
(`-UploadBaud 57600` ile daha düşük hız) ve
doğrulanan dört bölge kullanılır; NVS erase/write, SDK ayarı, ağ/router/firewall
ve ground kaynak/MDB değişikliği yoktur. Restore upload hatasında normal PIO
uploader fallback'i vardır. Baseline atlama/başka fault paketi/core seçimi reddedilir.

Yeni test hook'u gerçek TM_GEN görevini durdurmaz; yalnız onun heartbeat'ini
kontrollü bastırıp yeniden geçirir. Servis onarımı veya task restart kanıtı değildir.

1. İlk C200 gerçek watchdog/FDIR yolu ile oluşur; hook heartbeat'i yeniden açar.
2. HTTP istemcisi YAMCS'ten yalnız sabit `FDIR_SetMode(CmdParam=2)` gönderir;
   aktif neden sürerken kartın NACK'i gerekir.
3. Gerçek owner recovery C200'ü çözer; snapshot SAFE/boş ledger göstermelidir.
   Hook clear veya mode API'si çağırmaz, ayrı yer komutunu bekler.
4. İlk ayrı yer komutu kabul edilince heartbeat tekrar bastırılır. İkinci
   C200'ün revision'ı ilkinden büyük olmalı; sonra gerçek owner tekrar çözmelidir.
5. İkinci ayrı yer komutu kabul edilir; en az 14 s/dört snapshot ve üç yeni
   NOMINAL döngüsü gözlenir. Sonra normal firmware restore edilir.

Reader seri veriyi okurken ayrı bounded worker yalnız üç NOMINAL isteği gönderir:
bir blocked probe ve clear'lardan sonra iki istek. Her istekte link hedefi,
dry-run komut kimliği ve 8 bayt `1030c00000005202` profili doğrulanır; command
history Sent/binary ayrıca doğrulanır. Actual POST retry edilmez; kayıp yanıt
başarısızlıktır. Seri hatta gerçek CRC OK → FDIR route → ACK/NACK de aranır.
Worker iptal edilip join edilmeden reader çıkmaz/normal restore başlamaz.
Archive/clear/ACK-warning/link-control veya raw UDP komutu yoktur.

## Kanıt ve sınırlar

Hazırlık: 43 C grubu / 47 header, 41 C fixture, 52 production kaynak metadata'sı,
372 Python / 20 dosya, 22 graf/layout, 15 runner refusal ve 59 negatif profil
guard geçti. Recovery hook 17 C, reader/sabit NOMINAL sender 26 Python senaryosu.
Native command dry-run gerçek YAMCS'te ayrıca doğrulandı; bu preview TC göndermedi.
Normal ELF test define/kanca içermiyor; SDK config SHA256 aynı.

İki gerçek C200 raporu/timeout'u, farklı revision, iki owner-clear mesajı,
SAFE snapshot'ları, bir NACK/iki ACK, üç doğrulanmış YAMCS Sent/binary ve üç
CRC/FDIR route gerekir. Counter rollover/diğer fault/fatal/uncertainty ve yarış
ayrıntıları [host politikasındadır](watchdog_recovery.md); bunların hepsi kartta
enjekte edilmiş sayılmaz. Genel deadlock/physical repair veya uzun süreli soak değildir.

## Fiziksel sonuç

İlk paket `.pio/host-tests/wdt-recovery-20261016-154859-8671/`:
normal baseline 95.8 s geçti. Recovery reader ilk C200'de "No operating mission
before fault" diyerek durdu; bu koşu PASS sayılmaz. Başlangıçtan itibaren kesilen
heartbeat, ilk NOMINAL logundan önce timeout olabilir: WDT_MON priority 6,
TM_GEN priority 3; ilk operating log 5 s cadence'ine bağlıdır. Ayrı normal
baseline zaten operating mission'u doğrular. Bu, okuyucunun fazla dar oracle'ıydı.

Firmware değiştirilmeden reader'a complete startup zorunluluğu kondu; test
imajında ilk timeout öncesi NOMINAL log şartı kaldırıldı. İki episode, >=29 s
host-observed resume/clear aralığı (firmware eşiği 30 s), farklı revision,
SAFE/ground/CRC/Sent ve recovery sonrası üç NOMINAL döngüsü koşulları korunur.
İki regresyon eklendi: pre-NOMINAL timeout kabulü ve startup öncesi fault reddi;
24 recovery Python testi / toplam 370 Python tekrar geçti.

İlk paket exit 1'dir; finally normal restore 95.8 s / RESTORE PASS geçti.
İlk başarısızlık sonradan PASS'a çevrilmez. Yeniden denemeden önce ilk koşunun
normal imajda ürettiği güncel kayıtlar da ayrı export ile korunur.

İkinci paket `.pio/host-tests/wdt-recovery-20261016-155756-0354/`: normal baseline
95.8 s geçti. Gerçek C200/revision 1 ve SAFE/resume görüldü; reader, üretimdeki
CRC OK satırının `Handing to Router...` son ekini eşleştirmediği için FDIR route'u
CRC'siz sandı. Paket exit 1; normal restore 95.8 s / RESTORE PASS geçti. Bu da
PASS sayılmaz. Önizleme ve gerçek postprocessed NOMINAL binary/Sent history'si
sonradan salt okunur ayrıca doğrulandı; firmware değişmedi.

CRC eşleşmesi üretimdeki tam metne bağlandı. Source-contract testi CRC, route,
clear, ACK/NACK, arm ve mission log formatlarını doğrudan C kaynaklarıyla
eşleştirir. Ayrıca MODE commit'inin başka çekirdekteki ACK logundan önce
gözlenebileceği SMP sıralaması sınandı: final ACK/Sent kanıtları zorunlu kalır,
yalnız log sırası varsayılmaz. Recovery Python grubu 26, toplam Python 372'dir.

Üçüncü paket `.pio/host-tests/wdt-recovery-20261016-160757-7969/`: normal
baseline 95.8 s geçti. Test uygulaması seri aktarımının %6'sında esptool
`StopIteration` / `The chip stopped responding` hatası verdi; recovery reader
hiç başlamadı. Paket exit 1; finally normal restore 95.8 s / RESTORE PASS
geçti. Kullanıcı USB/karta dokunmadığını ve kesilme görmediğini bildirdi;
bu veri tek başına kablo/USB veya kart tarafındaki kök nedeni belirlemez.
Firmware/SDK/ağ ayarları değiştirilmeden 57600 baud ile tek tekrar seçildi.
Runner artık yalnız 57600/115200 kabul eder; geçersiz hız reddiyle runner
güvenlik senaryosu sayısı 15 oldu.

### Başarılı düşük hızlı paket

`.pio/host-tests/wdt-recovery-20261016-161747-3437/`, COM7, `-UploadBaud 57600`:

- Normal baseline **95.8 s PASS**; recovery gözlemi **135.8 s PASS**.
- Gerçek C200 timeout iki kez oluştu; retained revision'lar **1 ve 2**.
  Resume → owner-clear süreleri host gözleminde **40.016 s / 35.000 s**.
  Bu süreler firmware'in token sonrası >=30 s stable-window kuralıyla uyumludur;
  bütün görevler/yükler için en-kötü gecikme garantisi değildir.
- Her clear sonrası ledger boşken global mode **SAFE** kaldı. Aktif ilk neden
  sırasında bir NOMINAL isteği reddedildi; iki clear sonrasında ayrı iki istek
  kabul edildi. Son NOMINAL ilerlemesi ve en az 14 s/dört final snapshot geçti.
- Üç komutun CRC → FDIR route ve ACK/NACK kanıtları, YAMCS Sent/postprocessed
  binary kontrolleri geçti. TC sayacı **305 → 308**; başka gerçek mode komutu yok.
- Recovery sırasında beklenmeyen reset/başka critical neden yok; test-only
  ölçümde WDT_MON minimum boş stack **1588 bayt**. Normal imajda bu hook yok.
- Normal imaj geri yüklendi, **95.8 s PASS / RESTORE PASS**, paket **exit 0**.
  Bu yükleme/resetler RAM'i sıfırladı; dört doğrulanmış export bilgisayarda korunur.

Son restore sırasında salt okunur YAMCS kontrolü de geçti:
`normal_restore_hk_events.json`, üç güncel HK örneği (uptime 45.280/50.280/55.280 s),
yedi alan/birim ve 20 s pencerede 28 INFO olayının wire/XTCE/preprocessor eşleşmesi.
TC sayacı bu GET-only kontrolde **308 → 308** kaldı.

Kanıt dosyaları: `wdt_normal_check.log`, `wdt_recovery_check.log`,
`wdt_restore_check.log`; her upload/build için ayrıca ayrı log bulunur.
Loglar `.pio/` altında yerel kanıttır, üretim/test kaynakları orada tutulmaz.

| İmaj | Statik RAM | Flash kullanımı | firmware.bin SHA256 |
| --- | ---: | ---: | --- |
| Normal | 42884 | 809213 | `D18A4D25236FBE9A2405A2BC00031FF487229D893126F2FF64A029D5DA628EE9` |
| Task recovery test | 43220 | 810625 | `BDFCB8C2AC3582E8DF72349F3E9805FCED2813E75084CB7602E046B967321699` |

SDK config SHA256 değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Son metadata/ELF kontrolü 43 C grubu/41 fixture ve normal imajda test kancası
olmadığını, component kontrolü 52 üretim C kaynağının tek derlendiğini doğruladı.
Dört export'un offline CRC16/digest kontrolü ve SHA256 değerleri tekrar eşleşti.

Bu sonuç yalnız TM_GEN heartbeat drop/resume/re-drop ve ayrı ground-mode yolunu
kapatır. Gerçek task restart/servis onarımı, tüm task'larda fiziksel fault matrisi,
fatal/uncertainty veya kalıcı reboot-loop recovery ve uzun soak doğrulanmadı.
Uzun süreli test bu oturumda başlatılmadı. Düşük hızlı başarılı aktarım, önceki
seri aktarım kesilmesinin kök nedenini kesinleştirmez.
