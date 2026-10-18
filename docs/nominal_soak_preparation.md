# NOMINAL uzun koşu hazırlığı — tarihsel hazırlık kaydı

Güncel uygulama: yeni imaj yüklendi/NVS korundu; ilk aktif başlangıç FAIL oldu,
client sorgusu ve başlatma sırası düzeltildi. Kullanıcı onaylı tek tekrar
3600.2 s/120 rapor/720 NOMINAL ile PASS oldu;18 Ekim kanıtları yeniden doğrulandı.
Aşağıdaki "henüz yüklenmedi" ifadeleri hazırlık
anını anlatır. [Güncel uygulama ve sınırlar](nominal_soak_target.md).

17 Ekim 2026 kullanıcı isteği: SAFE gözleminden sonra aktif NOMINAL yükünü de
ölçmek. Bu çalışma yeni hata enjeksiyonu/profil eklemez. Önceki bir saatlik SAFE
PASS, C0719A… imajına aittir; aşağıdaki yeni imaja aktarılmış sonuç değildir.

## Güç ortamı düzeltmesi

`components/eps/eps_sim_model.h` platformdan bağımsız demo hesabıdır:

- Eşdeğer enerji kapasitesi **20 Wh**, lineer bus proxy'si **0..9 V**, başlangıç
  **8.5 V**; enerji farkı `net W × ölçülen saniye / 3600` ile Wh olarak eklenir.
- Ortam **60 s güneş / 60 s gölge**, güneşte **24 W**, gölgede0 W varsayar.
  En yüksek mevcut yük comms1 + payload5 + heater3 = **9 W**. Şarj/deşarj
  verimleri ayrı **0.90**; bu varsayımlarla bir döngüde net pozitif enerji vardır.
  Bu panel boyutlandırması fiziksel bir donanımın ölçümü veya enerji yeterliliği
  kanıtı değildir; eski5.5 W kaynağıyla aynı çevre koşulu test edilmiş sayılmaz.
- Gerilim/enerji kapasitede doyurulur; NaN/Inf güç/veri sağlıklıya çevrilmez.
  Güneş olmadığında kapasite gerçekten tükenir ve5 V korumasına ulaşılabilir.
  **5.0/8.5 V eşikleri, SAFE/fatal/boot kilidi ve owner-clear politikası aynı**.
- Solar task gerçek tick farkını kullanır; gecikme/32-bit tick wrap ölçülür.
  Log ve ADCS getter yalnız EPS kilidi bırakıldıktan sonra, özel snapshot ile
  çağrılır. Sıcaklık da bounded birinci-derece demo proxy'sidir, sensör değildir.
- EPS manager'ın solar telemetriyi her100 ms'de1.8 ile tekrar çarpması kaldırıldı.
  Solar girdinin sahibi yalnız ortam görevidir. SOC şimdi aynı lineer enerji
  proxy'sinin0..100 yüzdesidir; eski6.0..8.4 V heuristiği değildir.

## Payload iş yükü

Mevcut `SET_RATE` wire biçimi ve1..10 aralığı korunur; artık `Payload_Update`
gerçek monotonic-ms üst hız sınırı uygular. Örneğin1 Hz seçildiğinde aynı saniye
içindeki çağrılar yeni kayıt üretmez; gecikmeden sonra catch-up burst/retry yoktur.
İzin kontrolü aradaki çağrılarda da yapılır; güç/mod izni kaybolunca SAFE olur.
Başlangıç/yeniden START ve rate seçimi örnek saatini yeniden kurar. Henüz rate
seçilmemiş0 değeri eski her-update davranışıdır; koşu önce açıkça **1 Hz** seçer.
Payload'ın archive-full → ERROR politikası kaldırılmadı veya yumuşatılmadı.

## Veri kaybetmeden yer açma

Yeni `tools/archive_drain.py`, mevcut Yamcs transfer/CRC/dosyalama kodunu kullanır.
Varsayılan yalnız MDB dry-run'dır. Gerçek işlem için `--send --confirm-clear`,
yeni çıktı dizini ve `--once` veya300..7200 s süre açıkça gerekir.

Her batch: PENDING → mevcut oturumu sürdür veya BEGIN → bütün prefix kayıtlarını
CRC/digest ile indir → exclusive dosya + fsync/read-back → ikinci exclusive
kopya + fsync/read-back ve byte eşitliği → pending checkpoint → **yalnız o
manifest için CONFIRM**. Sonradan yazılan tail korunur. WIPE/CANCEL, fault-clear,
mod komutu, raw UDP veya reset yoktur. Transport retry0; belirsiz sonuçta durur,
aynı veya başka oturumu otomatik tekrar onaylamaz. Kaydedilen dosyalar tutulur.
İki kopya aynı bilgisayardadır; bağımsız yedek medyası garantisi değildir.

Süre sonuna gelince aktif bounded batch tamamlanır veya hata vererek durur;
bu bir gerçek zamanlı deadline garantisi değildir.1 Hz payload +5 s HK üretimi
yaklaşık1.2 kayıt/s oluşturur; gerçek aktarımın buna yetişmesi kart koşusunda
ölçülmelidir. Sink gecikir/başarısız olursa güvenlik politikası geçersiz kılınmaz;
NOMINAL koşusu PASS sayılmaz. Otomatik veri kaybını saklayan çözüm eklenmedi.

## Yer komutları / kurulum ayrımı

Bakımı yapılan XTCE'ye mevcut firmware handler'ları için `ADCS_SetMode`0..2 ve
`PAYLOAD_Init/Start/Stop` eklendi; yeni firmware opcode'u veya fault-clear API'si
yoktur. PAYLOAD_SetRate ve FDIR_SetMode/RearmBoot korunur. Native Yamcs5.12
doğrulaması **15 komut** yükledi;20 postprocessed komut/10 range reddi ve mevcut
recovery/transfer/extraction/preprocessor kontrolleri geçti. Çalışan yer istasyonu
**bu adımda güncellenmedi/restart edilmedi**; mevcut runtime bu yeni komutları
henüz içermez. YAMCS arşivi/ayarları silinmeyecek.

## Derleme ve doğrulama

Normal build33.41 s: RAM**42972**, flash**816129** bayt. `firmware.bin` SHA256:
`D36E800D53AE174BF94468FD99981E8E364A93C37DAECCCBF41398870C5422C4`.
SDK SHA256 aynı:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
Yeni MDB SHA256:
`C454FA2481F11E28A4126F8D91272CC16C854CDF14F567493A965379E4FE323C`.
Disk kimlikleridir; karttan okunmuş runtime build kimliği değildir.

Saf model + gerçek solar task fixture'ı; ölçülen dt/wrap, max-load güneş/gölge
bir saatlik hesap, karanlıkta tükenme, üst sınır/NaN ve EPS kilidi dışı I/O
kontrolleri geçti. Bu bir saatlik hesap gerçek kart dayanıklılık testi değildir.
Mevcut183 alt servis süreci, genişletilen payload rate vakası ve11 yeni Python
dual-save/retained-tail/permission/ambiguous-confirm testi geçti. Native MDB ve
18 Python static profile testi geçti. Tam regresyon **52 C grubu /53 bağımsız
header /533 Python testi (27 dosya)**,22 graf/layout,49 erken-ret,39 runner
workflow/artifact ve97 negatif profil kontrolüyle geçti; normal ELF/metadata'da
test hook'u yok. Log: `.pio/host-tests/nominal-prep-host-20261017-2057.log`.
Native Yamcs doğrulaması ayrıca kurulu5.12 jar'larıyla çevrimdışı yapıldı;
çalışan server/source/runtime MDB değiştirilmedi.
Canlı sunucudaki varsayılan `archive_drain.py` önizlemesi de geçti: yalnız
MDB generation dry-run; karta gönderim, CONFIRM veya dosya yazımı yok.

## Kart fazının ön koşulları ve kabulü

Yeni imaj **henüz yüklenmedi**; mevcut kart C0719A… / boot13 geçmişindedir.
Mevcut RAM önce iki canlı kopyada korunmalı. NVS upload öncesi/sonrası eşitliği
kapısı kullanılmalı; reset ve yeni boot için açık uygulama onayı alınmalı.
YAMCS source/runtime MDB güncellemesi yedekli ve mevcut DB korunarak yapılmalı.

Yeni boot sağlık penceresi → ayrı yer onayı → SAFE korunması → ayrı NOMINAL;
yerel EPS izinleri hazırken açık ADCS DETUMBLE ve payload rate1/INIT/START.
Sonra **3600 s `--require-mode NOMINAL`**, eşzamanlı doğrulanmış archive drain ve
gerçek payload kayıtlarının batch'lerde ilerlediği gözlenir. Her örnek NOMINAL
olmalı; mode düşmesi, yeni fault/reset, queue drop, eksik kaynak/stack/heartbeat
veya başarısız sink durdurma nedenidir. Otomatik tekrar NOMINAL/fault ACK yoktur.
Sonunda payload STOP ve kontrollü SAFE, kalan kayıtların doğrulanmış aktarımı
planlanır; test dışındaki saatler aktif yük PASS'ine eklenmez.

Eski aralıklı upload/geçmiş kaybının kök nedeni hâlâ açık; bu model değişikliği
onu çözmez. GitHub yayını yapılmadı ve bu hazırlık yayın yetkisi değildir.
