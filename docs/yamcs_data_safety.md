# YAMCS kaynakları ve veri kaybını önleme

2026-10-16. Mevcut yerel kurulum için dosya sahipliği ve temizlik sınırları.
Bu belgeyi hazırlarken sunucu yeniden başlatılmadı, kart resetlenmedi ve veri
silinmedi. GitHub'a yayımlama ve bağımsız temiz kurulum doğrulaması ayrı işlerdir.

## Hangi dosya düzenlenir?

Ground proje kökü bu makinede `C:/yamcs-ground/my-mission-control`.
Aşağıdaki ground yolları bu köke göredir. FSW yolları ise bu depoya göredir.

| Yol | Rol / bakım kuralı |
| --- | --- |
| FSW: `config/yamcs/xtce.xml` | Uydu MDB profilinin bakımı yapılan kaynağı. Profil değişikliği burada yapılır. |
| Ground: `src/main/yamcs/mdb/xtce.xml` | Ground'a kurulmuş kaynak kopyası. FSW profiliyle eşleşmesi gerekir. |
| Ground: `src/main/yamcs/etc/` | Sunucu/instance/link yapılandırma kaynakları. FSW kurulum betiği bunları değiştirmez. |
| Ground: `src/main/java/` ve `pom.xml` | Preprocessor/postprocessor ve Maven bağımlılık/derleme kaynakları; FSW deposunda yeniden oluşturulmaz. |
| Ground: `target/yamcs/mdb/` ve `target/yamcs/etc/` | Üretilmiş çalışma kopyaları. Kalıcı düzenleme yeri değildir; yeniden üretildiğinde üzerine yazılabilir. |
| Ground: `target/classes/` ve `target/bundle-tmp/lib/` | Derlenmiş Java sınıfları ve paketlenmiş kütüphaneler. Native MDB testi mevcut çıktıları kullanır. |
| Ground: `target/yamcs/yamcs-data/` | Bu kurulumun kalıcı yer istasyonu verileri. `target/` altında olsa da atılabilir derleme çıktısı değildir. |
| FSW: `.pio/` | Üretilmiş firmware, masaüstü test programları ve test kanıtları. Kaynak kod değildir; silmeden önce gerekli log/kanıtları ayrıca koru. |
| FSW: `local/archive/` | Bilgisayara indirilmiş kart arşivleri. Git'ten ignore edilir; yeniden derleyerek üretilemez. |

Mevcut ground kaynak ve runtime `yamcs.yaml` dosyalarında `dataDir: yamcs-data`
vardır. Yol çalışma dizinine göre çözülür; bu kurulumda veri dizini
`target/yamcs/yamcs-data/` olarak bulunur. Başka kurulumda aynı yolu varsayma:
etkin `dataDir` ve sunucunun çalışma dizinini birlikte kontrol et.

FSW profilini kurmak için [kurulum betiği](../tools/install_yamcs_mdb.ps1)
varsayılan olarak değişiklik yapmaz. Uygulama, açık `-Apply` ve mevcut hedefin
`-ExpectedCurrentHash` değerini ister; native kontrolden sonra kaynak MDB'yi
yedekleyip kopyalar. Kaynak kurulumu çalışan sunucunun belleğini değiştirmez.
Derleme ve runtime reload ayrı, kontrollü adımlardır. Ayrıntılar:
[YAMCS profili](yamcs_profile.md).

## Birbirinden bağımsız üç veri alanı

- **Kart RAM arşivi:** Reset, USB/güç kesilmesi veya firmware yüklemesiyle kaybolur.
  Yer istasyonundaki veritabanının mevcut olması bütün kart kayıtlarının
  indirildiğini kanıtlamaz.
- **Yer istasyonu veritabanı:** Alınmış telemetri/olay/komut geçmişini tutar;
  sunucunun veri dizininin silinmesiyle kaybolabilir. Kartı resetlemek bu dizini
  silmez; bu dizini silmek de kart RAM'ini temizlemez.
- **Doğrulanmış bilgisayar export'u:** Kart kayıtlarının ayrı dosya kopyasıdır.
  Kart resetinden bağımsız kalır, ancak dosyanın kendisi silinirse kaybolur.
  Git ignore bir yedekleme mekanizması değildir; dosya karta otomatik geri yüklenmez.

## Temizlik ve kontrol işlemlerinin etkisi

Aşağıdaki tablo komut çalıştırma talimatı veya silme izni değildir.

| İşlem | Etki / önce korunması gereken veri |
| --- | --- |
| FSW `pio run -e esp32dev` | Yalnız derleme; karta yükleme/reset yok. |
| FSW `tools/run_checks.ps1` | Masaüstü kontrolleri; `.pio/` çıktıları oluşturur, kartı veya YAMCS sunucusunu çalıştırmaz/resetlemez. |
| Ground `mvn clean` / `mvnw.cmd clean`, `target/` veya `target/yamcs/` silme | Bu yerel yapılandırmada yer istasyonunun kalıcı geçmişini de silebilir. Kaynak MDB'nin yedeği veritabanı yedeği değildir. |
| FSW `.pio/` silme | Derleme/test çıktıları ve burada tutulan ölçüm kanıtları kaybolur; bu dizindeki dosyayı tek yedek sayma. |
| FSW `local/` silme veya `git clean -fdx` | Ignore edilmiş export'ları ve yerel ayarları da silebilir. Genel temizlik olarak kullanma. |
| Kart upload/reset/güç kesme | Kart RAM kayıtları, pending aktarım ve per-boot hata durumu kaybolur. Önce güncel kayıtları doğrulanmış export olarak koru. |
| Arşiv `CONFIRM` / `--confirm-clear` | Yalnız doğrulanıp açıkça onaylanan snapshot prefix'ini karttan kaldırır. Rutin derleme veya dosya temizliği değildir. |
| Arşiv `CANCEL` | Aktarım oturumunu kapatır; kayıtları silmez. |

Sunucu verileri korunacaksa temizlemeden önce sunucuyu kontrollü durdurup gerçek
veri dizininin tutarlı yedeğini al ve geri yükleme yolunu doğrula. Çalışan
veritabanının rastgele dosya kopyası doğrulanmış yedek sayılmaz. Veri dizinini
`target/` dışına taşıma veya bir yedeği geri yükleme bu adımda yapılmadı/test edilmedi.

Kart için [doğrulanmış export akışını](yamcs_archive_client.md) kullan; başarıyla
kaydedilen dosyayı ayrıca koru. Tam indirme, CRC/digest, fsync ve geri okuma
kontrolleri tamamlanmadan reset veya silme işlemine geçme. `CONFIRM` yanıtı
kaybolduysa yeni bir oturumla tahmin yürütme; aynı dosyanın onayını protokole
uygun tekrar et. Dosya yedeği, CRC veya fsync fiziksel depolamanın her arızaya
dayanacağını garanti etmez.

## Bu dokümantasyon adımında yapılan kontrol

Depodaki profil, kurulu ground kaynak kopyası ve üretilmiş runtime MDB'nin
SHA256 eşleşmesi salt okunur kontrol edildi. Göreli belge bağlantıları, yerel
yol ayrımı ve kurulum betiğinin değişikliksiz varsayılanı kontrol edildi.
Temizlik, yedekten geri yükleme, yeni firmware veya uzun süreli test yapılmadı.
