# Bir saatlik kaynak gözlemi: SAFE kapsamı PASS

Bu eski C0719A… imajın SAFE gözlemidir. Daha sonra D36E80… imajda ayrı
[aktif NOMINAL saatlik koşusu da PASS oldu](nominal_soak_target.md); iki süre
ve iki imajın kapsamı birbirine eklenmez.

17 Ekim 2026, Türkiye saati **19:33:34**. Kullanıcının açık isteğiyle mevcut
normal firmware'de **3600 saniyelik** salt-okunur gözlem başlatıldı. Son rapor
**20:33:36**'da PASS olarak yazıldı; 20:37–20:38'de rapor, örnekler, araç/imaj
hash'leri ve pasif boot/geçmiş kontrolü değerlendirildi. Koşu başlangıcındaki
son tarih 20:34:14'tü; yalnız saatin geçmesi değil aşağıdaki kanıtlar kabul edildi.

## Sonuç ve değerlendirme

**3600.0 s / 120 tam rapor / PASS**. Raporlar seq25–144, cihaz uptime
755280–4325280 ms; ilk-son tam rapor arası **3569.984 s**. 119 rapor aralığı,
her raporda 14 görev ve yedi heartbeat sayacının ilerlemesi tekrar kontrol edildi.
Eksik blok, yeni gözlenen critical/fatal/panic, boot marker veya mission-queue
drop yok. Bu süre boyunca seri gözlemci komut/reset/yükleme/silme yapmadı.

| Ölçüm | Sonuç |
| --- | --- |
| Örneklenen mod | 720 SAFE, 0 NOMINAL; ilk kaynak raporunda henüz mod örneği yok, sonraki 119 rapor SAFE |
| Boş heap | 150020–150576 bayt; ilk 150576 → son 150272, fark −304 |
| Heap alt sınırı / en büyük boş blok minimumu | 135320 / 110592 bayt |
| En düşük lifetime boş stack | YLOG_TX: 868 bayt; bütün 14 rol ≥512, RECOVERY: 3664 |
| Örneklenen TM / TC peak | 0/10 ve 0/5; örnekler arası kesin tepe iddiası yok |
| En uzun observer duvar süresi | 138 ms (sınır 1000) |
| Mod örneklerinin en büyük aralığı | 5.078 s |

Heap ilk çeyrekte 150264–150576, ikinci çeyrekte 150020–150272; son iki çeyrekteki
**60 raporun tamamı 150272 bayt**. −304 bayt fark tek başına leak teşhisi değildir;
son yarım saatte süren bir düşüş görülmedi. Bu, bütün yüklerde leak yokluğu
kanıtı da değildir. Stack bütçesi azaltılmadı, eşikler gevşetilmedi.

Önce/sonra pasif kayıt: **boot13/history98** değişmedi, held0/storage1/stable1
korundu; ready0→1 gözlendi. Readiness'in değişmesi ayrı NOMINAL komutu değildir:
koşudaki bütün mod örnekleri SAFE kaldı, otomatik göreve dönüş iddia edilmez.
Okuyucu/normal imaj/SDK hash'leri başlangıç planıyla aynı kaldı. Kaynak raporu SHA256:
`F2832A0CB1685B6B4169EACE2DD38B28184C69F18CCFD2F86A86B96472F1CD2B`.

Altı kanıt dosyası ayrıca `local/evidence/soak-20261017-1933/` içine kopyalanıp
hash eşitliği doğrulandı; `.pio/` orijinalleri silinmedi. İki dizin aynı
bilgisayardadır, bağımsız fiziksel yedek veya GitHub yayını sayılmaz.

## Sabit kapsam

- Kanıt dizini: `.pio/live-check/soak-20261017-1933/`.
  `run_plan.json` başlangıç kimliği, araç hash'i ve kabul ölçütlerini;
  `resources.json` her tam rapordaki RUNNING checkpoint'ini ve son sonucu tutar.
- Ayrı, görünmeyen Python gözlemci süreci PID17716 olarak başlatıldı; uygulama
  kaynaklarına/yazılımına müdahale etmeden ölçümü tamamladı; son kontrolde çalışmıyor.
  Sohbetin açık tutulmasına gerek duyan bir AI döngüsü değildir. Bilgisayarın
  uyuması/kapanması, USB veya sürecin kesilmesi tamamlanmayı engelleyebilir.
- DTR/RTS düşük, reset/upload/flash/NVS/WIPE/fault-clear/yer onayı/mod TC'si yok.
  Bu test GitHub yayınına izin vermez.
- Normal imaj, son onaylı yüklemenin **C0719A…** snapshot'ıdır; runtime'dan build
  hash'i okunduğu iddia edilmez. Başlangıçta pasif boot13/held0/ready0/storage1/
  stable1/history98 kaydedildi. Yeniden onay veya hata temizleme yapılmadı.
- İş yükü mevcut simülasyon ve kendi koruma/mod politikasıdır; mod zorlanmaz.
  İlk 31.5 saniyede iki tam rapor ve yedi SAFE örneği görüldü, NOMINAL0.
  İlk rapor geç bağlanma nedeniyle UNKNOWN etiketlidir. Bu koşu bir saatlik
  NOMINAL mission-load testi diye sunulamaz.

## Koşudan önce belirlenen kabul ölçütleri

Gözlemci yeni critical/fatal/panic, gözlenen beklenmedik reboot, görev/rapor
eksikliği, kuyruk drop'u, durmuş heartbeat veya kaynak sınırı ihlalinde durur.
14 kalıcı rolün stack payı en az512 bayt; örneklenen boş heap en az32768,
lifetime alt sınır/en büyük blok en az16384 bayt; heartbeat yaş/aralığı en
fazla15000 ms, observer en fazla1000 ms. Tam raporlar 45 s içinde gelmeli;
cihaz/host sıra ve aralık kontrolü korunur. İlk iki raporda heap150576,
  en düşük lifetime stack YLOG_TX868 bayttı; nihai sonuçlar yukarıda ayrı verilir.

3600 s gözlem, en az119 tam rapor, ilk-son rapor aralığı en az3565 s ve güncel
son örnek gerekir. Sonuçta ayrıca aynı boot/geçmişin pasif kontrolü, okuyucu
hash'inin değişmediği, mod örnekleri, heap eğilimi, bütün stack/heartbeat
değerleri ve kuyruk peak'leri incelenmelidir. RUNNING/FAIL/INTERRUPTED başarı
sayılmaz. Gözlemcinin eski açıklama etiketindeki “13 tasks” ifadesi güncel değildir;
gerçek completeness/bütçe kontrolü RECOVERY dahil **14 rol** gerektirir.

## Sınırlar ve sonraki adım

SAFE'teki kaynak sürekliliği, bütün alt sistemlerin bir saat aktif görev yükü
taşıdığını veya önceki aktif arızanın onarıldığını kanıtlamaz. Örneklenen queue
peak kesin tepe değildir; CPU/WCET/fiziksel sensör yeterliliği ölçülmez.
Eski aralıklı upload/geçmiş kaybının kök nedeni hâlâ açık kalır.

Seçilen bir saatlik SAFE kaynak gözlemi ve sonuç değerlendirmesi tamamlandı;
bu koşuda ek kod düzeltmesi veya yeni senaryo gerektiren ölçüm ihlali görülmedi.
Sırada nihai README, temiz kurulum/yayın güvenliği ve kullanıcı onayıyla en son
GitHub vardır. Eski aktarım/geçmiş kaybı riski yayın notlarında korunmalıdır.
