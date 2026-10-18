# Aktif NOMINAL uzun koşusu — uygulama kaydı

17 Ekim 2026, Türkiye saati. Yeni hata enjeksiyonu/profil, WIPE veya GitHub yayını
yok. Önceki [SAFE saatlik PASS](long_run_target.md) ayrı imaj ve ayrı kapsamdır.
**Sonuç: 3600.2 s / 120 tam rapor / 720 NOMINAL örneği — PASS.**
17 Ekim koşusu tamamlandı;18 Ekim'de saklanan kanıtlar yeniden doğrulandı.
Bugünkü değerlendirme kart/seri port/Yamcs bağlantısı kullanmadı.

## Tamamlanan koşunun sonucu

| Ölçüm | Sonuç |
| --- | --- |
| Kaynak gözlem penceresi | 21:38:33 başlangıç,3600.2 s;120 tam rapor |
| Örneklenen modlar |720 NOMINAL; diğer modlar0; archive-full0/DEGRADED0/reset marker0 |
| Heap |150108–150500 bayt; ilk-son fark0; lifetime min136740, largest block min110592 |
| En düşük lifetime boş task stack | YLOG_TX864 bayt;14 rol izlendi, kabul alt sınırı512 |
| Heartbeat / gözlemci |7 sayacın ilerlemesi/aralık/yaş kapıları geçti; observer max126 ms |
| Doğrulanmış yer arşivi |867 batch/4286 kayıt;3554 payload +732 HK; her prefix için iki doğrulanmış dosya |
| Kapanış | Ayrı payload STOP ve SAFE kart yanıtı PASS; tail EMPTY; pending oturum yok |
| Son test durumu | boot14/held0/ready1/storage1/stable1/history98; recovery stack3676 bayt |

3554 payload, **64 baytlık sentetik görev verisi kaydıdır**: fotoğraf veya mod
değişimi değildir. Sayılar üretici/drain/STOP-SAFE kapanışının tamamını kapsar;
yalnız3600 s kaynak penceresi içindeki kesin üretim sayısı olarak sunulmaz.
20 Wh/24 W/sun-shadow EPS ortamı ve ADCS plant simülasyondur; fiziksel kamera,
kalibre edilmiş güç/attitude modeli veya gerçek radyo linki testi değildir.

18 Ekim değerlendirmesinde867 primary/backup çiftinin byte eşitliği, SHA256,
record CRC/manifest'i, tek archive epoch'u/artan transfer ID'leri ve sayımları
yeniden kontrol edildi.120 raporun sıra/target-host aralıkları,14 stack/yedi
heartbeat ve kayıtlı limitleri; operator aşamaları, tail EMPTY ve son sağlık
durumu kontrol edildi. Koşuya bağlanan araç/imaj/MDB/SDK hash'leri aynıydı.

Tamamlanan raporlar ve değerlendirme `.pio/` dışında
`local/evidence/nominal-soak-20261017-2136-final/` altında korundu. Kaydedilmiş veri
çiftleri `local/archive/nominal-soak-20261017-2136/` içindedir; ikisi de Git'e
girmez. `resources.json` SHA256:
`C949605A916167EC9CB74DF3F3F1D6988DBE01D936ED1D15CBFE59067B7E6CFD`.
`control.json` SHA256:
`B235C231C122A7EEEC1E9DEB64E11D862904130ABBEAFED973AC191E15E2BD90`.

Bu son durum17 Ekim test kapanışına aittir; bugün tekrar açılan kartın durumu
olduğu varsayılmaz. Mod/queue örneklenir; kesintisiz formal mod kanıtı, her queue
tepe değeri, matematiksel leak yokluğu, uzun süreli flight qualification veya
tamamen kayıpsız UDP iddiası yoktur. İlk FAIL ve eski upload riski aşağıda korunur.

## Koruma ve normal yükleme

Önceki RAM'deki 30 kayıt iki canlı export ile korunup ayrıca `local/archive/`
altına kopyalandı; SHA256 her kopyada:
`88A182BE5C255504DE36F37838E8217252B549DDBACB2CD6ACB8B561286FF02F`.
İki yerel kopya bağımsız yedek medyası değildir.

Normal D36E80… imaj, stub/460800 ile ilk denemede yüklendi. Upload öncesi ve
sonrası çift NVS okumasının byte hash'i aynı:
`821F1DFBD1AA9B1A522A6A8CEF18447ED96479A65C666854BA444B59AB35BFF0`.
Ham NVS saklanmadı; NVS erase/bozma yok. Boot13→14/history98 korundu. Yeni sağlık
→ ayrı yer onayı COMPLETE → held0/SAFE → ayrı NOMINAL ACK ve üç mission örneği
**90.984 s PASS**, recovery stack min3676 bayt. Bu kısa normal açılış kanıtıdır.

Firmware SHA256:
`D36E800D53AE174BF94468FD99981E8E364A93C37DAECCCBF41398870C5422C4`.
SDK SHA256:
`DEDE24521B0101D08880DAC86F3C711B6CE592CB3741DAAEB624B7BD4B659087`.
İmaj dosyası ve upload kanıtıdır; karttan okunan runtime build kimliği değildir.

Yamcs source/runtime MDB yedeklendi ve görev örneği yeniden başlatılarak profil
yüklendi. Java/dataDir/DB silinmedi. İlk profil15 komutluydu; sonraki yerel
toparlanma için mevcut firmware opcode04 `PAYLOAD_Reset` de eklendi:16 komut.
Son MDB SHA256:
`763BC61488BBBDB79838876F883471F12F6DFFBCF9B703C5DEBD97CDAE12C5EE`.
Bu payload RESET, yalnız payload ERROR→OFF ve yerel error_counter sıfırlamasıdır;
ESP reset, arşiv temizleme veya genel FDIR/NVS geçmişi silme değildir.

## İlk başlangıç denemesi: FAIL, saatlik koşu sayılmaz

21:22:21'de controller,21:22:51'de kaynak gözlemcisi/drain açıldı. Yardımcıdaki
45 s veri-ilerleme hesabı alt sistem hazırlığını da sayıyordu. Ayrıca sink,
payload START'tan sonra açıldığı için18 HK birikmişken ilk21 kayıtlık prefix'in
aktarılması bekleniyordu. Her kayıt sorgusu bütün paket indeksini tarıyor,
PENDING bile1.06–1.11 s sürüyordu. Gerçek arşiv21:23:02'de doldu ve payload ERROR'a
geçti; warning eşiği21:23:10'da DEGRADED kararı verdi.21:23:32'de tek yer komutuyla
SAFE'e alındı. Kontrol **FAIL/71.296 s**, kaynak dosyası **RUNNING/0 rapor** kaldı;
bu dosya devam eden test veya PASS değildir. Bütün ilk deneme süreçleri sonlandı.

Sink iki prefix'i ancak çift fsync/read-back doğrulamasından sonra CONFIRM etti:

| Prefix | Kaydedilen içerik | İki dosyanın SHA256 değeri |
| --- | --- | --- |
| İlk21 |18 HK +3 payload | `3ADD9BD3A89334F159492DE4AEC69F9D061F0152B6C8FCEE30C4DDC92D83630B` |
| Sonraki9 |2 HK +7 payload | `571542D3EEDC14D89D0F9458196F14069E43E7E2886E549A912FA8D8E47255A2` |

`local/archive/nominal-soak-20261017-2103/` içindeki30 kayıt/ikişer kopya tekrar
hash kontrolünden geçti. Sink'in kendi **PASS/41.187 s** sonucu yalnız bu güvenli
aktarımdır, NOMINAL başarı sonucu değildir. Arşiv doluyken yeni yazımlar reddedildi;
bu nedenle tüm üretim için **kayıpsız** iddiası yoktur. İlk deneme kanıtlarının18
dosyası hash eşitliğiyle `local/evidence/nominal-start-20261017-2103/` içine alındı.
Son pasif durum boot14/held0/ready0/storage1/stable1/history98 idi; reset yok.

## Somut düzeltme ve kullanıcı onaylı tek yeniden deneme

`tools/yamcs_archive_transfer.py` artık baseline/reply paket sorgularını yalnız
`/myproject/ArchiveTransferContainer` indeksine sınırlar. APID, CRC, manifest,
pagination, baseline çakışma ve Sent-history doğrulamaları kaldırılmadı. Üç
PENDING salt-okunur ölçümü0.187/0.329/0.296 s; baseline sorguları0.032–0.047 s.
Bu küçük ölçüm tek başına sürekli yük yeterliliği kanıtı değildir.

Değişen client, graceful stop ve MDB ile **535 Python testi/27 dosya** geçti.
Native Yamcs5.12:16 komut yüklendi,21 postprocessed komut/10 range reddi; mevcut
recovery/transfer/extraction/preprocessor kontrolleri geçti. Tek-koşuluk yardımcı
için8 komutun pre/post fixture'ları,48 bozuk paket reddi ve gerçek fake-transport
çift dosya/manifest/sayım/bozuk kopya reddi geçti. Önceki firmware52 C/53 header
regresyonu aynı imaja aittir; bu düzeltmede yeni firmware yüklenmedi.

Kullanıcının `devam et` onayıyla21:37:16'da yeni, ayrı çıktı dizininde tek deneme
başlatıldı: `.pio/live-check/nominal-soak-20261017-2136/`. Önceki FAIL dosyaları
değiştirilmedi. Aynı boot14/history98 için yeni pasif kontrol geçti.

Sıra: consumer ilk prefix/EMPTY turunu tamamlar → tek yerel PAYLOAD_Reset → yeni
ready1/held0/storage1/stable1/history98 → tek guarded NOMINAL ve üç örnek → ADCS
DETUMBLE + payload rate1/INIT/START → **3600 s require-mode NOMINAL** gözlemi.
45 s payload ilerleme penceresi artık gözlemin başlangıcından hesaplanır.

Kabul: mevcut heap/14 rol stack/yedi heartbeat/queue ve rapor aralığı kapıları;
aynı archive epoch'unda çift doğrulanmış kayıtlar; her45 s içinde gerçek payload
ilerlemesi; warmup öncesi içerik hariç en az2880 yeni64-byte payload kaydı.
Her örnek NOMINAL olmalı; bütün saat tamamlanmadan PASS yok. Bitişte tek STOP/SAFE,
batch arası sink durdurma, bounded tail aktarımı/EMPTY ve pasif boot/geçmiş/sağlık
kontrolü gerekir. Hata halinde tek SAFE; otomatik NOMINAL/RESET/fault ACK/tekrar
yok. Firmware'in koruma eşiği veya archive-full→ERROR politikası değiştirilmedi.

21:39:48 kontrol noktası: yerel RESET ve ayrı NOMINAL kart/Sent zincirleri PASS;
yeni ready1 ve üç mission örneği doğrulandı. ADCS/rate/INIT/START de PASS. Kaynak
gözlemi **21:38:33'te** başladı, beklenen bounded gözlem sonu **22:39:13**; ardından
kapanış/EMPTY/son pasif kontrol yapılır. İlk65.6 s'de3 tam kaynak raporu/14 NOMINAL
örneği, diğer mod0, heap150500 baytta sabit, observer max122 ms, archive-full0 ve
DEGRADED0. Controller o an68 payload/20 HK olmak üzere88 kaydı24 batch'te yeniden
doğrulamıştı; sink'in daha yeni checkpoint'i93 kayda ilerlemişti. Bu fark iki
checkpoint'in farklı anlara ait olmasıdır; saatlik sonuç değildir. Üç süreç o
anda canlıydı. Uzun çalışma için bilgisayar uyanık, Yamcs açık ve ESP bağlı kalmalı.

Yukarıdaki21:39:48 satırları tarihsel ara checkpoint'tir; süreç artık tamamlandı.
Seçilen NOMINAL uzun koşusu ve değerlendirmesi PASS. Eski seri aktarım/geçmiş
kaybının kök nedeni açık kalır; bu yükleme/uzun PASS o tarihsel riski kapatmaz.
