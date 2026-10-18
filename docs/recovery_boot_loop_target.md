# Seçilen üç-PANIC / BOOT_LOOP kart testi

2026-10-17. Kullanıcı, güncel RAM arşivinin korunması, üç kontrollü PANIC/reset,
silinmeyecek BOOT_LOOP geçmişi ve sonunda normal imaja dönüş için ayrı onay verdi.
Gerçek NVS kaydını bozma/silme, uzun test ve GitHub bu onaya dahil değildir.

## Hazırlık ve sınırlar

`esp32dev_recovery_boot_loop`, mevcut HK caller false-return profilini yalnız
`FSW_TEST_RECOVERY_BOOT_LOOP=1` ile genişletir. Normal profil bu kodu içermez.
Test, core init öncesi üretim recovery snapshot'ını okur: sağlam storage, açık
boot işareti, doğru SDK nedeni ve failed_boots=0/1/2 için HK false döner; gerçek
caller abort eder. SDK PANIC sonrası sayaç 3 olduğunda enjeksiyon bastırılır ve
gerçek HK/RTOS başlangıcı devam eder. Geçersiz/okunamayan durumda yeni abort
enjeksiyonu yapılmaz; gerçek HK çağrısı korunur. RTC işareti veya sayaç temizliği yoktur.

Beklenen zincir, baseline B ve geçmiş H için:

| Gözlenen boot | failed_boots | Journal flags | Beklenen |
| --- | --- | --- | --- |
| B+1 | 0 | H OR 1 | HK false / gerçek abort |
| B+2 | 1 | H OR 33 | SDK PANIC=4 / HK false / gerçek abort |
| B+3 | 2 | H OR 33 | SDK PANIC=4 / HK false / gerçek abort |
| B+4 | 3 | H OR 97 | SDK PANIC=4 / enjeksiyon bastırılır / SAFE |
| B+5, normal restore | 0 | H OR 97 | Geçmiş korunur, yeni boot onayı gerekir |

İlk üç boot sağlık penceresine ulaşmaz. Dördüncü boot'ta gerçek sağlık penceresi
başarısız açılış sayacını sıfırlar, **BOOT_LOOP/BAD_RESET/FAULT geçmişini silmez**.
Her iki çalışan fazda kilitli NOMINAL reddi, eski-token reddi, ayrı güncel boot
onayı queued/Sent/COMPLETE, SAFE ve ayrı NOMINAL Sent/ACK aranır; toplam en fazla
sekiz recovery/mode isteği, her fazda en az üç yeni mission örneği gerekir.
Yer onayı BOOT_LOOP bayrağını temizlemez; sonraki boot yine yeni review ister.

Runner aynı iki tam canlı 30-kayıt retained export, baseline tekrar kontrolü,
hash-bound dört imaj, her upload öncesi/sonrası çift NVS hash okuması ve finally
normal restore kapılarını kullanır. Ham NVS/credential saklanmaz. NVS retention
kanıtı yoksa normal imaj denense bile uygulama loader'da bırakılır. Seçilen test
başarısızken fallback PASS bütün paketi başarılı yapmaz; kısmi BOOT_LOOP geçmişi
beklenen tam geçmişle eşleşmezse fallback TC göndermeden reddeder.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_persistent_recovery.ps1 -Port COM7 -BoardIp 192.168.1.5 -BoardMac 34:5f:45:aa:e5:94 -AllowRamReset -AllowPersistentWrites -AllowRecoveryCommands -AllowTestRamLoss -AllowHistoricalFault -BootLoopTest -AllowBootLoopHistory
```

`panic_proof.json` ortak dosya adıdır; bu seçimde içindeki phase `boot-loop`,
boots=4 ve selected_abort_count=3 olmalıdır. Normal `restore_proof.json` yeni
BOOT_LOOP geçmişine ayrı açık onayla bağlanır. Passive inspection için de yeni
geçmiş açıkça belirtilir: `--inspect-current --allow-boot-loop-history`.
Eski tek-PANIC runner yeni geçmişi sessizce kabul etmez.

## Güncel durum

Yeni enjeksiyonun **16 C mock senaryosu**, dört-boot okuyucunun **16 Python testi**,
eski reader'ın 55 testi, **14 erken-ret / 39 workflow-artifact** ve 21 geçersiz
startup profil kontrolü geçti. Fake USB/HTTP/saat ile CLI, üç abort ve sticky
normal restore da sınandı. Bunlar gerçek RTOS/NVS kanıtı değildir. Tam masaüstü
**51 C / 52 header / 522 Python (26 dosya)**, 22 graf/layout, 48 erken-ret ve
39 workflow/artifact ile geçti. Log: `.pio/host-tests/recovery-boot-loop-host-20261017.log`.
Bu koşu `-BuildMetadata` olmadan yapıldı; normal metadata/ELF/dependency ve
59 watchdog + 21 startup + 17 lifecycle negatif profil kontrolleri ayrıca geçti.
İki imaj derlendi: normal RAM/flash **42964/813525**, BOOT_LOOP **42964/814293**;
imaj ayrımı/aynı partition-OTA kontrolü geçti. SDK config hash'i değişmedi.
## Fiziksel sonuç: tam seçilen paket PASS

Kanıt: `.pio/host-tests/persistent-recovery-20261017-143027-8741/`.
Runner **exit 0 / RESTORE PASS** ile bitti. İki upload da **460800/stub ile ilk
denemede** geçti; önceki aralıklı aktarım/geçmiş kaybı kök nedeni kapanmış sayılmaz.

- Başlangıç ve koruma sonrası passive baseline aynı **boot7/history34/held0/
  stable1/storage1** durumuna bağlandı. ready=0 idi; baseline kimlik kontrolüdür,
  eski boot'un sağlıklı veya onarılmış olduğu iddiası değildir. Yeni boot'lardaki
  sağlık penceresi ayrıca gerçek görev gözlemleriyle doğrulandı.
- Güncel **30 kayıt**, ilk resetten önce iki tam canlı retained export ve read-back
  ile korundu: `original.json`, `original-retained.json`, `preservation.json`.
  SHA256: `57A647C11C718234D1CF1C58BA02957DB5F4772121842D737FBAFF86540EEDB1`.
  Aynı hash'li ek iki dosya `local/archive/pre-recovery-boot-loop-20261017-143027.json`
  ve `pre-recovery-boot-loop-20261017-143027-retained.json` içinde. Üzerine yazma
  yok; dört kopya aynı bilgisayardadır, bağımsız yedek medya değildir. Kartın ilk
  RAM'i loader girişinde, geçici test RAM'i normal restore'da sıfırlandı.
- Her upload öncesi/sonrası gerçek MAC/partition ve iki NVS okuması doğrulandı.
  Test upload çifti SHA256:
  `D21A956D89C713EE23446A58DB6D2F54C701FBDCF3355D6A18499B3925C0102F`;
  normal restore çifti:
  `9AEAA598B553410CABE5383FDF1D90C60FBC2B66C6AC8F86F702D610F8CF1176`.
  Her çift kendi içinde aynıdır; arada app'in meşru journal yazıları yapıldı.
  NVS erase/bozma veya ham NVS/credential kaydı yoktur.
- `panic_proof.json`: phase=boot-loop, **79.610 s PASS**, gerçek boot'lar
  **8→9→10→11**, failed_boots **0→1→2→3**, flags **35→35→35→99**;
  SDK nedenleri **1,4,4,4** ve üç ayrı HK false/caller abort zinciri.
  Dördüncü boot'ta enjeksiyon bastırıldı; BOOT_LOOP/BAD_RESET/FAULT ile SAFE,
  gerçek sağlık, blocked/stale/approve/nominal zinciri ve beş NOMINAL örneği geçti.
- `restore_proof.json`: **79.734 s PASS**, normal **boot12/failed0/held1/flags99**.
  Önceki boot'un onayı yeterli olmadı; yeni sağlık ve ayrı dört istekli onay/SAFE/
  NOMINAL zinciri geçti. Beş mission örneği, iki fazda stack min **3668 bayt**.
  Toplam sekiz recovery/mod TC; Sent/binary ve kart yanıtı ayrı doğrulandı,
  belirsiz gönderim tekrarı veya arşiv CANCEL/CONFIRM/WIPE yoktur.
- Son passive `final_current_status.json`: **boot12/held0/ready1/stable1/storage1/
  history98**. `final_yamcs_live.json`: üç taze HK/yedi birim/**29 INFO olay**,
  duplicate wire=0; GET-only **TC349→349 PASS**. Kart normal imajda çalışıyor.

Son normal snapshot SHA256:
`6B8D117D6A29BD35C689C3C3AE2E91E4EE148DA416F4AC97BFE0907D638A381F`.
Test snapshot SHA256:
`542D6A3217AD3819EA8336971CD68987B53ABC7B3A89D290D14B751F03EF40AB`.
Bunlar yüklenen dosyaların hash'idir, karttan okunmuş firmware kimliği değildir.
Normal imaj yeniden derlendi; üretim recovery/MODE davranışı ve SDK ayarları
değişmedi. Bu seçilen BOOT_LOOP yolu kapandı; gerçek bozuk kayıt/diğer resetler,
yerinde domain repair ve watchdog ölçülmemiş sınırları açık. Uzun test, nihai
README/temiz klon ve en son GitHub bu adımda yapılmadı.

Üretim recovery/MODE politikası değişmedi. Üç HK caller abort'u seçilen gerçek
boot-loop yoludur; brownout/güç-kesintisi, bozuk NVS, bütün reset türleri, flash
ömrü, cache-off/debug veya yerinde domain repair kanıtı değildir.
[Önceki tek-PANIC tam paket](recovery_target.md), [açık kapanış listesi](release_checklist.md).
