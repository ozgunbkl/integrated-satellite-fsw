# Deployment tamamlanma kapısı

Güncelleme: 2026-10-14. Bu kapı, mevcut 10 + 2 saniyelik deployment simülasyonunun
yer komutuyla atlanmasını engeller. Anten sensörü/burn-wire/gerçek deployment
başarısı veya uçuş kuralına uygun sessizlik süresi doğrulaması değildir.

## Sözleşme

`system_mode` servisi, global moddan ayrı private bir `deployment_complete` bilgisi
tutar. Başlangıçta false'tur; sadece bu boot'un RAM'inde yaşar. Yeni mutex,
flash/NVS yazımı, persistent state veya telemetri alanı eklenmedi.

| Durum / işlem | Sonuç |
| --- | --- |
| Completion yok; NOMINAL/DEGRADED için Set veya Transition | Aynı mode mutex'i altında reddedilir; mevcut mod değişmez |
| Completion yok; yer komutuyla NOMINAL/DEGRADED | Gerçek FDIR handler'ın mevcut NACK yolu; ACK veya erken çalışma modu yok |
| İlk başarılı completion; mod INITIAL_BOOT, aktif hata engeli yok | Completion=true ve INITIAL_BOOT → NOMINAL aynı kilit bölgesinde kaydedilir |
| İlk başarılı completion; mod SAFE/CRITICAL/EMERGENCY | Completion=true; güvenli mod aynen korunur |
| Tekrar başarılı completion | true döner; mod/state/log tekrar değiştirilmez |
| Completion çağrısında servis/kilit yok | false; bu çağrı hiçbir değişiklik yapmaz, deployment task tekrar dener |

`SYSTEM_MODE_Init()` hâlâ startup-only ve once'tır; doğrudan operating başlangıç
modları NOMINAL/DEGRADED artık tahsisattan önce reddedilir. Production core zaten
INITIAL_BOOT kullanıyordu; init sırası/defaultları değişmedi. SAFE/EMERGENCY/
CRITICAL başlangıcı geçerlidir fakat completion olmadan normal çalışma moduna
geçemez. Geçersiz enum/ikinci init korunur.

`SYSTEM_MODE_CompleteDeployment()` yalnız deployment görevinin simülasyonu
bitirmesinden sonra çağrılması için iç servis sözleşmesidir. Production'da tek
caller bu görevdir. API'nin kendisi zaman/sensör kontrol etmez; başka trusted
firmware kodunun erken çağrısını engelleyen capability/authentication mekanizması
değildir. Bu API'yi ground handler veya genel recovery için kullanmayın.

Tamamlanma bool'u ilk kayıttan sonra bu boot boyunca monotondur. Manuel olarak
INITIAL_BOOT moduna dönmek onu sıfırlamaz, simülasyonu yeniden başlatmaz ve
completion'ın ikinci çağrısı tekrar NOMINAL'e zorlamaz. Yeni boot bilgiyi sıfırlar;
board reseti RAM archive'i de kaybettirir. Bu adımda reset/upload yapılmadı.

`SYSTEM_MODE_IsDeploymentComplete()` 10 ms kilit isteğiyle konservatif snapshot
verir. False, henüz bitmedi veya servis/kilit okunamadı demektir; önceki true
bilgiyi silmez. Getter ile ayrı mod okumaları birlikte atomic snapshot değildir.
Yetki denetimi Get→Set üzerinden değil, mutator içindeki aynı kilitte yapılır.

## Görev davranışı

Mevcut nominal beklemeler `pdMS_TO_TICKS(10000)` ve `pdMS_TO_TICKS(2000)` korunur.
Mesajlar artık açıkça simülasyon olduğunu ve burn-wire çıkışı olmadığını söyler;
fiziksel antenin açıldığı iddiası yoktur.

İki bekleme bittikten sonra task completion kaydetmeyi dener. Lock timeout'unda
başarı mesajı veya self-delete yoktur; `pdMS_TO_TICKS(100)` bekleyerek yalnız
bookkeeping'i tekrarlar. Safety wait/deployment adımları tekrarlanmaz. Başarılı
kayıttan sonra completion mesajı ve `vTaskDelete(NULL)` gelir. Mutex sürekli
kullanılamazsa retry sürer; yeni abort/deployment watchdog/reboot politikası
eklenmedi. Delay'ler scheduler/kilit beklemesiyle uzayabilir; kanıtlanmış fiziksel
12 saniye deadline veya worst-case tepki süresi değildir.

Loglar ilgili transaction'ın sonucudur; mode mutex'i bırakıldıktan sonra başka
bir fault kararı gelebilir. Task, completion mesajını "şu anda mutlaka NOMINAL"
şeklinde kullanmaz. Öncesinde veya sonrasında işlenmiş güvenli mod kararı ezilmez.

## Recovery sınırı

Bu yalnız startup/deployment önkoşuludur, genel fault-clear/recovery izni değildir.
Completion hiçbir FDIR warning counter'ını veya aktif fault nedenini temizlemez.
FDIR ACK_FAULT completion değildir ve critical/fatal kaydı silmez. Sonraki
[aktif hata kapısı](fdir_faults.md) kayıt ve operating-mode iznini aynı MODE
mutex'i altında tutar. `FDIR_HasActiveFaults()==false` fiziksel bütün hatalar geçti
diye yorumlanmaz; completion kapısı bu ayrı sorguya dayanmaz.

Completion sonrasında manuel mode komutu da active critical/fatal/uncertainty
kapısına uyar; aktif nedenler varken completion bu engeli atlayamaz. Alt servislerin
kendi EPS/PDU/local SAFE/explicit restart koşulları ayrıca sürer. EPS owner yalnız
8.5 V finite ölçüm + aynı revision ile low-voltage critical kaydını çözebilir;
global NOMINAL yazmaz. Watchdog, verified diğer-source/fatal recovery ve genel
boot/mission grafiği hâlâ açıktır.

## Doğrulama

`tests/host/deployment/check.c`, gerçek system_mode, deployment task ve FDIR ground
handler kaynaklarını birlikte çalıştırır. RTOS/zaman/YAMCS event sınırları taklit
edilir; gerçek task delete/scheduler/UDP/CRC/board kullanımı yoktur.

212 ayrı süreç senaryosu:

- Completion öncesi dört erişilebilir mod × altı hedef × Set/Transition/ground:
  72; completion sonrasında altı mod × altı hedef × aynı yollar: 108.
- Altı modda ilk/tekrar completion davranışı: 6.
- 11 görev senaryosu: normal boot, mevcut üç safety mode, iki simülasyon
  beklemesinde fault, transient retry/kalıcı retry, retry sırasında CRITICAL,
  completion lock'undan hemen önce ve transaction'dan hemen sonra FATAL.
- 15 diğer senaryo: completion/query/mutator lock hataları, idempotent boot dönüşü,
  tekrar init, ACK'in kapıyı açmaması, invalid/missing mode, callback snapshot,
  critical/fatal korunması, uninitialized servis, iki operating init reddi ve allocation failure.

Delay sırası, retry sayısı, false-success/self-delete yokluğu, mode/flag korunması,
gerçek ACK/NACK, mutex release ve tahsisatsız init reddi kontrol edilir. Alt servis/
EPS power test fixture'ları artık operating init kullanmaz; gerçek completion
API'siyle **mantıksal post-deployment** state hazırlar. Bu fixture çağrıları
deployment timer kanıtı değildir; gerçek görev beklemeleri yeni grupta taklit
edilerek ayrı doğrulanır. Mevcut assertion'lar veya fault/recovery koşulları gevşetilmedi.

2026-10-14: toplam 17 C host grubu, mevcut yedi dosyada 79 Python kontrolü ve
normal `esp32dev` derlemesi geçti. RAM 42188/327680, flash 795377/1740800 bayt;
SDK config SHA256 aynı. Karta yükleme/reset veya RAM archive'i silen işlem yok.
Wi-Fi/YAMCS/router/firewall/credential/partition ayarları değişmedi. Mevcut
compiler uyarıları bu adımda topluca temizlenmedi. Fiziksel boot/fault/recovery,
gerçek zamanlılık veya flight qualification sonucu değildir.
