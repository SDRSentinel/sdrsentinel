# rf_tools ve kraken_doa Kullanım Kılavuzu
## Pulse Analizi ve KrakenSDR ile Yön Bulma: Kurulum, Arayüz ve Sorun Giderme

**Yazar:** Süleyman Poyraz  
**Proje:** `rf_tools` ve `kraken_doa` Modules for SDR++  
**Doküman Kodu:** SDRPP-RF-DOC-04

---

Bu kılavuz iki modülün bugünkü halini anlatır:

- **`rf_tools`**: Tek bir SDR ile pulse algılama, PDW (pulse descriptor word) çıkarma, PRI ve emitter analizi, pulse içi modülasyon incelemesi ve dışa aktarım.
- **`kraken_doa`**: KrakenSDR ile yön bulma (direction of arrival, DOA). Tek SDR ile çalışmaz.

> Not: SDRPP-RF-DOC-01..03 dokümanları, `rf_tools` içinde bulunan ve simülatörle beslenen eski DOA altyapısını anlatır. O kod kaldırıldı; yön bulma artık yalnızca `kraken_doa` modülündedir.

---

## 1. Derleme ve modüllerin etkinleştirilmesi

Her iki modül de varsayılan olarak derlenir:

| CMake seçeneği | Modül | Bağımlılık |
|---|---|---|
| `OPT_BUILD_RF_TOOLS` | `rf_tools` | fftw3f |
| `OPT_BUILD_KRAKEN_DOA` | `kraken_doa` | eigen3 |

```bash
cmake -B build -G Ninja && ninja -C build
```

Yeni bir yapılandırmayla ilk açılışta iki sayfa da sol menünün altında, kapalı ve etkin olarak gelir ("RF Tools" ve "KrakenSDR DOA"). Başlığın sağındaki ↗ simgesi sayfayı ayrı bir pencereye çıkarır. Pencereyi kapatınca sayfa menüdeki yerine geri döner. Bu tercih `config.json` içindeki `menuElements` listesinde (`detached`) saklanır. Mevcut bir yapılandırmada sayfa ayrı pencerede açılıyorsa pencereyi kapatmak yeterlidir.

---

## 2. rf_tools

### 2.1 Başlangıç

1. Kaynağı seçin (ör. RTL-SDR) ve Play ile akışı başlatın.
2. "RF Tools" sayfasını açın. "Enabled" işaretli olmalıdır.
3. Modül kendi VFO'sunu oluşturur. Bu VFO, ayarlamayı ele geçirmesin diye spektrumdaki tıklamaları yok sayar. Taşımak için önce "Select VFO" ile seçin, sonra waterfall'da incelemek istediğiniz sinyalin üzerine sürükleyin. Analiz yalnızca bu VFO'nun bant genişliği içinde yapılır. Durum satırı merkez frekansı ve bant genişliğini gösterir.

### 2.2 Pulse / PDW sekmesi

**Algılama ayarları:**

- **Auto threshold** açıkken eşik, gürültü tabanının "Above noise (dB)" kadar üstüdür. Gürültü tabanı kayan ortalamayla izlenir ve 3 dB histerezis uygulanır. Bu bir CFAR dedektörü değildir.
- **Auto threshold** kapalıyken eşik "Threshold (dBFS)" ile elle verilir.
- **Min PW / Max PW (µs)**: Bu aralığın dışındaki pulse'lar atılır. Varsayılan Min PW 2 µs'dir. Dedektör ayrıca en az 4 örnek ister. Gürültü kaynaklı sahte pulse görüyorsanız Min PW'yi yükseltin (ör. 20 µs).

**Tablo:** ID, TOA (s), PW (µs), dF (kHz, VFO merkezine göre), tepe ve ortalama güç, SNR ve PRI. Kalibrasyon açıksa güç dBm, kapalıysa dBFS olarak gösterilir. "Clear" tabloyu temizler, "Auto-scroll" yeni satırları takip eder.

**Hardware Power Calibration (gr-pdw):**

- "Enable dBm Calibration" ile dBFS değerleri dBm'e çevrilir.
- Varsayılan tablo, GTRI'nin USRP B210 için ölçtüğü değerlerdir (gr-pdw, GPLv3). Tablo modülün içine gömülüdür.
- "SDR Gain (dB)", "Antenna Port" ve "Channel", ölçüm sırasındaki donanım ayarıyla aynı olmalıdır.
- "Load JSON..." ile kendi tablonuzu yükleyebilirsiniz, "B210 Default Profile" varsayılana döner.
- **Varsayılan tablo yalnızca Ettus USRP B210 için geçerlidir.** RTL-SDR, HackRF, Airspy ya da başka USRP modellerinde dBm değerleri yanlıştır; dBFS kullanın ya da kendi alıcınız için ölçülmüş bir tabloyu "Load JSON..." ile yükleyin. Panel bunu her zaman yazar. Kalibrasyon açıkken varsayılan tablo seçiliyse ve SDR++ kaynağı "USRP" değilse kırmızı bir uyarı da gösterir. B210'da bile cihazdan cihaza fark olduğu için değerler yaklaşıktır.

**Export & Continuous Recording:**

| Format | İçerik |
|---|---|
| CSV (.csv) | PDW tablosu |
| JSON Session (.json) | PDW'ler ve oturum bilgisi |
| SigMF Metadata (.sigmf-meta) | SigMF meta verisi |
| Binary / HDF5 (.pdwbin) | Sürekli kayıt için ikili akış (format sürümü 2) |

- "Start Live Stream Recording" kaydı başlatır, "Export Current Buffer Snapshot" o anki tamponu yazar.
- Dosyalara gerçek örnekleme hızı ve dedektörün zaman tabanı yazılır. Mutlak zaman `time_base + toa` ile hesaplanır.
- Kayıt sırasında örnekleme hızı değişirse kayıt durdurulur; aksi halde iki ayrı zaman tabanı aynı dosyaya karışırdı.

`.pdwbin` dosyasını HDF5'e çevirmek için:

```bash
uv run --no-project --with numpy --with h5py python misc_modules/rf_tools/tools/read_sdrpp_pdw.py recordings/pdw_stream.pdwbin
```

HDF5 dosyası gr-pdw'nin attribute ve dataset adlarını kullanır. Tüm dataset'ler float64'tür ve sonradan büyütülebilir. gr-pdw'de olmayan alanlar `sdrpp` grubundadır. `pulse_power` (tepe güç) ve `noise_power` (pulse öncesi ortalama) gr-pdw'den farklı tanımlıdır; her dataset'in `definition` attribute'u tanımı yazar.

### 2.3 Histograms

PRI modülasyon sınıflandırmasını (sabit, stagger, jitter; ortalama PRI, standart sapma, PRF ve stagger seviyeleri) ve PRI, PW ile frekans kayması (dF) dağılımlarını gösterir. Sınıflandırma için en az 6 pulse gerekir. "Bin count" ile çözünürlük ayarlanır, "Reset" birikimi sıfırlar.

### 2.4 Emitter Tracking

Pulse'ları frekans ve pulse genişliği benzerliğine göre emitter izlerine gruplar. Ayarlar "Deinterleaving Parameters" altındadır:

- **Frequency Tolerance (kHz)** ve **PW Tolerance (µs)**: aynı emitter sayılma toleransları.
- **Timeout (sec)**: güncellenmeyen izin silinme süresi.

"Export Tracks (JSON)" izleri dışa aktarır. Yön bilgisi kullanılmaz.

### 2.5 Signal Inspector (MOP)

Son yakalanan pulse'ı ayrıntılı gösterir:

- Zaman alanı dalga biçimi (Envelope, I, Q)
- Anlık frekans
- I/Q takımyıldızı
- STFT spektrogram
- Chirp matched filter (pulse compression)

"Freeze / Hold Pulse" ekrandaki pulse'ı sabitler, "Unfreeze & Catch Next" bir sonrakini bekler.

Pulse 8 örnekten kısaysa incelenemez. Üst satırdaki "Too short" sayacı kaç pulse'ın bu yüzden atlandığını gösterir. Matched filter için en az 16 örnek gerekir.

**Matched filter sekmesi:** LFM chirp algılanırsa eğim otomatik alınır; "Manual / Force Slope" ile elle verilebilir. Pencere seçenekleri Rectangular, Hamming ve Hann'dır. Sıkıştırma oranı, SNR kazancı ve tepe yan lob oranı (PSLR) gösterilir. Hesap GUI thread'inde, yalnızca sekme açıkken yapılır.

### 2.6 Resonator (S21 magnitude)

S21 genlik izinden −3 dB genişliği ve dip derinliğiyle Q_L, Q_i ve Q_c tahmin eder. Yalnızca genlik kullanılır; faz, kablo gecikmesi, asimetri ve doğrusal olmayan davranış hesaba katılmaz. Sayfa boş başlar ("No trace"); iz iki gerçek yoldan gelir:

**1. Bir VNA ölçümünü içe aktarmak.** NanoVNA gibi bir cihazla ölçülmüş Touchstone `.s2p` dosyası (v1; RI, MA ve DB formatları; Hz–GHz birimleri) ya da `freq_hz, s21_db[, phase_deg]` sütunlu CSV yolunu girip "Load"a basın. `.s2p` dosyasından S21 okunur. `.s1p` dosyası yansıma (S11) olduğu için reddedilir.

**2. SDR++ alıcısıyla skaler |S21| taraması.** Yalnızca alıcı olan bir SDR (RTL-SDR dahil) ve harici bir geniş bant kaynak (gürültü kaynağı ya da tracking generator) gerekir.

1. Kaynağı DUT'un girişine, DUT'un çıkışını SDR'ye bağlayın. SDR++'ta kaynağı başlatın. Kazancı sabitleyin ve AGC'yi kapatın.
2. "Sweep range" bölümünde başlangıç, bitiş ve nokta sayısını; tarama bölümünde RBW, VFO offset, settle ve dwell değerlerini girin. Tahmini süre gösterilir; 10 dakikayı aşan taramalar reddedilir.
3. DUT yerine düz bir kablo (thru) takıp **1. Measure thru**'ya basın.
4. DUT'u takıp **2. Measure DUT**'a basın. Her nokta için S21 = P_DUT − P_thru hesaplanır.
5. İsteğe bağlı: kaynağı kapatıp **3. Measure noise floor**'a basın. Gürültü tabanına 3 dB'den yakın noktalar güvenilmez işaretlenir (turuncu) ve fit dışında bırakılır. Sayfa en düşük dinamik aralığı da gösterir.

Tarama ayrı bir VFO ile yapılır; DC sızıntısından kaçınmak için hedefin altına offset kadar merkez kaydırılır. Tarama bitince merkez frekans ve seçili VFO eski haline döner. Tarama sırasında diğer VFO'lar merkezle birlikte kayar. Başlangıç, bitiş, nokta, RBW ya da offset değişirse thru referansı geçersiz olur. "Export CSV" o anki izi yazar.

**Fit doğrulaması:** Fit, izdeki 1.5 dB'den derin her dipi rezonans sayar. Bu yüzden sonuç, aşağıdaki koşulların hepsi sağlanmadıkça turuncu **[NOT VALIDATED]** etiketiyle, nedeni yazılarak ve grafikteki f0 işareti soluk gösterilir:

- Skaler taramada DUT taramasından önce gürültü tabanı ölçülmüş olmalı. Aksi halde, örneğin kaynak bağlı değilken iki tarama arasında değişen yayın sinyalleri sahte bir "rezonans" üretebilir. Bu, RTL-SDR ile anten bağlıyken yapılan denemede görüldü.
- Skaler taramada thru, kullanılan her noktada gürültü tabanının en az 20 dB üstünde olmalı.
- Skaler taramada dip, gürültü tabanına 3 dB'den fazla yaklaşmamalı. Yaklaşırsa derinlik ve Q_i yalnızca alt sınırdır.
- Her izde −3 dB genişliği içinde en az 3 kullanılabilir nokta olmalı. Değilse nokta sayısını artırın ya da aralığı daraltın.

Harici bir VNA dosyası için yalnızca son koşul aranır. Doğruluk; alıcının doğrusallığı, kazanç kararlılığı, RBW ve kaynağın düzlüğüyle sınırlıdır.

---

## 3. kraken_doa

### 3.1 Gerekenler

- **KrakenSDR**: ortak saatli 5 alıcı ve dahili gürültü kaynağı.
- **Beş özdeş anten**, ölçüsü bilinen dairesel (UCA) ya da doğrusal (ULA) dizi olarak yerleştirilmiş.
- **Linux makine** (Raspberry Pi 4/5 ya da x86_64), üzerinde KrakenSDR'nin DAQ yazılımı `heimdall_daq_fw` çalışır. DAQ, gürültü kaynağıyla kanalların örnek gecikmesini, fazını ve genliğini hizalar. Hizalanmış veriyi şu portlardan sunar:
  - **TCP 5000**: IQ verisi
  - **TCP 5001**: frekans ve kazanç kontrolü
- **Ağ**: varsayılan ayarda (2.4 MS/s, 5 kanal, kompleks float32) veri hızı yaklaşık 768 Mbit/s'dir. İki uç arasında kablolu gigabit Ethernet gerekir, ya da DAQ'de decimation açılmalıdır. Wi-Fi yetmez.

TCP IQ sunucusunu hangi başlatma seçeneğinin açtığı DAQ imajına göre değişir; kendi imajınızda doğrulayın. Protokolün ayrıntıları `misc_modules/kraken_doa/HEIMDALL_PROTOCOL.md` dosyasındadır.

### 3.2 Bağlantı

1. "KrakenSDR DOA" sayfasında **Host** alanına DAQ makinesinin IP adresini, **Data port** alanına 5000 yazın ve **Connect**'e basın.
2. Durum satırı sırasıyla şunları gösterir:
   - **Connecting**
   - **Connected …, waiting for the first Heimdall frame**: TCP bağlantısı kuruldu ama henüz geçerli bir Heimdall başlığı gelmedi.
   - **Receiving Heimdall frames**: en az bir geçerli başlık alındı.
3. 10 saniye içinde geçerli bir başlık gelmezse bağlantı hatayla kapanır. Karşı taraf başka bir şey gönderirse "not a Heimdall IQ server" hatası verilir. macOS'ta 5000 portunu AirPlay Receiver kullanır; DAQ'ye SSH tüneli kurarken yerel port olarak başka bir numara seçin.
4. Frekans ve kazancı SDR++'tan ayarlamak istiyorsanız **Control the DAQ** kutusunu işaretleyin (port 5001):
   - **Frekans**: MHz girip Apply'a basın. DAQ yeniden kalibre olur; bu sürede açı çizilmez ve sayfa "recalibrating" der.
   - **Gain**: DAQ'nin kabul ettiği değerlerden seçilir ve tüm kanallara uygulanır.
   - **AGC**: donanım AGC'sini açar.

### 3.3 DAQ durumu

Bağlıyken sürekli görünür:

- Donanım kimliği, kanal sayısı, merkez frekans, örnekleme hızı, CPI uzunluğu
- Senkron durumu: INIT, sample cal, fractional cal, IQ cal, track lock, track
- `delay_sync` ve `iq_sync` bayrakları, gürültü kaynağı durumu
- Kanal bazında ADC taşması (kırmızı "!") ve IF kazançları
- Alınan, kullanılabilir ve tipine göre kare sayıları
- Kayıp kareler (cpi gaps), burada üzerine yazılan kareler ve Mbit/s cinsinden veri hızı

Kayıp ya da üzerine yazılan kare sayısı artıyorsa bağlantı yetmiyordur. DAQ'de decimation'ı artırın ya da CPI'yi küçültün.

### 3.4 Sinyal ve dizi ayarları

**Signal:**

- **Offset (kHz)**: hedef sinyalin DAQ merkez frekansına göre uzaklığı.
- **Bandwidth (kHz)**: seçilen kanal genişliği.
- **Squelch (dBFS)**: bu seviyenin altında açı hesaplanmaz. Güncel kanal gücü hemen altında gösterilir.

**Array:**

- **Type**: UCA (dairesel) ya da ULA (doğrusal).
- **Elements**: KrakenSDR için 5. DAQ'nin gönderdiği kanal sayısıyla eşleşmezse açı hesaplanmaz.
- **Radius** (UCA) ya da **spacing** (ULA), metre cinsinden. Varsayılan 0.17 m bir başlangıç değeridir; kendi anten yerleşiminizi girin. Sayfa bu ölçüyü o anki frekansta dalga boyu cinsinden gösterir. Komşu antenler arası mesafe yarım dalga boyunu aşarsa uzamsal örtüşme (aliasing) uyarısı verir.
- **Array offset / Compass offset (deg)**: dizinin fiziksel yönelimi ve pusula düzeltmesi. Açı `(360 − θ_tepe + compass_offset) mod 360` ile verilir; bu kural KrakenSDR'nin referans istemcisiyle aynıdır.
- **ULA half**: Forward, Backward ya da Both. Doğrusal dizi önü ve arkayı ayıramaz; Both seçilirse ayna görüntüsü de görünür.
- **Algorithm**: Bartlett, Capon ya da MUSIC. MUSIC için kaynak sayısı (**Sources**) elle verilir; tahmin edilmez.

### 3.5 Bearing bölümü

Açı yalnızca şu koşulların hepsi sağlandığında çizilir:

- Kare bir DATA karesi.
- DAQ hem örnek gecikmesini hem fazı hizalı olarak işaretlemiş (`delay_sync_flag = 1` ve `iq_sync_flag = 1`).
- Kanal sayısı dizi ayarıyla eşleşiyor.
- Sinyal squelch üstünde.
- Sonuç taze: en fazla 5 saniye ya da 3 kare süresi (hangisi büyükse) önce alınmış bir kareden.

Bu durumda pusula grafiği, 0–359° spektrum, açı değeri, tepe kalitesi (dB), MUSIC özdeğerleri ve geçmiş tablosu gösterilir. Aksi halde grafik çizilmez; nedeni tek bir satırda yazılır:

| Durum satırı | Anlamı | Yapılacak |
|---|---|---|
| Not connected | Bağlantı yok | Host/port girin, Connect |
| Waiting for the first Heimdall frame | TCP açık, başlık yok | Doğru porta bağlandığınızı ve DAQ'nin IQ sunucusunun çalıştığını kontrol edin |
| DAQ calibrating (state …) | DAQ henüz hizalanmadı | Bekleyin; frekans değişiminden sonra normaldir |
| Frame not usable (…) | CAL ya da DUMMY kare | Kendiliğinden geçer |
| Channel count mismatch | Dizi elemanı ≠ kanal sayısı | "Elements" değerini düzeltin |
| Below squelch | Sinyal zayıf | Offset ve bandwidth'i kontrol edin ya da squelch'i düşürün |
| Last bearing is stale | Uzun süredir yeni sonuç yok | Bağlantıyı ve sinyali kontrol edin |

### 3.6 Sınırlar

- Modül henüz gerçek bir KrakenSDR ile doğrulanmadı. İlk kullanımda, yeri bilinen bir vericiyle açıları ve pusula yönünü kontrol edin.
- Tek açı konum değildir; konum için en az iki farklı noktadan ölçüm gerekir.
- Kapalı alanda yansımalar tepeyi kaydırabilir ya da bölebilir.
- Tek bir RTL-SDR ile yön bulunamaz.

---

## 4. Testler

```bash
cd build
./misc_modules/rf_tools/test_root_dev_integration
./misc_modules/rf_tools/test_rf_tools_pulse_detector
./misc_modules/kraken_doa/test_kraken_heimdall_header
./misc_modules/kraken_doa/test_kraken_heimdall_client
./misc_modules/kraken_doa/test_kraken_doa_dsp
./misc_modules/kraken_doa/test_kraken_doa_pipeline
uv run --no-project --with numpy --with h5py python ../misc_modules/rf_tools/tests/test_pdw_roundtrip.py
```

**Dış referansa karşı yapılan kontroller:**

- B210 güç kalibrasyonu, GTRI referans değerleriyle karşılaştırılır.
- `kraken_doa` testleri, Heimdall protokolünü belgelenmiş bayt düzenine karşı denetler.

**Kendi içinde tutarlılık testleri:** Geri kalan testler sentetik veriyle kodun tutarlılığını sınar. `kraken_doa` testlerinde kullanılan sahte Heimdall sunucusu yalnızca test dosyalarında bulunur, modüle bağlı değildir.

**Donanım:** Gerçek donanımla (B210, KrakenSDR) yapılmış bir doğrulama yoktur.
