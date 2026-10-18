# Kalıcı recovery: kısa kart paketi ve fiziksel sonuç

En son olağan yükleme: CRC bakım düzeltmeli C0719A… normal imaj, boot12→13,
history98/NVS korunması, yalnız iki TC ve **91.015 s PASS**. WIPE veya yeni
enjeksiyon yapılmadı. [Güncel kanıt](maintenance_closeout.md). Aşağıdaki seçilen
hata paketleri kendi sürümleriyle tarihsel kanıttır.

## 17 Ekim aktarım incelemesi: koruma güçlendirildi, kök neden açık

Yeni hata senaryosu çalıştırılmadı. `095321-8129/normal_restore_upload.log`
içindeki ROM/no-stub `01056430: Requested resource not found` yanıtı, yerel
esptool 4.9.0 `util.py/FatalError.WithResult` tarafından ROM durum kodu `0x0105`
olarak çözümlenir; Windows'ta COM kaybolduğunun kanıtı değildir. `loader.py`
retry/SLIP zincirindeki sonraki `StopIteration` asıl hatayı örtebilir. Ayrı
`094200-1600/panic_upload.log` stub kesintisi aynı ROM yanıtı diye birleştirilmez.
Kabloya dokunulduğu veya fiziksel USB kopması kanıtlanmadı.

Eski ROM logundaki tek başına `Erasing flash...` ifadesi tam-chip silme kanıtı
değildir: yerel `cmds.py` no-stub dosya yazımında da bunu basar. Yazdırılan dört
silme aralığı NVS `0x9000..0xefff` alanını kapsamaz; eski koşuda byte probe olmadığı
için bu da retention kanıtı değildir. Sonradan gözlenen boot1/history0 kaybı
gerçek bulgu olarak korunur, nedeni kesinleşmedi.

Normal startup bir NVS init denemesi yapar; otomatik erase/retry yoktur.
Recovery portu yalnız kendi namespace/key load/write/commit/read-back yolunu
kullanır. Bu kaynak incelemesi tarihsel kaybın nedenini kanıtlamaz.
Koruma gerektiren runner artık `UseRomLoader` seçimini build/seri/I/O öncesinde
reddeder; no-stub fallback yoktur. Varsayılan doğrulanmış stub/460800 yolu,
tam RAM koruma ve her upload öncesi/sonrası NVS eşitliği kapıları korunur.
15 erken-ret ve 39 workflow/artifact mock geçti; bu bir kök neden düzeltmesi
veya yeni fiziksel stres testi değildir.

## Güncel ek kapanış: üç-PANIC BOOT_LOOP + normal restore PASS

Ayrı kullanıcı onayıyla seçilen üç ardışık başarısız açılış yolu da gerçek kartta
geçti: boot7→8→9→10→11→normal12, üç SDK PANIC=4, BOOT_LOOP eşiği ve yapışkan
geçmiş; **79.610 s + 79.734 s / exit 0 / RESTORE PASS**. Güncel 30 kayıt iki canlı
export ve iki ek yerel kopyada korunuyor; iki upload boyunca NVS byte hash'i aynı
kaldı. Son normal **6B8D11…**, boot12/held0/ready1/history98, YAMCS TC349→349 PASS.
[Yeni faz kanıtı, hash'ler ve kesin sınırlar](recovery_boot_loop_target.md).
Aşağıdaki tek-PANIC koşuları kendi imaj/geçmişleriyle tarihsel kanıttır; önceki
aktarim/geçmiş kaybı FAIL'leri ve açık kök nedenler silinmez.

## Önceki 2026-10-17 fiziksel sonuç: seçilen tek-PANIC tam paket PASS

Kanıt dizini: `.pio/host-tests/persistent-recovery-20261017-140419-1177/`.
Runner **exit 0 / RESTORE PASS** ile tamamlandı. Bu sonuç aşağıdaki eski FAIL
koşularını silmez; aralıklı aktarım/geçmiş kaybı kök nedeninin çözüldüğü iddiası değildir.

- Bootstrap sonrası indeks 29 iki GET ile aynı 20 bayt/CRC16 `0xD06A` olarak
  doğrulandı; ancak bundan sonra yeni tam paket başlatıldı. Passive baseline ve
  arşiv sonrası tekrar kontrolü aynı boot=4/history=2/held=0 durumuna bağlandı.
- İlk resetten önce **30 kaydın iki tam canlı retained export'u** ve dosya hash'leri
  doğrulandı: `original.json`, `original-retained.json`, `preservation.json`.
  İki dosyanın SHA256'sı:
  `6F65C3D84BC02E05E98CEBA66E0A116B0674CD78E976F7FF7CE148A83C6AF175`.
  Sonrasında aynı hash'li ek kopyalar `local/archive/` altına
  `pre-persistent-recovery-20261017-140419.json` ve
  `pre-persistent-recovery-20261017-140419-retained.json` adlarıyla, mevcut dosya
  üzerine yazmadan alındı. Bunlar aynı bilgisayardadır; bağımsız yedek medya değildir.
  Başlangıç RAM'i
  loader girişinde, testin geçici RAM'i normal restore sırasında sıfırlandı.
- İki dört-bölgeli upload **460800/stub ile ilk denemede** geçti. Gerçek MAC ve
  resident partition tablosu doğrulandı. Her upload öncesi/sonrası NVS iki kez
  okundu; uygulama çalışmazken byte hash'leri aynı kaldı. İlk çift:
  `561DE8FD0D04224E466AC485C0A5C1030A10FE3A5E520BB899F2AE644C03E927`;
  normal restore çifti:
  `5E88FF931CF53AFD24204135C1F7A7FAF097C842124CF8F2A78E598B6EEE4322`.
  İki faz arasında uygulama meşru journal yazıları yaptı; bu iki farklı hash'in
  birbirine eşit olması beklenmez. Ham NVS/credential kaydedilmedi; erase yapılmadı.
- `panic_proof.json`: **78.422 s PASS**, boot **5→6**, flags **3→35**,
  failed_boots **0→1**, gerçek HK caller false/abort ve SDK PANIC reset nedeni 4.
  Önceki FAULT korundu, BAD_RESET eklendi. Sağlık penceresi, kilitli NOMINAL reddi,
  eski-token reddi, yer onayı queued/Sent/COMPLETE, held=0/SAFE ve ayrı NOMINAL
  Sent/ACK doğrulandı; beş yeni NOMINAL örneği gözlendi.
- `restore_proof.json`: **78.797 s PASS**, normal imaj boot **7**, flags=35,
  failed_boots=0/held=1 ile açıldı. Önceki boot'un onayı yeni boot'u açmadı;
  yeni sağlık ve dört istekli aynı ayrı-onay/SAFE/NOMINAL zinciri geçti.
  Beş yeni NOMINAL örneği ve iki fazda recovery stack minimum **3668 bayt**.
- Bootstrap iki, PANIC fazı dört, restore dört: onaylı **10 recovery/mode TC**
  sınırı aşılmadı; arşiv salt-okuma/retained aktarım trafiği bu ayrı sınıra dahil değildir.
  Recovery/mod komutları yeniden denenmedi; arşiv CANCEL/CONFIRM/WIPE yapılmadı.
- Son `final_current_status.json`: **boot=7, held=0, ready=1, storage_ok=1,
  stable=1, history=34**, stack 3668. `final_yamcs_live.json`: üç güncel HK,
  yedi alan/birim, **29 INFO olay**, duplicate wire=0, GET-only **TC278→278 PASS**.
  Kart normal **153DF1…** imajda; bu normal görev gözlemleri uzun çalışma değildir.

Üretim C/SDK/normal imaj bu adımda değişmedi. Son tam masaüstü **50 C / 52 header /
506 Python (25 dosya)**, 22 graf/layout, 46 erken-ret, 92 negatif profil ve
33 workflow/artifact geçti; kart paketi iki build/ELF/metadata ayrımını da tekrar
doğruladı. Diğer kalıcılık/boot-loop/bozuk kayıt yolları, yerinde domain repair,
watchdog kapsam kapanışı ve eski aktarım/kayıp kök nedeni açık kalır. Uzun test,
nihai README/temiz klon ve en son GitHub sırası değişmedi; bu adımda yapılmadı.

## Geçmiş farkında tam paket: uygulama / koşu öncesi kontrol

2026-10-17 sonraki adımında okuyucu/runner güncellendi; üretim firmware'i ve SDK
değişmedi. İlgili **55 reader + 15 probe + 11 arşiv Python**, **12 erken-ret ve
33 workflow/artifact** senaryosu vardır. Aşağıdaki eski fiziksel sonuçlar yeni
paketin geçtiği anlamına gelmez. Tam regresyon **50 C / 52 header / 506 Python
(25 dosya)**, 22 graf/layout, 46 erken-ret ve 92 negatif profil ile geçti:
`.pio/host-tests/persistent-history-aware-host-20261017.log`. Onaylı kart koşusunun
sonucu yukarıdadır; aşağıda hazırlık ve ilk güvenli ret kaydı korunur.

İlk geçmiş-farkında deneme `.pio/host-tests/persistent-recovery-20261017-135325-5081/`
içinde **flash/reset öncesi FAIL** kaldı. Passive baseline boot=4/held=1/history=2,
ready=1/stable=1 idi. PENDING=NOT_FOUND ve sonraki ayrı BEGIN=EMPTY yanıtı arşivin
boş olduğunu doğruladı; 30-slot koruma kapısı gevşetilmedi. Bu denemede test imajı
yüklenmedi, NVS probe'u başlamadı, otomatik normal restore de gerekmedi.

Kullanıcıya boş SAFE açılışı açıklandı; önce bu boot'u iki ek yer-onayı/NOMINAL
komutuyla çalıştırıp kayıt oluşturma ve ardından tam paket için **toplam en fazla
10 recovery/mode TC** ayrıca onaylandı. Boot=4/history=2'ye sabitlenmiş yerel
bootstrap, reset yapmadan **47.719 s PASS**: rearm Sent/COMPLETE, held=0/SAFE,
ayrı NOMINAL Sent/ACK ve üç mission örneği; stack minimum 3672 bayt. Geçmiş silinmedi.
İlk HK arşiv kaydı iki GET_BY_INDEX okumasında aynı 20 bayt/CRC16 `0x7E26` ile
doğrulandı; bu yalnız okuma, BEGIN/prefix pinleme veya silme değildir. Henüz 30
tam kayıt oluşmadan yeni BEGIN başlatılmadı; son indeksin gerçekten bulunduğu
doğrulandıktan sonra paket yukarıdaki yeni kanıt dizininde yeniden çalıştırıldı.

- Arşiv korumasından önce ve sonra **reset/TC/NVS yazısı yapmayan** passive status
  ile aynı tamamlanmış boot/sayaç/geçmiş/gate bağlanır. Arada değişirse loader yok.
- Beklenen sticky baseline yalnız 0/2/32/34 olabilir. FAULT biti için ayrı
  `-AllowHistoricalFault` / `--allow-fault-history` onayı gerekir. Yeni/beklenmeyen
  FAULT, fatal/uncertainty/overflow/boot-loop, eksilen geçmiş veya sayaç reddedilir.
  PANIC ilk boot baseline+1, ikinci boot +2; normal restore tam bir sonraki boot'tur.
- NVS probe artık **iki upload'ın öncesine/sonrasına bağlıdır**; app dururken aynı
  byte hash'i gerekir. Önceki/sonraki farklı app fazlarının meşru NVS yazıları
  birbirleriyle eşit olmak zorunda değildir. Raw NVS hâlâ kaydedilmez.
- Kısmi test upload'ında da normal flash denenir. Pre-restore NVS kanıtı yoksa veya
  karşılaştırma değişirse normal imaj yazılmış olsa bile **app loader'da bırakılır**;
  bilinmeyen geçmişle otomatik onay/işletim başlatılmaz. Başarısız seçim PASS olamaz.
- Kullanıcı yeni gerçek PANIC/reset paketine, 30 kayıtlık iki canlı korumaya,
  korunacak NVS geçmişine, en fazla sekiz recovery/mode TC'sine ve bu güvenli
  loader'da-durma sınırına ayrıca onay verdi. Uzun test/GitHub bu onayda yoktur.

## 2026-10-17 ek teşhis: tekli yazımlarda NVS korundu

Önceki paket FAIL kaydı aşağıda aynen korunur; **aralıklı aktarım/sayaç kaybı kök
nedeni henüz kapanmadı**. Bu ek koşu aynı normal 153DF1… imajla yapıldı; yeni test
firmware'i, NVS silme veya politika gevşetme yoktur.

- Güncel 30 kayıt iki yeni tam canlı retained export ile korundu:
  `.pio/host-tests/flash-nvs-isolation-20261017-101600-5153/original.json` ve
  `original-retained.json`; SHA256
  `154D8BB0AC630798CE16D29D49AF3B369AFBD201A113E3D479311C73C1725421`.
  Bu oturumun RAM'i loader girişinde sıfırlandı; bilgisayar kopyaları korunuyor.
- Yeni `tools/flash_nvs_probe.py` yerel esptool 4.9.0/CP210x/ESP32 yolu içindir.
  Loader girişi RAM'i resetler; **flash yazmaz/silmez, uygulamayı başlatmaz**.
  Beklenen MAC ve gerçek resident partition tablosu (tam layout/MD5/padding)
  doğrulanır. NVS'nin 24576 baytı iki kez okunup eşitlik ve SHA256/per-page hash'leri
  kaydedilir. Ham NVS/SSID/parola dosyaya veya çıktıya konmaz; raw hata metni de
  saklanmaz. Loader'da kalır; ayrı kontrollü uygulama açılışı şarttır.
- İlk iki loader girişi/okuması aynı NVS özetini verdi. Sonra normal partition,
  bootloader, OTA seçim verisi ve uygulama **ayrı ayrı** standart 460800/stub ile
  yazıldı. Her yazım hash doğrulamasından geçti; her adım sonrası iki NVS okuması
  aynı ilk SHA256'yı verdi:
  `131EA8728C30EDA15B5205DE03DFD70225DCC18109A0079EF383D5B08ED0F583`.
  Bu tekli yazım sırası bir arada dört dosyalı eski başarısız paketin tekrarı
  değildir; önceki kaybın nedenini kanıtlamaz. Bu koşuda kesilme de olmadı.
- Normal kontrollü açılışta sayaç **2→3** ilerledi; `boot=3/held=1/flags=3`, ardından
  `ready=1/stable=1/flags=2` gözlendi. Eski nonfatal FAULT geçmişi **korunmuştu**.
  Dar PANIC okuyucusu yalnız flags 1/33 kabul ettiğinden açılışı reddetti ve TC
  göndermedi (`normal_boot_after_staged_upload.json` FAIL). Bu reddi silmedik veya
  eski FAULT geçmişini PANIC geçmişi diye kabul ettirmedik.
- Kullanıcıya bu farklı geçmiş açıklandı ve **bu açılışa özel ayrı onay alındı**.
  Reset yapmadan yalnız iki TC gönderildi: boot=3 rearm → Sent/binary + kart
  queued/COMPLETE; held=0/ready=1 ve en az iki saniye sonra SAFE; ardından ayrı
  NOMINAL → Sent/binary + kart ACK ve üç yeni NOMINAL örneği. **64.391 s PASS**,
  recovery stack minimum **3672 bayt**. FAULT geçmişi silinmedi; sonraki boot yine
  sağlık ve yeni yer onayı ister. Yerel, boot=3/history=2'ye sabitlenmiş operatör
  harness'i genel target runner veya PANIC paketinin PASS'i değildir.
- SAFE'teki GET-only kontrol üç taze HK/yedi birim/35 INFO olay, duplicate=0 ve
  TC195→195 ile geçti; onay komutları bu salt-okunur kontrolden sonra gönderildi.
  İki TC sonrası son GET-only kontrol de üç taze HK/yedi birim/**31 INFO olay**,
  duplicate=0 ve **TC197→197** ile geçti; yeni reset/komut yapılmadı.

Kanıt dizini: `.pio/host-tests/flash-nvs-isolation-20261017-101600-5153/`.
`loader_before.json`, `loader_entry_repeat.json`, `after_partition.json`,
`after_bootloader.json`, `after_ota.json`, `after_app.json`, dört upload logu,
`normal_attach_status.log`, `normal_safe_live.json`,
`current_boot_ground_approval.json`, `normal_after_ground_approval_live.json` ve
`host_checks.log` korunur. Son tam masaüstü
**50 C / 52 header / 492 Python (25 dosya)**, 22 graf/layout, 45 erken-ret,
92 negatif profil ve 20 workflow/artifact geçti. Yeni probe'un 15 testi yanlış
layout/board/kanıt, kısa/kararsız okuma, NVS değişimi, no-overwrite ve raw hata
sansürünü kapsar. Son baud-seçim değişikliği sonrası 11 erken-ret/20 workflow
kontrolü ayrıca tekrar geçti; masaüstü testleri fiziksel dayanıklılık veya her
upload'ın güvenilirliği değildir.

Runner varsayılanı artık normal PlatformIO ve bu tekli yazımlarda kullanılan
**460800/stub** yoluyla eşleşir; 57600/115200 veya ROM yalnız açık seçimdir.
Bu, **kök neden düzeltmesi değil**, gözlenen başarılı konfigürasyonu eşitleyen
bir önlemdir. [Espressif'in teşhis rehberi](https://docs.espressif.com/projects/esptool/en/release-v4/esp32/troubleshooting.html)
da aktarım hatası ile uygulamanın çalışmamasını ayrı ele alır; buradan kartımıza
kesin USB/güç/sürücü teşhisi çıkarılmaz. Yeni probe henüz target runner'ın otomatik
fazlarına bağlanmadı; bu koşu kontrollü operatör adımlarıyla yapıldı.

Bir sonraki kontrol, mevcut **FAULT geçmişini koruyan ve açık onaylı** oracle ile
seçilen PANIC + tam normal restore yoludur; eski 1/33-only otomatik runner'ı mevcut
geçmişle çalıştırmak uygun değildir. Salt testi geçsin diye NVS temizlenmez.
Uzun test/GitHub başlatılmadı.

## Önceki 2026-10-17 fiziksel sonuç: kısmi, paket FAIL

Kullanıcının ayrı arşiv/reset/NVS/TC/geçici test-RAM kaybı onayıyla çalıştırıldı.
**Seçilen PANIC/onay fazı geçti; paket bütünü ve yükleme boyunca kalıcılık geçmedi.**
Kart sonunda test imajıyla değil, hash'i aşağıdaki **153DF1… normal imajla** bırakıldı.

| Adım | Gerçek sonuç / sınır |
| --- | --- |
| Ground | 11 komutlu MDB source/runtime'a yedekli kuruldu, yalnız `myproject` instance yeniden başlatıldı. Native kontrol ve canlı iki dry-run geçti; mevcut Java sunucusu/veri dizini silinmedi. Repo/source/runtime SHA256 `056799FF96A38B8097D8FFDFE87A115BF930CBA8AFC748E500FE141C2C61ADF2`. |
| Orijinal RAM | İlk yükleme öncesi 30 kayıt iki tam canlı retained okumayla korundu. CRC/digest/fsync/read-back ve aynı dosya hash'i doğrulandı; ayrıca `local/archive/` altına iki kopya alındı. |
| İlk paket | 57600/stub test yüklemesi bootloader sırasında, finally normal restore uygulama yazımında kesildi. İkisi de FAIL; reader/restore başarısı iddia edilmez. |
| Okuyucu düzeltmesi | Firmware'deki iki mevcut yerel demo TC'si ground recovery isteği sanılıyordu. Yalnız tam injector-marker/CRC/router zincirleri ayrı ilişkilendirildi; keyfi/tekrar/yanlış TC hâlâ reddedilir. Üretim firmware'i değişmedi. |
| Sınırlı tekrar | 115200/ROM test yüklemesi hash doğrulamasıyla geçti. `panic_proof.json`: **79.391 s PASS**, gerçek HK false→abort→SDK PANIC=4; boot 1→2, failed 0→1, held=1/flags=33. |
| Gerçek dört TC | Kilitli NOMINAL NACK, stale token reddi, güncel rearm queued→COMPLETE, SAFE sonrası ayrı NOMINAL ACK geçti. Dört Sent/binary ve kart yanıtı ayrı doğrulandı; beş NOMINAL örneği, recovery stack minimum **3676 bayt**. |
| Normal restore | ROM tam restore uygulama yazımında %91'de kesildi; strict `restore` reader hiç çalışmadı. Sonraki yalnız-app/uncompressed ROM denemesi de %40'ta kesildi. Standart PIO 460800/stub normal yükleme tüm hash'lerle geçti. Bu manuel kurtarma paketin otomatik devamı/PASS'i değildir. |
| Normal son kontrol | Reset öncesi attach boot=1/flags=0 gösterdi. Ayrı kontrollü resetten sonra fallback **47.734 s PASS**, boot=2/failed=0/held=0; altı NOMINAL örneği ve stack minimum 3676. Yalnız normal çalışma kanıtıdır, PANIC geçmişinin restore boyunca korunduğu anlamına gelmez. |
| Canlı YAMCS | Son GET-only kontrol üç taze HK/yedi birim/**31 INFO olay**, duplicate wire=0 ve TC **132→132** ile geçti. |

### İki açık sorun

1. **Yüklemede tekrarlayan yanıt kesilmesi:** stub/ROM ve farklı hızlarda farklı
   adreslerde oldu; yalnız hızı azaltmak veya ROM kullanmak çözüm diye sunulamaz.
   Kullanıcı fiziksel bağlantıya dokunmadığını/kesilme görmediğini bildirdi.
   Son kontrolde COM7/CP210x `OK`, sürücü 11.4.0.393; okunan iki saatlik System
   kayıtlarındaki seçili aygıt hataları CP210x kopmasını kanıtlamadı. Bu, hiç USB/
   güç/UART hatası olmadığı kanıtı da değildir. Kök neden **belirlenmedi**; sürücü,
   güç yönetimi, kablo, SDK veya firmware kaynaklı diye kesin teşhis konmadı.
2. **Tam upload sonrası kalıcı geçmiş/sayaç kaybı:** önceki fallback boot=2 iken
   sonraki test boot=1 başladı. PANIC fazı boot=2/BAD_RESET geçmişi üretmesine rağmen
   son normal upload sonrası yine boot=1/flags=0 görüldü. Sadece izleyen kontrollü
   reset boot=1→2 kalıcılığını gösterdi. Upload aralığında geçmiş korunması **FAIL/
   açık**; kaybın hangi yükleme/SDK/flash adımında oluştuğu ayrılmadı. Görünen erase
   aralıkları NVS `0x9000..0xefff` alanını hedeflemiyor; bu, gerçek byte'ların korunduğu
   kanıtı değildir. NVS erase/onarım ile sorun gizlenmedi. Bu çözülmeden seçilen
   PANIC + normal-flash/korunan-geçmiş paketi kapanmaz; aynı paketi körlemesine
   tekrarlamak veya yalnız uzun teste geçmek doğru kapanış değildir.

Sonuç metinleri de ayrıldı: `fallback PASS` yalnız normal boot/sağlık/görev ilerlemesi
der; yükleme boyunca geçmiş korunduğunu veya seçilen paketin geçtiğini söylemez.
İlk paketteki okuyucu-atıf FAIL'i ve tüm aktarım FAIL'leri olduğu gibi tutulur.

### Yerel kanıtlar ve verinin konumu

- İlk koşu: `.pio/host-tests/persistent-recovery-20261017-094200-1600/`;
  `preservation.json`, `original*.json`, upload/restore logları ve önceki fallback
  dosyaları. İlk reader atıf hatası başarı kanıtı değildir.
- İkinci koşu: `.pio/host-tests/persistent-recovery-20261017-095321-8129/`;
  `panic_proof.json` PASS; `normal_restore_upload.log` FAIL; manuel ROM/PIO logları,
  `pio_attach_normal_status_retry.log`, `pio_fallback_normal_proof.json` PASS ve
  `normal_after_recovery_live.json` PASS. Başarılı strict `restore_proof.json` yoktur.
- Orijinal 30 kayıt ayrıca `local/archive/pre-persistent-recovery-20261017-094200.json`
  ve `...-094200-retained.json` içinde; dört dosyada SHA256
  `39DA364074AE3079498171C4EBC7DBC2F47BBFA6DEC1D8770D64F5262C1CA134`.
  Kart RAM'i onaylı resetlerle sıfırlandı; bu veri bilgisayar kopyalarından erişilebilir,
  karta otomatik geri aktarılmadı. İkinci koşunun kendi geçici 30 kaydı da iki kopyada
  korundu; eski orijinal kayıtlarla karıştırılmaz. Kopyalar aynı bilgisayardadır.
- Ground source/runtime eski MDB `.before-fsw-20261017-094105-9997.bak` ve
  `.before-recovery-20261017-094132-7030.bak` kopyalarında; veri dizini korunmuştur.
- Okuyucu düzeltmesi sonrası tam masaüstü: **50 C / 52 header / 477 Python
  (24 dosya)**, 22 graf/layout, 45 erken-ret, 92 negatif profil ve **20 workflow/artifact**.
  Log: `.pio/host-tests/persistent-recovery-after-reader-fix-20261017.log`.
  Son fallback/restore sonuç metni ayrımından sonra 41 reader + 11 arşiv Python,
  11 erken-ret ve 20 workflow/artifact ayrıca tekrar geçti. Bu metin/test değişikliği
  için yeni fiziksel upload yapılmadı; normal firmware hash'i değişmedi.

SDK/ağ ayarları değiştirilmedi; yeni üretim kodu eklenmedi. Uzun test, nihai README,
temiz klon ve GitHub yayını yapılmadı. Geniş recovery/boot-loop/bozuk-NVS/yerinde
onarım matrisi de seçilen kısmi PASS ile kapanmaz.

## Önceki hazırlık kaydı (fiziksel koşudan önce)

2026-10-17 ilk hazırlığında **gerçek kart koşusu yapılmamıştı.** O adımda yalnız
araç/fixture/doküman değişti; normal ve seçilen HK-panic imajı derlendi. Upload,
COM erişimi, canlı API/TC, ground kurulumu/reload veya gerçek NVS işlemi yoktu.
[Üretim politikası](recovery_policy.md), [açık kapanış listesi](release_checklist.md).

## Seçilen kısa senaryo

1. Çalışan YAMCS linklerinin doğru karta baktığını ve yeni `FDIR_RearmBoot` ile
   mevcut NOMINAL komutunun binary üretimini dry-run ile kontrol et. Kurulu runtime
   MDB yeni komutu içermiyorsa **arşiv komutu/yükleme öncesi dur**. Araç MDB kurmaz,
   sunucu/link açmaz veya reload yapmaz.
2. Normal ve yalnız `HK_Init` false-return profillerini derle; normal metadata/ELF
   ayrımını kontrol et. Dört flash dosyasının boy/yerleşimini doğrula, ayrı yerel
   kopyalarının SHA256'larını sabitle. SDK config veya kopyalar değişirse upload yok.
3. Orijinal RAM'i iki **canlı** retained okumayla koru. Her kayıt CRC/digest ve
   fsync/read-back kontrolünden geçer; iki dosya byte-identical ve **30 kaydın
   tamamı** olmalıdır. Önceki dosya, yalnız BEGIN cevabı, boş arşiv veya eski
   küçük pending prefix reset izni sayılmaz. Küçük pending varsa otomatik CANCEL
   yapılmaz; operatör incelemesi gerekir. 30-slot sınırı mevcut RAM sözleşmesidir.
4. Tek seçilmiş test imajını yükle. Uploader `--after no_reset` ile uygulamayı
   arada başlatmadan loader'da bırakır; okuyucu tek gözlenen açılışı başlatır.
   İlk boot'ta HK false→gerçek caller hatası→abort; ikinci boot'ta gerçek SDK
   PANIC=4, bir artan kalıcı boot/failed-start sayacı ve SAFE kilidi beklenir.
5. Deployment/yedi heartbeat/sağlık penceresinden sonra dört ayrı TC:
   kilitliyken NOMINAL→NACK; önceki boot token'ı→rearm reddi; güncel token→queued
   **ve sonra** COMPLETE; SAFE gözlemi/en az iki saniye sonra ayrı NOMINAL→ACK.
   YAMCS Sent history ve tam postprocessed binary ayrıca doğrulanır; Sent veya
   queued tek başına kart başarısı değildir. CRC/router yanıtları ilişkilendirilir.
6. `finally` içinde sabitlenmiş **normal** firmware'i geri yükle. Başarılı seçilmiş
   koşudan sonra tam bir sonraki boot, normal SDK reset nedeni, sıfır failed-start
   sayacı, korunan BAD_RESET geçmişi ve yeniden SAFE kilidi beklenir. Aynı dört
   komut/ayrı onay akışı ve en az üç güncel NOMINAL örneği tekrar doğrulanır.
   Onaydan sonra `held=0/ready=1/storage_ok=1` tanısı ve minimum 512 bayt recovery
   stack boşluğu gerekir; her reader en fazla 240 saniyedir.

Önceden mevcut domain fault/fatal/uncertainty/overflow veya BOOT_LOOP geçmişi bu
seçilmiş senaryonun otomatik onayı için uygun değildir: okuyucu durur, NVS silmez
ve asıl nedeni onarmaya çalışmaz. Beklenmeyen reboot, erken NOMINAL, sahte/tekrar
marker, storage/stack/health kaybı, yanlış token/argüman/binary veya eksik history
PASS sayılmaz. Belirsiz rearm/NOMINAL POST'u **tekrar gönderilmez**.

## Yan etkiler ve gereken ayrı onay

- İlk upload'tan **önceki orijinal 30 RAM kaydı** iki dosyada korunur; kalıcı arşiv
  özelliği eklenmez. Kopyalar aynı bilgisayardadır, bağımsız yedek ortamı değildir.
- Sonraki kontrollü testlerde üretilen geçici RAM kayıtları reset/restore ile
  kaybolabilir; bu ayrıca açık `AllowTestRamLoss` onayı gerektirir. Bu izin
  orijinal koruma aşaması başarısızken yükleme yapılmasına izin vermez.
- Recovery namespace'i normal firmware tarafından gerçekten yazılır. Kötü reset
  geçmişi **restore ile temizlenmez**: sonraki USB/güç açılışlarında da yeni
  sağlık/yer onayı gerekir. NVS erase, namespace silme/bozma veya fault clear yoktur.
- İki reader'ın her birinde en fazla dört gerçek recovery/mode isteği vardır
  (normal tam pakette toplam sekiz). Arşiv koruma TC'leri ayrı BEGIN/PENDING/READ
  işlemleridir; retry yalnız mevcut bounded retained-read politikasındadır.
- İlk kısmi upload veya seçilmiş reader hatasında da normal restore denenir.
  Başarısız seçilmiş test FAIL kalır; fallback normal gözlem onu PASS'e çeviremez.
  Restore upload/read başarısızsa bunu gizleyen PASS yoktur; kart durumu ayrıca
  incelenir. Otomatik hard-reset/PIO fallback ile gözlenmemiş boot oluşturulmaz.
- SDK config, ağ ayarları, ground veritabanı veya Git değiştirilmez. İmajlar
  credentials içerebilir; yerel `.pio/` kanıt/binary'leri yayımlanmaz.

Aşağıdaki komut ilk hazırlıkta çalıştırılmamıştı. **Yukarıdaki açık aktarım/kalıcılık
sorunları çözülmeden yeniden önerilen koşu değildir.** Güncel RAM, kalıcı NVS yan etkileri,
geçici test verisi kaybı ve ground komutları için ayrı onaydan sonra kullanılabilir:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_persistent_recovery.ps1 -Port COM7 -AllowRamReset -AllowPersistentWrites -AllowRecoveryCommands -AllowTestRamLoss
```

Önkoşul: yeni MDB'nin çalışan ground/runtime'a ayrı onayla kurulmuş olması ve
iki komutun mevcut komut linkinden doğru karta gidebilmesi. Repo native PASS bu
kurulumu kanıtlamaz. Eski `test_watchdog_system.ps1` engeli kaldırılmadı; yeni
paket o runner'ın otomatik-NOMINAL beklentisini veya güvenlik kapılarını bypass etmez.

## Masaüstü kanıtı ve kalanlar

Yeni Python testleri **37 reader/sender/CLI + 11 arşiv koruma** senaryosudur.
Gerçek araç kodu, sahte seri/HTTP/saat ile çalışır. CLI'nin PASS/FAIL/INTERRUPTED,
no-overwrite ve ham seri/özel HTTP ayrıntısını dışarı vermeme yolları da sınandı.
11 PowerShell erken-ret guard'ı gerçek araç/port olmadan; **19 workflow/artifact**
senaryosu gerçek runner gövde/fonksiyonlarını mock builder/uploader ile sınar:
preflight/build/preservation/receipt/image hatası öncesi flash yok; kısmi yükleme/
reader hatasında finally restore; failed seçim PASS olamaz; değişen hash/SDK
uploader'a ulaşamaz. Bunlar gerçek USB/reset/flash kanıtı değildir.

Tam kontrol: **50 C / 52 header / 473 Python (24 dosya)**, 22 graf/layout,
34 eski + 11 yeni erken-ret, 59+16+17 negatif profil geçti. Son araç düzenlemesinden
sonra yeni 48 Python/19 workflow ve imaj ayrımı ayrıca tekrar geçti.
Normal build RAM/flash **42964/813525**, HK-panic **42964/813885** bayt.
Normal firmware SHA256 önceki recovery build'iyle aynı:
`153DF1197889F6028F70E06054E91969335EC0EC334D660A8DAC3061FFA7D998`.
Seçilmiş HK-panic firmware SHA256:
`A6F8C64E0DAF077C70C5C05BCD0FDAE056040C7B96184574170893AF730E5FBA`.
SDK config SHA256 değişmedi:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.

İki ELF'de recovery var; yalnız testte seçilmiş HK wrapper var. Partition ve OTA
binary'leri birebir aynı. Bootloader'lar aynı boy/segment/entry düzenindedir;
byte farkları derleme zaman damgası ve bağlı checksum/hash'tedir (SDK compile-date
seçeneği açık). Bootloader'ın birebir aynı hash olması yanlış bir kabul şartı
olarak kullanılmadı; runner her profilin kendi tam byte'larını ayrı hash ile sabitler.
Yerel loglar `.pio/host-tests/persistent-recovery-build-prep-20261017.log` ve
`persistent-recovery-host-prep-20261017.log`; mock fixture'lar ayrı üretilmiş dizindedir.

Tam regresyon, derleme ve hash sonuçları [kapanış listesinde](release_checklist.md)
takip edilir. İmajlar yalnız derlenmiştir; yeni kalıcı politika kartta henüz
doğrulanmış değildir. Gerçek bozuk NVS/CRC, üç başarısız boot zinciri, fiziksel
güç-kesintisi/flash dayanımı ve geniş task/watchdog/domain repair matrisi bu
**seçilmiş PANIC + normal reset** paketiyle kapanmaz. Flash latency ölçümü de
eklendi denmez. Kalanlar açık tutulur; uzun test/GitHub başlatılmaz.
