# Yayın öncesi kontrol — 18 Ekim 2026

## Son yayın kararı

18 Ekim'de kullanıcı mevcut temiz kaynakların mevcut GitHub deposunun `main`
dalına normal commit/push ile gönderilmesini açıkça onayladı. Parola rotasyonu
kullanıcının sorumluluğunda kalır ve henüz doğrulanmış sayılmaz. Bu onay Git
geçmişini temizleme/force-push, router ayarı veya firmware upload yetkisi değildir.
Hedef API üzerinden `ozgunbkl/integrated-satellite-fsw` olarak doğrulandı; yerel
ve uzak `main` başlangıçta aynıydı. Kaynak commit'i
`ba0c3e27bb68ac000230e7921d546e088e0f1f6f`, normal push ile yayımlandı.
GitHub'dan yeni shallow `main` kopyası alındı: commit/tree kimliği ve389 dosyanın
CRLF-normalize içeriği yerel kaynaklarla eşleşti; özel dosyalar bulunmadı.
Bu, yayımlanan kaynakların doğrulamasıdır; bağımsız temiz makine/dependency
kurulumu veya clone üzerinde ikinci firmware derlemesi iddiası değildir.
Aşağıdaki önceki "yayın bekliyor" ifadeleri önceki checkpoint'tir.

İlk teknik preflight ESP/seri port/Yamcs, firmware yükleme, reset, TC veya
commit/push kullanmadı. Sonraki normal commit/push yukarıdaki ayrı onayla yapıldı.
Git geçmişi/router değiştirilmedi; yeni hata senaryosu açılmadı.

## Tamamlanan sonuç değerlendirmesi

[NOMINAL uzun koşusu](nominal_soak_target.md)3600.2 s/120 rapor/720 NOMINAL PASS.
867 veri çifti/4286 kayıt18 Ekim'de yeniden doğrulandı; tamamlanan kanıtlar
`.pio/` dışında ignored `local/evidence/nominal-soak-20261017-2136-final/` altına
hash kontrolüyle alındı. İlk başarısız başlangıç ve eski upload riski korunur.

## Kaynak ve kurulum

- README güncel alt sistemleri, sentetik payload/kamera ayrımını, kaynak/uzun
  çalışma sonuçlarını ve30 kayıtlık RAM arşivinin consumer bağımlılığını açıklar.
- Platform sürümü gerçekten test edilen `espressif32@6.12.0` değerine sabitlendi.
  Bu toolchain seçimini sabitler; fiziksel firmware mantığı/koruma eşikleri
  değiştirilmedi ve yeni imaj karta yüklenmedi.
- Yerel `debug.log` ve kişisel çok-klasörlü VS Code workspace dosyası ignore edildi;
  dosyalar silinmedi. `local/`, `.pio/`, derleme metadata ve Python cache zaten
  ignored. `tests/host/wifi_service/include/wifi_credentials.h` yalnız test fixture'ı
  olup gerçek yerel header değildir; kaynak kopyasında korunmalıdır.
- Yeni build dizini ve özel Wi-Fi header'ı olmadan, Git'e aday mevcut dosyalardan
  hash-doğrulanmış **yerel kaynak snapshot** hazırlanıp build/test kontrolü yapılır.
  Bu işlem aynı bilgisayarın kurulu toolchain/package cache'ini kullanır. Bağımsız
  temiz makine, sıfırdan dependency download veya uzak GitHub clone iddiası değildir.

389 dosyalık kaynak snapshot'ında özel Wi-Fi header'ı olmadan normal build
**263.72 s PASS**:RAM24472,flash357369 bayt. SDK hash'i öncekiyle aynı. Bu ağ
ayarı olmayan ayrı build'dir; bağlantılı D36E80… imajının footprint'i veya kart
uzun test kimliği olarak sunulmaz ve karta yüklenmedi. Yeni metadata/ELF'de57
proje kaynağı/16 component/63 doğrudan header bağı, test hook'larının yokluğu ve
özel proje `local/` include/header'ının bulunmadığı doğrulandı. SDK'nin public
`esp_wifi/include/local` klasörü özel kullanıcı ayarı değildir.

İlk masaüstü denemesi test-formatlama nedeniyle FAIL oldu: uzun checkout yolunda
PowerShell `ValidateSet` kelimesini `ValidateS`/`et` diye iki satıra bölüyordu.
Gerçek argüman reddi çalışıyordu; test yalnız ekran metnine bağlıydı. Mevcut dört
vakada invariant `ParameterArgumentValidationError` ve ilgili parametre kontrolüne
geçildi.34 refusal hem asıl klasörde hem kaynak kopyasında tekrar geçti. Firmware/
runner izin kapısı değişmedi, yeni test senaryosu eklenmedi. İlk log korunur.

Native Yamcs çevrimdışı kontrolü kaynak kopyasında da16 komutla PASS; çalışan
server veya kart kullanılmadı. Tam `run_checks.ps1 -BuildMetadata` tekrarı
**exit0/PASS**:52 C grubu/53 header/535 Python(27 dosya);22 graph-layout,
15+34=49 runner-refusal/39 workflow-artifact ve59+21+17=97 invalid-profile koruması
geçti. Normal ELF/metadata test izolasyonu doğrulandı. İlk FAIL'in aksine bu tam
geçiş ve build logları korunur; tamamlanan çıktı sonradan RUNNING diye sunulmaz.

**Teknik kapanış tamamlandı.** Build/test, özel ayarsız yeni build dizinindeki
kaynak snapshot doğrulamasıdır. Sonraki GitHub shallow clone'un kaynak eşitliği
ayrıca doğrulandı; bu adımlar bağımsız temiz makine kurulumu sayılmaz.

Test edilen snapshot'ın ilk dosya manifest'i ve tool
çıktıları `.pio/release-1018/` altında tutulur. Yukarıdaki tek test-fixture
düzeltmesi ilk manifest'ten sonra hem asıl kaynakta hem snapshot'ta uygulandı;
kapanış manifest'i son durum için ayrıca saklanır. Bu kopya Git
geçmişini taşımaz; henüz commit edilmemiş değişiklikleri de içerir. Dokümantasyon
sonradan kapanış sonucuyla güncellenebilir; üretim kaynak/config farkı ayrıca
kontrol edilmelidir.

Son kanıtlar `local/evidence/release-preflight-20261018/` altında saklanır.
Dokümantasyon kapanışı sonrası snapshot belgeleri güncellenir; üretim/test/araç/
config dosyalarının asıl kaynaklarla hash eşitliği tekrar denetlenir. Yeni
firmware binary yalnız build kanıtıdır, test edilen eski kart imajının yerine
geçirilmez veya yayımlanmaz.

Commit öncesi `main/eps_harvest.c` içindeki altı satır sonu boşluğu temizlendi;
test edilen snapshot ile satır-sonu boşlukları hariç aynı olduğu doğrulandı.
Bu formatlama firmware mantığını veya koruma eşiklerini değiştirmedi. İlk
preflight raporu tarihsel olarak korunur; sonraki yayın onayı/clone kanıtı
`local/evidence/publication-20261018/` altında ayrı saklanır.

## Güvenlik ve yayın kapısı

Mevcut `check_wifi_credentials.ps1` kontrolü: güncel Git'e aday metin dosyalarında
yerel SSID/parola literal eşleşmesi yok, gerçek header ignored. **Historical HEAD
exposure: True.** Bu araç bütün geçmiş/log/artifact veya her secret türü için
tam bir güvenlik sertifikası değildir; firmware binary de credential içerebilir.
389 Git-adayı dosyada ek private-key/token biçim taraması eşleşme0 verdi;
bu sınırlı kontrol eski Wi-Fi bulgusunu veya bütün secret türlerini kapatmaz.

Parola değişimi router sahibince yapılmalı; değiştirilen değer sohbete veya Git'e
yazılmamalı. Rotasyon bu proje kontrolüyle yapılmış/doğrulanmış sayılmaz.
Sonradan geçmiş temizlemek tek başına daha önce görülebilmiş parolayı iptal etmez.
Eski sürümlerdeki exposure'u ele almak için repo sahibiyle ayrı history/yayın
kararı gerekir; otomatik force-push veya geçmiş rewrite yapılmayacak.

Normal GitHub yayını ve389 dosyalık kaynak karşılaştırması tamamlandı. Parola
rotasyonu bu işlemin sonucu olarak doğrulanmış sayılmaz. Gerçek veri, CV, özel header ve credential içeren
firmware paylaşılmayacak. Bir saatlik PASS, eski upload arızasının kök nedenini
kapatmaz; sürümün bilinen sınırı olarak kalır.
