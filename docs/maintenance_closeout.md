# Seçilen arşiv bakımı ve normal imaj kontrolü

2026-10-17. Kullanıcı daha fazla hata senaryosu istemedi; mevcut güvenli bakım
yolu tamamlandı. Yeni profil, karantina/salvage veya hata enjeksiyonu eklenmedi.
Uzun test ve GitHub yayını bu adımda yapılmadı.

## Kod ve masaüstü doğrulaması

Mevcut açık `WIPE_ALL`/`Archive_Reset()` yolu önce arşiv kilidini alır; eski CRC
revision'ını sessiz, sıfır-bekleme MODE sorgusuyla yakalar. Temizlikten sonra
4096 RAM baytı, write index ve aktif aktarım/receipt/bitmap'in boş olduğu aynı
kilit altında doğrulanır. Yalnız aynı nonfatal CRC revision'ı Try resolver ile
çözülebilir. Busy/fatal/yeni revision korunur; mod, diğer aktif hatalar ve kalıcı
geçmiş değiştirilmez. Aktarım/boot kimlikleri yeniden kullanılmaz. Sonuç
`COMPLETE`/`DEFERRED` olarak kilit bırakıldıktan sonra bildirilir.

Bu, bozuk verinin onarımı değildir. Otomatik silme yoktur; açık WIPE yıkıcıdır.
Bu çalışmada **karta WIPE gönderilmedi**, fiziksel CRC bozulması denenmedi.
Mevcut `real_archive_crc` host vakası yanlış WIPE boyu/anahtarını, yalnız CRC
çözülürken başka CDH critical/SAFE'in korunmasını ve fatal CRC reddini doğrular.
İzole arşiv fixture'ları sessiz Try API bağımlılığına uyarlandı.

Tam regresyon logu: `.pio/host-tests/crc-maintenance-host-20261017-160705.log`.
51 C grubu, 52 header, 522 Python/26 dosya, 22 graf/layout, o sıradaki 48 refusal,
97 negatif profil ve 39 workflow/artifact geçti; normal metadata/ELF'de test
hook'u yok. Ardından ROM yolunu reddeden değişiklikle etkilenen 15 erken-ret ve
39 workflow/artifact tekrar geçti (34 eski + 15 yeni = 49 refusal).
Normal build: RAM **42964**, flash **813901** bayt, 57 C/16 component/63 header edge.

## Onaylı normal yükleme ve olağan çalışma: PASS

Kanıt dizini: `.pio/host-tests/normal-maintenance-20261017-1914/`.

- Reset öncesi salt-okunur baseline: boot12/held0/ready1/storage1/stable1/history98.
- 30 kayıt iki tam canlı retained export ve iki ek yerel kopyada korundu;
  byte eşitliği ve manifest doğrulandı. Export SHA256:
  `34445FBF483602B488A335173A272B2C3EB9B527356E2AA4684E2A835FDF1F43`.
  Ek kopyalar `local/archive/pre-normal-maintenance-20261017-1914*.json`.
- Dört normal dosya doğrulanmış offset/boyut/hash ile snapshot alındı.
  NVS aralığı yazılmadı; stub/460800 upload ilk denemede geçti ve her dosyanın
  cihaz hash doğrulaması başarılı oldu. Loader'da tutuldu, arada app açılmadı.
- Upload öncesi/sonrası NVS iki kez okundu, hash aynı kaldı:
  `19F01DDFC18038F31F26EB260350F93A8F1EB47E345435EA41208EEA56DDF2DB`.
  Ham NVS kaydedilmedi; erase/clear yapılmadı.
- Tek gözlenen normal boot: **12→13**, failed_boots0, held1, journal flags99;
  SDK reset reason1. Yaklaşık **47.968 s** içinde yeni SAFE sağlık penceresi
  tamamlandı; status history98 korundu.
- Yalnız **iki** gerçek recovery/mod TC'si: güncel boot13 yer onayı COMPLETE,
  held0 sonrası SAFE'in korunması, ardından ayrı NOMINAL ACK. Her biri Sent
  history ve kartın CRC/router/sonuç zinciriyle bağlandı; retry yapılmadı.
  **91.015 s / PASS**, üç NOMINAL örneği, recovery stack min **3664** bayt.
- YAMCS salt-okunur kontrol: üç taze HK, yedi doğru birim, 29 INFO olay,
  duplicate wire0 ve TC **414→414**. Bu kontrolde ek komut gönderilmedi.
- Son pasif seri kayıt: boot13/held0/ready1/storage1/stable1/history98,
  stack3664. İlave reset/TC yapılmadı.

Yüklenip gözlenen normal `firmware.bin` SHA256:
`C0719A5BE5BA81B39FEFE356A511D8EC919C4F81134330F320D066E6B013BAAC`.
Bu disk/yükleme snapshot kimliğidir, karttan okunmuş runtime build kimliği değil.
SDK SHA256 değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.

## Kalan risk ve sabit kapanış sırası

Eski aralıklı upload kesintisi ve geçmiş kaybı **kök neden olarak çözülmedi**.
ROM durum yanıtı ile Windows USB kopması ayrıldı; koruma gerektiren runner'da
ROM/no-stub yolu kapatıldı. Başarılı yeni yükleme, eski kaybın nedenini kanıtlamaz.
[Mevcut loglara dayalı inceleme](recovery_target.md).

Yeni senaryo açılmayacak. Sıra: kullanıcıyla planlanan 1 saatlik uzun çalışma,
sonuç değerlendirmesi/somut sorun varsa düzeltme, nihai README/temiz kurulum ve
güvenlik kontrolleri; kullanıcı onayıyla GitHub en son. Eski aktarım riski son
yayın değerlendirmesinde açıkça korunmalıdır; bu adım tüm projeyi bitirmiş sayılmaz.
