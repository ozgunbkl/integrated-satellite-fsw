# Görev timeout'u için doğrulanmış liveness toparlanması

2026-10-16. Kullanıcı tercihi: kararlı heartbeat doğrulanınca yalnız ilgili
timeout kaydı otomatik çözülsün; global sistem kendiliğinden NOMINAL'e dönmesin.
Kod/masaüstü/normal ESP32 derlemesinden sonra kontrollü iki timeout/toparlanma
döngüsü gerçek kartta geçti; sonunda normal firmware restore edildi.
[Fiziksel kanıt, başarısız önceki koşular ve sınırlar](watchdog_recovery_target.md).

## Kapsam ve koşullar

Owner `WATCHDOG_Check()`; hedef yalnız `(SRC_CDH, 0xC200 + task_id)`, ID 0..6.
Heartbeat görev döngüsünün ilerlediğine işarettir; sensör/servis işleminin
başarılı veya fiziksel donanımın sağlıklı olduğunun kanıtı değildir. Burada
çözülen neden **görev liveness timeout'udur**, genel alt sistem arızası değildir.

1. Mevcut strict `elapsed > 15000 ms` kontrolü timeout'u latch eder ve kilit
   dışında FDIR critical raporu üretir. Çözülmemiş episode tekrar raporlanmaz.
2. Sonraki supervisor check sıfır olmayan revision'ı yakalar. Önceki pet'ler
   kanıt sayılmaz; pencere bu token'dan sonraki ilk gerçek pet ile başlar.
3. En az **30000 ms**, en az **3 farklı-tick pet**, ardışık pet'lerde **en çok
   15000 ms boşluk** ve çözüm anında **en çok 5000 ms son-pet yaşı** gerekir.
   Eşit sınırlar kabul edilir; aynı tick'teki burst pencereyi hızlandıramaz.
4. Aynı revision yeniden doğrulanıp yalnız o critical kayıt çözülür. Yeni rapor
   veya fatal promotion araya girdiyse eski revision reddedilir.
5. Yalnız resolve başarılıysa o task'ın timeout latch'i rearm edilir. Sonraki
   timeout yeni revision/rapor üretir. Son pet, cumulative pet/min-max timing,
   startup epoch, SDK feed/config ve diğer latch'ler sıfırlanmaz.

Pet boşluğu 15 s'yi aşarsa pencere yeniden başlar. Eksik revision, revision
değişimi veya query/resolve failure eski kanıtı geçersiz kılar; yeni token/tam
pencere gerekir. İlk token yakalama ve 5 s supervisor taraması nedeniyle
timeout'tan itibaren kesin 30 s'de çözüm garantisi yoktur. Scheduler/kilit
müsaitliği kararı geciktirir. Task restart veya servis onarımı yapılmaz.

Kaydı olmayan bir latch kendi kendine temizlenmez; başka kod generic resolver
ile watchdog kaydını silmemelidir. Bu kaydın owner'ı yalnız watchdog'dur.

## Korunan engeller

Global SAFE/CRITICAL/EMERGENCY değiştirilmez; NOMINAL/DEGRADED için ayrı yer
komutu ve mevcut deployment/fault kapıları gerekir. Warning ACK pencereyi
kısaltmaz. Diğer task/source/kod, fatal, `0xC20F` state-unavailable, ledger
overflow ve recording uncertainty temizlenmez. Örneğin C200 çözülse de arşiv
CRC nedeni varsa normal çalışma engeli sürer. Aynı kaydın fatal promotion'ı
generic resolver tarafından reddedilir. Yeni komut/MDB/telemetri alanı yoktur.

## Kilit ve yarış politikası

Check yalnız tek supervisor caller'ına aittir; eşzamanlı check API'si değildir.
Timeout raporu dönmeden bu caller sonraki check'te token yakalayamaz.
Pet/check/pencere aynı heartbeat mutex'iyle korunur. Tek nested yol
**heartbeat → MODE**: `FDIR_TryGetFaultRevision` ve
`FDIR_TryResolveCriticalFault` MODE'yi **sıfır tick beklemeyle** bir kez dener.
Busy ise çıktı/veri değişmeden false; retry döngüsü veya yeni uncertainty yok.
Normal 10 ms owner API'leri ve EPS politikası aynıdır. MODE callback'i watchdog
çağırmaz; mission görevleri pet'i diğer kilitleri bıraktıktan sonra gönderir.

Query ile resolve arasındaki yeni rapor exact revision sayesinde korunur.
Resolve'dan hemen sonraki yeni rapor da silinmez. Heartbeat kilidi altında
FDIR report/warning mutex'i, printf/TIME/SDK/I/O yoktur. Bu bounded kaynak ve
host incelemesi gerçek SMP scheduling, priority inheritance veya WCET kanıtı değildir.

## Doğrulama ve maliyet

`tests/host/watchdog/check.c`: önceki 78 + yeni 54 = **132 süreç**.
Yedi task'ta stable-window/relapse/29.99–30.00 s/rollover (28); resume/token/burst/
gap/freshness, query/resolve/rereport/fatal yarışları, diğer task/source/fatal,
taşma/belirsizlik/C20F, ACK/deployment ve mod korunması (26).

`tests/host/fdir_faults/check.c`: önceki 117 + yeni 10 = **127 süreç**.
Try API zero-wait success/busy, unchanged output, null/invalid source, stale
revision, fatal, diğer neden/uncertainty ve init öncesi davranış.

Gerçek watchdog + FDIR + MODE C kaynakları kullanılır; RTOS/SDK/zaman/log
sınırları taklittir. Race enjeksiyonu deterministik actor sırasıdır, fiziksel
iki-core yarış kanıtı değildir. İlk kod/host adımındaki normal build RAM **42884/327680**, flash
**809193/1740800** bayt; önceki başlangıç korumalı build'e göre +88 statik RAM /
+692 flash. Yeni heap/mutex/task yok; SDK config SHA256 aynı.
Firmware SHA256:
`C6AC1511C2047FA020276E5C62C1FF08EAD553C7F3D954DDF54339CD179740C1`.
Ortak kontrol komutu [test rehberindedir](../tests/host/README.md).

İlk kod/host adımında tam 42 C grubu / 47 header / 346 Python (19 dosya), 22 graf/layout ve 9 runner
refusal, 47 negatif compile guard ve güncel normal metadata/ELF kontrolleri
geçti. Native YAMCS sunucu/MDB testi bu değişiklikte yeniden çalıştırılmadı;
komut/MDB/wire kaynağı değiştirilmedi. O ilk adımda donanıma komut veya seri port erişimi yoktu.

Sonraki target hazırlığında 43 C/47 header, 372 Python (20 dosya), 22 graf/layout,
15 runner-refusal ve 59 negatif profil kontrolü geçti. Güncel normal imaj
RAM 42884, flash 809213 bayt; SDK config aynı. Ayrı test profili gerçek heartbeat'i
yalnız bastırır/açar, fault-clear/mode/SDK feed çağırmaz. Kartta iki gerçek
C200/revision 1–2, 40.016/35.000 s resume-clear, SAFE'in korunması, bir NACK/iki
ACK ve üç doğrulanmış yer komutu görüldü. Test 135.8 s; normal baseline/restore
95.8'er saniye, paket exit 0 / RESTORE PASS. Güncel hash ve tam kapsam
[target belgesindedir](watchdog_recovery_target.md); eski build/hash tarihsel kalır.

## Açık kalanlar

TM_GEN dropout → resume → stable clear → ikinci dropout ve ayrı yer komutuyla
normal moda dönüşün bounded kart yolu kapandı. Dört boot'un güncel RAM kopyası
her tekrar öncesi ayrı dosyalarda korundu; upload/reset yalnız açık izinle yapıldı.
Eski kalıcı task-loss/SDK reset kanıtları bu yeni akışın yerine sayılmadı.
Diğer task'ların fiziksel matrisi, gerçek servis/task onarımı, kalıcı
reboot-loop/fatal/uncertainty recovery ve uzun soak ayrı açık işlerdir.
