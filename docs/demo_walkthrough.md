# Tekrarlanabilir geliştirme demosu

2026-10-18. Projeyi açıklamak ve mevcut araçlarla yeniden kontrol etmek için
kısa akış. Uçuş yeterliliği, temiz makine kurulumu veya bütün roadmap'in bittiği
iddiası değildir. [Güncel kapanış listesi](release_checklist.md).

**Sonuç özeti:**17 Ekim'de normal D36E80… imajda aktif NOMINAL gözlemi3600.2 s/
120 rapor/720 NOMINAL PASS; ardından STOP/SAFE ve arşiv EMPTY doğrulandı.3554
sentetik64-byte payload kaydı ve732 HK,867 çift export'ta korundu. Bunlar fotoğraf
veya mod değişimi değil, tam üretim/drain/kapanış aralığının kayıtlarıdır.
[Koşu, ilk FAIL ve ölçüm sınırları](nominal_soak_target.md). Aşağıdaki eski kart
kontrol noktaları tarihsel kanıttır; yeniden denenmesi gereken senaryo listesi değil.
Belge/build/host testleri için ESP gerekmez; fiziksel demo ise ayrı önkoşulludur.

## Önce neyi gösteriyoruz?

ESP32 gerçek FreeRTOS görevlerini, kuyrukları, mutexleri, TM/TC ve hata
yönetimini çalıştırır. EPS enerjisi, sensörler, actuator ve yörünge modeli
simülasyondur. [Mimari ve görevler](../README.md#runtime-architecture),
[alt sistem izinleri](subsystem_safety.md) ve [wire biçimi](tm_tc_contract.md).

İki bağımsız kanıt vardır: bilgisayardaki güncel kaynakların testleri ve
kartta gerçekten çalışan imajın gözlemi. Diskteki `firmware.bin` hash'i, karttan
okunmuş build kimliği değildir. 16 Ekim'de seçilen üç startup hata/PANIC/ikinci
boot testi geçti; [ilk restore kesintisi tarihsel kayıttadır](startup_target_tests.md).
17 Ekim'de ayrı onayla EPS/FDIR/ADCS/payload zinciri 24.0 s / tek boot içinde,
ardından güncel normal imaj/restore 95.7 s geçti. [Güncel kart kanıtı](mission_lifecycle_target.md).
Bu bounded sonuçlar pre-init uncertainty veya bütün lifecycle hata yollarının
fiziksel testi değildir.

17 Ekim'in sonraki recovery adımında kalıcı özet, SAFE boot kilidi ve `FDIR_RearmBoot`
eklendi; ayrı onayla **11 komutlu MDB ground source/runtime'a kuruldu** ve imajlar
yüklendi. İlk restore paketinin başarısızlığı korunur. Son geçmiş-farkında tam
paket **exit 0 / RESTORE PASS**: PANIC 78.422 s + normal restore 78.797 s,
sayaç4→5→6→7, FAULT/BAD_RESET retention ve her upload boyunca NVS byte eşitliği.
Bu koşunun normal 153DF1… imajında boot7/held0/ready1/history34, recovery stack min
3668 bayt; canlı üç HK/yedi birim/29 INFO ve GET-only TC278→278 geçti. Önceki
aralıklı aktarım/kayıp kök nedeni açık; bu bir uzun çalışma veya bütün recovery
matrisi kanıtı değildir. Eski reset runner engeli yerinde;
[güncel sonuç ve kalan açıklar](recovery_target.md).

Sonraki ayrı onaylı üç-PANIC BOOT_LOOP paketi de **79.610 + 79.734 s / exit 0 /
RESTORE PASS** geçti: sayaç7→8→9→10→11→12, failed0→1→2→3 ve BOOT_LOOP eşiği;
her iki upload'da NVS korunması ve yeni boot onayı. O kontrol noktasında **6B8D11… normal**
imajda boot12/held0/ready1/history98, üç HK/yedi birim/29 INFO ve GET-only
TC349→349 PASS. [Son kart kanıtı ve sınırlı kapsam](recovery_boot_loop_target.md).

Son bakım düzeltmesinden sonra kartta **C0719A… normal** imaj yüklendi: boot13,
history98 ve upload boyunca NVS korundu. Yeni sağlık/yer onayı sonrası SAFE,
ayrı NOMINAL ve üç örnek **91.015 s PASS**; GET-only YAMCS TC414→414 geçti.
Kartta WIPE/enjeksiyon yapılmadı. [Güncel kanıt](maintenance_closeout.md).

## 1. Derleme ve masaüstü testleri

Komutlar FSW proje kökünden PowerShell'de çalıştırılır. Doğrulanan ortam:
PlatformIO Espressif32 6.12.0 / ESP-IDF 5.5.0, Cygwin GCC ve Python.
Bu yollardaki kurulumlar yoksa kendi araç yollarınızı kullanın; başka işletim
sisteminde aynı kurulumun doğrulandığı varsayılmaz.

Firmware derlemek veya test etmek için kart veya özel Wi-Fi header'ı gerekmez.
Eksik header ile Wi-Fi kullanılamaz ama derleme mümkündür. Bağlı demo için
[yerel ayar örneğinden](../config/wifi_credentials.example.h) ignored
`local/wifi_credentials.h` hazırlayın; var olan özel dosyanın üzerine yazmayın.
Gerçek değerler Git'e eklenmez. [Ayar politikası](wifi.md).

```powershell
# Yalnız normal firmware derlemesi; upload/reset yok.
& 'C:\.platformio\penv\Scripts\platformio.exe' run -e esp32dev

# Güncel normal derlemenin metadata/ELF ayrımı dahil bütün masaüstü kontrolleri.
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_checks.ps1 -Compiler C:\cygwin64\bin\gcc.exe -Python C:\.platformio\penv\Scripts\python.exe -BuildMetadata
```

Beklenen kontrol noktası:52 C grubu,53 bağımsız header,535 Python/27 dosya,
22 graf/layout, 34 eski + 15 yeni runner-refusal, 39 yeni workflow/artifact mock ve
59 watchdog + 21 startup + 17 lifecycle geçersiz profil koruması. Yeni recovery grupları 47+12+31 mock süreçtir; BOOT_LOOP enjeksiyonu ayrıca 16 süreçtir. Başlangıç
grubu 58, alt servis güvenliği 183 süreçtir. Bunlar gerçek RTOS scheduling veya
karta fault injection kanıtı değildir. [Test ayrıntıları](../tests/host/README.md).

## 2. Gerçek YAMCS koduyla çevrimdışı profil kontrolü

Mevcut [yamcs-ground projesi](https://github.com/ozgunbkl/yamcs-ground) kullanılır;
yeni bir yer istasyonu oluşturulmaz. JDK 17+, derlenmiş post/preprocessor
sınıfları ve mevcut paketlenmiş YAMCS 5.12.0 jar'ları gerekir.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check_yamcs_mdb.ps1 -GroundProject C:\yamcs-ground\my-mission-control
```

Araç bağımlılık indirmez ve sunucu/link/kart başlatmaz. 30 parametre, 6 container,
15 somut +1 abstract komut profili; gerçek extraction/postprocess ve event
kuralları sınanır. Çıktının sonu offline native PASS olmalıdır. Bu, çalışan
sunucunun aynı MDB'yi yüklediğinin kanıtı değildir; [profil/kurulum ayrımı](yamcs_profile.md).
Güncel repo MDB'sini test etmek kurulu ground MDB'sini değiştirmez. Yeni boot onayı
üç uint32/BE binary ve yedi range/missing-argument reddiyle çevrimdışı doğrulandı.

## 3. Çalışan kart/YAMCS ile komutsuz demo

Önkoşul: kartta bilinen normal firmware, çalışan `myproject/realtime` YAMCS
instance'ı ve eşleşen maintained MDB vardır. Yerel web/API adresi
`http://localhost:8090`; bu, TM UDP 10015 / TC UDP 10025'ten farklıdır.
Başka kurulumun adresini varsaymayın. Bu bölüm servis başlatmaz veya düzeltmez.

```powershell
# Yalnız HTTP GET: üç güncel HK, yedi alan/birim ve bounded olay penceresi.
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/check_yamcs_telemetry.py --url http://localhost:8090

# Gerçek TC değil: yalnız sunucuda BEGIN/PENDING binary üretim önizlemesi.
# --send EKLEMEYİN; pending oturumu/kart arşivi değişmez.
& 'C:\.platformio\penv\Scripts\python.exe' -B tools/yamcs_archive_transfer.py --url http://localhost:8090
```

İlk araç `result: PASS` ve değişmeyen TC sayacı raporlar. İkinci araç
`dry-run generation only` ile bitmelidir. API hatası/stale veri/MDB uyuşmazlığı
PASS değildir; bunu gidermek için otomatik upload/reset/reload yapılmaz.

Rapor saklamak için ilk komuta `--output <YENI_DOSYA.json>` eklenebilir; parent
dizin önceden mevcut olmalı, dosya yeni olmalıdır. `local/` veya `.pio/` yerel
kanıt için kullanılabilir; bu konumlar kalıcı yedek veya yayımlanmış artifact değildir.

YAMCS'te HK voltaj/SOC/gyro ve olay akışını gösterin. Sıcaklık alanındaki sıfır
ölçülmüş sıcaklık sayılmaz. Bu kontrol NOMINAL şartı aramaz; SAFE'te de HK akışı
sürer. HK gelmesi yedi mission task'ının hepsinin sağlıklı olduğunu kanıtlamaz;
watchdog heartbeat kart içi ayrı mekanizmadır.

16 Ekim yerel tekrarında native test ve iki canlı komut yukarıdaki varsayılanlarla
geçti: üç HK/yedi birim, 20 s pencerede 233 eşleşen olay ve 82 tekrar çifti;
duplicate wire yok. TC sayacı GET kontrolünde ve dry-run sonrasında **308** kaldı.
Repo/ground kaynak/runtime MDB SHA256'ları aynıydı. Yerel kanıt dizini:
`.pio/host-tests/demo-preflight-20261016/`. Bu bir temiz klon veya yeni firmware
kart smoke testi değildir; C/header/Python regresyonu bu yalnız-doc adımında
tekrarlanmadı, [son kod kontrol noktası](subsystem_safety.md#2026-10-16-birlikte-boot-safe-ve-owner-recovery-akışı) geçerlidir.

## 4. Normal demodan ayrı işlemler

Firmware upload, serial `--reset`, fault profilleri, gerçek `--send`,
arşiv CONFIRM/CANCEL ve mode komutları bu temel demoda **yoktur**. RAM arşivi
upload/reset/güç kesintisinde kaybolur; resetli işlem öncesi güncel kayıtlar
[doğrulanmış export](yamcs_archive_client.md) ile korunmalı ve işlem onaylanmalıdır.
YAMCS `target/` dizinini temizlemeyin; [veri sahipliği/temizlik riski](yamcs_data_safety.md).

Seçilen uzun testler tamamlandı: ayrı imajda [SAFE kaynak gözlemi](long_run_target.md)
ve son imajda [aktif NOMINAL gözlemi](nominal_soak_target.md). SAFE süresi NOMINAL'e
eklenmez; yeniden çalıştırmak veya yeni senaryo açmak bu demonun önkoşulu değildir.
Bu belge güncellemesi kartı resetlemez veya yeniden test başlatmaz.

Kalan sıra: nihai belgeler/kaynak-only kurulum kontrolleri, credential güvenliği
ve kullanıcının açık onayıyla **en son GitHub yayını**. Tarihsel credential
exposure hâlâ ele alınmalıdır; [yayın kapısı](release_preflight.md).

GitHub'a gerçek credentials, yerel CV/export, `.pio/` ve özel ağa göre derlenmiş
firmware binary eklemeyin; binary de credentials içerebilir. Güncel dosya kontrolü
bütün Git geçmişinin temizliğini veya credential rotasyonunu kanıtlamaz.
Yayın öncesi kontroller ve açık teknik işler [kapanış listesindedir](release_checklist.md).
