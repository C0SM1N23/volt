# VOLT — Fault-Tolerant Software-Defined Vehicle Compute Platform

**Spec tehnic complet — versiunea 2.0 (extinsa)**
Autor: Cosmin Bunea · Status: design freeze pentru Phase 1 · Limbaj: C++23 · Target: Linux/POSIX (+ QNX port), MCU: Zephyr

---

## Cuprins

- §0 TL;DR · de ce versiunea asta e mai buna · KPI-uri
- §1 Goals / Non-goals · §2 Glosar · §3 Vedere de ansamblu
- §4 Principii de arhitectura (D1-D5) · §5 Straturi software · §6 Structura repo
- §7 Build, toolchain, standard de cod · §8 Platform layer · §9 Scheduler real-time
- §10 IPC zero-copy · §11 Stack de comunicatie (CAN-FD, ISO-TP, Ethernet, SOME/IP, DoIP, E2E, SecOC)
- §12 Service middleware · §13 Distributed runtime (SWIM, Raft, lease/fencing, placement, replicare, failover)
- §14 Verificare formala (TLA+) · §15 Safety (HARA, degradare, redundanta, fault manager)
- §16 Diagnostics (UDS, DTC, security access, flashing/OTA) · §17 Cybersecurity · §18 Termic/putere
- §19 Simulare de vehicul · §20 Fault injection · §21 DST + record/replay · §22 Observabilitate
- §23 Testare · §24 CI/CD · §25 Performanta · §26 HIL · §27 QNX + KVM
- §28 Documentatie si traceability · §29 Plan de executie pe faze
- §30 **Demo-uri (D1-D10)** · §31 **Matricea completa functionalitate → test → demo**
- §32 Interviu · §33 Riscuri · §34 Anexe · §35 Ultimul cuvant

**Partea II — completari critice:**
- §36 Catalogul de interfete SOME/IP · §37 Specificatia functionala a fiecarui serviciu
- §38 Lantul de timp end-to-end · §39 Boot, lifecycle, pornire incompleta · §40 Gateway CAN↔Ethernet
- §41 Starea sigura fizica (fail-silent vs fail-active) · §42 Model de erori, threading, IO blocant
- §43 Limitari cunoscute · §44 Licentiere · §45 Lista de verificare finala

**Partea III — extinderi de maxima valoare:**
- §46 Monitorizare pe 3 niveluri (E-Gas) · §47 **Nivelul 3: monitor independent pe Uno R3 + calea de oprire**
- §48 Mixed-criticality si matricea de izolare · §49 Determinism pe retea (software) · §50 WCET static + sinteza de orar cu SMT
- §51 PKI, UDS 0x29, cripto hibrida, fault injection fizic · §52 XCP + A2L · §53 FMI/FMU + ROS 2
- §54 CBMC, FMEDA, FTA generata · §55 ADAS cu senzori reali · §56 API in stil `ara::com` + manifeste
- §57 Demo-uri suplimentare (D11-D15) · §58 Bullet-uri de CV suplimentare

---

## 0. TL;DR — ce e VOLT in trei propozitii

VOLT e o platforma distribuita de compute automotive scrisa de la zero in C++23, care ruleaza functii de vehicul (brake control, sensor fusion, vehicle dynamics, diagnostics) pe 2-4 noduri fizice separate, legate prin CAN-FD si Automotive Ethernet.

Cand un nod moare, cand reteaua pierde 40% din pachete, cand un senzor incepe sa minta sau cand un task depaseste deadline-ul, platforma **detecteaza, izoleaza, migreaza si continua sa controleze fizic un actuator real** — cu un failover masurat in milisecunde si demonstrabil live.

Firul rosu al intregului proiect: **"pot sa omor un compute node in fata ta, si mana de frana continua sa functioneze, iar eu iti arat numarul exact de milisecunde in care s-a intamplat asta si de ce numarul ala e sub bugetul de siguranta."**

---

## 0.1 De ce versiunea asta e mai buna decat ideea initiala

Ideea initiala era deja solida. Ce am adaugat si de ce:

| Adaugat | De ce aduce plus valoare |
|---|---|
| **Model de executie determinist (actor + injected environment)** | Un singur design decision din care ies gratis: state replication, record/replay, deterministic simulation testing si debugging time-travel. Transforma proiectul din "colectie de module" in "arhitectura cu o idee". |
| **Deterministic Simulation Testing (stil FoundationDB / TigerBeetle)** | Rulezi tot sistemul distribuit intr-un singur thread cu ceas virtual si RNG cu seed. Injectezi 10.000 de scenarii de fault pe noapte in CI. Orice bug e reproductibil din seed. Foarte putini oameni au asta pe CV, si e exact tipul de rigurozitate pe care il apreciaza rolurile de safety. |
| **Record / Replay + time-travel debugging** | Poti reproduce offline, ciclu cu ciclu, exact ce a facut sistemul in demo-ul de la interviu. Se leaga direct de experienta ta de verificare RTL. |
| **Split-brain prevention cu fencing tokens verificate in MCU** | Demo-ul killer: tai reteaua, ambele noduri se cred primary, actuatorul fizic refuza comanda cu epoch vechi. E o problema reala de distributed systems rezolvata cu un mecanism real de safety. |
| **gPTP-lite (time sync distribuit) + scheduling time-triggered** | Un ceas global sub-milisecunda intre noduri. Fara el, "failover in 3.7 ms" nu e o cifra masurabila serios. Cu el, ai si baza pentru executie time-triggered, ca in platformele reale. |
| **Compatibilitate pe fir cu SOME/IP real + DBC real + ISO-TP real** | Deschizi Wireshark in demo si framele tale se decodeaza cu dissectorul standard. Rulezi `candump` / `cantools` peste busul tau. Testezi impotriva `vsomeip` si a modulului kernel `can-isotp`. Credibilitate instant: nu e "SOME/IP-like", e interoperabil. |
| **Model checking in TLA+ pentru protocolul de failover** | Demonstrezi ca invariantul "cel mult un primary" tine sub partitii de retea, nu doar ca "a mers la mine". Rar la un student, foarte greu de contestat la interviu. |
| **SecOC (AES-CMAC truncat + freshness manager) + E2E Profile** | Nume de specificatii reale AUTOSAR, implementate concret, nu "am pus un CRC". |
| **UDS flashing 0x34/0x36/0x37 + OTA A/B cu rollback** | "Software-defined vehicle" fara update mechanism e incomplet. Adaugi si un update campaign manager cu rollback automat la boot failure. |
| **Freedom from interference via cgroups v2 + PREEMPT_RT** | Concept ISO 26262 real (partitionare de resurse), demonstrat cu masuratori: task-ul de brake isi tine deadline-ul in timp ce alt nod e la 100% CPU si aloca memorie ca un nebun. |
| **Traceability generata automat din cod** | Un tool care scaneaza `// @satisfies REQ-SAF-012` si numele testelor si genereaza matricea REQ -> design -> cod -> test -> rezultat. Asta e ASPICE mindset demonstrat, nu declarat. |
| **Dashboard web + export Perfetto** | Wow factor vizual. Timeline-urile de scheduling se deschid in Perfetto UI ca la Android/Chrome. |
| **Switch managed real (VLAN + port mirroring + rate limit)** | Faci congestie si prioritizare reala pe fir, nu simulata in software. Costa ~90 RON si iti da o sectiune intreaga de masuratori credibile. |

Nu am scos nimic din scopul initial. Toate cele 36 de sectiuni originale exista aici, extinse.

---

## 0.2 KPI-uri — cum arata "gata si bun"

Proiectul nu e "gata" cand compileaza. E gata cand tabelul asta e completat cu numere masurate pe hardware real si reproductibile de oricine cloneaza repo-ul.

| # | Metrica | Tinta | Cum se masoara |
|---|---|---|---|
| K1 | Jitter activare task 1 ms (P99) | < 100 µs | histograma HDR, 1h soak, PREEMPT_RT + isolcpus |
| K2 | Deadline miss rate task safety-critical | 0 in 3.6M activari | contor in scheduler |
| K3 | Latenta IPC shared-memory one-way (P50/P99) | < 2 µs / < 8 µs | benchmark ping-pong, TSC |
| K4 | Latenta RPC SOME/IP intra-nod (P99) | < 250 µs | trace end-to-end |
| K5 | Latenta app-to-app, de la `publish()` in BrakeControl la iesirea fizica a actuatorului (P99) | < 3 ms pe UART 1 Mbit, < 8 ms pe WiFi | timestamp PC + contor 80 MHz pe ESP32, corelate prin puls comun |
| K6 | Detectie caderea unui nod | < 15 ms | SWIM (probe 3 ms + indirect) pe Ethernet **si** heartbeat CAN 0x400 @ 5 ms, in paralel |
| K7 | Failover complet (detectie -> comanda valida de pe noul primary) | < 25 ms | timestamp global gPTP |
| K8 | Buget FTTI respectat pentru SG-01 | 25 ms << 100 ms | analiza + masuratoare |
| K9 | Intrerupere in comanda actuatorului la failover | < 2 cicluri de control (2 ms) | log actuator MCU |
| K10 | Alocari dinamice pe calea safety in steady state | exact 0 | malloc hook + `no_alloc_scope` |
| K11 | Startup boot -> RUNNING | < 800 ms | trace lifecycle |
| K12 | Overhead tracing activat | < 2% CPU | benchmark A/B |
| K13 | Seeds de simulare determinista rulate/noapte in CI | > 10.000 | CI artifact |
| K14 | Replay bit-exact al unei sesiuni inregistrate | hash de stare identic 100% | `volt-replay --verify` |
| K15 | Coverage linii pe `platform/`, `safety/`, `communication/` | > 85% | gcov/llvm-cov gate in CI |
| K16 | Invariant `AtMostOnePrimary` verificat formal | TLC pass, 3 noduri, partitii | model TLA+ in CI |
| K17 | Frame-uri decodate corect de Wireshark standard | 100% | pcap in repo |
| K18 | Degradare controlata la 40% packet loss | ramane OPERATIONAL, DTC corect | campanie fault injection |
| K19 | Sincronizare de timp intre noduri (timestamping software) | offset RMS < 50 µs intre noduri Linux, < 200 µs catre MCU-uri | verificare independenta cu PPS improvizat pe GPIO (ESP32 → Uno R3) |
| K20 | Prioritizare + shaper time-aware in software: latenta clasei de control la 100% congestie pe clasele inferioare | crestere P99 < 15% (fata de cateva ordine de marime fara prioritizare) | `tc` + generator local de trafic |
| K21 | Nivelul 3: de la detectarea unei comenzi invalide la taierea liniei ENABLE | < 300 µs (reactie GPIO pe AVR), < 5 ms pentru esec de question/answer | contor de 80 MHz pe ESP32 (rezolutie ~12,5 ns) |
| K22 | WCET: bound static (LLVM IR + model de timp) vs. maxim observat | raport < 2,5 si **0 subestimari** in 10^8 executii | `volt-wcet` + soak |
| K23 | Izolare de resurse: interferenta asupra jitterului RT cu `stress-ng` la 100% CPU+memorie in paralel | < 5% in configuratiile cu cgroups si KVM | matricea de interferenta din §48 |
| K24 | XCP DAQ: 32 de semnale la 1 kHz catre un tool extern | 0 pierderi in 10 min | `pyxcp` (open source, gratuit) |
| K25 | Verificare formala la nivel de cod (CBMC) pe functiile critice | 0 asertiuni violate, 0 UB | CI |
| K26 | FMEDA: acoperire de diagnostic calculata pentru calea de frana | SPFM ≥ 90% (ilustrativ) | `tools/fmeda` |
| K27 | ADAS cu senzor real: latenta camera → cerere de deceleratie | P99 < 120 ms | trace end-to-end |
| K28 | Autentificare diagnostica pe certificate (UDS 0x29) | handshake < 200 ms, 0 acceptari invalide | `tests/integration/uds_auth` |

Regula: **fiecare numar din tabel are un script care il regenereaza.** `make kpi` produce `docs/KPI_REPORT.md`.

---

## 1. Goals / Non-goals

### 1.1 Goals
1. O platforma reala, distribuita, care ruleaza pe minim 2 masini fizice distincte + 1 MCU.
2. Determinism verificabil: acelasi input -> aceeasi stare, bit cu bit.
3. Fault tolerance masurabila, nu declarata.
4. Interoperabilitate cu tooling automotive standard (Wireshark, can-utils, cantools, vsomeip, python-udsoncan, doipclient).
5. Cod C++ modern, fara UB, curat sub ASan/UBSan/TSan, cu un subset de reguli AUTOSAR C++14 aplicat automat.
6. Documentatie si traceability de nivel industrial, generate automat unde se poate.
7. Demo-uri repetabile, scriptate, care ruleaza in < 5 minute fiecare.

### 1.2 Non-goals (si de ce)
| Nu facem | Motiv |
|---|---|
| Implementare AUTOSAR Classic/Adaptive completa | E gigantic; rezultatul ar fi "fake AUTOSAR". Facem arhitectura *inspirata*, cu module proprii bine facute. |
| Certificare ISO 26262 | Nu se poate face de un individ. Facem *concepte* + analiza HARA-lite + argumentatie. Documentam explicit ca nu e certificare. |
| Stack TCP/IP propriu | Zero valoare adaugata. Folosim stack-ul Linux si ne concentram pe stratul automotive. |
| **Scrierea unui hypervisor de la zero** | Nu aduce valoare fata de a **folosi** corect izolarea existenta: cgroups v2, network namespaces, KVM si (daca boot-eaza pe laptop) Xen, toate cu masuratori de interferenta (§48). Scrierea unui hypervisor propriu ar consuma efort fara sa adauge nimic demonstrabil in plus. |
| Simulator grafic 3D de vehicul | Distragere. Modelul de dinamica e matematic; vizualizarea e 2D in dashboard. |
| 40 de protocoale superficial | Mai bine 6 protocoale corecte si interoperabile. |

### 1.3 Anti-goal principal
**Nimic in repo nu are voie sa fie "demonstrativ".** Daca un modul nu e testat, masurat si folosit de altcineva din sistem, nu intra pe main. Un README care promite mai mult decat livreaza codul e cel mai rapid mod de a pierde un interviu.

---

## 2. Glosar

| Termen | Sens in VOLT |
|---|---|
| **Node** | O unitate de calcul care ruleaza un `volt-runtime`: hostul, o masina virtuala KVM sau un network namespace dedicat. |
| **Runtime** | Procesul supervizor de pe un node: porneste servicii, monitorizeaza, comunica in cluster. |
| **Service** | Unitate logica de functionalitate (BrakeControl, SensorFusion). Ruleaza intr-un proces izolat sau intr-un thread dedicat, in functie de criticality. |
| **Actor** | Modelul de executie al unui service: single-threaded, event-driven, determinist. |
| **Environment** | Interfata prin care un actor atinge lumea (timp, IO, RNG, log). Are 2 implementari: `RealEnvironment`, `SimEnvironment`. |
| **Epoch / Fencing token** | Numar monoton crescator asociat proprietatii asupra unui actuator. Previne split-brain. |
| **FTTI** | Fault Tolerant Time Interval — cat timp are sistemul de la aparitia unui fault pana trebuie sa fie in safe state. |
| **DTC** | Diagnostic Trouble Code. |
| **E2E** | End-to-End protection: CRC + alive counter + data ID peste un mesaj. |
| **SecOC** | Secure Onboard Communication: MAC truncat + freshness value. |
| **SIL / HIL** | Software-in-the-Loop / Hardware-in-the-Loop. |
| **DST** | Deterministic Simulation Testing. |

---

## 3. Vedere de ansamblu a sistemului

### 3.1 Topologie fizica (deployment tinta, cu hardware-ul pe care il ai deja)

**Constrangere de proiectare asumata: buget zero.** Tot ce urmeaza foloseste doar ce ai deja in casa: un laptop cu Linux, **ESP32**, **Arduino Uno R4 WiFi**, **Arduino Uno R3** si cateva fire de legatura. Nu se cumpara nimic si nu se imprumuta nimic. Constrangerea asta nu reduce proiectul — schimba doar mediul fizic, si de fapt adauga o poveste buna: *am proiectat platforma astfel incat stratul de transport si cel de actuare sa fie interschimbabile, si am demonstrat-o rulandu-le pe hardware complet diferit de cel automotive*.

```
        ┌──────────────────────── LAPTOP (Linux PREEMPT_RT) ────────────────────────┐
        │                                                                            │
        │  NODE A (host)         NODE B (VM KVM sau netns)     NODE C (netns)         │
        │  Safety domain         ADAS domain                   Gateway + diag         │
        │  BrakeControl*         BrakeControl~ SensorFusion    UDS/DoIP, HMI          │
        │        │                      │                             │              │
        │        └──── vcan0 / vxcan + volt-cantun (tunel CAN) ───────┘              │
        │        └──── veth + VLAN + tc netem (retea reala, izolata) ────┘           │
        └───────────┬──────────────────────────────┬─────────────────────────────────┘
                    │ USB-serial (1 Mbit)          │ WiFi (UDP, SOME/IP real)
                    │                              │
        ┌───────────▼──────────┐        ┌──────────▼───────────┐
        │ ARDUINO UNO R4 WiFi  │        │ ESP32                │
        │ = ACTUATOR ECU       │        │ = SAFETY MONITOR     │
        │ - verifica E2E/MAC   │◄──────►│   (Nivel 2, E-Gas)   │
        │ - verifica epoch     │  GPIO  │ - recalculeaza       │
        │ - matrice LED 12×8   │ ENABLE │   comanda permisa    │
        │   = presiune frana   │  (fir) │ - question/answer    │
        │ - PWM = comanda      │        │ - detine linia de    │
        │   analogica          │        │   ENABLE (shutdown)  │
        └───────┬──────────────┘        └──────────┬───────────┘
                │ fir analogic (PWM → A0)          │ GPIO 3.3 V
                │                                   │
        ┌───────▼──────────────┐
        │ ARDUINO UNO R3 (AVR) │
        │ = NIVEL 3 E-Gas      │
        │ - question/answer    │
        │   catre ESP32        │
        │ - DETINE linia       │
        │   ENABLE             │
        │ - watchdog hardware  │
        │ - cod minimal,       │
        │   revizuibil integral│
        └──────────────────────┘

Legaturile fizice (6 fire in total):

```
Uno R4  PWM out  ──┬──────────────► Uno R4  A0   (readback propriu, diagnoza de etaj de iesire)
                   └──────────────► ESP32   ADC  (canal de senzor independent → cross-check)
ESP32   GPIO     ──────────────────► Uno R3  D2   (raspuns la challenge / heartbeat Nivel 2)
Uno R3  D3       ──────────────────► ESP32   GPIO (challenge Nivel 3 → Nivel 2)
Uno R3  D4       ──────────────────► Uno R4  D7   (linia ENABLE — calea de oprire)
GND     ─────────────────────────── GND comun pentru toate trei placile
```

**Ce e real in montajul asta**, si de ce fiecare inlocuire e legitima:

| Element automotive | Inlocuit cu | De ce ramane valid |
|---|---|---|
| 3 compute nodes fizice | 1 host + VM KVM + network namespaces, fiecare cu CPU-uri si cgroups proprii | partitiile de resurse, partitiile de retea (`ip netns` + `tc netem`), caderile de nod si testele de quorum sunt **identice** ca semantica; izolarea e mai slaba, si o spui |
| Bus CAN-FD fizic | `vcan0`/`vxcan` pe Linux + tunel `volt-cantun` (frame CAN incapsulat in UDP/serial) catre MCU-uri | stiva CAN-FD completa (frame, DBC, ISO-TP, E2E, SecOC, bus-off simulat) e aceeasi si e validata cu `can-utils`/`cantools`; doar stratul fizic difera. In plus, TWAI-ul din ESP32 ruleaza in mod **self-test** pentru a demonstra generarea de frame-uri CAN pe siliciu real |
| Servo / actuator hidraulic | matricea LED 12×8 de pe Uno R4 (bara de presiune) + iesire PWM analogica | e o iesire fizica, observabila si masurabila; poate fi taiata electric de linia ENABLE |
| Encoder de roata | Uno R3 care citeste analogic iesirea PWM a lui R4 | **bucla e inchisa fizic**, prin fir, cu zgomot si cuantizare reale; scoti firul = defect de senzor real |
| Al doilea canal de monitorizare (Nivel 2) | ESP32 — Xtensa dual-core, ESP-IDF/FreeRTOS | independenta reala: alt procesor, alt ceas, alt toolchain, alt cod |
| Al treilea nivel de monitorizare, independent | Uno R3 — AVR ATmega328P, 8 biti, arhitectura complet diferita de ambele celelalte | e cel mai simplu dispozitiv din sistem si are cel mai simplu cod: exact ce cere principiul E-Gas pentru Nivelul 3 |
| Diagnoza etajului de iesire | Uno R4 isi citeste inapoi propria iesire pe A0, iar ESP32 o citeste independent pe ADC-ul lui | readback de actuator + cross-check intre doua ADC-uri diferite = redundanta de senzor 2oo2, reala si fizica |
| Calea de oprire hardware | fir GPIO Uno R3 → pin ENABLE pe Uno R4 | oprirea nu trece prin laptop si nici prin ESP32; e impusa de dispozitivul cel mai simplu |
| Analizor logic | ESP32 cu timer de 80 MHz care marcheaza pe GPIO momentul comenzii si al opririi | rezolutie de ~12,5 ns, suficienta pentru K21 |

**Atentie la nivele de tensiune** (partea in care se ard placi): ESP32 e pe **3,3 V**, Arduino Uno R3 si R4 sunt pe **5 V**.
- ESP32 → intrare Arduino: se poate direct (5 V citeste 3,3 V ca HIGH).
- Arduino → intrare ESP32: **niciodata direct**. Se face un divizor rezistiv (2 rezistente, daca ai in sertar) sau, ca sa nu ai nevoie de nimic, se foloseste **modul open-drain**: pinul Arduino e configurat `INPUT` pentru HIGH (linia e trasa sus de pull-up-ul intern de 3,3 V al ESP32) si `OUTPUT LOW` pentru LOW. Asa linia nu urca niciodata peste 3,3 V. Aceeasi tehnica se foloseste si pe linia ENABLE.
- Linia analogica PWM (5 V) catre ADC-ul ESP32 (max 3,3 V, si neliniar peste ~2,5 V): se limiteaza amplitudinea in software, capand PWM-ul la ~60% din perioada si documentand scalarea; sau, daca ai doua rezistente, un divizor 2:1.
- Masa comuna intre toate placile e obligatorie, altfel citirile analogice sunt zgomot.

Toate astea intra in `docs/HIL_WIRING.md` cu schema si cu motivul fiecarei decizii — si sunt exact genul de detaliu care arata ca ai lucrat efectiv cu hardware, nu doar in simulare.

### 3.2 Fluxul de date principal (bucla de control)

```
  Vehicle Simulator (Node C sau proces separat)
        │  publica @ 1 kHz: wheel speeds, yaw, ax, ay, steering
        ▼
  [ Ethernet / SOME-IP notifications, VLAN 10, prio 6 ]
        ▼
  SensorFusionService (Node B)
        │  filtru complementar + plausibility + E2E check
        ▼
  VehicleDynamicsService (Node A, replicat pe B)
        │  estimare slip, mu, stare vehicul
        ▼
  BrakeControlService (Node A ACTIVE, Node B STANDBY)
        │  ABS/traction logic, 1 ms
        ▼
  [ frame 0x201 BrakeCommand + E2E + SecOC MAC + epoch, prin vcan0 + tunel ]
        ▼
  ARDUINO UNO R4  →  verifica MAC, counter, epoch, linia ENABLE  →  iesire fizica
        │              (matrice LED 12×8 + PWM)
        └─→ ARDUINO UNO R3 citeste analogic iesirea si trimite 0x102 WheelSpeeds inapoi
```

Bucla e inchisa fizic: software-ul comanda o iesire pe o placa, alta placa o citeste analogic si o raporteaza inapoi, iar sistemul reactioneaza. Daca vreodata apar un servo si doua transceivere CAN, se schimba doar backendul de driver — nimic din straturile de deasupra.

---

## 4. Principii de arhitectura (partea cea mai importanta a spec-ului)

Totul in VOLT deriva din cinci decizii. Daca intelegi decizile astea, restul e detaliu.

### D1. Serviciile sunt actori deterministi peste un Environment injectat

Un service **nu are voie** sa apeleze direct `clock_gettime`, `recv`, `rand`, `new`, `printf`. Toate trec prin `Environment`.

```cpp
// platform/include/volt/actor.hpp
namespace volt {

class Environment {
public:
  virtual ~Environment() = default;

  // timp
  virtual Timestamp now() const noexcept = 0;            // ceas global (gPTP-disciplined)
  virtual Timestamp mono() const noexcept = 0;           // ceas monoton local
  virtual TimerId   set_timer(Duration d, TimerTag) = 0;
  virtual void      cancel_timer(TimerId) noexcept = 0;

  // comunicare
  virtual void      publish(TopicId, PayloadView) = 0;
  virtual RequestId call(ServiceId, MethodId, PayloadView, Duration timeout) = 0;
  virtual void      respond(RequestId, PayloadView) = 0;

  // nedeterminism controlat
  virtual uint64_t  random() noexcept = 0;

  // observabilitate
  virtual void      log(Level, std::string_view, LogArgs) noexcept = 0;
  virtual void      trace(TraceEventId, uint64_t arg) noexcept = 0;

  // memorie (pool-uri pre-alocate, fara heap)
  virtual Allocator& allocator() noexcept = 0;
};

class IActor {
public:
  virtual ~IActor() = default;
  virtual void on_start(Environment&) = 0;
  virtual void on_message(const Message&, Environment&) = 0;
  virtual void on_timer(TimerId, TimerTag, Environment&) = 0;
  virtual void on_stop(Environment&) noexcept = 0;

  // pentru replicare, checkpoint, replay verification
  virtual void  serialize(StateWriter&) const = 0;
  virtual void  deserialize(StateReader&) = 0;
  virtual Hash  state_hash() const noexcept = 0;
};

} // namespace volt
```

**Ce castigi din regula asta:**

| Feature | Cum cade gratis din D1 |
|---|---|
| Deterministic Simulation Testing | Injectezi `SimEnvironment` cu ceas virtual + RNG cu seed. Rulezi tot clusterul intr-un thread. |
| Record / Replay | Inregistrezi tot ce *intra* prin `Environment`. La replay redai aceleasi intrari. |
| State replication | `serialize()` + jurnal de mesaje = state machine replication clasica. |
| Detectie de divergenta | `state_hash()` la fiecare tick, comparat intre ACTIVE si STANDBY. |
| Testare unitara fara mock-uri urate | Environment-ul *e* mock-ul, si e acelasi in toate testele. |
| Fault injection | `SimEnvironment` poate intarzia, dublica, pierde sau corupe orice mesaj. |

Enforcement: un test in CI care face `nm --undefined-only` pe obiectele din `services/` si esueaza daca apar simboluri interzise (`clock_gettime`, `malloc`, `socket`, `rand`, `printf`, ...). Lista in `tools/forbidden_symbols.txt`.

### D2. Doua planuri separate: control plane si data plane

- **Control plane**: membership, elections, orchestrare, config, diagnostics. Poate aloca, poate folosi TCP, tolereaza latente de ordinul ms.
- **Data plane**: bucla de control. Zero alocari, zero locks blocante, zero syscalls in calea critica (shared memory + spinning controlat / `futex` doar la wake-up). Deadline-uri de ordinul sutelor de µs.

Regula scrisa in `DESIGN.md`: *nimic din control plane nu poate bloca data plane*. Verificata cu TSan + un test de interferenta (K1 sub load de control plane).

### D3. Un singur mecanism de timp: timpul global

Toate timestamp-urile din sistem (loguri, trace, mesaje, DTC snapshots) sunt in `volt::Timestamp` = nanosecunde de la boot-ul clusterului, disciplinate de gPTP-lite. Fara asta, un "failover in 3.7 ms" masurat cu doua ceasuri diferite e o minciuna. Cu asta, poti pune pe acelasi timeline evenimente de pe 3 masini si un MCU.

### D4. Failure detection separat de failure decision

Detectorul (SWIM gossip) spune doar "nodul B nu raspunde". Decizia ("preia BrakeControl pe A cu epoch 42") apartine unui mecanism cu quorum si lease. Motivul: un detector nu poate fi si perfect si rapid; separi ca sa poti face detectorul agresiv (rapid, cu false pozitive) si decizia conservatoare (safe, cu fencing).

### D5. Safety inaintea disponibilitatii, intotdeauna

Cand sistemul nu poate demonstra ca e safe, merge in safe state, chiar daca ar putea "probabil" continua. Fiecare tranzitie de degradare are un criteriu explicit si un DTC.

---

## 5. Straturi software

```
┌──────────────────────────────────────────────────────────────┐
│ APPLICATIONS                                                 │
│  BrakeControl · TractionControl · SteeringAssist · ADAS      │
│  SensorFusion · VehicleDynamics · Diagnostics · HMI          │
├──────────────────────────────────────────────────────────────┤
│ SERVICE MIDDLEWARE                                           │
│  Service Registry · Discovery (SOME/IP-SD) · RPC · Pub/Sub   │
│  QoS · Versioning · Backpressure · Health · Failover client  │
├──────────────────────────────────────────────────────────────┤
│ DISTRIBUTED RUNTIME                                          │
│  Membership (SWIM) · Config store (Raft) · Leases + Fencing  │
│  Placement/Orchestration · State replication · Recovery      │
├──────────────────────────────────────────────────────────────┤
│ REAL-TIME RUNTIME                                            │
│  Scheduler (TT/RM/EDF) · Deadline monitor · Watchdog         │
│  Lifecycle · Health · Thermal/Power · Memory manager         │
├──────────────────────────────────────────────────────────────┤
│ COMMUNICATION                                                │
│  CAN-FD · ISO-TP · Ethernet (UDP/TCP/VLAN/multicast)         │
│  SOME/IP · SOME/IP-SD · DoIP · gPTP-lite · E2E · SecOC       │
├──────────────────────────────────────────────────────────────┤
│ PLATFORM ABSTRACTION (PAL)                                   │
│  Time · Threads · Process · Shm · Sockets · Files · NVM      │
│  Impl: Linux/POSIX  ·  QNX (port)  ·  Sim (deterministic)    │
├──────────────────────────────────────────────────────────────┤
│ HOST OS: Linux PREEMPT_RT (primar) · QNX 8 SDP (port)        │
└──────────────────────────────────────────────────────────────┘
```

PAL are trei implementari si **acelasi test suite ruleaza peste toate trei**. Asta e argumentul de portabilitate, si e verificabil.

---

## 6. Structura repo-ului

```
volt/
├── CMakeLists.txt
├── CMakePresets.json              # dev, rt, asan, ubsan, tsan, release, sim, cross-aarch64, qnx
├── vcpkg.json / conanfile.txt     # doar deps externe minime
├── .clang-format .clang-tidy      # subset AUTOSAR C++14 / MISRA C++
├── .github/workflows/             # ci.yml, nightly-dst.yml, fuzz.yml, perf.yml, hil.yml
│
├── apps/
│   ├── volt-runtime/              # supervizorul unui node
│   ├── volt-sim/                  # vehicle simulator standalone
│   ├── volt-diag/                 # tester UDS/DoIP (CLI)
│   ├── volt-monitor/              # TUI dashboard (ftxui)
│   ├── volt-web/                  # bridge WebSocket + SPA React
│   ├── volt-inject/               # CLI de fault injection
│   ├── volt-replay/               # replay + time-travel debugger
│   └── volt-dst/                  # deterministic simulation test runner
│
├── platform/
│   ├── pal/                       # posix/, qnx/, sim/  (+ teste comune)
│   ├── core/                      # types, expected, span helpers, hash, endian
│   ├── time/                      # clock, timers, gPTP-lite client/server
│   ├── memory/                    # pools, arenas, bounded queues, no_alloc_scope
│   ├── ipc/                       # shm transport, SPSC/MPSC rings, seqlock, UDS, mq
│   ├── sched/                     # scheduler, RTA analysis, deadline monitor
│   ├── lifecycle/                 # startup graph, states, dependency resolver
│   ├── health/                    # heartbeat, resource sampling, reporting
│   ├── watchdog/                  # sw watchdog + hw watchdog binding
│   ├── log/                       # structured logging, lock-free ring
│   ├── trace/                     # tracing, Perfetto export
│   ├── config/                    # schema, parse, validate, hot-reload
│   └── actor/                     # IActor, Environment, mailbox, dispatcher
│
├── distributed/
│   ├── membership/                # SWIM gossip failure detector
│   ├── consensus/                 # Raft (config + ownership log)
│   ├── lease/                     # leases, epochs, fencing tokens
│   ├── placement/                 # constraint solver pentru plasare servicii
│   ├── replication/               # checkpoint, delta, log shipping, standby
│   └── recovery/                  # failover orchestration
│
├── communication/
│   ├── can/                       # socketcan, frames, dbc parser, codegen, cyclic tx
│   ├── isotp/                      # ISO 15765-2
│   ├── eth/                       # udp, tcp, multicast, vlan, epoll reactor
│   ├── someip/                    # header, serialization, SD, client/server
│   ├── doip/                      # ISO 13400
│   ├── e2e/                       # profile 05 / 11 style
│   ├── secoc/                     # AES-CMAC, freshness manager
│   └── serdes/                    # zero-copy serialization + schema compiler
│
├── diagnostics/
│   ├── uds/                       # server + client, sesiuni, servicii
│   ├── dtc/                       # manager, debounce, aging, snapshots
│   ├── nvm/                       # persistenta, wear, integritate
│   └── flashing/                  # 0x34/36/37, A/B partitions, rollback
│
├── safety/
│   ├── fault_manager/
│   ├── plausibility/
│   ├── redundancy/                # dual channel, voter, comparator
│   ├── degradation/               # state machine, ladder
│   ├── safe_state/
│   └── monitoring/                # program flow monitoring, alive supervision
│
├── security/
│   ├── crypto/                    # wrapper peste mbedTLS/OpenSSL
│   ├── keystore/
│   ├── secure_boot/
│   ├── access_control/            # RBAC pentru diagnostic
│   └── ids/                       # intrusion detection
│
├── services/
│   ├── sensor_fusion/  vehicle_dynamics/  brake_control/
│   ├── traction_control/  steering_assist/  adas_acc/
│   ├── diagnostics_service/  logging_service/  hmi/
│
├── simulation/
│   ├── vehicle/                   # dinamica, model roti, anvelope
│   ├── sensors/                   # zgomot, latenta, drift, moduri de defect
│   ├── actuators/
│   ├── scenarios/                 # YAML/Lua
│   └── faults/                    # motorul de fault injection
│
├── firmware/
│   ├── uno_r4_actuator/          # actuator ECU: E2E+SecOC+epoch, matrice LED, PWM, ENABLE
│   ├── esp32_monitor/            # Nivel 2 E-Gas: model de comanda permisa, Q/A, linia ENABLE
│   ├── uno_r3_l3monitor/         # Nivel 3: question/answer, linia ENABLE, watchdog hardware
│   └── shared/                   # cod comun PC/MCU: CRC, E2E, CMAC, frame, semnale din DBC
│
├── tests/
│   ├── unit/  property/  fuzz/  integration/  dst/  sil/  hil/  perf/  soak/
│
├── formal/
│   └── tla/                       # Failover.tla, Lease.tla, E2E.tla + configs
│
├── tools/
│   ├── dbc2cpp/  did_gen/  traceability/  kpi_report/  perf_compare/
│   └── forbidden_symbols.txt
│
├── config/                        # node_a.yaml, node_b.yaml, cluster.yaml, vehicle.dbc
├── docs/                          # vezi sectiunea 32
└── requirements/                  # REQ-*.md, HARA.md, TARA.md
```

---

## 7. Build, toolchain, standard de cod

### 7.1 Build
- **CMake >= 3.28**, `CMakePresets.json`, targeturi per modul, `volt::` namespace pentru targeturi exportate.
- C++23 (`std::expected`, `std::flat_map`, `std::print` doar in tooling, nu in data plane).
- Compilatoare suportate si testate in CI: **GCC 14**, **Clang 19**. Cross: `aarch64-linux-gnu` pentru RPi.
- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wold-style-cast -Wnon-virtual-dtor -Werror`.
- `-fno-exceptions` **doar** pe data plane? Nu — decizie documentata in ADR-004: pastram exceptiile, dar sunt **interzise pe calea critica** (verificat cu `-fno-exceptions` build variant pentru bibliotecile `platform/` si `services/`, care trebuie sa compileze si asa).
- Erori: `std::expected<T, ErrorCode>` peste tot in data plane; exceptii doar in initializare si tooling.

### 7.2 Dependinte externe (minime, justificate in ADR)
| Dep | Pentru ce | De ce nu scriu eu |
|---|---|---|
| GoogleTest + GoogleBenchmark | teste, micro-benchmarks | standard industrie |
| fmt (sau `std::format`) | formatare loguri | rezolvat |
| mbedTLS sau OpenSSL | AES-CMAC, SHA-256, ECDSA pt secure boot | crypto scrisa de mana = red flag |
| yaml-cpp | config | plictisitor |
| ftxui | TUI dashboard | UI, nu core |
| hdr_histogram | percentile corecte | statistica corecta conteaza |
| rapidcheck | property-based testing | |
| **NIMIC** pentru CAN/SOME/IP/UDS/DoIP/gPTP | astea **sunt** proiectul | |

### 7.3 Coding standard
- `.clang-format` bazat pe LLVM cu 100 coloane.
- `.clang-tidy` cu un subset explicit de reguli AUTOSAR C++14 / MISRA C++ 2023, plus `bugprone-*`, `cert-*`, `concurrency-*`, `performance-*`, `readability-*`.
- Fiecare deviatie de la o regula are un comentariu `// NOLINT(rule) — deviation: DEV-012` si o intrare in `docs/DEVIATIONS.md` cu justificare. Asta e *exact* practica automotive.
- Reguli proprii (in `tools/`): fara `new`/`delete` in `services/` si `safety/`; fara `std::string` pe data plane; fara `#include <iostream>` nicaieri in productie.

### 7.4 Reguli de modern C++ aplicate concret
| Feature | Unde exact in VOLT |
|---|---|
| RAII | `no_alloc_scope`, `PriorityGuard`, `TraceScope`, `ShmMapping` |
| `std::unique_ptr` | ownership de servicii in runtime |
| `std::shared_ptr` | **doar** pentru config immutable partajat (documentat de ce) |
| `std::span` / `string_view` | toate API-urile de payload, zero copii |
| `std::expected` | intoarcerea erorilor pe data plane |
| Concepts | `template<Serializable T>`, `template<Signal S>`, constrangeri pe transport |
| `constexpr` / `consteval` | tabele de CRC, layout de mesaje, ID-uri de servicii, tabela TT |
| Atomics + memory order explicit | ring buffers, seqlock, heartbeat counters |
| Lock-free | SPSC ring (data plane), MPSC pentru loguri |
| Move semantics | loaned samples, transferul de buffere zero-copy |
| Custom allocators | pool per tip de mesaj, arena per ciclu de control |
| `[[nodiscard]]`, `[[likely]]` | peste tot unde conteaza |
| Coroutines | doar in control plane (clientul async din `volt-diag`); interzise pe data plane (alocari ascunse la frame) — regula scrisa in ADR-006 |

---

## 8. Platform layer — specificatii pe module

### 8.1 Model de proces si thread

Trei niveluri de izolare, alese per serviciu prin config:

| Nivel | Cand | Mecanism |
|---|---|---|
| `THREAD` | servicii low-criticality, latenta minima | thread in procesul runtime |
| `PROCESS` | default | `fork+exec`, shm pentru date, UDS pentru control |
| `PARTITION` | safety-critical | proces + cgroup v2 dedicat (cpu.max, memory.max, io) + CPU pinuit + `SCHED_FIFO` |

Config:
```yaml
services:
  - name: BrakeControl
    isolation: PARTITION
    criticality: SAFETY_CRITICAL
    cgroup: { cpu_max: "600000 1000000", memory_max: "64M" }
    cpu_affinity: [2]
    sched: { policy: SCHED_FIFO, priority: 80 }
    period_us: 1000
    deadline_us: 900
    wcet_budget_us: 400
    restart_policy: { max_restarts: 3, window_s: 10, on_exhaustion: DEGRADE }
    replication: { mode: ACTIVE_STANDBY, standby_on: [NodeB], sync_period_us: 5000 }
```

**Freedom from interference** — demonstrabil: cgroup-ul limiteaza CPU si memoria; un serviciu "rau" (`stress-ng` intr-un cgroup vecin) nu poate impinge BrakeControl peste deadline. Test dedicat: `tests/integration/foi_cpu_hog.cpp`, KPI K1 masurat sub load.

### 8.2 Timp — `platform/time`

```cpp
struct Timestamp {           // ns de la epoch-ul clusterului
  int64_t ns;
  constexpr auto operator<=>(const Timestamp&) const = default;
};

class Clock {
public:
  Timestamp global() const noexcept;   // disciplinat gPTP
  Timestamp mono()   const noexcept;   // CLOCK_MONOTONIC_RAW
  int64_t   offset_ns() const noexcept;
  int64_t   drift_ppb() const noexcept;
  bool      synced() const noexcept;   // offset stabil sub prag
};
```

**gPTP-lite** (`platform/time/ptp/`):
- Un `TimeMaster` (ales dupa `priority` + node id, cu fallback la caderea masterului — BMCA simplificat).
- Mesaje `SYNC`, `FOLLOW_UP`, `DELAY_REQ`, `DELAY_RESP` peste UDP multicast pe VLAN 10.
- Timestamping cu `SO_TIMESTAMPING` (software, si hardware daca NIC-ul suporta — verifici cu `ethtool -T`).
- Servo PI pe offset + estimare de drift prin regresie liniara pe ultimele N esantioane.
- Tinta realista: **offset RMS < 50 µs** software-timestamped pe LAN gigabit; < 5 µs cu hw timestamping.
- Expune metrica in dashboard si o folosim ca *precondition* pentru masuratorile de failover: daca `synced() == false`, testele de timing sunt marcate invalide. Onestitatea asta e un plus la interviu.

### 8.3 Memory engineering — `platform/memory`

Componente:
1. `FixedPool<T, N>` — free-list, O(1), fara fragmentare, alocare index-based (poate fi in shm).
2. `Arena` — bump allocator resetat la fiecare ciclu de control.
3. `BoundedQueue<T, N>` — SPSC lock-free (Vyukov / cache-line padded), plus varianta MPSC.
4. `MessagePool` — pool per tip de mesaj cu `loan()` / `release()`, alocat in shm pentru zero-copy.
5. `no_alloc_scope` — RAII care instaleaza hook pe `malloc`/`operator new` (via `__libc_malloc` hook sau simplu override global cu flag thread-local) si:
   - in Debug: `abort()` cu backtrace;
   - in Release: incrementeaza contor + `trace(ALLOC_VIOLATION)` + ridica DTC intern.
6. `AllocationTracker` — per-thread: count, bytes, peak, high-water mark; export in metrics.

```cpp
void BrakeControl::on_timer(TimerId, TimerTag, Environment& env) {
  volt::no_alloc_scope guard;             // K10 enforced la runtime
  auto arena = env.allocator().frame();   // reset automat la iesire
  ...
}
```

Test: soak de 1h cu `--check-no-alloc`, rezultat asteptat 0 violari, publicat in KPI report.

### 8.4 Logging si tracing — `platform/log`, `platform/trace`

**Logging structurat, lock-free:**
- Producatorii scriu inregistrari binare (format ID + argumente) intr-un ring MPSC per-thread; un thread de drenaj le formateaza si le scrie. Costul in calea critica: ~40-80 ns.
- Format binar cu string-uri deduplicate la compile time (`consteval` hash pentru format string, tabela exportata) → loguri mici si rapide. Decodorul e `volt-logdec`.
- Nivele: TRACE/DEBUG/INFO/WARN/ERROR/FATAL; filtrare per modul, la runtime, prin UDS control socket.

**Tracing:**
- Evenimente: `TASK_ACTIVATE`, `TASK_START`, `TASK_END`, `DEADLINE_MISS`, `MSG_TX`, `MSG_RX`, `RPC_BEGIN`, `RPC_END`, `STATE_CHANGE`, `FAULT_RAISED`, `FAILOVER_*`, `ALLOC_VIOLATION`.
- Ring buffer per-CPU, 16 bytes/eveniment, timestamp din `rdtsc` calibrat pe ceasul global.
- **Export in format Perfetto/Chrome trace** → deschizi `ui.perfetto.dev` si vezi timeline-ul distribuit al tuturor nodurilor pe aceeasi axa de timp. Asta e un moment de "wow" real in demo.
- Overhead tintit: K12 < 2%.

### 8.5 Config — `platform/config`
- YAML cu schema si validare stricta (tip, range, dependinte intre campuri). Erori de config = mesaj clar, nu crash.
- `cluster.yaml` descrie noduri, servicii, plasare preferata, constrangeri, retea, VLAN, prioritati, CAN matrix path.
- Hot-reload pentru parametrii de calibrare (non-safety) prin UDS WriteDataByIdentifier + notificare `on_config_change`.
- Parametrii de calibrare au metadate (min, max, unitate, ASIL) si sunt exportati de `tools/calib_export` intr-un fisier stil A2L simplificat (`build/volt.a2l`), folosit de dashboard pentru a genera automat sliderele de calibrare. Verificare: test care compara fiecare parametru declarat cu cel expus prin UDS DID.

### 8.6 Watchdog — `platform/watchdog`
Trei niveluri, toate implementate:
1. **Alive supervision (task-level)**: fiecare task raporteaza un contor la fiecare activare; supervizorul verifica contorul in fereastra `[min, max]` (nu doar "s-a schimbat" — si prea des e un fault).
2. **Deadline supervision**: verificat de scheduler.
3. **Program flow monitoring (logical supervision)**: checkpoint-uri in cod (`VOLT_CHECKPOINT(id)`) verificate impotriva unui graf de flux permis, generat din config. Detecteaza "am sarit peste jumatate din bucla". Concept direct din ISO 26262 / AUTOSAR WdgM — impresioneaza pentru ca putini il implementeaza.
4. **Node watchdog**: runtime-ul face kick la `/dev/watchdog` (softdog in dev, hardware pe RPi). Daca runtime-ul moare, nodul se reseteaza — si asta se vede in demo-ul de failover.

Escaladare:
```
task nu raspunde
  → 1 fereastra ratata: WARN + DTC pending
  → 3 ferestre ratate:  restart serviciu (max 3 in 10 s)
  → restart-uri epuizate: isolate serviciu + DEGRADED
  → serviciu safety-critical indisponibil si fara standby: SAFE STATE
  → runtime blocat: hardware watchdog reset node
```

---

## 9. Scheduler real-time — `platform/sched`

### 9.1 Model de task

```cpp
struct TaskSpec {
  TaskId       id;
  std::string  name;
  Duration     period;          // 0 = sporadic
  Duration     deadline;        // implicit <= period, dar permitem si > period
  Duration     wcet_budget;     // buget declarat, folosit pentru admission control
  Priority     priority;        // 0..99, mapat pe SCHED_FIFO
  Criticality  criticality;     // SAFETY_CRITICAL / HIGH / MEDIUM / LOW / BEST_EFFORT
  CpuSet       affinity;
  SchedClass   klass;           // TIME_TRIGGERED / RATE_MONOTONIC / EDF / SPORADIC
  Duration     offset;          // pentru tabela time-triggered
  OverrunAction on_overrun;     // LOG / KILL_JOB / DEGRADE / SAFE_STATE
};
```

### 9.2 Trei clase de scheduling, implementate toate

1. **TIME_TRIGGERED** — tabela offline de sloturi, executata sincronizat pe ceasul global gPTP. Toate nodurile executa acelasi hiperperiod aliniat la aceeasi origine de timp. Asta iti da determinism distribuit si latente end-to-end previzibile. Generarea tabelei o face un tool offline (`tools/tt_schedule_gen`) care rezolva constrangerile (perioade, precedente, exclusivitati) cu un solver simplu (greedy + backtracking pentru rezultat rapid, **plus** o formulare a aceleiasi probleme in MiniZinc rulata offline pentru a demonstra optimalitatea solutiei gasite; un checker separat valideaza orice tabela produsa de oricare dintre cele doua).
2. **RATE_MONOTONIC** — prioritati statice invers proportionale cu perioada, mapate pe `SCHED_FIFO`.
3. **EDF** — implementat peste `SCHED_DEADLINE` (Linux) pentru un subset; comparat experimental cu RM in `docs/PERFORMANCE.md`. Sectiune de tip "am comparat doua politici si am masurat" = foarte bine la interviu.

### 9.3 Admission control cu analiza de raspuns (RTA)

Nu accepti un task daca setul devine neschedulabil. Implementezi analiza clasica:

```
R_i^{(0)} = C_i
R_i^{(k+1)} = C_i + Σ_{j ∈ hp(i)} ⌈ R_i^{(k)} / T_j ⌉ · C_j
converge → R_i ; schedulabil daca R_i ≤ D_i
```
plus varianta cu blocking factor (protocol de mostenire a prioritatii) si jitter de release.

CLI:
```
$ volt-sched analyze config/node_a.yaml
Task              T(us)   D(us)   C(us)   R(us)   Util    Verdict
BrakeControl       1000     900     400     412   0.400   OK  (margin 488us)
WheelProcessing    5000    4500     610    1034   0.122   OK
VehicleDynamics   10000    9000    1720    2765   0.172   OK
Diagnostics       20000   20000    3210    6104   0.161   OK
Logging          100000  100000    4100   10312   0.041   OK
--------------------------------------------------------------------
Total utilization: 0.896   RM bound (n=5): 0.743   → RM bound depasit,
                                                      dar RTA confirma schedulabilitatea.
```
Faptul ca explici *de ce* utilizarea peste bound-ul Liu&Layland e in regula pentru ca RTA e exact iar bound-ul e suficient dar nu necesar — asta e discutia care te separa de restul candidatilor.

### 9.4 Monitorizare la runtime

Per task se colecteaza, cu HDR histogram:
- **activation jitter** (diferenta fata de momentul teoretic)
- **response time** (activare → terminare)
- **execution time** (start → terminare, cu `CLOCK_THREAD_CPUTIME_ID` pentru CPU pur)
- **deadline misses**, **overruns fata de bugetul WCET**
- **preemptions** (din `/proc/PID/status` nonvoluntary_ctxt_switches sau perf)
- **utilizare CPU** per core

Raport:
```
$ volt-monitor perf --duration 60
Task              Avg      P50      P99      P99.9    Max      Miss  Overrun  Jitter P99
BrakeControl      183us    179us    241us    288us    317us       0        0      41us
WheelProcessing   611us    598us    802us    913us   1.02ms       0        0      63us
VehicleDynamics  1.72ms   1.70ms   2.13ms   2.41ms   2.66ms       0        0      88us
Diagnostics      3.21ms   3.09ms   4.02ms   5.11ms   6.30ms       1        2     140us
```

**WCET instrumentation**: masuratoarea nu e WCET real (nu ai analiza statica), si spui asta explicit. Ce raportezi e "observed maximum execution time" + un factor de siguranta, si documentezi in `docs/PERFORMANCE.md` de ce distinctia conteaza. Onestitatea tehnica = credibilitate.

### 9.5 Overrun handling
```
task depaseste bugetul WCET
   ├─ LOW/BEST_EFFORT     → job abandonat, contor++, DTC pending
   ├─ MEDIUM/HIGH         → job terminat, dar DTC + reducere de functionalitate optionala
   └─ SAFETY_CRITICAL     → nu abandonezi niciodata un job de frana la jumatate;
                            marchezi DEGRADED, verifici cauza, si daca se repeta
                            de N ori → migrare pe alt node sau SAFE STATE
```

---

## 10. IPC si transport zero-copy — `platform/ipc`

### 10.1 Mecanisme implementate si comparate
| Mecanism | Utilizare in VOLT | De ce |
|---|---|---|
| **Shared memory + SPSC ring** | data plane intra-nod | latenta minima |
| **Shared memory + seqlock** | date de tip "sample" (ultima valoare valida) | cititori multipli fara blocare |
| **Unix domain sockets** | control plane intra-nod | simplu, cu credentiale (`SO_PEERCRED`) |
| **POSIX message queues** | comparatie / fallback | benchmark |
| **UDP multicast** | pub/sub inter-nod | scalabil |
| **TCP** | RPC de control, DoIP | fiabil |

Benchmark obligatoriu in README:
```
IPC one-way latency (payload 64B, 1M iteratii, isolcpus, PREEMPT_RT)
mechanism            P50      P99     P99.9     max     throughput
shm SPSC ring       0.42us   1.10us   2.30us   18us     22.1 M msg/s
shm seqlock         0.31us   0.88us   1.90us   12us     31.4 M msg/s
unix domain socket  6.80us  12.40us  24.10us   96us      1.4 M msg/s
POSIX mq            8.20us  15.70us  31.00us  120us      1.1 M msg/s
TCP loopback       19.40us  34.20us  71.00us  310us      0.6 M msg/s
UDP loopback       14.10us  26.80us  55.00us  240us      0.8 M msg/s
```
Plus grafic de distributie (histograma log) generat automat.

### 10.2 Zero-copy: modelul de loan

```cpp
auto pub = env.publisher<WheelSpeeds>(Topic::WheelSpeeds);
auto loan = pub.loan();                 // buffer din pool-ul din shm, fara alocare
loan->fl = 12.4f; loan->fr = 12.5f;
pub.publish(std::move(loan));           // publicarea = mutarea unui index in ring
```
Consumatorul primeste un `Sample<T>` care e o vedere read-only in shm cu refcount atomic; buffer-ul se intoarce in pool cand ultimul cititor il elibereaza. Detectie de "slow consumer": daca pool-ul se epuizeaza, aplici politica QoS (drop-oldest / block / fault).

### 10.3 Serializare — `communication/serdes`
- Format binar propriu, fix-layout, aliniat, little-endian pe fir, cu ID de schema + versiune.
- **Schema compiler** (`tools/serdes_gen`): fisier `.vmsg` → C++ header cu `constexpr` layout, encode/decode, versionare cu campuri optionale.
- Pe intra-nod: zero copy (structura e deja layout-ul de fir).
- Pe SOME/IP: serializare conforma (big-endian, alignment, dynamic length arrays) ca sa fie decodabil de Wireshark.

```
// vehicle_state.vmsg
message VehicleState v2 {
  u64 timestamp_ns;
  f32 speed_mps;
  f32 yaw_rate_rps;
  f32 wheel_speed[4];
  u8  quality;      // @since v2
  e2e { profile: 11, data_id: 0x1A2B }
}
```

---

## 11. Stack de comunicatie automotive

### 11.1 CAN-FD — `communication/can`

**Nivel 0 — driver**: SocketCAN (`PF_CAN`, `CAN_RAW`), cu:
- `vcan0` pentru SIL, `can0` real pentru HIL,
- timestamping hardware (`SIOCSHWTSTAMP` / `SO_TIMESTAMPING`),
- filtre in kernel (`CAN_RAW_FILTER`) ca sa nu treci prin userspace pentru frame-uri irelevante,
- `CAN_RAW_FD_FRAMES` pentru CAN-FD, bitrate 500k arbitration / 2M data,
- error frames (`CAN_ERR_FILTER`) → detectie bus-off, error-passive, error-warning, ack error.

**Nivel 1 — frame layer**: encode/decode, DLC↔lungime pentru FD (0..64B), BRS, ESI, extended ID.

**Nivel 2 — signal layer**: parser DBC complet:
- mesaje, semnale, byte order (Intel/Motorola), scaling (factor/offset), min/max, unitati, value tables, multiplexoare (inclusiv extended multiplexing), atribute, comentarii.
- `tools/dbc2cpp`: DBC → header C++ `constexpr` cu accesori type-safe:
```cpp
// generat din vehicle.dbc
struct Msg_BrakeCommand {            // 0x201, 8 bytes, cycle 10ms
  static constexpr CanId id{0x201};
  Signal<float, 0, 12, ByteOrder::Intel, 0.025f, 0.0f> pressure_bar;
  Signal<uint8_t, 12, 4> mode;
  Signal<uint8_t, 16, 4> alive_counter;
  Signal<uint8_t, 24, 8> crc;
};
```
- Validare incrucisata: acelasi DBC citit de `cantools` (Python) trebuie sa produca aceleasi valori. Test automat care compara encode/decode pentru 100k vectori aleatori. **Asta e o dovada foarte tare de corectitudine.**

**Nivel 3 — communication management**:
- transmisie ciclica cu offset-uri pentru a evita burst-uri (fazare automata),
- transmisie event-triggered cu debounce si rate limit,
- receptie cu timeout supervision per mesaj (deadline monitoring) → DTC la timeout,
- alive counter + CRC per mesaj (E2E),
- gateway routing CAN↔Ethernet cu tabela de rutare configurabila (asta e nodul C),
- bus load calculation si raportare (util pentru demo: "am incarcat busul la 78% si uite ce se intampla").

**Bus-off handling**: masina de stari conforma cu comportamentul controllerului — error active → warning → passive → bus-off → recovery cu backoff, cu DTC si degradare.

**Matricea CAN (extras din `config/vehicle.dbc`)**:

| ID | Nume | Cycle | Sender | Continut |
|---|---|---|---|---|
| 0x100 | VehicleSpeed | 10 ms | VehicleDyn | speed, quality, alive, crc |
| 0x102 | WheelSpeeds | 5 ms | MCU | 4× wheel speed, alive, crc |
| 0x110 | IMU | 5 ms | Sim | ax, ay, yaw rate |
| 0x120 | SteeringAngle | 10 ms | Sim | angle, rate |
| 0x201 | BrakeCommand | 1 ms | BrakeCtrl | pressure/wheel, mode, epoch, alive, crc |
| 0x202 | BrakeCommand_SecOC | 1 ms | BrakeCtrl | MAC truncat 24 biti + FV |
| 0x301 | ECUStatus | 100 ms | toti | state, faults, cpu, temp |
| 0x400 | NodeHeartbeat | 5 ms | toti | node id, epoch, health |
| 0x7E0/0x7E8 | UDS req/resp | on demand | tester/ECU | ISO-TP |

### 11.2 ISO-TP (ISO 15765-2) — `communication/isotp`
Implementare completa, pentru ca fara ea UDS pe CAN nu exista:
- Single Frame, First Frame, Consecutive Frame, Flow Control.
- BS (block size), STmin (inclusiv valorile in µs 0xF1..0xF9), wait frames (FC.WAIT), overflow (FC.OVFLW).
- Timere N_As, N_Ar, N_Bs, N_Br, N_Cs, N_Cr cu valorile din standard si tratarea timeout-urilor.
- Adresare normala si extinsa; suport pentru CAN-FD (payload > 8, `escape sequence` pentru lungimi mari).
- **Validare**: interop cu modulul kernel `can-isotp` (`isotpsend`/`isotprecv`) si cu `python-can-isotp`. Test automat in CI cu `vcan0`.

### 11.3 Automotive Ethernet — `communication/eth`
- Reactor bazat pe `epoll` (edge-triggered), un thread de retea per prioritate de trafic.
- UDP unicast + multicast (IGMP), TCP, `SO_REUSEPORT`, `SO_PRIORITY` mapat pe PCP 802.1p, VLAN tagging (fie prin interfete `vlan`, fie raw sockets cu tag explicit).
- Pe switch-ul managed: VLAN 10 control (prio 6), VLAN 20 diagnostics (prio 2), VLAN 30 logging (prio 0), + port mirroring pentru Wireshark, + rate limiting pentru testele de congestie.
- **TSN-lite**: implementezi un shaper software time-aware (idee 802.1Qbv) pe transmisie: sloturi de timp sincronizate gPTP in care doar clasa de trafic X are voie sa transmita. Nu e TSN hardware, si spui asta, dar demonstrezi conceptul si masori efectul asupra latentei P99 sub congestie. Sectiune foarte tare in `PERFORMANCE.md`.
- Masuratori: throughput, latenta P99 cu si fara prioritizare, efectul congestiei de pe VLAN 30 asupra VLAN 10 (raspunsul corect: aproape zero, si asta demonstrezi).

### 11.4 SOME/IP — `communication/someip`
**Compatibil pe fir**, ca sa fie decodat de Wireshark.

Header (16 bytes):
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-------------------------------+-------------------------------+
|          Service ID           |     Method ID / Event ID      |
+-------------------------------+-------------------------------+
|                            Length                             |
+-------------------------------+-------------------------------+
|          Client ID            |          Session ID           |
+-------+-------+-------+-------+-------------------------------+
| Proto | Iface | MsgTy | RetCd |
+-------+-------+-------+-------+
```
- Message types: REQUEST 0x00, REQUEST_NO_RETURN 0x01, NOTIFICATION 0x02, RESPONSE 0x80, ERROR 0x81, si variantele TP (0x20+) pentru segmentare.
- Return codes: E_OK 0x00, E_NOT_OK 0x01, E_UNKNOWN_SERVICE 0x02, E_UNKNOWN_METHOD 0x03, E_NOT_READY 0x04, E_WRONG_PROTOCOL_VERSION 0x07, E_WRONG_INTERFACE_VERSION 0x08, E_MALFORMED_MESSAGE 0x09.
- **SOME/IP-TP**: segmentare pentru payload > MTU (offset + more segments flag).
- Serializare conforma: big-endian, alignment configurabil, dynamic arrays cu length field, strings cu BOM — suficient cat sa treaca prin dissectorul Wireshark.

**SOME/IP-SD** (service discovery), pe UDP multicast 224.244.224.245:30490:
- Entries: FindService 0x00, OfferService 0x01, StopOffer, SubscribeEventgroup 0x06, SubscribeEventgroupAck 0x07, Nack.
- Options: IPv4 Endpoint, IPv4 Multicast, Configuration.
- Faza de initial wait + repetition phase (cu backoff exponential) + main phase cyclic offer — exact ca in spec.
- TTL, reboot detection (flag + session id).

**Interop test**: un client `vsomeip` (open source, COVESA) trebuie sa gaseasca si sa apeleze un serviciu VOLT. Daca reuseste, ai o dovada uriasa in README: *"tested against the reference COVESA vsomeip implementation"*.

### 11.5 DoIP (ISO 13400) — `communication/doip`
- Header: protocol version 0x02 + inverse, payload type, payload length.
- Payload types implementate: 0x0001/0x0004 vehicle identification req/resp, 0x0005/0x0006 routing activation req/resp, 0x0007/0x0008 alive check, 0x8001 diagnostic message, 0x8002 ack, 0x8003 nack, 0x4001 entity status, 0x4003 power mode.
- UDP 13400 pentru discovery, TCP 13400 pentru sesiuni diagnostice.
- Routing activation cu tipuri de activare si autentificare (leaga de RBAC-ul din security).
- **Interop**: testat automat in CI impotriva bibliotecii Python `doipclient` (scriptul `tests/integration/doip_interop.py`), care trebuie sa descopere entitatea, sa activeze rutarea si sa citeasca DID-ul F190.

### 11.6 E2E protection — `communication/e2e`
Profil inspirat din AUTOSAR E2E Profile 05/11:
- CRC-16 (sau CRC-8 pentru mesaje mici) peste payload + Data ID (unic per semnal/mesaj) — Data ID intra in CRC, deci un mesaj corect dar "pus pe canalul gresit" e detectat (masquerading).
- Alive counter 4 biti, incrementat la fiecare transmisie.
- Masina de stari a receptorului cu starile reale: `E2E_P_OK`, `E2E_P_REPEATED`, `E2E_P_WRONGSEQUENCE`, `E2E_P_ERROR`, `E2E_P_NOTAVAILABLE`, `E2E_P_NONEWDATA`, plus fereastra de monitorizare (ex: din ultimele 20 de esantioane, minim 15 OK, maxim 2 erori) → altfel fault.
- Se aplica pe **toate** mesajele safety-relevant, si pe CAN si pe Ethernet.
- Demo direct: `volt-inject corrupt --topic wheel_speeds --bitflip 1` → receptorul detecteaza si sistemul degradeaza.

### 11.7 SecOC — `communication/secoc`
- MAC: AES-128-CMAC truncat la 24 biti (ca in practica automotive, unde nu ai loc de 128 de biti pe CAN).
- Freshness Value: contor pe 8 biti pe fir + contor de trip pe 16 biti, cu un Freshness Value Manager centralizat care sincronizeaza periodic FV-ul (mesaj dedicat) — protectie anti-replay reala.
- Verificare la receptie: reconstruire FV, calcul MAC, comparatie in timp constant.
- Politica: mesajele safety-critical au SecOC obligatoriu; restul, optional (configurabil).
- Masori overhead-ul: "AES-CMAC pe 8 bytes = X µs pe Cortex-M4 la 170 MHz, Y ns pe x86 cu AES-NI" — inca o masuratoare reala in raport.

---

> **Conventie pentru restul documentului.** Fiecare functionalitate e descrisa cu acelasi sablon:
> **DE CE** (ce problema rezolva) · **CUM** (design + implementare concreta) · **VERIFICARE** (testul exact care dovedeste ca merge) · **DEMO** (unde apare in demo-urile din §30).
> Sectiunea §31 contine matricea completa functionalitate → cerinta → cod → test → demo, pentru **toate** modulele, inclusiv cele din §7-§11.

---

## 12. Service middleware — `communication/someip` + `distributed/`

### 12.1 Service Registry si model de serviciu

**DE CE.** Fara un registry, serviciile trebuie sa stie unde sunt celelalte, iar migrarea unui serviciu pe alt nod devine imposibila fara reconfigurare manuala. Registry-ul e ce transforma un set de procese intr-o platforma.

**CUM.**
```cpp
struct ServiceDescriptor {
  ServiceId    service_id;      // 16 biti, ca in SOME/IP
  InstanceId   instance_id;     // 16 biti
  Version      major, minor;    // compatibilitate semantica
  NodeId       node;            // unde ruleaza acum
  Endpoint     udp, tcp;        // adresa curenta
  Epoch        epoch;           // fencing token al proprietarului
  ServiceState state;           // OFFERED / SUSPENDED / DEGRADED / STOPPED
  Criticality  criticality;
  Health       health;          // ultimul heartbeat + metrici
};
```
- Registry-ul e **replicat prin Raft** (§13.2) pe nodurile de control. Sursa de adevar unica, consistenta, cu istoric.
- Peste el sta SOME/IP-SD ca protocol pe fir: cine ofera, cine cauta, cine se aboneaza.
- Versionare: un client cere `major` exact si `minor >= X`. Nepotrivire → `E_WRONG_INTERFACE_VERSION` si DTC de configuratie (scenariu real: ai flash-uit doua ECU-uri cu versiuni incompatibile).

**VERIFICARE.**
- `tests/unit/registry_*.cpp`: tranzitii de stare, versionare, conflicte de instanta.
- `tests/integration/sd_discovery.cpp`: 3 noduri, 12 servicii, toate se descopera in < 500 ms de la boot.
- `tests/dst/registry_partition.cpp`: sub partitie de retea, registry-ul nu produce niciodata doua instante ACTIVE ale aceluiasi serviciu (invariant verificat pe 10.000 seeds).

**DEMO.** D1 (boot & discovery), D3 (failover).

### 12.2 Comunicatie: RPC + pub/sub + QoS

**DE CE.** Doua stiluri diferite de interactiune: cerere-raspuns (diagnostics, comenzi) si flux continuu (senzori). Ambele trebuie sa se comporte previzibil cand reteaua se strica.

**CUM.**
- **RPC**: request/response cu `Client ID` + `Session ID`, timeout per apel, retry doar pentru metode declarate idempotente, circuit breaker (dupa N esecuri consecutive, calea e marcata degradata si nu mai blochezi bucla).
- **Pub/Sub**: eventgroups, abonare prin SD, livrare pe UDP multicast (date periodice) sau TCP (date rare si importante).
- **QoS per topic**, configurabil:
  | Parametru | Valori | Efect |
  |---|---|---|
  | `reliability` | BEST_EFFORT / RELIABLE | UDP vs TCP, retransmisie |
  | `history` | KEEP_LAST(n) / KEEP_ALL | dimensiune buffer |
  | `deadline` | µs | lipsa unui esantion → fault de timeout |
  | `on_full` | DROP_OLDEST / DROP_NEW / BLOCK / FAULT | politica de backpressure |
  | `priority` | 0..7 | mapare pe PCP 802.1p / SO_PRIORITY |
- **Backpressure**: niciun consumator lent nu are voie sa blocheze producatorul din data plane. Politica default pe topicurile de senzori: `KEEP_LAST(1)` + `DROP_OLDEST`, iar pierderea se contorizeaza si se raporteaza (nu se ascunde).

**VERIFICARE.**
- `tests/unit/qos_policies.cpp` — fiecare politica, cu producator rapid si consumator lent simulat.
- `tests/perf/rpc_latency.cpp` — KPI K4 (P99 < 250 µs intra-nod).
- `tests/integration/slow_consumer.cpp` — producatorul de 1 kHz isi tine jitterul (K1) desi un consumator dorm 50 ms per mesaj.

**DEMO.** D2 (control loop), D6 (congestie de retea).

### 12.3 Client cu failover transparent

**DE CE.** Cand `BrakeControl` migreaza de pe A pe B, `VehicleDynamics` nu are voie sa afle asta printr-un crash.

**CUM.** Proxy-ul de client tine cache-ul de endpoint din registry, primeste notificare de schimbare (`OfferService` cu epoch nou), reconecteaza, si **refuza automat orice raspuns cu epoch mai vechi decat ultimul vazut** (protectie la mesaje intarziate de la fostul primary).

**VERIFICARE.** `tests/integration/client_failover.cpp` — omoara serverul in mijlocul unui RPC; clientul trebuie sa primeasca fie raspuns de la noul primary, fie o eroare clara `E_NOT_READY`, niciodata date de la epoch vechi.

---

## 13. Distributed runtime — inima proiectului

### 13.1 Membership si detectie de defecte — SWIM (`distributed/membership`)

**DE CE.** Trebuie sa stii, rapid si fara un punct central, care noduri sunt vii. Heartbeat all-to-all nu scaleaza si e zgomotos; SWIM e algoritmul folosit in practica (Consul, Serf) si e implementabil corect in ~800 de linii.

**CUM.**
- Fiecare nod, la fiecare `T_protocol = 3 ms`, alege un peer aleator si trimite `PING`.
- Daca nu raspunde in `T_timeout = 2 ms`, cere la `k = 2` alti membri sa faca `PING-REQ` (indirect probing, fereastra 3 ms) — asta elimina falsele pozitive cauzate de un link punctual.
- Daca nici indirect nu raspunde: nodul devine `SUSPECT`, se propaga prin gossip; dupa `T_suspect = 5 ms` fara dezmintire → `FAULTY`.
- **Detectie pe doua retele independente.** SWIM ruleaza pe Ethernet (VLAN 10). In paralel, fiecare nod emite `0x400 NodeHeartbeat` pe CAN la 5 ms. Un nod e declarat FAULTY doar cand **ambele** cai tac; daca tace doar una, nu e un nod cazut, ci o **defectiune de retea** — alta reactie (DEGRADED pe reteaua afectata, DTC de comunicatie, fara migrare de servicii). Distinctia asta elimina cea mai frecventa cauza de failover inutil si e un argument bun de arhitectura: doua canale fizic independente pentru aceeasi decizie.
- **Incarnation numbers**: un nod suspectat pe nedrept se poate dezminti crescandu-si incarnarea. Previne flapping.
- Piggyback: informatia de membership calatoreste pe mesajele de PING, nu pe un canal separat.
- Buget de detectie (cel mai defavorabil caz): 3 (asteptarea urmatoarei probe) + 2 (timeout direct) + 3 (indirect) + 5 (suspect) + ~2 (propagare gossip) = **15 ms** = KPI K6. Confirmarea independenta pe CAN vine la maxim 15 ms (3 heartbeat-uri ratate).

```cpp
enum class MemberState : uint8_t { ALIVE, SUSPECT, FAULTY, LEFT };
struct MemberView {
  NodeId id; MemberState state; uint32_t incarnation;
  Timestamp last_seen; NodeHealth health;
};
```

**VERIFICARE.**
- `tests/dst/swim_*.cpp` — in simulatorul determinist: pierderi 0-60%, partitii, noduri lente, restart-uri. Proprietati verificate: (a) un nod cazut e detectat de **toti** in < 50 ms; (b) niciun nod viu nu e marcat FAULTY mai mult de o data la 10.000 de rulari (rata de fals pozitiv masurata si raportata).
- `tests/integration/kill_node.cpp` — masoara K6 pe hardware real, 100 de repetari, histograma.

**DEMO.** D3.

### 13.2 Config store replicat — Raft (`distributed/consensus`)

**DE CE.** Registry-ul si proprietatea asupra serviciilor trebuie sa fie consistente chiar si sub partitii. Un registry "eventual consistent" pe controlul franei = doua noduri care cred amandoua ca detin frana. Raft rezolva exact asta si e explicabil la interviu in 3 minute.

**CUM.** Implementare Raft focalizata pe ce ai nevoie, nu pe tot:
- Leader election cu termeni, randomized election timeout (150-300 ms — control plane, nu data plane).
- Log replication cu `AppendEntries`, commit index, aplicare in ordine pe masina de stari (= registry-ul + tabela de proprietate).
- Persistenta `currentTerm`, `votedFor`, log pe disc cu `fsync` si CRC per intrare.
- Snapshot + compactare cand log-ul depaseste N intrari.
- **Fara** membership changes dinamice (joint consensus) — cluster static de 3 noduri, documentat ca decizie constienta (ADR-011), pentru ca nu ai nevoie de elasticitate intr-o masina.
- Comenzile aplicate: `REGISTER_SERVICE`, `OFFER`, `WITHDRAW`, `GRANT_OWNERSHIP(service, node, epoch)`, `SET_CONFIG`, `RAISE_FAULT`.

**VERIFICARE.**
- `tests/dst/raft_*.cpp`: 10.000 de seeds cu partitii aleatorii, crash-uri, mesaje reordonate/duplicate. Invarianti verificati mecanic: Election Safety, Log Matching, Leader Completeness, State Machine Safety.
- `tests/unit/raft_log.cpp`: persistenta, recuperare dupa kill -9 in mijlocul unui `fsync` (simulat prin `SimEnvironment` cu scrieri partiale).
- Comparatie de comportament cu specificatia TLA+ (§14).

**DEMO.** D3, D4 (split-brain).

### 13.3 Leases, epochs, fencing tokens (`distributed/lease`)

**DE CE.** Raft iti da consens, dar consensul e prea lent pentru fiecare ciclu de control de 1 ms. Solutia standard: leader-ul Raft acorda un **lease** (dreptul exclusiv de a controla un actuator) pe o durata limitata, iar detinatorul actioneaza local si rapid. Ca sa fie sigur, fiecare comanda poarta un **fencing token** (epoch) verificat de destinatie.

**CUM.**
1. Nodul A cere proprietatea asupra `BrakeActuator`. Raft comite `GRANT_OWNERSHIP(BrakeActuator, A, epoch=42)`.
2. A primeste lease valid `T_lease = 30 ms`, cu reinnoire la fiecare 10 ms.
3. Fiecare frame `0x201 BrakeCommand` contine `epoch` pe 8 biti (rollover gestionat) + MAC SecOC.
4. **MCU-ul memoreaza cel mai mare epoch vazut si refuza orice comanda cu epoch mai mic.** Comanda respinsa → contor + eveniment de securitate raportat inapoi pe `0x301`.
5. Daca A nu-si poate reinnoi lease-ul (partitie), **A se opreste singur** la `T_self_yield = 20 ms` de la ultima reinnoire reusita (cu 10 ms inainte de expirarea lease-ului), intra in safe state local si nu mai trimite comenzi. Regula: cel care pierde contactul se retrage, nu asteapta sa fie dat afara. Marginea de 10 ms acopera `max_clock_error` raportat de gPTP — daca eroarea de ceas creste peste prag, marginea creste automat, iar daca ceasul nu e sincronizat deloc, resursa trece in clasa UNFENCED (vezi mai jos).

**Doua clase de resurse, doua reguli de acordare** — detaliul care face schema si sigura, si rapida:

| Clasa | Exemple | Cand se poate acorda epoch nou | De ce e sigur |
|---|---|---|---|
| **FENCED** — destinatia verifica epoch-ul | BrakeActuator, SteeringActuator (MCU compara cu `max_epoch_seen`) | **imediat dupa detectie**, fara sa astepti expirarea lease-ului vechi | chiar daca A e viu si trimite in continuare cu epoch 42, MCU-ul refuza fizic; exclusivitatea e impusa la actuator, nu de ceas |
| **UNFENCED** — destinatia nu poate verifica | scriere in NVM partajat, comanda catre un ECU tert fara suport de epoch | doar dupa `T_lease + max_clock_error` de la ultima reinnoire confirmata | singura garantie disponibila e temporala |

Toate actuatoarele din VOLT sunt **FENCED prin constructie** — cerinta de arhitectura `REQ-SAF-031`, nu accident. De aceea failover-ul poate fi de 25 ms desi lease-ul e de 30 ms: cele doua mecanisme raspund la intrebari diferite. Lease-ul spune *cine crede ca detine*; fencing-ul spune *cine e ascultat efectiv*.

6. **Succesiune preautorizata.** Fara ea, bugetul de 25 ms nu e realizabil in cel mai rau caz. Odata cu acordarea epoch-ului 42 catre A, Raft comite in acelasi log si o intrare conditionata `SUCCESSOR(BrakeActuator, NodeB, epoch=43, condition = A declarat FAULTY de un quorum)`. La caderea lui A, B nu mai are nevoie de o runda noua de consens: are deja dreptul si trebuie doar sa constate ca exista quorum care il declara pe A cazut. Asta scoate din calea critica replicarea Raft (~4 ms) si, critic, **cazul in care nodul cazut era chiar leader-ul Raft** — o alegere noua ar dura 150-300 ms si ar distruge bugetul FTTI. Dupa preluare, clusterul reface consensul in fundal si preautorizeaza urmatorul succesor. Daca nu exista quorum (doua noduri din trei pierdute), nimeni nu preia si sistemul intra in safe state — indisponibilitate, nu nesiguranta.

**VERIFICARE.**
- Invariantul central: **niciodata doua noduri nu detin simultan un lease valid pentru acelasi actuator**. Verificat in trei feluri: TLA+ (§14), DST (10.000 seeds cu ceasuri care deriva), si test fizic (D4).
- `tests/hil/split_brain.cpp` — se taie fizic cablul de retea; se citeste log-ul MCU-ului si se verifica ca nicio comanda cu epoch vechi nu a fost acceptata.

**DEMO.** D4 — cel mai impresionant demo din tot proiectul.

### 13.4 Placement si orchestrare (`distributed/placement`)

**DE CE.** Cand un nod cade, cineva trebuie sa decida *ce* se muta si *unde*, respectand resurse si criticalitate. Un `if` hardcodat nu e o platforma.

**CUM.** Un rezolvator de constrangeri simplu si determinist (greedy cu prioritati + backtracking limitat), rulat pe leader-ul Raft:
- Constrangeri hard: utilizare CPU dupa plasare < 85%, memorie disponibila, prezenta hardware necesar (ex: `BrakeControl` are nevoie de acces la CAN → doar noduri cu `can0`), anti-afinitate (ACTIVE si STANDBY nu pe acelasi nod).
- Ordonare: SAFETY_CRITICAL > HIGH > MEDIUM > LOW. Daca nu incap toate, se **opresc de jos in sus** serviciile optionale (`Logging`, `HMI`, `ADAS`) si se emite un DTC de resurse.
- Rezultatul e un plan (`PlacementPlan`) comis prin Raft, deci toata lumea vede acelasi plan.
- Recalculare declansata de: cadere de nod, revenire de nod, depasire de resurse, comanda manuala din tester.

```
$ volt-monitor placement
PLAN #17 (epoch 42, committed at t=12.418s)
  NodeA [cpu 62% mem 41%]: SensorFusion*, VehicleDynamics*, BrakeControl*, Diagnostics
  NodeB [cpu 38% mem 22%]: SensorFusion~, VehicleDynamics~, BrakeControl~, ADAS, Logging
  NodeC [cpu 11% mem 09%]: Gateway, HMI, DiagTester
  (* = ACTIVE, ~ = STANDBY)
  Rejected: none. Degraded: none.
```

**VERIFICARE.** `tests/unit/placement_*.cpp` cu scenarii tabelate (input: noduri + resurse + servicii; output asteptat: plan exact). `tests/dst/placement_churn.cpp`: noduri care cad si revin de 200 de ori; invariant: niciodata doua ACTIVE, niciodata un serviciu SAFETY_CRITICAL oprit cat timp exista un nod care il poate gazdui.

**DEMO.** D3, D7 (degradare progresiva).

### 13.5 State replication (`distributed/replication`)

**DE CE.** Un `BrakeController` care reporneste de la zero produce un salt in comanda (termenul integral al regulatorului, starea de ABS, filtrele). La 1 ms de control, saltul e vizibil fizic. Deci standby-ul trebuie sa aiba starea, nu doar codul.

**CUM.** Doua mecanisme complementare, ambele derivate din D1:
1. **Checkpoint periodic**: `serialize()` la fiecare 5 ms → mesaj `STATE_SNAPSHOT(service, seq, epoch, hash, payload)` catre standby, pe VLAN 10.
2. **Delta / log shipping**: intre checkpoint-uri, se trimit mesajele de intrare cu numar de secventa; standby-ul le aplica pe copia lui, deci ruleaza aceeasi masina de stari cu o intarziere mica ("hot standby"). Divergenta se detecteaza comparand `state_hash()` la fiecare checkpoint.

```cpp
struct StateSnapshot {
  ServiceId svc; uint64_t seq; Epoch epoch;
  Timestamp taken_at; Hash64 state_hash;
  uint32_t payload_len; /* payload */
};
```
- **Stale-state detection**: standby-ul cu un snapshot mai vechi de `3 × sync_period` se marcheaza `STANDBY_STALE` si **nu** e eligibil pentru preluare fara reinitializare controlata (preluare cu bumpless transfer degradat: pornire din starea curenta a senzorilor, nu din integrator vechi).
- **Bumpless transfer**: la preluare, noul ACTIVE limiteaza rata de variatie a comenzii in primele N cicluri (`slew rate limiter`), ca sa nu existe un salt fizic.

**VERIFICARE.**
- `tests/unit/state_roundtrip.cpp`: pentru fiecare serviciu, `deserialize(serialize(s)) == s` si `state_hash` identic (property-based, 10k stari aleatorii).
- `tests/integration/failover_bump.cpp`: masoara discontinuitatea comenzii de frana la failover; criteriu K9: < 2 cicluri si salt < 5% din domeniu.
- `tests/dst/replica_divergence.cpp`: injecteaza pierderi pe canalul de sincronizare si verifica ca divergenta e **detectata**, nu ignorata.

**DEMO.** D3 (graficul comenzii de frana in timpul failover-ului — linia continua, fara salt: asta e imaginea care vinde proiectul).

### 13.6 Protocolul de failover — cronologia exacta

**DE CE.** "Failover in ~4 ms" fara o descompunere pe faze nu e inginerie. Bugetul pe faze e ce arati la interviu.

**CUM.**
```
t=0.000 ms   NodeA moare (kill -9 pe runtime / cablu scos / panic)
t=+0.0-3.0   NodeB ajunge la urmatoarea proba SWIM catre A (perioada 3 ms)
t=+5.0       PING direct expirat (2 ms) → PING-REQ prin NodeC
t=+8.0       NodeC confirma: fara raspuns → NodeA = SUSPECT, gossip
t=+13.0      T_suspect (5 ms) expirat → NodeA = FAULTY pe calea Ethernet
t=+13.0      In paralel: al 3-lea heartbeat CAN 0x400 ratat → confirmare pe a doua retea
             ─── K6: detectie ≤ 15 ms, confirmata pe doua retele independente ───
t=+13.5      NodeB constata quorum {B,C} care il declara pe A FAULTY
t=+14.0      NodeB activeaza succesiunea preautorizata: epoch 43, fara runda noua de consens
             (de aceea bugetul tine si daca A era leader-ul Raft)
t=+15.0      BrakeControl(B): STANDBY → ACTIVE, restore din ultimul snapshot (age ≤ 5 ms)
t=+16.0      Prima comanda 0x201 cu epoch=43, slew-limited
t=+17.5      Actuatorul accepta (43 > 42) si iesirea se misca; comenzile reziduale cu epoch 42 sunt refuzate
t=+18..40    In fundal: consensul se reface, se comite plasarea #18 si se preautorizeaza
             urmatorul succesor
             ─── K7: failover total ≤ 25 ms · K9: intrerupere ≤ 2 cicluri ───
```
Fiecare linie de mai sus e un eveniment de trace, cu timestamp global, si apare in export-ul Perfetto. Demo-ul afiseaza aceasta cronologie automat dupa fiecare injectie.

**VERIFICARE.** `tests/integration/failover_timeline.cpp` ruleaza 100 de failover-uri consecutive si produce:
```
Failover statistics (n=100, hardware, PREEMPT_RT)
  detection      : P50  11.8 ms  P99  14.9 ms  max 15.4 ms
  decision       : P50   3.4 ms  P99   5.1 ms  max  6.0 ms
  takeover       : P50   2.9 ms  P99   4.2 ms  max  4.8 ms
  TOTAL          : P50  18.1 ms  P99  23.6 ms  max 24.9 ms   [K7 ≤ 25 ms: PASS]
  command gap    : P50   1.0 ms  P99   2.0 ms  max  2.0 ms   [K9 ≤ 2 ms : PASS]
  unsafe commands: 0                                          [PASS]
```

**DEMO.** D3.

### 13.7 Reintegrarea unui nod (rejoin)

**DE CE.** Un sistem care supravietuieste la caderea unui nod dar nu stie sa-l primeasca inapoi e jumatate de sistem. Rejoin-ul e si locul unde apar cele mai urate bug-uri (zombie primary).

**CUM.** Nodul revenit porneste **intotdeauna** in `OBSERVER`: se sincronizeaza pe ceas (gPTP), preia snapshot-ul Raft, isi afla epoch-ul curent, constata ca nu mai detine nimic, si intra ca STANDBY. Preluarea inapoi (failback) **nu e automata** — e o comanda explicita (`volt-diag cluster failback --service Brake --to NodeA`) sau o politica configurabila cu histereza (nod stabil > 30 s). Motivul: failback automat produce oscilatii, iar in automotive nu vrei sa muti controlul franei pentru ca reteaua a clipit.

**VERIFICARE.** `tests/dst/rejoin_zombie.cpp`: un nod e izolat, revine cu ceas deviat si stare veche; invariant: nu emite niciodata o comanda cu epoch pe care il credea al lui.

**DEMO.** D3 (partea a doua: revenirea nodului si failback controlat).

---

## 14. Verificare formala — `formal/tla`

**DE CE.** Protocoalele de failover au bug-uri care apar o data la 10.000 de rulari, in ordini de mesaje pe care nu le vei genera niciodata manual. Un model checker le gaseste in cateva minute. In plus, foarte putini studenti au asa ceva, iar la un interviu de safety e o dovada de maturitate.

**CUM.** Trei specificatii, model-checked cu TLC:
1. `formal/tla/Lease.tla` — proprietatea si fencing-ul.
   - Invariant `AtMostOneOwner`: `\A s \in Services : Cardinality({n \in Nodes : owner[n][s] /\ leaseValid[n][s]}) <= 1`
   - Invariant `NoStaleCommand`: nicio comanda acceptata de actuator nu are epoch < maxEpochSeen.
   - Model: 3 noduri, partitii arbitrare, ceasuri cu deriva marginita, pierderi de mesaje.
2. `formal/tla/Failover.tla` — orchestrarea completa (detectie → plan → grant → takeover).
   - Liveness: `<>[] (ClusterHasMajority => \E n : owner[n][Brake])` — daca exista majoritate, cineva ajunge sa detina frana.
3. `formal/tla/E2E.tla` — masina de stari a receptorului E2E: nicio secventa de mesaje nu duce la acceptarea a doua mesaje cu acelasi counter (protectie la replay).

Fiecare actiune TLA+ e mapata la o functie C++ printr-un comentariu `// @tla Lease!AcquireLease`, iar `tools/traceability` verifica ca **toate** actiunile din model au corespondent in cod (si invers).

**VERIFICARE.** Rulat in CI (`.github/workflows/formal.yml`) la fiecare modificare din `distributed/`: TLC cu configuratie mica (3 noduri, 2 servicii, adancime marginita), timp < 10 min. Rezultat: `formal/results/*.log` publicat ca artifact.

**DEMO.** D9 (partea de rigoare): arati contraexemplul pe care TLC l-a gasit intr-o versiune veche a protocolului si cum l-ai reparat. Un contraexemplu real, gasit de tine, povestit corect, valoreaza cat 5 module.

---

## 15. Safety — `safety/`

### 15.1 Analiza de pericol (HARA-lite) — `requirements/HARA.md`

**DE CE.** Toate numerele de timing din proiect (perioade, timeouts, buget de failover) trebuie sa vina de undeva. In automotive vin din FTTI, care vine din analiza de hazard. Fara asta, `T_lease = 50 ms` e o cifra inventata; cu asta, e o decizie de inginerie.

**CUM.** Analiza pe 6 hazarde, cu format standard:

| ID | Hazard | Situatie | S | E | C | ASIL* | Safety Goal | FTTI |
|---|---|---|---|---|---|---|---|---|
| H-01 | Franare neintentionata pe toate rotile | 100 km/h, autostrada | S3 | E4 | C2 | D | SG-01: sistemul nu trebuie sa comande frana fara cerere | 100 ms |
| H-02 | Lipsa franarii la cerere | oprire de urgenta | S3 | E3 | C3 | D | SG-02: comanda de frana valida trebuie sa ajunga la actuator | 100 ms |
| H-03 | Franare asimetrica necomandata | curba, viteza medie | S2 | E4 | C2 | C | SG-03: diferenta stanga/dreapta limitata fara cerere | 150 ms |
| H-04 | Comanda pe baza de senzor eronat | derapaj | S2 | E4 | C2 | C | SG-04: date de senzor implauzibile nu trebuie folosite | 80 ms |
| H-05 | Comanda de la un nod neautorizat (split-brain / atac) | orice | S3 | E4 | C3 | D | SG-05: doar detinatorul curent al lease-ului poate comanda | 50 ms |
| H-06 | Pierderea totala a controlului la caderea unui nod | orice | S3 | E3 | C2 | C | SG-06: caderea unui nod nu duce la pierderea functiei de baza | 100 ms |

\* ASIL-urile sunt *ilustrative*, obtinute prin aplicarea tabelului de clasificare. Documentul spune explicit: **acesta nu este un proces certificat ISO 26262; este o analiza in stilul metodei, folosita pentru a deriva cerinte si bugete de timp.**

**Derivarea bugetelor** (partea care conteaza):
```
SG-06, FTTI = 100 ms
  detectie          ≤ 15 ms   → SWIM 3/2/3/5 ms + heartbeat CAN 3×5 ms  (K6)
  decizie + grant   ≤  6 ms   → Raft pe LAN, quorum 2/3
  preluare + restore≤  4 ms   → snapshot la 5 ms, restore < 1 ms
  ─────────────────────────
  total             ≤ 25 ms   → margine 75 ms (factor 4×)      (K7)

SG-05, FTTI = 50 ms
  lease 30 ms, reinnoire 10 ms, retragere proprie la 20 ms
  → detinatorul tace cu 10 ms inainte de expirarea propriului lease
  → succesorul e preautorizat, deci preluarea nu depinde de o runda noua de consens
  → fencing la actuator (epoch verificat in MCU) = a doua bariera, independenta de ceas
  → cele doua bariere au moduri de defectare diferite (temporala vs. secventiala)
```

**VERIFICARE.** Fiecare Safety Goal are cerinte `REQ-SAF-xxx`, fiecare cerinta are minim un test, iar `tools/traceability` esueaza build-ul daca un SG nu are lant complet pana la un test care a trecut.

### 15.2 Cerinte de siguranta — `requirements/REQ-SAF.md`

Format fix, cu ID stabil (exemple reale, nu placeholder):

```
REQ-SAF-001 (from SG-01, ASIL D)
  BrakeControl shall not output a pressure command > 0 unless
  (a) a valid deceleration request is present with E2E state OK, AND
  (b) the node holds a valid lease for BrakeActuator, AND
  (c) plausibility checks on wheel speeds passed within the last 20 ms.
  Verification: tests/unit/brake_guard.cpp, tests/dst/brake_no_spurious.cpp
  Implementation: services/brake_control/guard.cpp  // @satisfies REQ-SAF-001

REQ-SAF-014 (from SG-04, ASIL C)
  If two redundant wheel-speed sources differ by more than 3 km/h for
  more than 30 ms, the system shall mark the signal as untrusted, store
  DTC C0031, and switch BrakeControl to fallback algorithm within 20 ms.
  Verification: tests/integration/wheel_divergence.cpp
  Implementation: safety/plausibility/wheel_cross_check.cpp
```

### 15.3 Masina de stari de degradare — `safety/degradation`

**DE CE.** Sistemul are nevoie de un raspuns *definit* la orice combinatie de defecte, altfel comportamentul la fault e emergent (= imprevizibil = periculos).

**CUM.** Doua dimensiuni ortogonale, ambele explicite:

```
SYSTEM STATE (disponibilitate)          SAFETY STATE (siguranta)
  INIT                                    OPERATIONAL
   ↓                                        ↓
  RUNNING  ←──────┐                       DEGRADED
   ↓              │                         ↓
  DEGRADED  ──────┤ recovery              RESTRICTED
   ↓              │ (cu histereza)          ↓
  LIMP_HOME ──────┘                       SAFE_STATE (fara revenire automata)
   ↓
  SHUTDOWN
```

Tabel de tranzitii (extras, complet in `docs/SAFETY.md`) — fiecare tranzitie are conditie, actiune, DTC si timp maxim:

| De la | La | Conditie | Actiune | DTC | Deadline |
|---|---|---|---|---|---|
| RUNNING | DEGRADED | 1 senzor redundant pierdut | fallback pe estimare | C0031 | 20 ms |
| RUNNING | DEGRADED | nod pierdut, standby preia | migrare servicii | U0100 | 25 ms |
| DEGRADED | LIMP_HOME | al 2-lea senzor pierdut sau ambele canale de control diverg | limitare comanda la 30%, avertizare HMI | C0045 | 20 ms |
| orice | SAFE_STATE | lease invalid, E2E fail persistent, watchdog epuizat | oprire comanda, actuator in pozitie sigura, latch | U3000 | 10 ms |
| SAFE_STATE | — | nicio revenire fara ciclu de alimentare + stergere DTC prin tester | — | — | — |

**Regula de latch**: iesirea din SAFE_STATE nu se face automat, niciodata. Se face prin `volt-diag reset --ecu safety` (UDS 0x11). Asta e comportament corect si il explici la interviu.

**VERIFICARE.**
- Masina de stari e generata din tabel (`tools/fsm_gen`: tabel YAML → cod C++ + diagrama PlantUML + tabel in docs). Deci documentatia si codul **nu pot** sa diverge — argument foarte bun.
- `tests/unit/degradation_fsm.cpp`: parcurgere exhaustiva a tuturor perechilor (stare × eveniment); nicio combinatie nedefinita.
- `tests/dst/degradation_chaos.cpp`: injectie aleatorie de fault-uri; invariant: **nicio secventa de fault-uri nu produce o comanda de frana in afara limitelor din REQ-SAF-001**.

**DEMO.** D5, D7.

### 15.4 Plausibility & redundanta — `safety/plausibility`, `safety/redundancy`

**DE CE.** Un senzor stricat care raporteaza valori "frumoase" e mai periculos decat unul care tace. Detectia se face prin comparatii, nu prin incredere.

**CUM.** Patru familii de verificari, toate configurabile per semnal:
| Tip | Exemplu | Parametri |
|---|---|---|
| **Range check** | viteza roata ∈ [0, 300] km/h | min, max |
| **Rate check** | acceleratie roata < 25 m/s² | max_delta / dt |
| **Temporal check** | esantion mai nou de 20 ms, fara duplicate | timeout, alive counter |
| **Cross check** | roata FL vs FR vs viteza estimata din IMU vs GPS-lite | toleranta, durata, histereza |

**Redundanta duala** pe calea de control:
```
                sensor data (E2E-verified)
                        │
            ┌───────────┴───────────┐
            ▼                       ▼
    Controller A                Controller B
    (PID + ABS, float)          (algoritm diferit:
                                 tabelar / bang-bang, int fixed-point)
            │                       │
            └───────────┬───────────┘
                        ▼
                   Comparator
        |A - B| ≤ tol pentru ≥ N cicluri?
            ┌───────────┴───────────┐
        DA  ▼                       ▼  NU
   emite min(A,B) (conservator)   FaultManager:
                                  - DTC C0072
                                  - trece pe canalul cu ultimul istoric bun
                                  - daca ambele sunt suspecte → RESTRICTED
```
**Detaliu care conteaza**: cele doua canale folosesc **algoritmi diferiti si tipuri numerice diferite** (diversitate), nu acelasi cod rulat de doua ori. Doua copii ale aceluiasi bug dau acelasi rezultat gresit — asta e diferenta dintre redundanta reala si redundanta decorativa, si e exact ce te intreaba un inginer de safety.

**Votare 2-out-of-3** acolo unde exista trei surse (viteza: roti fata, roti spate, integrare IMU): mediana + eliminarea valorii extreme, cu marcarea sursei eliminate.

**VERIFICARE.**
- `tests/property/plausibility.cpp` (rapidcheck): pentru orice secventa de esantioane cu un defect injectat de tip X, defectul e detectat in ≤ D ms; si: pentru orice secventa **fara** defect, rata de fals pozitiv = 0 pe 10^7 esantioane.
- `tests/integration/dual_channel_divergence.cpp`: se corupe intentionat canalul B; se verifica DTC-ul, comanda emisa si timpul de reactie.

**DEMO.** D5.

### 15.5 Fault Manager — `safety/fault_manager`

**DE CE.** Fault-urile vin din 8 module diferite (scheduler, retea, E2E, senzori, watchdog, memorie, termic, securitate). Fara un punct central de agregare, fiecare modul si-ar inventa propria reactie.

**CUM.**
```cpp
struct Fault {
  FaultId    id;             // enum global, generat din tabel YAML
  Severity   severity;       // INFO / WARNING / ERROR / CRITICAL
  FaultClass klass;          // SENSOR / COMM / COMPUTE / TIMING / SECURITY / THERMAL
  Timestamp  first_seen, last_seen;
  uint16_t   occurrences;
  DebounceState debounce;    // PASSED / PENDING / CONFIRMED / HEALING
  SnapshotId snapshot;       // date congelate la momentul confirmarii
};
```
- **Debounce** configurabil per fault: counter-based (`+1` la fiecare aparitie, `-1` la fiecare absenta, prag de confirmare) sau time-based (X ms continuu).
- **Healing/aging**: dupa N cicluri de conducere fara reaparitie, fault-ul trece in `HEALING` si apoi se sterge (comportament real de ECU).
- **Reactii** definite in tabel, nu in cod: `fault_id → [set_dtc, notify_service, degrade_to, disable_feature, enter_safe_state]`.
- Fiecare fault confirmat produce un **snapshot** (viteza, stare sistem, ultimele 50 ms de semnale relevante, versiuni software) — folosit apoi in `ReadDTCInformation`.

**VERIFICARE.** `tests/unit/fault_debounce.cpp` (tabelat: secventa de aparitii → stare asteptata la fiecare pas). `tests/integration/fault_to_dtc.cpp`: fiecare tip de fault injectabil trebuie sa produca exact DTC-ul documentat — test generat automat din tabelul de fault-uri, deci **imposibil de uitat cand adaugi un fault nou**.

**DEMO.** D5, D6, D7.

---

## 16. Diagnostics — `diagnostics/`

### 16.1 Server UDS (ISO 14229) — `diagnostics/uds`

**DE CE.** Diagnosticul e limbajul comun al industriei. Un ECU fara UDS nu e un ECU. In plus, iti da un canal de control extern pentru demo-uri (citesti stari, injectezi, resetezi, faci flash).

**CUM.** Server complet, cu tabel de servicii si verificare de conditii (sesiune, securitate, stare):

| SID | Serviciu | Sub-functii implementate | Sesiune ceruta | Securitate |
|---|---|---|---|---|
| 0x10 | DiagnosticSessionControl | 01 default, 02 programming, 03 extended | orice | nu |
| 0x11 | ECUReset | 01 hard, 02 key-off-on, 03 soft | extended | nu |
| 0x14 | ClearDiagnosticInformation | — | extended | nu |
| 0x19 | ReadDTCInformation | 01 count, 02 by status mask, 04 snapshot, 06 extended data, 0A supported | orice | nu |
| 0x22 | ReadDataByIdentifier | multi-DID | orice | partial |
| 0x23 | ReadMemoryByAddress | — | extended | da |
| 0x27 | SecurityAccess | 01/02 level1, 03/04 level2 (programming) | extended | — |
| 0x28 | CommunicationControl | 00 enable, 01 enableRx/disableTx, 03 disable | extended | nu |
| 0x2E | WriteDataByIdentifier | — | extended | da |
| 0x2F | InputOutputControlByIdentifier | short-term adjust, freeze, reset, return | extended | da |
| 0x31 | RoutineControl | 01 start, 02 stop, 03 results | extended | partial |
| 0x34 | RequestDownload | — | programming | da (level2) |
| 0x36 | TransferData | — | programming | da |
| 0x37 | RequestTransferExit | — | programming | da |
| 0x3E | TesterPresent | 00, 80 (no response) | orice | nu |
| 0x85 | ControlDTCSetting | 01 on, 02 off | extended | nu |

- **NRC-uri** implementate corect: 0x10 general reject, 0x11 service not supported, 0x12 sub-function not supported, 0x13 incorrect length, 0x22 conditions not correct, 0x24 request sequence error, 0x31 request out of range, 0x33 security access denied, 0x35 invalid key, 0x36 exceed attempts, 0x37 required time delay not expired, 0x78 response pending (cu trimitere periodica pentru operatii lungi).
- **S3 timer**: revenire la sesiunea default dupa 5 s fara TesterPresent — implementat, testat.
- **Rutine** (0x31) utile si demonstrabile: `0xF001` self-test, `0xF002` fault injection (doar in sesiune extended + security level 2!), `0xF003` failover manual, `0xF004` calibrare senzori, `0xF005` benchmark IPC.
- Transport: ISO-TP pe CAN (0x7E0/0x7E8) **si** DoIP pe Ethernet — acelasi server, doua transporturi, ceea ce demonstreaza o separare curata.

**DID-uri implementate** (`config/dids.yaml` → cod generat):
| DID | Continut | R/W |
|---|---|---|
| F186 | Active diagnostic session | R |
| F187 | Spare part number | R |
| F189 | Software version (git hash + tag) | R |
| F190 | VIN | R |
| F1A0 | Node ID + rol curent (ACTIVE/STANDBY) | R |
| F1A1 | Epoch curent al lease-urilor detinute | R |
| 0100 | Vehicle speed (live) | R |
| 0101 | Wheel speeds ×4 (live) | R |
| 0110 | System state + safety state | R |
| 0120 | CPU load, memory, temperatura per nod | R |
| 0200 | Calibrare: prag ABS slip | R/W |
| 0201 | Calibrare: limita presiune frana | R/W |
| 0300 | Statistici scheduler (misses, jitter P99) | R |
| 0301 | Statistici retea (loss, latenta P99) | R |

**VERIFICARE.**
- `tests/unit/uds_*.cpp`: pentru fiecare serviciu, vectori request/response byte cu byte, inclusiv toate NRC-urile.
- `tests/integration/uds_interop.py`: rulat in CI cu biblioteca **`udsoncan`** (Python, referinta open-source) peste `vcan0` — daca un tester independent, scris de altcineva, vorbeste cu serverul tau, serverul tau e corect.
- Fuzzing: `tests/fuzz/uds_fuzz.cpp` (libFuzzer) pe parserul de request — obiectiv: 0 crash-uri, 0 finding-uri ASan dupa 1h de fuzzing in nightly.

**DEMO.** D8.

### 16.2 DTC Manager — `diagnostics/dtc`

**DE CE.** DTC-urile sunt memoria de defecte a masinii. Implementarea naiva ("pun codul intr-o lista") rateaza exact partile care conteaza: debounce, status bits, snapshot, aging, persistenta.

**CUM.**
- Format DTC pe 3 bytes (ISO 14229) + status byte pe 8 biti cu **toti** bitii standard:
  `testFailed`, `testFailedThisOperationCycle`, `pendingDTC`, `confirmedDTC`, `testNotCompletedSinceLastClear`, `testFailedSinceLastClear`, `testNotCompletedThisOperationCycle`, `warningIndicatorRequested`.
- Ciclu de operare (operation cycle) gestionat explicit: la fiecare "key-on" se reseteaza bitii de ciclu.
- **Snapshot record** (freeze frame): pana la 3 snapshot-uri per DTC, cu DID-uri configurabile.
- **Extended data record**: contor de aparitii, contor de cicluri de la ultima aparitie, contor de aging, timestamp global.
- **Aging**: dupa 40 de cicluri fara reaparitie, DTC-ul se sterge automat.
- Persistenta in NVM (`diagnostics/nvm`) cu: doua copii + CRC32, scriere atomica (write-then-swap), detectie de coruptie la boot cu DTC dedicat, si limitare de uzura (nu scrii la fiecare aparitie, ci la confirmare/schimbare de stare).

**Catalogul de DTC-uri** (complet in `config/dtcs.yaml`, generat in cod si in docs):
| DTC | Descriere | Debounce | Reactie |
|---|---|---|---|
| C0031 | Wheel speed sensor FL implausible | 5 aparitii / 30 ms | fallback + DEGRADED |
| C0045 | Two wheel speed sources lost | 3 / 20 ms | LIMP_HOME |
| C0072 | Dual-channel control divergence | 2 / 10 ms | canal sigur + DEGRADED |
| U0100 | Lost communication with compute node | 3 heartbeat | failover |
| U0155 | CAN bus off | imediat | RESTRICTED |
| U0401 | Invalid data received (E2E fail) | 5 / 50 ms | DEGRADED |
| U3000 | Control unit internal failure (deadline/watchdog) | 3 / 3 ms | SAFE_STATE |
| P0217 | Compute node overtemperature | 1 / 1 s | throttling |
| B1001 | SecOC MAC verification failed | 3 / 100 ms | reject + security event |
| B1002 | Stale epoch command rejected (split-brain attempt) | 1 | security event + log |

**VERIFICARE.** `tests/unit/dtc_lifecycle.cpp` — pentru fiecare DTC: aparitie → pending → confirmed → snapshot corect → aging → sters, cu verificarea fiecarui bit de status la fiecare pas. `tests/integration/nvm_power_loss.cpp` — se intrerupe alimentarea (simulata prin `kill -9` in mijlocul scrierii, si real prin taierea alimentarii la MCU) de 200 de ori: dupa fiecare repornire, NVM-ul trebuie sa fie fie in starea veche, fie in cea noua, niciodata corupt.

**DEMO.** D8.

### 16.3 Security Access si control de acces — `diagnostics` + `security/access_control`

**DE CE.** Rutina de fault injection sau scrierea unui parametru de calibrare nu are voie sa fie accesibila oricui trimite un frame CAN.

**CUM.** Seed & key real, nu XOR:
- Level 1 (citire extinsa): seed 4 bytes, key = `AES-128-CMAC(secret_L1, seed)` trunchiat la 4 bytes.
- Level 2 (programming / injection / calibrare): seed 8 bytes, key = CMAC cu alt secret; plus **delay timer** exponential dupa esecuri (0x37 pana expira) si limita de 3 incercari per ciclu.
- Rolurile (`READ_ONLY`, `SERVICE`, `ENGINEERING`, `PRODUCTION`) sunt mapate pe nivelurile de securitate si pe listele de servicii/DID-uri permise, in `config/access_control.yaml`.
- Cheile nu stau in cod: sunt in `security/keystore` (fisier criptat cu o cheie derivata din parola, in dev; interfata pregatita pentru HSM, documentata ca abstractizare).

**VERIFICARE.** `tests/unit/security_access.cpp`: cheie gresita → 0x35, a 4-a incercare → 0x36, apoi 0x37 pana la expirare; seed nou la fiecare cerere (niciodata acelasi seed de doua ori — verificat statistic).

**DEMO.** D8.

### 16.4 Flashing + OTA cu A/B si rollback — `diagnostics/flashing`

**DE CE.** "Software-defined vehicle" fara mecanism de update e o vorba goala. In plus, update-ul e locul unde o platforma poate deveni bricked — deci e o problema de siguranta, nu de confort.

**CUM.**
1. Tester: `0x10 02` (programming session) → `0x27` level 2 → `0x31 01 FF00` (check programming preconditions: vehicul oprit, fara fault-uri critice) → `0x34 RequestDownload` (adresa+lungime, format) → N × `0x36 TransferData` (block counter, max block length negociat) → `0x37 RequestTransferExit` → `0x31 01 FF01` (check memory: verificare semnatura) → `0x11 01` reset.
2. **Partitii A/B**: imaginea noua se scrie in slotul inactiv. Bootloader-ul (pe MCU: bootloader propriu; pe Linux: un `volt-updater` + symlink atomic + systemd) porneste slotul nou marcat "trial".
3. **Health check post-boot**: daca noul software nu declara `mark_good()` in 30 s (adica nu a ajuns la RUNNING cu toate serviciile safety sanatoase), bootloader-ul **face rollback automat** la slotul anterior si ridica DTC-ul de update esuat.
4. **Semnatura**: fiecare imagine e semnata ECDSA P-256; verificarea semnaturii e obligatorie inainte de activare (se leaga de secure boot, §17.2).
5. Update coordonat pe cluster: nodurile se actualizeaza **pe rand**, niciodata ACTIVE si STANDBY simultan; orchestrarea verifica ca exista mereu un detinator valid al fiecarui serviciu safety-critical.

**VERIFICARE.**
- `tests/integration/flash_ok.cpp`: update complet, verificare versiune noua prin DID F189.
- `tests/integration/flash_interrupted.cpp`: intrerupere la 10%, 50%, 90% si in timpul scrierii de metadate → sistemul trebuie sa ramana bootabil de fiecare data (200 de iteratii).
- `tests/integration/flash_bad_signature.cpp`: imagine modificata cu 1 bit → refuzata, DTC ridicat.
- `tests/integration/flash_rollback.cpp`: imagine care crapa intentionat la boot → rollback automat in < 60 s, sistemul functional pe versiunea veche.

**DEMO.** D8 (update live pe MCU in timp ce clusterul continua sa controleze franarea pe nodul redundant — imagine foarte puternica).

---

## 17. Cybersecurity — `security/`

### 17.1 Model de amenintare (TARA-lite) — `requirements/TARA.md`

**DE CE.** ISO/SAE 21434 e ceruta explicit in joburile din zona. Un capitol de securitate fara model de amenintare e doar o lista de tehnologii.

**CUM.** Tabel de amenintari cu vectori concreti si contramasuri implementate (nu teoretice):

| ID | Amenintare | Vector | Impact | Contramasura implementata | Test |
|---|---|---|---|---|---|
| T-01 | Injectie de comanda de frana | acces la busul CAN (OBD) | S3 | SecOC (MAC + freshness) | `secoc_reject_forged` |
| T-02 | Replay al unei comenzi legitime | inregistrare + retransmisie | S3 | Freshness value + E2E counter | `secoc_reject_replay` |
| T-03 | Split-brain provocat (DoS pe retea) | flood pe VLAN 10 | S3 | lease + fencing + rate limit + prioritizare | `split_brain_attack` |
| T-04 | Acces neautorizat la rutine de inginerie | tester conectat | S2 | SecurityAccess L2 + RBAC + delay timer | `security_access_bruteforce` |
| T-05 | Flash cu imagine modificata | update malitios | S3 | semnatura ECDSA + secure boot + rollback | `flash_bad_signature` |
| T-06 | Bus flooding / DoS pe CAN | frame-uri la rata maxima | S2 | IDS: detectie de rata anormala + bus load monitor | `ids_flood` |
| T-07 | Nod compromis care trimite mesaje valide sintactic | malware pe un nod | S2 | chei per-nod, IDS de plauzibilitate, anti-afinitate | `ids_rogue_node` |
| T-08 | Extragere de chei din binar | acces la filesystem | S1 | keystore criptat + abstractizare HSM | review + `keystore_test` |

### 17.2 Secure boot (simulat, dar corect ca lant)

**DE CE.** Fara integritatea codului, toate celelalte masuri sunt inutile.

**CUM.** Lant de incredere pe 4 etape, cu masuratori (hash-uri) inregistrate:
```
Root key (in keystore / TPM-like file) 
   → verifica semnatura bootloader-ului     → masura M0
      → bootloader verifica imaginea runtime → masura M1
         → runtime verifica manifestul de servicii (hash per binar) → M2
            → fiecare serviciu verifica fisierele de config semnate  → M3
```
- Manifestul (`manifest.json` semnat) contine hash-ul SHA-256 al fiecarui binar si al fiecarui fisier de config, plus versiuni si permisiuni (ce servicii pot cere ce lease-uri).
- La orice nepotrivire: refuz de pornire a serviciului + DTC + eveniment de securitate. Sistemul porneste in `RESTRICTED` daca un serviciu non-critic e invalid; refuza sa porneasca deloc daca un serviciu safety e invalid.
- Pe MCU (Zephyr): verificare de semnatura la boot cu cheia publica in flash read-only (write-protected).

**VERIFICARE.** `tests/integration/secure_boot.cpp`: modifica 1 byte in fiecare artefact pe rand (binar, config, manifest, semnatura) → in toate cele 4 cazuri, pornirea e refuzata cu eroarea corecta.

### 17.3 Intrusion Detection System — `security/ids`

**DE CE.** Detectia e a doua linie dupa prevenire, si e ceruta explicit in rolurile de cybersecurity automotive.

**CUM.** Detectoare deterministe (fara ML, ca sa fie explicabile si testabile):
1. **Rata anormala**: fiecare ID de CAN are o perioada asteptata; abatere > ±25% pentru > 100 ms → alerta. Detecteaza flooding si spoofing naiv.
2. **Frame-uri de la sursa gresita**: gateway-ul stie ce ID poate veni de pe ce interfata; un `0x201` care apare pe interfata de diagnostic = alerta imediata.
3. **Contoare de MAC/E2E esuate**: prag pe fereastra glisanta.
4. **Epoch stale rejections**: orice comanda respinsa de MCU pentru epoch vechi genereaza un eveniment (poate fi split-brain benign sau atac — se coreleaza cu starea clusterului).
5. **Anomalii de secventa in UDS**: cereri de securitate repetate, sesiuni deschise si nefolosite, scanare de DID-uri (multe `0x22` cu DID-uri inexistente) = comportament de atacator care mapeaza ECU-ul.
6. **Corelator**: mai multe alerte in aceeasi fereastra → nivel de incident (LOW/MED/HIGH) → reactie: log, DTC, restrictionare acces diagnostic, si la HIGH → refuzul sesiunilor de programare.

Toate evenimentele merg intr-un log de securitate separat, append-only, cu hash chaining (fiecare intrare contine hash-ul celei anterioare) → nu poate fi modificat retroactiv fara detectie.

**VERIFICARE.** `tests/integration/ids_*.cpp` — un script de atac (`tools/attack/`) executa fiecare vector din tabelul TARA; testul verifica ca alerta corecta apare in < 200 ms si ca **nu** apar alerte in trafic normal timp de 1h (rata de fals pozitiv = 0).

**DEMO.** D9.

### 17.4 Fuzzing ca politica, nu ca accesoriu
Toate parserele care ating date externe sunt fuzzate: CAN frame decoder, DBC parser, ISO-TP reassembly, SOME/IP header + payload, SOME/IP-SD entries/options, DoIP header, UDS request, manifest de flash.
- libFuzzer + ASan/UBSan/MSan, corpus versionat in `tests/fuzz/corpus/`, minimizat.
- CI: 60 s per target la fiecare PR (smoke), 1h per target in nightly.
- **Criteriu**: 0 crash-uri, 0 leak-uri, 0 timeouts. Orice finding devine un test de regresie in `tests/fuzz/regressions/`.

---

## 18. Power & thermal management — `platform/health` + `services/thermal`

**DE CE.** Platformele de compute automotive sunt limitate termic, nu computational. Managementul termic e o cerinta reala in fisele de post si e o sursa buna de comportament de degradare demonstrabil.

**CUM.**
- Surse reale de date pe Linux: `/sys/class/thermal/thermal_zone*/temp`, `/sys/class/hwmon/`, frecventa din `/sys/devices/system/cpu/cpufreq/`, plus RAPL (`/sys/class/powercap/intel-rapl/`) pentru consum estimat pe x86. Pe RPi: `vcgencmd measure_temp` / sysfs.
- **Model termic** pentru scenarii sintetice (cand nu poti incalzi fizic placa): `T[k+1] = T[k] + dt/C · (P(load) − (T[k] − T_amb)/R)`, cu R si C calibrate din masuratori reale (incarci CPU-ul, masori curba, faci fit). Asta e o mini-lucrare de identificare de sistem si suna excelent.
- **Politica pe 5 trepte**, cu histereza:
  | Stare | Prag | Actiune |
  |---|---|---|
  | NORMAL | < 65 °C | nimic |
  | ELEVATED | 65-75 °C | opreste tracing detaliat, reduce rata de logging |
  | THROTTLING | 75-85 °C | suspenda servicii LOW/BEST_EFFORT, reduce frecventa ADAS de la 50 la 20 Hz |
  | CRITICAL | 85-95 °C | migreaza serviciile non-safety pe alt nod, DTC P0217, HMI warning |
  | EMERGENCY | > 95 °C | pastreaza doar bucla de safety, notifica clusterul, pregateste transferul de proprietate |
- **Regula absoluta**: nicio treapta nu are voie sa atinga task-urile SAFETY_CRITICAL. Verificat prin test.
- Bugetare de putere pe cluster: fiecare nod raporteaza consumul estimat; un `PowerManager` central poate cere reducerea sarcinii optionale cand bugetul total e depasit.

**VERIFICARE.**
- `tests/unit/thermal_fsm.cpp` — histereza (nu oscileaza la pragul exact), toate tranzitiile.
- `tests/integration/thermal_load.cpp` — `stress-ng` incalzeste real CPU-ul; se verifica secventa de treceri si, critic, **K1 ramane indeplinit** in toate treptele.

**DEMO.** D7.

---

## 19. Simulare de vehicul — `simulation/`

**DE CE.** Ai nevoie de o sursa de date realista si repetabila. Fara model de vehicul, "wheel slip" si "ABS" sunt vorbe; cu model, ai un sistem in bucla inchisa in care comanda ta chiar schimba ce citesc senzorii.

**CUM.**

### 19.1 Modelul de dinamica (suficient, nu exagerat)
- **Bicycle model** cu 3 grade de libertate (longitudinal, lateral, yaw), pas de integrare 1 ms, Runge-Kutta 4.
  ```
  m·(v̇x − vy·r) = Fx_total − Fdrag − Frr
  m·(v̇y + vx·r) = Fy_f·cos(δ) + Fy_r
  Iz·ṙ          = a·Fy_f·cos(δ) − b·Fy_r
  ```
- **Dinamica de roata** individuala (4 roti): `J·ω̇ = T_drive − T_brake − Fx·R`, cu calculul slip-ului `λ = (v − ω·R)/max(v, ω·R, ε)`.
- **Model de anvelopa**: Pacejka "magic formula" simplificata (`Fx = D·sin(C·atan(B·λ))`), cu coeficient de aderenta `μ` configurabil per roata → asa simulezi gheata sub o singura roata (split-μ), scenariu clasic de ABS.
- **Transfer de sarcina** longitudinal si lateral (afecteaza forta normala pe fiecare roata, deci si aderenta) — ieftin de implementat, mare castig de realism.
- Determinist: pas fix, fara dependenta de wall-clock; ruleaza si in timp real, si accelerat ×100 pentru teste.

### 19.2 Modele de senzori
Fiecare senzor are un pipeline configurabil: `valoare reala → cuantizare → zgomot (gaussian, seed) → bias/drift → latenta → rata de esantionare → mod de defect`.

| Mod de defect | Parametri | Ce testeaza |
|---|---|---|
| `STUCK` | valoare | temporal check |
| `OFFSET` | delta | cross check |
| `DRIFT` | rata/s | detectie lenta |
| `NOISE_BURST` | amplitudine, durata | filtrare + range check |
| `DROPOUT` | probabilitate | timeout supervision |
| `SPIKE` | amplitudine, frecventa | rate check |
| `FROZEN_PLAUSIBLE` | — | cel mai greu: valoare fixa dar in range si plauzibila → detectat doar prin cross-check cu IMU |

### 19.3 Scenarii — `simulation/scenarios/*.yaml`
Un scenariu e un fisier declarativ, versionat, reproductibil:
```yaml
name: emergency_brake_split_mu_with_node_failure
seed: 0xC0FFEE
duration_ms: 8000
vehicle: { mass: 1500, wheelbase: 2.7, mu_default: 1.0 }
timeline:
  - at_ms: 0     ; action: set_speed        ; value: 27.8      # 100 km/h
  - at_ms: 1000  ; action: set_mu           ; wheel: FL ; value: 0.25
  - at_ms: 1500  ; action: driver_brake     ; value: 1.0       # panic brake
  - at_ms: 1800  ; action: inject           ; fault: node_failure ; target: NodeA
  - at_ms: 3000  ; action: inject           ; fault: can_loss     ; rate: 0.4
  - at_ms: 5000  ; action: clear_faults
expectations:
  - metric: max_yaw_rate            ; max: 0.35      # nu pleaca in derapaj
  - metric: stopping_distance_m     ; max: 62
  - metric: brake_command_gap_ms    ; max: 2
  - metric: unsafe_commands         ; equals: 0
  - metric: safety_state_final      ; equals: DEGRADED
  - dtc_present: [C0031, U0100]
```
Scenariile **sunt** teste: `volt-sim run scenarios/*.yaml --assert` ruleaza in CI si esueaza daca o asteptare nu e indeplinita.

**VERIFICARE.** Modelul in sine se valideaza prin: (a) conservarea energiei intr-o rulare fara frecare (eroare < 0,1% pe 10 s); (b) distanta de oprire teoretica `v²/(2·μ·g)` vs simulata (eroare < 5%); (c) comparatie a raspunsului la treapta de volan cu valori din literatura pentru un vehicul de referinta. Se documenteaza in `docs/SIMULATION.md` — nimeni nu se asteapta la validare industriala, dar se asteapta sa stii ca modelul trebuie validat.

**DEMO.** D2, D5, D7.

---

## 20. Fault injection — `simulation/faults` + `apps/volt-inject`

**DE CE.** Un sistem fault-tolerant care nu poate fi stricat la comanda nu poate fi demonstrat. Framework-ul de injectie e ce transforma afirmatiile in dovezi.

**CUM.** Taxonomie completa, fiecare cu punct de injectie definit si efect observabil:

| Categorie | Fault | Punct de injectie | Efect asteptat |
|---|---|---|---|
| **Compute** | `node_failure` | `kill -9` runtime / oprire alimentare | failover < 25 ms |
| | `node_freeze` | `SIGSTOP` (mai rau decat crash: nodul pare viu la nivel de link) | detectie prin heartbeat, nu prin TCP |
| | `service_crash` | `kill` proces serviciu | restart local < 50 ms |
| | `service_hang` | bucla infinita injectata prin rutina UDS | watchdog → restart |
| | `deadline_miss` | busy-wait injectat in task | DTC U3000, degradare |
| | `cpu_overload` | `stress-ng` in cgroup vecin | freedom from interference: K1 tine |
| | `memory_pressure` | alocare masiva in cgroup vecin | OOM izolat, safety neatins |
| | `alloc_violation` | `new` injectat in `no_alloc_scope` | detectat, contorizat, DTC |
| **Comunicatii** | `can_loss` | drop probabilistic in stratul CAN | timeout supervision, DTC |
| | `can_bus_off` | eroare fortata / scurt fizic pe HIL | recuperare cu backoff, RESTRICTED |
| | `can_flood` | injector de frame-uri la rata maxima | IDS, bus load, prioritizare |
| | `eth_loss/dup/reorder/delay` | `tc netem` (real!) sau `SimEnvironment` | QoS, retry, degradare |
| | `link_down` | `ip link set down` / cablu scos | partitie, lease, fencing |
| | `partition` | `iptables` intre subseturi de noduri | quorum, split-brain prevenit |
| **Date** | `sensor_*` | modele din §19.2 | plausibility, cross-check |
| | `bitflip` | flip pe un bit din payload | E2E CRC detecteaza |
| | `stale_data` | congelarea unui esantion | alive counter detecteaza |
| **Securitate** | `replay` | retrimiterea unui frame inregistrat | SecOC freshness respinge |
| | `forged_mac` | MAC gresit | respins, B1001 |
| | `stale_epoch` | comanda cu epoch vechi | respinsa de MCU, B1002 |
| | `rogue_tester` | scanare DID / brute-force key | IDS + delay timer |
| **Mediu** | `overtemp` | model termic fortat / incalzire reala | throttling, P0217 |
| | `power_loss` | taierea alimentarii MCU-ului pe HIL | NVM intact, boot corect |

Interfata unica, aceeasi in SIL, DST si HIL:
```bash
volt-inject node_failure --target NodeA --at now
volt-inject can_loss     --rate 0.4 --duration 5s
volt-inject sensor_fault --sensor wheel_FL --mode FROZEN_PLAUSIBLE
volt-inject partition    --groups "A|B,C" --duration 3s
volt-inject replay       --frame 0x201 --count 50
volt-inject campaign     --file campaigns/nightly.yaml   # sute de injectii scriptate
```
Injectiile sunt disponibile si prin UDS RoutineControl `0xF002` (cu security level 2), deci pot fi declansate de la un tester, exact ca la un ECU real de dezvoltare.

**VERIFICARE.** Fiecare tip de fault are un test dedicat care verifica **si** ca injectia chiar a avut loc (altfel testul trece degeaba): fiecare injectie emite un eveniment de trace confirmat de sistem, si testul asteapta acel eveniment inainte de a verifica reactia. Detaliul asta previne cea mai frecventa capcana din testarea de fault injection.

**DEMO.** Toate demo-urile.

---

## 21. Deterministic Simulation Testing + Record/Replay

Aici e diferentiatorul tehnic al proiectului.

### 21.1 DST — `apps/volt-dst`, `tests/dst/`

**DE CE.** Bug-urile de concurenta si de sisteme distribuite apar la intreteseri rare. Rularea in timp real le gaseste dupa luni; simularea determinista le gaseste in minute si, mai important, **le poate reproduce**.

**CUM.**
- Tot clusterul (3 noduri × N servicii + retea + vehicul) ruleaza **intr-un singur thread**, peste `SimEnvironment`.
- Ceasul e virtual: timpul avanseaza la urmatorul eveniment din coada. O rulare de 10 minute "de vehicul" dureaza ~2 secunde real.
- Reteaua e un model cu latenta, pierdere, reordonare, duplicare — toate derivate din `rng(seed)`.
- Planificarea thread-urilor logice, ordinea de livrare a mesajelor, momentul crash-urilor: toate din acelasi RNG.
- Un test = `seed` + `scenariu` + `invarianti`.
```bash
volt-dst run --seed 0x1234 --scenario brake_failover --checks all
volt-dst sweep --seeds 10000 --jobs 16 --scenario-set nightly --report out.json
volt-dst minimize --seed 0x1234   # reduce scenariul la pasii minimi care reproduc bug-ul
```
- **Invarianti verificati la fiecare pas** (nu doar la final):
  1. `AtMostOnePrimary` per serviciu.
  2. Nicio comanda de actuator acceptata cu epoch < max vazut.
  3. Nicio comanda in afara limitelor din REQ-SAF-001.
  4. Daca exista majoritate si un nod sanatos capabil, in ≤ 25 ms exista un detinator (liveness marginit).
  5. Nicio stare de serviciu nu diverge intre ACTIVE si STANDBY dincolo de fereastra de sincronizare.
  6. Fiecare fault injectat produce exact DTC-ul din tabel.
- **Minimizare automata** (delta debugging): cand un seed esueaza, runner-ul incearca sa elimine pasi din scenariu pana obtine reproducerea minima, si o salveaza ca test de regresie.

**VERIFICARE (a verificatorului).** Ca sa dovedesti ca DST-ul chiar prinde bug-uri, tii in repo un set de **bug-uri injectate intentionat** (`tests/dst/mutants/`): 12 mutatii cunoscute ale protocolului (ex: lease acordat inainte de expirare, epoch necomparat, snapshot aplicat in ordine gresita). CI-ul verifica periodic ca DST-ul le prinde pe toate 12 in < 1000 de seeds. Asta e mutation testing aplicat la un protocol distribuit — extrem de convingator.

**DEMO.** D9.

### 21.2 Record / Replay / time-travel — `apps/volt-replay`

**DE CE.** Cand ceva ciudat se intampla in demo-ul live sau pe HIL, vrei sa il poti studia dupa, ciclu cu ciclu. Si vrei sa poti demonstra ca sistemul e determinist.

**CUM.**
- In modul `--record`, runtime-ul scrie un jurnal binar cu **toate intrarile** fiecarui actor: mesaje primite (cu timestamp global), declansari de timer, valori returnate de `now()` si `random()`, evenimente de lifecycle. Overhead tintit < 5% (buffer per-CPU + scriere asincrona pe alt CPU).
- La replay, `ReplayEnvironment` reda exact aceleasi intrari. Dupa fiecare pas se compara `state_hash()` cu cel inregistrat → orice divergenta e semnalata imediat, cu pasul exact.
- Comenzi:
```bash
volt-replay verify session.vrec               # determinism bit-exact (KPI K14)
volt-replay goto  --t 12.418s                 # sari la un moment
volt-replay step  --actor BrakeControl --n 5  # pas cu pas
volt-replay watch --var integrator            # urmareste o variabila in timp
volt-replay why   --event SAFE_STATE          # subtrace cauzal: lantul de evenimente
                                              # care a dus la tranzitie
volt-replay export --perfetto out.pftrace
```
- `why` construieste un **subtrace cauzal minim**: pornind de la evenimentul tinta, urmareste inapoi dependentele (ce mesaj a declansat ce handler, ce fault a produs ce reactie) si arata doar lantul relevant. E exact ideea din debuggerele de trace si e ceva ce ai facut deja conceptual in zona de verificare RTL.

**VERIFICARE.** `tests/integration/replay_determinism.cpp`: 50 de sesiuni inregistrate (SIL si HIL), fiecare rejucata de 3 ori → 100% hash-uri identice. Orice esec inseamna o sursa de nedeterminism nedeclarata, si e tratat ca bug de arhitectura (violare D1).

**DEMO.** D9.

---

## 22. Observabilitate — `platform/trace`, `apps/volt-monitor`, `apps/volt-web`

**DE CE.** Un sistem distribuit pe care nu-l poti vedea nu poate fi depanat si, la interviu, nu poate fi aratat. Observabilitatea e si unealta ta de lucru, si wow factor-ul.

### 22.1 TUI — `volt-monitor` (ftxui)
```
┌─ VOLT SYSTEM MONITOR ──────────────────── t=00:04:12.418 · gPTP ±18us ─┐
│ NODE   STATE      CPU   MEM   TEMP   SERVICES        LEASES            │
│ A      ONLINE     64%   41%   54°C   7 ok            Brake(42) Steer(11)│
│ B      DEGRADED   91%   78%   72°C   6 ok / 1 restart  —                │
│ C      ONLINE     11%   09%   43°C   4 ok            —                  │
├────────────────────────────────────────────────────────────────────────┤
│ SERVICE          NODE  ROLE     PERIOD  P99      MISS  STATE            │
│ BrakeControl     A     ACTIVE    1 ms   241 us      0  HEALTHY          │
│ BrakeControl     B     STANDBY   1 ms     —         —  SYNCED (age 3ms) │
│ SensorFusion     B     ACTIVE    5 ms   802 us      0  HEALTHY          │
│ VehicleDynamics  A     ACTIVE   10 ms  2.13 ms      0  HEALTHY          │
│ Diagnostics      B     ACTIVE   20 ms  4.02 ms      1  RESTARTING       │
├─ COMMS ────────────────────────────────────────────────────────────────┤
│ CAN  load 41%  loss 0.3%  bus-off 0   ETH loss 0.1%  P99 428us          │
├─ VEHICLE ──────────────────────────────────────────────────────────────┤
│ v=87.4 km/h  ax=-0.2 g  yaw=0.04 rad/s  slip FL/FR/RL/RR: 2/2/1/1 %     │
│ brake cmd: 0.0 bar   mode: NORMAL   driver: coasting                    │
├─ FAULTS (2 active) ────────────────────────────────────────────────────┤
│ C0031 CONFIRMED  Wheel speed FL implausible      12 occ   t=00:03:58    │
│ P0217 PENDING    Node B overtemperature           1 occ   t=00:04:10    │
└────────────────────────────────────────────────────────────────────────┘
```

### 22.2 Dashboard web — `volt-web` (bridge C++ WebSocket + SPA React)
Patru panouri, toate alimentate de acelasi flux de telemetrie:
1. **Topologie** — noduri, servicii, lease-uri, sageti de comunicatie care se coloreaza la trafic si se rup vizual la partitie.
2. **Timeline** — activari de task-uri pe fiecare CPU al fiecarui nod, pe o axa de timp comuna (gPTP), cu marcaje pentru fault-uri si failover.
3. **Vehicul** — vedere 2D top-down: masina, cele 4 roti cu slip colorat, vectori de forta, curba de viteza si de comanda de frana in timp real.
4. **Control** — butoane de fault injection, sliderele de calibrare (generate din fisierul A2L), butoane de scenariu.

Regula: dashboard-ul e **doar consumator**. Nu poate influenta data plane-ul decat prin canalul de diagnostic autentificat. Asta e o decizie de securitate si o spui.

### 22.3 Export Perfetto
`volt-trace export --perfetto out.pftrace` → deschis in `ui.perfetto.dev`: vezi toate nodurile, toate thread-urile, toate mesajele ca fluxuri (flow events) intre procese si masini. Momentul in care arati un failover distribuit in Perfetto e momentul in care interviul isi schimba tonul.

### 22.4 Metrici
Model de metrici propriu (counter, gauge, histogram) cu export in doua formate: text pentru CLI si **Prometheus exposition format** pe un port HTTP (`/metrics`), ca sa poti rula Grafana daca vrei grafice istorice. Metrici cheie: `volt_task_jitter_us`, `volt_deadline_miss_total`, `volt_failover_duration_ms`, `volt_can_bus_load_ratio`, `volt_e2e_errors_total`, `volt_lease_epoch`, `volt_alloc_violations_total`.

**VERIFICARE.** `tests/integration/observability.cpp` — verifica ca fiecare eveniment important (failover, DTC, degradare) apare in toate cele trei canale (log, trace, metrici) cu acelasi timestamp global; si `tests/perf/trace_overhead.cpp` pentru K12.

---

## 23. Strategia de testare

**DE CE.** Un proiect de fault tolerance fara o strategie de testare explicita e o contradictie in termeni. Si asta e sectiunea pe care o citeste primul un inginer senior.

**CUM.** Sapte niveluri, fiecare cu rol clar:

| Nivel | Ce testeaza | Unealta | Cand ruleaza | Criteriu |
|---|---|---|---|---|
| **Unit** | logica pura, parsere, masini de stari | GoogleTest | fiecare commit | coverage > 85% pe core |
| **Property** | invarianti pe intrari generate | rapidcheck | fiecare commit | 1000 cazuri/proprietate |
| **Fuzz** | parsere expuse | libFuzzer + sanitizers | PR: 60 s; nightly: 1 h | 0 crash-uri |
| **Integration** | 2-3 procese reale, retea reala locala | GoogleTest + harness | fiecare commit | toate PASS |
| **DST** | protocoale distribuite, invarianti globali | `volt-dst` | PR: 500 seeds; nightly: 10.000 | 0 violari |
| **SIL** | scenarii de vehicul complete | `volt-sim --assert` | fiecare commit | toate asteptarile |
| **HIL** | hardware real, CAN fizic, actuator | runner self-hosted | nightly + inainte de release | toate PASS |
| **Soak** | stabilitate lunga | harness | saptamanal, 24 h | 0 leak-uri, 0 misses, memorie plata |
| **Perf** | regresii de performanta | GoogleBenchmark + compare | fiecare PR | fara regresie > 10% |

Reguli suplimentare:
- **Fiecare bug reparat devine un test.** Fisier `tests/regressions/` cu numele issue-ului.
- **Testele nu au `sleep()` arbitrar.** Sincronizarea se face pe evenimente si pe ceasul virtual, altfel testele devin instabile si ajungi sa le ignori. Un test flaky se repara sau se sterge in aceeasi zi.
- **Fiecare test declara ce cerinta acopera** (`// @verifies REQ-SAF-014`), altfel tool-ul de traceability semnaleaza cerinte neacoperite.

---

## 24. CI/CD — `.github/workflows/`

**DE CE.** Automatizarea e ce face diferenta intre "un proiect mare" si "un proiect mare care inca functioneaza dupa 6 luni". Si e mentionata explicit in fisele de post.

**CUM.** Cinci workflow-uri:

**`ci.yml`** (la fiecare push/PR, tinta < 15 min):
```
matrix: {gcc-14, clang-19} × {Debug, RelWithDebInfo}
  ├─ clang-format --dry-run --Werror
  ├─ clang-tidy pe fisierele modificate (cu setul AUTOSAR)
  ├─ build (warnings = errors)
  ├─ verificare simboluri interzise pe services/ si safety/
  ├─ unit + property tests
  ├─ build ASan+UBSan → unit + integration
  ├─ build TSan → integration (concurenta)
  ├─ DST: 500 seeds
  ├─ SIL: toate scenariile cu --assert
  ├─ interop: udsoncan, can-isotp, cantools, doipclient (in container)
  ├─ fuzz smoke: 60 s/target
  ├─ coverage → gate 85% pe platform/, safety/, communication/
  ├─ tools/traceability → esueaza daca o cerinta nu are test
  └─ artefacte: raport KPI partial, coverage HTML, trace-uri
```
**`nightly.yml`**: DST 10.000 seeds (16 job-uri paralele), fuzz 1h/target, soak 4h, build cross aarch64, build QNX, model checking TLA+.
**`perf.yml`** (runner self-hosted cu PREEMPT_RT si CPU izolate): benchmark-uri, comparatie cu baseline-ul de pe `main`, comentariu automat pe PR cu tabelul de regresii. Statistica facuta corect: minim 10 rulari, comparatie pe mediana si P99, prag de zgomot masurat.
**`hil.yml`**: declansat manual si nightly, pe runner-ul cu placa conectata; ruleaza suita HIL si publica log-urile MCU + video-ul? (nu — publica log-uri si grafice; video-ul il faci manual pentru README).
**`release.yml`**: tag → build release, semnare imagini, generare `docs/`, publicare artefacte + raport KPI complet.

**Gate-uri obligatorii pentru merge**: build curat, toate testele, coverage, fara regresie de performanta, traceability completa, changelog actualizat.

---

## 25. Performanta — metodologie si tinte

**DE CE.** Numerele fara metodologie nu inseamna nimic, iar un inginer senior te va intreba imediat "pe ce ai masurat si cum".

**CUM (metodologia, scrisa in `docs/PERFORMANCE.md`):**
- Mediu: kernel PREEMPT_RT, `isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3`, IRQ-uri mutate de pe CPU-urile izolate, `cpupower frequency-set -g performance`, turbo si C-states dezactivate, `mlockall(MCL_CURRENT|MCL_FUTURE)`, stiva pre-atinsa.
- Baseline: `cyclictest -m -p 80 -i 1000 -l 3600000` rulat inainte de orice masuratoare, cu rezultatul publicat — daca latenta de baza a sistemului e 300 µs, jitterul tau de 41 µs ar fi imposibil, si asta se vede.
- Masurare: `rdtsc`/`clock_gettime(CLOCK_MONOTONIC_RAW)` calibrat, HDR histogram, minim 1M esantioane, raportare P50/P99/P99.9/max (**niciodata doar media**), plus interval de incredere.
- Warm-up ignorat explicit (primele 1000 de iteratii), cache-uri incalzite, si o rulare de control fara warm-up pentru a arata efectul.
- Toate scripturile in `tools/kpi_report/`; `make kpi` regenereaza `docs/KPI_REPORT.md` cu grafice.

**Tabelul de tinte** = tabelul KPI din §0.2. Fiecare rand are: scriptul care il masoara, mediul, data ultimei masuratori si commit-ul.

**Optimizari planificate si masurate (nu ghicite):**
1. Profilare cu `perf record` + flamegraph inainte de orice optimizare.
2. Cache: alinierea structurilor la linie de cache, false sharing eliminat (verificat cu `perf c2c`).
3. Branch prediction: `[[likely]]` pe calea nominala din bucla de control.
4. Syscalls: eliminate din calea critica (masurat cu `strace -c` pe un ciclu).
5. Copii: numarul de copii per mesaj masurat si redus la 0 pe calea intra-nod.
Fiecare optimizare intra in repo cu masuratoarea "inainte/dupa" in commit message. Asta e o practica pe care o vei folosi si la job.

---

## 26. HIL — hardware real, buget zero

**DE CE.** Diferenta dintre un proiect de student si un proiect de inginer e ca al doilea misca ceva in lumea reala si masoara ce s-a intamplat. Hardware-ul aduce probleme pe care simularea nu ti le da: zgomot, cuantizare, latenta de driver, fire care se desprind, nivele de tensiune, resetari.

### 26.1 Ce folosesti (tot ce ai deja)

| # | Componenta | Rol in VOLT | Ce demonstreaza |
|---|---|---|---|
| 1 | **Laptop, Linux** | Node A (host) + Node B (VM KVM) + Node C (network namespace) | cluster distribuit, partitionare, failover |
| 2 | **Arduino Uno R4 WiFi** (Renesas RA4M1, 5 V, matrice LED 12×8, WiFi + BLE) | **Actuator ECU**: verifica E2E + SecOC + epoch, afiseaza presiunea de frana pe matrice, scoate comanda pe PWM | fencing in hardware, safe state fizic, doua canale de comunicatie (UART + WiFi) |
| 3 | **ESP32** (dual-core, 3,3 V, WiFi + BLE, TWAI/CAN, timer 80 MHz) | **Safety monitor (Nivel 2)**: recalculeaza comanda permisa, question/answer, detine linia de ENABLE | monitorizare independenta, calea de oprire hardware, masurarea latentei |
| 4 | **Arduino Uno R3** (AVR ATmega328P, 8 biti, 5 V) | **Nivelul 3 E-Gas**: question/answer catre ESP32, detine linia ENABLE, watchdog hardware | monitorizare independenta pe al treilea tip de siliciu, cale de oprire care nu trece prin laptop |
| 5 | Fire de legatura, breadboard (ce ai) | ENABLE, legatura analogica, masa comuna | — |
| 6 | Camera laptopului | perceptie ADAS (§55) | functie ADAS reala, cu incertitudine reala |

**Cost total: 0 lei.** Daca vreodata apar 25 de lei pentru doua transceivere CAN, montajul se extinde la un bus CAN fizic **fara nicio modificare de software** — pentru ca stratul de transport e o abstractizare (`ICanDriver`), si asta e chiar unul dintre argumentele de arhitectura. Pana atunci, backendul e tunelul.

### 26.2 Transportul: `volt-cantun` (tunel CAN peste UART/UDP)

**DE CE.** Fara transceivere nu ai bus CAN diferential. Dar stiva CAN nu are nevoie de bus ca sa fie corecta: are nevoie de un canal care transporta frame-uri. Industria face acelasi lucru (tunelare CAN peste Ethernet) cand testeaza distribuit.

**CUM.**
- Pe Linux: `vcan0` (sau `vxcan` intre namespace-uri) e busul logic. `volt-cantun` citeste frame-urile de pe `vcan0` si le incapsuleaza intr-un cadru propriu (`SOF | len | can_id | flags | payload | CRC16`) pe **UART** (1 Mbit catre R4 si R3) si pe **UDP** (catre ESP32 si R4 prin WiFi).
- Pe MCU-uri: acelasi decodor, aceeasi structura de frame, acelasi cod de E2E si SecOC ca pe PC (fisiere partajate intre `platform/` si `firmware/`, compilate pentru ambele).
- **Simularea proprietatilor de bus** ramane in software si e testabila: arbitraj dupa ID (cozile de transmisie sunt ordonate dupa prioritate), bus load calculat din numarul de biti pe secunda la bitrate-ul configurat, si moduri de eroare injectabile (bus-off, error-passive, ACK error) cu exact aceleasi efecte in stiva ca pe hardware.
- **ESP32 in mod TWAI self-test**: controllerul CAN real din ESP32 genereaza si receptioneaza frame-uri proprii, fara transceiver. Il folosesti ca sa demonstrezi ca encoderul tau de frame produce ceva ce **siliciu CAN real** accepta — o validare care nu costa nimic si care e greu de contestat.

**VERIFICARE.** `tests/integration/cantun_fidelity.cpp`: 100.000 de frame-uri aleatorii trec `vcan0 → tunel → MCU → tunel → vcan0` si trebuie sa se intoarca bit-identic, cu latenta masurata. `tests/hil/twai_selftest.cpp`: frame-urile generate de codul tau sunt acceptate de controllerul TWAI din ESP32.

### 26.3 Actuatorul si senzorul — bucla fizica

**Actuator (Uno R4 WiFi).** Comanda de frana (0-100%) se materializeaza in doua forme simultan:
1. **Matricea LED 12×8** ca bara de presiune — vizual, imediat, perfect pentru video-ul de demo.
2. **Iesire PWM** pe un pin, filtrata simplu (media in software pe partea de citire) — semnalul analogic *este* actuatorul.

Pinul de **ENABLE** e o intrare controlata de ESP32: cand e LOW, firmware-ul R4 forteaza PWM = 0 si matricea afiseaza starea sigura, **indiferent** de ce comanda a primit. Verificarea se face si in hardware (pinul e citit inainte de fiecare scriere) si logic (nu exista cale de cod care sa scrie PWM fara sa treaca prin verificare).

**Senzori — doua canale independente, si de aici iese redundanta reala.** Aceeasi linie analogica (iesirea PWM a lui R4) e citita de doua dispozitive diferite:
1. **Uno R4 isi citeste inapoi propria iesire** pe A0 — asta e *diagnoza etajului de iesire* (output stage readback), o tehnica standard: ECU-ul confirma ca ce a comandat chiar a ajuns pe pin. Diferenta intre comanda si readback peste un prag → fault intern, DTC, safe state.
2. **ESP32 citeste aceeasi linie** pe ADC-ul lui, complet independent (alt convertor, alta referinta, alt ceas), o converteste in "viteza de roata" printr-un model invers si o trimite pe WiFi ca frame `0x102` cu E2E si alive counter.

Cele doua citiri se compara in `SensorFusionService`: ai un **cross-check 2oo2 pe hardware real**, cu doua ADC-uri care se comporta diferit (offset, zgomot, neliniaritate). Nu mai e un test sintetic — e exact situatia din care apar C0031-urile adevarate. Si iti da o injectie de defect gratuita: acoperi cu degetul pinul, sau scoti unul dintre fire, si vezi divergenta detectata.

**Injectii fizice de defect, gratuite:**
| Actiune fizica | Defect simulat | Reactie asteptata |
|---|---|---|
| scoti firul analogic | senzor pierdut | timeout supervision → C0031 → DEGRADED |
| atingi firul cu degetul | zgomot / valori implauzibile | range + rate check |
| scoti USB-ul de la R3 | nod de senzor cazut | detectie pe doua canale, fallback |
| tii ENABLE la LOW | monitorul opreste actuatorul | safe state fizic, latch, DTC |
| resetezi R4 in mijlocul unei comenzi | ECU de actuare repornit | epoch pastrat in flash, refuzul comenzilor vechi |
| pornesti si un al doilea "primary" fals de pe laptop | split-brain | R4 refuza epoch-ul vechi (D4) |

### 26.4 Firmware

**Uno R4 WiFi** (`firmware/uno_r4_actuator/`) — Arduino core sau, mai bine pentru CV, **Zephyr** (RA4M1 e suportat) sau FSP-ul Renesas. Recomandare: incepe pe Arduino core ca sa functioneze repede, apoi porteaza pe Zephyr ca exercitiu — si documenteaza portarea, e o sectiune buna.
Functii: receptie pe UART **si** WiFi (doua canale independente — detectia dubla din §13.1 devine reala), verificare CRC E2E + alive counter + AES-CMAC trunchiat + epoch, `max_epoch_seen` salvat in memoria nevolatila, timeout de 20 ms → rampa catre stare sigura, matrice LED, PWM, raportare de stare la 100 ms (contoare de refuzuri, erori de MAC, timeouts).

**ESP32** (`firmware/esp32_monitor/`) — ESP-IDF (FreeRTOS). Functii: model simplificat de comanda permisa (Nivel 2), question/answer catre PC si catre Nivel 3, controlul liniei ENABLE, masurarea cu timer de 80 MHz a latentei comanda-invalida → oprire (K21), server UDP pentru telemetrie, TWAI self-test.

**Uno R3** (`firmware/uno_r3_sensor/`) — Arduino core, cod mic si determinist: esantionare, filtrare, incadrare in frame, transmisie.

**Cod partajat**: E2E, CRC, CMAC, structurile de frame si tabelul de semnale generat din DBC sunt **acelasi cod sursa** pe PC si pe MCU-uri (subset C++17 fara alocari, fara exceptii). Un test in CI compileaza acelasi fisier pentru host, AVR, RA4M1 si Xtensa si compara rezultatele pe 10^6 vectori. Asta e o dovada foarte tare de portabilitate si de corectitudine.

### 26.5 Suita HIL

| Test | Ce dovedeste | Cum se masoara fara aparate |
|---|---|---|
| `hil_loop_latency` | latenta comanda → actuator → senzor → bucla | timestamp pe PC + contor de 80 MHz pe ESP32, corelate printr-un puls comun |
| `hil_node_failure` | `kill -9` pe Node A: comanda continua de pe Node B | matricea LED nu se stinge; log de discontinuitate |
| `hil_split_brain` | partitie intre namespace-uri; un "zombie" trimite epoch vechi | log R4: `REJECTED epoch=42 (current=43)` |
| `hil_enable_cut` | ESP32 taie ENABLE la comanda invalida | K21, masurat pe ESP32 |
| `hil_sensor_divergence` | scoti unul dintre cele doua fire de citire analogica, de 50 de ori | cross-check 2oo2 detecteaza divergenta, DEGRADED corect, fara comenzi nesigure |
| `hil_reset_storm` | R4 resetat de 200 de ori in timpul comenzilor | epoch persistent, fara acceptare de comenzi vechi |
| `hil_replay` | frame-uri inregistrate retrimise pe tunel | respinse de SecOC/freshness |
| `hil_wifi_loss` | oprirea WiFi-ului | comutare pe canalul UART, DTC de comunicatie, fara migrare de servicii |
| `hil_soak_12h` | stabilitate | 0 deadline misses, memorie plata, 0 refuzuri false |

Rezultatele in `docs/HIL_REPORT.md` + **un video de 60 s**: matricea LED care continua sa afiseze presiunea in timp ce omori un nod. E cea mai buna reclama posibila si costa zero.

---

## 27. Portabilitate: QNX si virtualizare

### 27.1 Port QNX — `platform/pal/qnx/`

**DE CE.** QNX apare explicit in cerintele de la NTT DATA si AROBS pe zona Brasov. Un port real, chiar limitat, valoreaza infinit mai mult decat "familiar with QNX" pe CV.

**CUM.**
- QNX SDP 8.0 are licenta gratuita non-comerciala; se instaleaza QNX Momentics si se ruleaza intr-o VM x86_64.
- Se porteaza **doar PAL-ul**: timp (`ClockCycles`, `clock_gettime`), thread-uri si prioritati (QNX are 256 de nivele si scheduling FIFO/RR/sporadic — mapare documentata), shared memory (`shm_open`/`mmap` — API POSIX, deci portabil), IPC nativ QNX (`MsgSend`/`MsgReceive`/`MsgReply` — implementat ca al doilea backend de IPC, ceea ce e chiar interesant de comparat cu shm-ul Linux), socket-uri (stack io-pkt), timere.
- Ce ruleaza pe QNX: PAL + scheduler + IPC + un serviciu de control simplu, cu **acelasi test suite** pentru PAL si scheduler.
- Ce **nu** ruleaza: SocketCAN (nu exista pe QNX — se documenteaza ca abstractizarea `ICanDriver` are un backend QNX neimplementat, si de ce).
- Livrabil: `docs/PORTABILITY.md` cu tabelul "feature × Linux × QNX × Sim", masuratori comparative de latenta IPC si de jitter intre Linux PREEMPT_RT si QNX pe aceeasi masina. **Comparatia asta e un capitol pe care putini il au.**

**VERIFICARE.** CI nightly: build QNX (cross-compile) + rularea testelor de PAL in VM-ul QNX prin ssh; rezultat publicat.

### 27.2 Virtualizare — Node C in KVM

**DE CE.** Platformele automotive moderne ruleaza domenii multiple pe acelasi SoC, separate prin hypervisor (QNX Hypervisor, Xen). Nu scriem un hypervisor, dar demonstram si masuram deployment-ul virtualizat, ceea ce e exact ce se face in practica.

**CUM.** Node C (gateway + diagnostics + HMI) ruleaza intr-o VM KVM/QEMU cu:
- CPU-uri pinuite (`vcpupin`), `hugepages`, driver `vhost-net` sau, si mai bine, **SR-IOV/`macvtap`** pentru retea cu overhead mic;
- interfata CAN pasata in VM prin USB passthrough (adaptorul USB-CAN) — deci VM-ul chiar vorbeste pe busul fizic;
- masurarea overhead-ului: latenta IPC, latenta de retea si jitter in VM vs pe metal, publicate in `docs/PORTABILITY.md`.

**VERIFICARE.** `tests/integration/vm_deployment.cpp` ruleaza acelasi set de teste de integrare cu Node C in VM; criteriu: toate trec, cu tinte de latenta relaxate cu un factor documentat.

---

## 28. Documentatie, cerinte si traceability

**DE CE.** In automotive, procesul e jumatate din meserie. Automotive SPICE apare in fisele de post. Nu poti "certifica" nimic singur, dar poti demonstra ca intelegi lantul cerinta → arhitectura → design → cod → test → rezultat, si ca il tii coerent automat.

### 28.1 Setul de documente (toate obligatorii, toate scrise)
| Fisier | Continut |
|---|---|
| `README.md` | pitch, GIF cu demo-ul de failover, quickstart in 5 comenzi, tabelul KPI |
| `docs/ARCHITECTURE.md` | vederi C4 (context, containere, componente), decizii, diagrame |
| `docs/DESIGN.md` | design detaliat per modul, structuri de date, algoritmi |
| `docs/adr/ADR-001..0NN.md` | Architecture Decision Records: context, optiuni, decizie, consecinte |
| `docs/SAFETY.md` | HARA, safety goals, FTTI, masina de degradare, argumentatie |
| `docs/CYBERSECURITY.md` | TARA, contramasuri, chei, secure boot, IDS |
| `docs/COMMUNICATION.md` | matricea CAN, cataloagele SOME/IP, formate pe fir, exemple hexa |
| `docs/DIAGNOSTICS.md` | servicii UDS, DID-uri, DTC-uri, secvente exemple |
| `docs/PERFORMANCE.md` | metodologie, rezultate, comparatii, flamegraph-uri |
| `docs/TESTING.md` | strategia, cum rulezi fiecare nivel |
| `docs/FAULT_INJECTION.md` | catalogul de fault-uri si reactiile asteptate |
| `docs/TRACEABILITY.md` | **generat automat** |
| `docs/KPI_REPORT.md` | **generat automat** |
| `docs/PORTABILITY.md` | Linux vs QNX vs VM vs Sim |
| `docs/HIL_REPORT.md` | rezultate pe hardware |
| `docs/DEVIATIONS.md` | deviatii de la regulile de cod, cu justificare |
| `docs/GLOSSARY.md` | termeni |

Diagrame: PlantUML/Mermaid **in repo ca text**, randate in CI (nu imagini desenate manual care se invechesc). Tipuri: context, deployment, secventa (failover, UDS session, boot), masini de stari (degradare, E2E, DTC, ISO-TP), flux de date, arbore de defecte (FTA) pentru cele 6 hazarde.

### 28.2 Tool-ul de traceability — `tools/traceability`

**CUM.** Un script Python care:
1. Parseaza `requirements/*.md` → toate ID-urile `REQ-*` cu textul lor si legatura la `SG-*`.
2. Scaneaza sursele dupa `// @satisfies REQ-xxx` si `// @tla Module!Action`.
3. Scaneaza testele dupa `// @verifies REQ-xxx` si numele testelor.
4. Citeste rezultatele ultimei rulari CI (JUnit XML).
5. Genereaza `docs/TRACEABILITY.md`:
```
REQ-SAF-001  BrakeControl shall not output pressure without valid request
   design   : docs/DESIGN.md#brake-guard
   code     : services/brake_control/guard.cpp:41
   tests    : brake_guard.NoCommandWithoutRequest        PASS
              brake_no_spurious (DST, 10000 seeds)       PASS
   status   : COVERED / VERIFIED
```
6. **Esueaza build-ul** daca: o cerinta nu are implementare, o cerinta nu are test, un test referentiaza o cerinta inexistenta, sau un safety goal nu are lant complet.

**VERIFICARE.** Tool-ul are testele lui (`tools/traceability/tests/`) cu repo-uri sintetice.

### 28.3 Managementul cerintelor
Cerintele traiesc in Markdown, versionate cu codul (nu in Jira), fiecare cu: ID stabil, sursa (safety goal / TARA / functional), text testabil (fara "should be fast"), criteriu de verificare, status. Modificarea unei cerinte cere modificarea testelor in acelasi PR — regula scrisa in `CONTRIBUTING.md`.

---

## 29. Plan de executie — faze, livrabile, criterii de terminare

Nu exista estimari de timp in acest plan, intentionat. Singurul lucru care conteaza e **ordinea de dependenta** si faptul ca fiecare treapta se termina cu ceva demonstrabil si testat. Nu treci mai departe cu datorii de test.

### 29.1 Trunchiul (ordine obligatorie — fiecare treapta depinde de cea de dinainte)

| # | Treapta | Se termina cand |
|---|---|---|
| T0 | Repo, CMake, CI cu toate gate-urile, ADR-uri initiale | CI verde pe un "hello world" care trece prin format, tidy, sanitizers, coverage |
| T1 | Platform: PAL, erori, logging lock-free, tracing + Perfetto, config validata, pool-uri, `no_alloc_scope` | K10 si K12 masurate |
| T2 | Actor + Environment + IPC zero-copy | K3 masurat; simbolurile interzise blocate in CI |
| T3 | Scheduler (TT/RM/EDF), RTA, deadline monitoring, watchdog pe 3 niveluri, lifecycle | K1, K2, K11 masurate pe PREEMPT_RT |
| T4 | Simulator de vehicul + senzori cu moduri de defect + scenarii cu asertiuni | 5 scenarii ruleaza in CI |
| T5 | CAN-FD + DBC + codegen + E2E | interop `cantools`/`candump` pe 100k vectori |
| T6 | Servicii de control + bucla inchisa in SIL | scenariul de franare de urgenta trece cu asertiuni numerice |
| T7 | Ethernet + SOME/IP + SD + DoIP + gPTP | Wireshark decodeaza; interop `vsomeip`; K17 |
| T8 | ISO-TP + UDS + DTC + NVM | interop `udsoncan` + `can-isotp`; fuzzing curat |
| T9 | Safety: HARA, cerinte, plausibility, redundanta duala, fault manager, degradare | traceability completa pentru SG-01..06 |
| T10 | Distributed runtime: SWIM, Raft, lease + fencing, succesiune preautorizata, placement, replicare, failover | K6, K7, K9 masurate; 100 de failover-uri fara comenzi nesigure |
| T11 | DST + record/replay + minimizare | K13, K14; cele 12 mutante prinse |
| T12 | HIL v1: tunel CAN, firmware pe Uno R4 + ESP32 + Uno R3, bucla fizica inchisa | K5 masurat; demo-ul in care matricea LED nu tresare la caderea unui nod |

Dupa T12 ai deja proiectul complet ca poveste. Tot ce urmeaza il face **mai bun si mai greu de egalat**, si se poate ataca in orice ordine.

### 29.2 Ramuri de aprofundare (independente intre ele, dupa T12)

| Ramura | Continut | Adauga |
|---|---|---|
| **R-SAFETY** | E-Gas pe 3 niveluri distribuit pe trei tipuri de siliciu, calea de oprire pe fir, FMEDA, FTA generata, memory integrity (§46, §47) | greutatea de safety reala |
| **R-TIME** | shaper time-aware in software, `taprio`, sincronizare verificata cu PPS improvizat, sinteza de tabela TT cu Z3, WCET static (§49, §50) | determinism demonstrat, nu declarat |
| **R-PLATFORM** | matricea de izolare (proces/RT/cgroups/netns/KVM), Xen daca boot-eaza, QNX in VM, Yocto in QEMU, ARM64 emulat (§48) | portabilitate si partitionare |
| **R-SEC** | PKI + UDS 0x29, SecOC cu rotatie de chei, semnaturi hibride ECDSA + ML-DSA, IDS extins, fault injection fizic (§51) | zona de cybersecurity |
| **R-TOOLING** | XCP + A2L cu DAQ, FMI/FMU, bridge ROS 2, CBMC, chaos continuu (§52, §53, §54) | interoperabilitate cu tooling industrial |
| **R-ADAS** | lidar 2D + camera, detectie de banda, ACC pe obiect real, planificare simpla (§55) | continut real in domeniul ADAS |
| **R-AUTOSAR** | API in stil `ara::com` (proxy/skeleton/event/field) + manifest de executie (§56) | limbaj comun cu Adaptive AUTOSAR |

**Cum arata munca zilnica**: un branch per feature, PR cu descriere care contine cerinta acoperita si masuratoarea inainte/dupa, merge doar cu CI verde. Un jurnal `docs/devlog/YYYY-MM.md` cu ce ai invatat si ce a mers prost — la interviu, jurnalul asta e aur, pentru ca arata cum gandesti, nu doar ce ai livrat.

---

## 30. Demo-uri — cum demonstrezi ca functioneaza

Regula pentru toate: **scriptate, sub 5 minute, repetabile pe orice masina**, cu output care se explica singur. Fiecare demo are un script in `demos/` si o inregistrare (asciinema + video pentru cele cu hardware) in README.

### D1 — Boot si descoperire (90 s)
```bash
make demo-boot
```
Ce se intampla: pornesc 3 runtime-uri, se sincronizeaza pe ceas, se formeaza clusterul, Raft alege leader, se plaseaza 17 servicii, se descopera prin SOME/IP-SD, sistemul ajunge RUNNING.
Ce arati: timpul total de boot (K11), tabela de plasare, si `tcpdump`/Wireshark cu mesajele SD reale decodate de dissectorul standard.
Ce dovedeste: descoperire, orchestrare, conformitate cu protocolul.

### D2 — Bucla de control in bucla inchisa (2 min)
```bash
make demo-control    # scenario: emergency_brake_split_mu
```
Ce se intampla: vehiculul simulat merge cu 100 km/h, roata FL are μ = 0,25 (gheata), soferul franeaza brusc; ABS-ul moduleaza per roata.
Ce arati: dashboard-ul cu slip pe fiecare roata, comanda de frana, distanta de oprire; apoi `volt-monitor perf` cu jitter si deadline misses = 0.
Ce dovedeste: sistem real-time functional, control care chiar face ceva, K1/K2.

### D3 — Failover de nod (2 min) ★ demo-ul principal
```bash
make demo-failover
# in alt terminal, in mijlocul franarii:
volt-inject node_failure --target NodeA
```
Ce se intampla: Node A moare in timpul unei franari; B preia in < 25 ms.
Ce arati:
1. graficul comenzii de frana — **linie continua**, fara salt, cu momentul failover-ului marcat;
2. cronologia automata (detectie / decizie / preluare) cu bugete;
3. Perfetto: timeline distribuit al tuturor nodurilor;
4. pe HIL: **bara de pe matricea LED nu tresare**, iar Uno R3 raporteaza in continuare valori coerente.
```
=== FAILOVER REPORT ===
  fault injected      t = 12.418 000 s   (NodeA runtime SIGKILL)
  last cmd from A     t = 12.418 412 s   epoch 42
  detection complete  t = 12.430 100 s   (+11.7 ms)  SWIM + CAN heartbeat: A=FAULTY
  ownership taken     t = 12.432 900 s   (+14.5 ms)  succesor preautorizat, epoch 43
  state restored      t = 12.436 000 s   (+17.6 ms)  snapshot age 3.1 ms
  first cmd from B    t = 12.437 300 s   (+18.9 ms)  accepted by MCU
  command gap         1.9 ms  (2 cycles)  [K9 PASS]
  unsafe commands     0                   [PASS]
  safety state        OPERATIONAL → DEGRADED (U0100)
```

### D4 — Split-brain (2 min) ★ demo-ul care castiga interviul
```bash
make demo-splitbrain
volt-inject partition --groups "A|B,C" --duration 5s
```
Ce se intampla: Node A e izolat de retea, dar e viu si crede ca inca detine frana. B primeste epoch 43. A **se retrage singur** inainte de expirarea lease-ului. Pentru a arata a doua bariera, rulezi si varianta cu retragerea dezactivata artificial (`--force-zombie`): A continua sa trimita comenzi cu epoch 42, iar **MCU-ul le respinge fizic**.
Ce arati: log-ul de pe Uno R4 cu `REJECTED epoch=42 (current=43)`, contorul de refuzuri crescand, actuatorul care asculta doar de B, si DTC-ul B1002.
Ce dovedeste: intelegi split-brain, fencing tokens, defense in depth, si diferenta dintre "merge de obicei" si "e sigur prin constructie".

### D5 — Senzor care minte (90 s)
```bash
make demo-sensor
volt-inject sensor_fault --sensor wheel_FL --mode FROZEN_PLAUSIBLE
```
Ce se intampla: senzorul raporteaza o valoare fixa dar plauzibila (nu e detectabila prin range check). Cross-check-ul cu IMU si cu celelalte roti o prinde in ~28 ms.
Ce arati: momentul detectiei, DTC C0031 cu snapshot, trecerea pe algoritmul de fallback, comportamentul vehiculului inainte/dupa.
Ce dovedeste: plausibility real, nu decorativ.

### D6 — Retea sub atac de congestie (2 min)
```bash
make demo-network
volt-inject eth_loss --rate 0.4 ; volt-inject can_flood --rate max
```
Ce arati: latenta P99 pe VLAN 10 (control) **aproape neschimbata** in timp ce VLAN 30 (logging) e sufocat — datorita prioritizarii 802.1p si a shaper-ului time-aware; bus load CAN la 95%, IDS-ul care semnaleaza flooding-ul, sistemul care intra in DEGRADED controlat si revine curat.
Ce dovedeste: QoS real pe hardware real, degradare controlata, detectie de intruziune.

### D7 — Degradare in cascada (2 min)
```bash
make demo-degradation
```
Secventa scriptata: overtemp pe B → throttling → cade un senzor → DEGRADED → cade al doilea senzor → LIMP_HOME → cade nodul A → migrare + oprirea serviciilor optionale → violare de deadline pe calea safety → SAFE_STATE cu latch.
Ce arati: fiecare tranzitie cu conditia care a declansat-o, DTC-ul, si timpul de reactie fata de deadline-ul din tabel.
Ce dovedeste: comportamentul la defecte e **definit**, nu emergent.

### D8 — Diagnostic si OTA (3 min)
```bash
volt-diag --transport doip --ecu safety session extended
volt-diag read-did F190 F189 0110 0300
volt-diag dtc read --status confirmed
volt-diag dtc snapshot C0031
volt-diag security-access --level 2
volt-diag routine start F001            # self-test
volt-diag flash --file build/mcu_v2.bin --slot auto
volt-diag dtc clear
```
Ce arati: un tester independent (`udsoncan` in Python) facand acelasi lucru — dovada de conformitate; update-ul MCU cu rollback fortat; clusterul care continua sa franeze in tot acest timp.
Ce dovedeste: UDS/DoIP corecte, securitate, OTA sigur.

### D9 — Rigoare: DST, replay, formal (3 min)
```bash
volt-dst sweep --seeds 2000 --scenario-set core        # 2000 de universuri in ~40 s
volt-dst run --seed 0x8E31 --explain                    # bug-ul reprodus exact
volt-replay why --event SAFE_STATE --session demo.vrec  # lantul cauzal minim
tlc formal/tla/Lease.tla -config Lease.cfg              # invariantul verificat
```
Ce arati: cum un bug gasit la seed-ul X e reprodus identic de 3 ori, minimizat automat la 6 pasi, si transformat in test de regresie; apoi contraexemplul TLC dintr-o versiune veche a protocolului.
Ce dovedeste: nivelul de inginerie. Asta e demo-ul dupa care intrebarile devin "cand poti incepe".

### D10 — Tur de 60 de secunde (pentru recrutori si LinkedIn)
Un video montat: boot → franare ABS → `kill -9` → matricea LED care continua sa afiseze presiunea → cronologia pe ecran → partitie de retea → placa care respinge epoch-ul vechi → dashboard. Fara narativ tehnic, doar imagini si numere. Asta e ce pui in README, sus de tot.

---

## 31. Matrice completa: functionalitate → de ce → cum se verifica → demo

| # | Functionalitate | Modul | Cerinte | Test principal | KPI | Demo |
|---|---|---|---|---|---|---|
| 1 | PAL POSIX/QNX/Sim | `platform/pal` | REQ-PLT-001..008 | acelasi suite pe 3 backend-uri | — | D1 |
| 2 | Logging lock-free | `platform/log` | REQ-PLT-010 | `log_perf`, `log_concurrent` | K12 | D2 |
| 3 | Tracing + Perfetto | `platform/trace` | REQ-PLT-012 | `trace_overhead`, `perfetto_valid` | K12 | D3 |
| 4 | Pool-uri + `no_alloc_scope` | `platform/memory` | REQ-PLT-020 | `alloc_guard`, soak 1 h | K10 | D2 |
| 5 | Cozi bounded lock-free | `platform/memory` | REQ-PLT-022 | property + TSan + model | K3 | D2 |
| 6 | Actor + Environment | `platform/actor` | REQ-PLT-030 | simboluri interzise, unit | K14 | D9 |
| 7 | IPC shm zero-copy | `platform/ipc` | REQ-PLT-035 | `ipc_bench`, `slow_consumer` | K3 | D2 |
| 8 | Scheduler TT/RM/EDF | `platform/sched` | REQ-RT-001..012 | `sched_jitter`, `rta_check` | K1,K2 | D2 |
| 9 | Analiza RTA | `platform/sched` | REQ-RT-014 | vectori vs calcul manual | — | D2 |
| 10 | Watchdog pe 3 niveluri | `platform/watchdog` | REQ-RT-020 | `wdg_hang`, `wdg_flow` | — | D7 |
| 11 | Lifecycle + graf de dependinte | `platform/lifecycle` | REQ-RT-030 | `startup_order`, `dep_fail` | K11 | D1 |
| 12 | gPTP-lite | `platform/time` | REQ-COM-050 | `ptp_offset`, master failover | — | D1 |
| 13 | CAN-FD + SocketCAN | `communication/can` | REQ-COM-001..015 | interop `cantools`/`candump` | K5 | D2 |
| 14 | Parser DBC + codegen | `communication/can` | REQ-COM-005 | 100k vectori vs `cantools` | — | D2 |
| 15 | Bus-off handling | `communication/can` | REQ-COM-012 | `hil_bus_off` | — | D6 |
| 16 | ISO-TP | `communication/isotp` | REQ-COM-020 | interop kernel `can-isotp` | — | D8 |
| 17 | Ethernet + VLAN + QoS | `communication/eth` | REQ-COM-030 | `eth_priority`, congestie reala | — | D6 |
| 18 | Shaper time-aware | `communication/eth` | REQ-COM-034 | latenta P99 sub congestie | — | D6 |
| 19 | SOME/IP wire-compatible | `communication/someip` | REQ-COM-040 | Wireshark + `vsomeip` | K4,K17 | D1 |
| 20 | SOME/IP-SD | `communication/someip` | REQ-COM-042 | `sd_discovery`, TTL, reboot | — | D1 |
| 21 | DoIP | `communication/doip` | REQ-COM-045 | interop `doipclient` | — | D8 |
| 22 | E2E protection | `communication/e2e` | REQ-SAF-020 | property + TLA+ + bitflip | — | D5 |
| 23 | SecOC | `communication/secoc` | REQ-SEC-010 | `secoc_replay`, `forged_mac` | — | D4 |
| 24 | Registry + versionare | `distributed` | REQ-DST-001 | `registry_partition` (DST) | — | D1 |
| 25 | RPC + pub/sub + QoS | middleware | REQ-DST-005 | `qos_policies`, `rpc_latency` | K4 | D2 |
| 26 | SWIM | `distributed/membership` | REQ-DST-010 | DST + `kill_node` | K6 | D3 |
| 27 | Raft | `distributed/consensus` | REQ-DST-015 | DST 10k seeds + TLA+ | — | D3 |
| 28 | Lease + fencing | `distributed/lease` | REQ-SAF-030 | TLA+, DST, `hil_split_brain` | — | D4 |
| 29 | Placement | `distributed/placement` | REQ-DST-020 | tabelat + `placement_churn` | — | D3 |
| 30 | State replication | `distributed/replication` | REQ-DST-025 | `state_roundtrip`, `failover_bump` | K9 | D3 |
| 31 | Protocol de failover | `distributed/recovery` | REQ-SAF-035 | `failover_timeline` ×100 | K7 | D3 |
| 32 | Rejoin + failback | `distributed/recovery` | REQ-DST-030 | `rejoin_zombie` (DST) | — | D3 |
| 33 | HARA + safety goals | `requirements` | SG-01..06 | traceability gate | — | — |
| 34 | Plausibility | `safety/plausibility` | REQ-SAF-014 | property, `FROZEN_PLAUSIBLE` | — | D5 |
| 35 | Redundanta duala + voter | `safety/redundancy` | REQ-SAF-018 | `dual_channel_divergence` | — | D5 |
| 36 | Fault manager + debounce | `safety/fault_manager` | REQ-SAF-040 | `fault_debounce`, `fault_to_dtc` | — | D7 |
| 37 | Masina de degradare | `safety/degradation` | REQ-SAF-045 | exhaustiv + chaos DST | — | D7 |
| 38 | Safe state cu latch | `safety/safe_state` | REQ-SAF-050 | `safe_state_latch` | — | D7 |
| 39 | Server UDS | `diagnostics/uds` | REQ-DIA-001..020 | interop `udsoncan` + fuzz | — | D8 |
| 40 | DTC manager + snapshots | `diagnostics/dtc` | REQ-DIA-030 | `dtc_lifecycle` | — | D8 |
| 41 | NVM robust | `diagnostics/nvm` | REQ-DIA-035 | `nvm_power_loss` ×200 | — | D8 |
| 42 | Security access | `security/access_control` | REQ-SEC-020 | brute-force, delay timer | — | D8 |
| 43 | Flashing + A/B + rollback | `diagnostics/flashing` | REQ-DIA-040 | 200 de intreruperi | — | D8 |
| 44 | Secure boot | `security/secure_boot` | REQ-SEC-001 | 4 mutatii de artefacte | — | D8 |
| 45 | IDS | `security/ids` | REQ-SEC-030 | 8 vectori TARA + 1 h fals poz. | — | D9 |
| 46 | Fuzzing | `tests/fuzz` | REQ-SEC-040 | 8 targeturi, nightly 1 h | — | — |
| 47 | Termic/putere | `services/thermal` | REQ-PWR-001 | `thermal_load` real | — | D7 |
| 48 | Simulator de vehicul | `simulation/vehicle` | REQ-SIM-001 | validare energie/distanta | — | D2 |
| 49 | Modele de senzori | `simulation/sensors` | REQ-SIM-010 | 7 moduri de defect | — | D5 |
| 50 | Scenarii cu asertiuni | `simulation/scenarios` | REQ-SIM-020 | ruleaza in CI | — | D2 |
| 51 | Fault injection | `simulation/faults` | REQ-TST-001 | fiecare fault confirmat prin trace | — | toate |
| 52 | DST | `apps/volt-dst` | REQ-TST-010 | 12 mutante prinse | K13 | D9 |
| 53 | Record/replay + `why` | `apps/volt-replay` | REQ-TST-020 | 50 sesiuni × 3 rulari | K14 | D9 |
| 54 | TLA+ | `formal/tla` | REQ-TST-030 | TLC in CI | K16 | D9 |
| 55 | TUI + dashboard web | `apps/volt-monitor`, `volt-web` | REQ-OBS-001 | `observability` | — | toate |
| 56 | Metrici Prometheus | `platform/trace` | REQ-OBS-010 | format valid, scrape test | — | D6 |
| 57 | Firmware Zephyr + MCUboot | `firmware/` | REQ-HIL-001 | `ztest` + suita HIL | K5 | D3,D4 |
| 58 | Suita HIL | `tests/hil` | REQ-HIL-010 | 9 teste, nightly | K5,K7 | D3 |
| 59 | Port QNX | `platform/pal/qnx` | REQ-PLT-005 | suite PAL pe QNX | — | — |
| 60 | Deployment KVM | infra | REQ-PLT-007 | `vm_deployment` | — | D1 |
| 61 | Traceability automata | `tools/traceability` | REQ-PRC-001 | teste pe repo sintetic | — | — |
| 62 | CI/CD complet | `.github` | REQ-PRC-010 | pipeline verde | — | — |

Daca un rand din tabelul asta nu are toate coloanele completate, feature-ul nu e terminat. Asta e definitia de "gata" pentru intreg proiectul.

---

## 32. Ce spui la interviu

### 32.1 Bullet-uri de CV (fiecare demonstrabil live)
- Designed and implemented a distributed, fault-tolerant automotive compute platform in C++23 running on 3 heterogeneous nodes (x86_64 Linux PREEMPT_RT, aarch64, KVM guest) with sub-25 ms service failover and zero unsafe actuator commands across 10.000+ injected-fault scenarios.
- Built a deterministic actor runtime enabling state-machine replication, bit-exact record/replay and single-threaded deterministic simulation testing of the full distributed system; 10.000 randomized fault seeds executed nightly in CI.
- Implemented CAN-FD, ISO-TP, SOME/IP (wire-compatible, validated against COVESA vsomeip and Wireshark), SOME/IP-SD, DoIP and a gPTP-derived global time base with < 50 µs cluster-wide synchronization.
- Implemented UDS (ISO 14229) and DoIP (ISO 13400) diagnostic services including sessions, security access, DTC lifecycle with debounce/aging/snapshots, and signed A/B firmware update with automatic rollback.
- Implemented a real-time scheduler with time-triggered, rate-monotonic and EDF classes, response-time schedulability analysis, deadline/jitter instrumentation and CPU partitioning; measured P99 activation jitter of 41 µs with zero deadline misses over 3.6M activations.
- Implemented split-brain prevention using Raft-backed ownership leases and fencing tokens enforced in MCU firmware; verified formally in TLA+ and physically on hardware by network partition.
- Implemented AUTOSAR-inspired E2E protection and SecOC (AES-CMAC + freshness manager), secure boot chain, role-based diagnostic access control and a rule-based automotive intrusion detection system.
- Developed SIL and HIL verification infrastructure with vehicle dynamics simulation, 25+ fault injection types, scenario-based assertions, and a physically closed control loop across three microcontrollers, including physical fault injection (disconnected sensors, resets, cut enable line).
- Built zero-copy shared-memory transport and custom allocators achieving 0.42 µs P50 IPC latency with statically verified zero dynamic allocation on the safety path.
- Established automated requirement-to-test traceability, MISRA/AUTOSAR-based static analysis, sanitizers, fuzzing and performance-regression gating in CI.

### 32.2 Intrebari care vor veni si raspunsul scurt
| Intrebare | Raspunsul tau |
|---|---|
| "De unde stii ca failover-ul e sub 25 ms?" | Din bugetul derivat din FTTI-ul lui SG-06, masurat pe 100 de repetari, cu ceas global gPTP; iata histograma si descompunerea pe faze. |
| "Ce se intampla daca ambele noduri se cred primary?" | Nu se poate intampla, din doua motive independente: lease-ul cu retragere proprie inainte de expirare, si fencing token-ul verificat in MCU. Iata invariantul in TLA+ si log-ul MCU-ului din testul fizic. |
| "E redundanta reala sau ai rulat acelasi cod de doua ori?" | Doua algoritmi diferiti, doua reprezentari numerice diferite; comparator cu toleranta si histereza. Doua copii ale aceluiasi bug ar da acelasi raspuns gresit. |
| "Cum ai testat concurenta?" | TSan pe integrare, plus DST: tot sistemul intr-un thread cu ceas virtual si intreteseri controlate de seed; 10.000 de universuri pe noapte si minimizare automata a contraexemplelor. |
| "E ISO 26262?" | Nu, si nu pretind asta. Am aplicat metoda (HARA, safety goals, FTTI, degradare, freedom from interference) ca sa derivez cerinte si bugete verificabile. |
| "Ce a fost cel mai greu?" | (Raspuns real din devlog: de obicei sincronizarea de timp si stale state la rejoin.) |

---

## 33. Riscuri si cum le eviti

| Risc | Semnal de alarma | Contramasura |
|---|---|---|
| Proiect lat si superficial | 20 de directoare, 3 teste | Regula: niciun modul nou pana cel precedent nu are teste + masuratoare |
| Blocare pe perfectiune in P1-P2 | 6 saptamani pe alocatoare | Time-box pe faza; "suficient de bun + masurat" bate "elegant" |
| Demo care nu merge la interviu | scripturi ad-hoc | `make demo-*` rulat inainte de fiecare release, pe o masina curata |
| Numere care nu se pot reproduce | valori scrise manual in README | `make kpi` regenereaza tot; README-ul citeste din raport |
| Teste flaky ignorate | "mai ruleaza o data" | Politica: flaky = bug, reparat in 24 h sau testul e sters |
| Documentatie invechita | diagrame PNG desenate manual | Diagrame ca text, masini de stari generate din tabele |
| Overselling pe CV | "ISO 26262 compliant" | Formulari exacte: "inspired by", "concepts applied", cu documentul care explica limita |
| Hardware care intarzie tot | astepti placa 3 saptamani | Totul merge intai in SIL cu `vcan0`; HIL-ul e un backend, nu o dependenta |
| Burnout | 3 luni fara nimic vizibil | Fiecare faza se termina cu un demo; publici progres lunar |

---

## 34. Anexe

### A. Comenzi utile (mediu de dezvoltare)
```bash
# CAN virtual pentru SIL
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
# CAN-FD real
sudo ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on && sudo ip link set up can0
candump -tz -x vcan0            # sniffing
cansend vcan0 201##1DEADBEEF    # frame CAN-FD manual
# degradare de retea reala
sudo tc qdisc add dev eth0 root netem loss 40% delay 5ms 2ms distribution normal
# izolare CPU (in /etc/default/grub)
isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 irqaffinity=0,1 intel_pstate=disable
# baseline de latenta
sudo cyclictest -m -S -p 80 -i 1000 -l 1000000 -h 400
```

### B. Fisier de configuratie complet (exemplu `config/node_a.yaml`)
```yaml
node:
  id: NodeA
  role: [COMPUTE, SAFETY]
  interfaces: { can: can0, eth: eth0.10, diag_eth: eth0.20 }
  rt: { isolated_cpus: [2,3], mlockall: true, sched_reset_on_fork: true }
cluster:
  peers: [NodeB, NodeC]
  raft: { election_timeout_ms: [150,300], heartbeat_ms: 50 }
  swim: { period_ms: 3, timeout_ms: 2, indirect_k: 2, indirect_ms: 3, suspect_ms: 5 }
  can_heartbeat: { id: 0x400, period_ms: 5, miss_threshold: 3 }
  ptp:  { role: AUTO, sync_interval_ms: 125, max_offset_us: 100 }
services:
  - { name: SensorFusion,    isolation: PROCESS,   criticality: HIGH,            period_us: 5000,  cpu: [3], replication: ACTIVE_STANDBY }
  - { name: VehicleDynamics, isolation: PROCESS,   criticality: HIGH,            period_us: 10000, cpu: [3], replication: ACTIVE_STANDBY }
  - { name: BrakeControl,    isolation: PARTITION, criticality: SAFETY_CRITICAL, period_us: 1000,  cpu: [2],
      sched: { policy: SCHED_FIFO, priority: 90 },
      lease: { resource: BrakeActuator, class: FENCED, duration_ms: 30, renew_ms: 10, self_yield_ms: 20, preauthorized_successor: NodeB },
      replication: { mode: ACTIVE_STANDBY, sync_period_us: 5000, standby_on: [NodeB] } }
  - { name: Diagnostics,     isolation: PROCESS,   criticality: MEDIUM,          period_us: 20000, cpu: [1] }
  - { name: Logging,         isolation: THREAD,    criticality: BEST_EFFORT,     period_us: 100000, cpu: [0] }
telemetry: { trace: true, metrics_port: 9101, record: false }
```

### C. Exemplu de sesiune UDS pe fir (hexa, ca in `docs/DIAGNOSTICS.md`)
```
tester → ecu   02 10 03 00 00 00 00 00        DiagnosticSessionControl(extended)
ecu → tester   06 50 03 00 32 01 F4 00        pozitiv, P2=50ms P2*=5000ms
tester → ecu   03 22 F1 90 00 00 00 00        ReadDataByIdentifier(F190 = VIN)
ecu → tester   10 14 62 F1 90 56 4F 4C        FF (ISO-TP), lungime 0x14
tester → ecu   30 00 00 00 00 00 00 00        FlowControl: CTS, BS=0, STmin=0
ecu → tester   21 54 30 30 30 30 30 30        CF#1
ecu → tester   22 31 32 33 34 35 36           CF#2  → "VOLT00000012345 6"
tester → ecu   02 27 03 00 00 00 00 00        SecurityAccess requestSeed(L2)
ecu → tester   0A 67 03 A1 B2 C3 D4 E5        seed 8 bytes
tester → ecu   0A 27 04 <cmac_trunc_4>        sendKey
ecu → tester   02 67 04 00 00 00 00 00        acces acordat
tester → ecu   04 31 01 F0 02 ...             RoutineControl start: fault injection
```

### D. Targeturi `make` (interfata unica a proiectului)
```
make build | test | test-unit | test-dst | test-sil | test-hil | test-fuzz
make asan | ubsan | tsan | coverage | tidy | format
make kpi | docs | trace-perfetto | bench | bench-compare
make demo-boot | demo-control | demo-failover | demo-splitbrain
make demo-sensor | demo-network | demo-degradation | demo-diag | demo-rigor
make sim-up | sim-down | cluster-up | cluster-down | flash-mcu
```

### E. Nume alternative pentru proiect
VOLT e bun (scurt, tehnic, automotive). Alternative daca vrei altceva: **AXIOM**, **KEEL**, **HELIX**, **VERTEX**. Numele conteaza mai putin decat prima fraza din README.

---

## 35. Ultimul cuvant

Proiectul asta nu impresioneaza prin numarul de tehnologii. Impresioneaza pentru ca are **o singura afirmatie centrala** — *"pot demonstra ca sistemul isi pastreaza controlul unui actuator fizic atunci cand se strica orice componenta, si pot demonstra cu numere de ce e sigur"* — si pentru ca fiecare modul din el exista ca sa sustina afirmatia aceea.

Cand scrii README-ul, primele trei randuri trebuie sa fie afirmatia, GIF-ul cu matricea LED care nu tresare cand omori un nod, si tabelul KPI. Restul documentatiei e pentru cine vrea sa verifice.

Si inca ceva, pentru ca proiectul e mare: **ordinea conteaza mai mult decat viteza.** Trunchiul din §29.1 iti da, la capat, un sistem care functioneaza complet si se demonstreaza singur. Ramurile din §29.2 il duc apoi in zona in care nu mai ai cu cine sa fii comparat: trei niveluri de monitorizare pe trei arhitecturi de procesor diferite, cu o cale de oprire care e un fir si nu o functie, sincronizare de ceas verificata cu un PPS improvizat din doua placi, WCET demonstrat in trei feluri, si o matrice de izolare masurata in cinci configuratii. Nu exista un moment in care proiectul "se termina" — exista doar momentul in care fiecare ramura pe care ai atins-o e completa si testata.

---
---

# PARTEA II — completari critice (rezultatul sanity-check-ului)

Sectiunile de mai jos acopera lucruri fara de care spec-ul nu putea fi implementat asa cum e scris: interfetele concrete ale serviciilor, lantul de timp end-to-end, secventa de boot, gateway-ul, definitia fizica a starii sigure, modelul de erori si limitarile asumate.

---

## 36. Catalogul de interfete — SOME/IP si topicuri

**DE CE.** §11.4 descrie protocolul, dar nu si *ce servicii exista*. Fara catalog nu poti scrie nici serverul, nici clientul, nici testul de interop, si nici dissectorul din Wireshark nu are ce sa arate coerent. Catalogul e sursa de adevar, tinut in `config/services.yaml` si transformat in cod de `tools/serdes_gen`.

| Service ID | Nume | Instanta | Metode (RPC) | Evenimente (pub/sub) | Ciclu event |
|---|---|---|---|---|---|
| 0x1001 | `VehicleStateService` | 0x0001 | `getVehicleState()`, `getWheelSpeeds()`, `getYawRate()` | `onVehicleState` (eventgroup 0x01) | 10 ms |
| 0x1002 | `SensorFusionService` | 0x0001 | `getFusedState()`, `getSensorHealth()` | `onFusedState` (0x01), `onSensorFault` (0x02) | 5 ms / la eveniment |
| 0x1003 | `BrakeControlService` | 0x0001 | `requestDeceleration(float, RequestId)`, `getBrakeState()`, `setMode(Mode)` | `onBrakeState` (0x01), `onDegradation` (0x02) | 10 ms |
| 0x1004 | `SteeringAssistService` | 0x0001 | `requestSteerTorque(float)`, `getSteerState()` | `onSteerState` (0x01) | 10 ms |
| 0x1005 | `TractionControlService` | 0x0001 | `setEnabled(bool)`, `getSlipState()` | `onSlipState` (0x01) | 10 ms |
| 0x1006 | `AdasAccService` | 0x0001 | `setTargetSpeed(float)`, `setHeadway(u8)`, `engage()`, `disengage()` | `onAccState` (0x01) | 50 ms |
| 0x1007 | `DiagnosticsService` | 0x0001 | `readDid(u16)`, `writeDid(u16, bytes)`, `readDtc(mask)`, `clearDtc()`, `startRoutine(u16, bytes)` | `onDtcChanged` (0x01) | la eveniment |
| 0x1008 | `ClusterService` | 0x0001 | `getMembers()`, `getPlacement()`, `requestFailback(svc,node)`, `getLeases()` | `onMembershipChange` (0x01), `onPlacementChange` (0x02) | la eveniment |
| 0x1009 | `HealthService` | 0x0001 | `getNodeHealth()`, `getServiceHealth(id)` | `onHealthReport` (0x01) | 100 ms |
| 0x100A | `ThermalService` | 0x0001 | `getThermalState()`, `setPowerBudget(float)` | `onThermalState` (0x01) | 500 ms |
| 0x100B | `LoggingService` | 0x0001 | `setLevel(module, level)`, `flush()` | — | — |
| 0x100C | `SimulationService` (doar in SIL/HIL, refuzat in build-ul de "productie") | 0x0001 | `loadScenario(name)`, `injectFault(spec)`, `setMu(wheel,val)`, `pause()`, `step(ms)` | `onScenarioEvent` (0x01) | la eveniment |

Reguli:
- Metodele care schimba starea vehiculului (`requestDeceleration`, `engage`) sunt **REQUEST cu raspuns obligatoriu** si poarta `RequestId` + E2E; niciodata fire-and-forget.
- Metodele idempotente sunt marcate ca atare in YAML si doar ele pot fi reincercate automat de client.
- Fiecare eveniment periodic are un `deadline` in QoS: lipsa a 3 esantioane consecutive → fault de comunicatie in serviciul consumator.
- **Topicurile interne** (intra-nod, shm zero-copy) sunt separate de serviciile SOME/IP si nu sunt expuse pe retea: `topic/wheel_speeds_raw`, `topic/imu_raw`, `topic/brake_cmd_internal`, `topic/state_snapshot`. Regula: pe retea pleaca doar ce are consumator pe alt nod.

**VERIFICARE.** `tests/integration/service_catalog.cpp` verifica automat ca fiecare serviciu din YAML e oferit la runtime, cu ID-urile si versiunile din catalog, si ca fiecare eveniment respecta ciclul declarat (±10%). Testul de interop `vsomeip` foloseste acelasi YAML pentru a genera configuratia clientului de referinta.

---

## 37. Specificatia functionala a serviciilor

**DE CE.** Pana acum serviciile au fost nume intr-o diagrama. Ca sa le poti implementa, fiecare are nevoie de: intrari, iesiri, algoritm, perioada, ce face cand ii lipsesc datele. Asta e continutul lui `docs/DESIGN.md` per serviciu.

### 37.1 `SensorFusionService` — 5 ms, HIGH
- **In**: `wheel_speeds_raw` (4×, 5 ms, de la MCU prin CAN 0x102), `imu_raw` (ax, ay, yaw rate, 5 ms), `steering_angle` (10 ms).
- **Algoritm**: verificari E2E si de plauzibilitate (§15.4) → estimare a vitezei vehiculului prin **filtru complementar**: pe termen scurt integrarea acceleratiei longitudinale, pe termen lung media rotilor nemotoare, cu ponderare in functie de slip-ul estimat; estimare yaw rate din IMU cu compensare de bias (bias-ul se estimeaza cand vehiculul e in linie dreapta si la viteza constanta). Fiecare iesire poarta un **indicator de calitate** (0-100) si lista surselor folosite.
- **Out**: `onFusedState` (viteza, yaw, acceleratii, viteze de roata validate, calitate, masca de surse valide).
- **Degradare**: 1 roata invalida → estimare din celelalte 3, calitate 70; 2 roti pe aceeasi punte invalide → doar IMU, calitate 40, se semnaleaza `onSensorFault`; IMU invalid + 2 roti invalide → calitate 0 → consumatorii intra in fallback.

### 37.2 `VehicleDynamicsService` — 10 ms, HIGH
- **In**: `onFusedState`.
- **Algoritm**: slip longitudinal per roata `λ = (v − ωR)/max(v, ωR, ε)`; estimare a aderentei disponibile `μ̂` prin observarea raportului dintre deceleratia realizata si cea comandata (filtrata, cu memorie de 500 ms); detectie de suprafata split-μ (diferenta stanga/dreapta persistenta); estimare de sarcina pe roata din transferul longitudinal/lateral.
- **Out**: `onVehicleState` (slip ×4, μ̂ ×4, sarcini, flag split-μ, stabilitate).
- **Degradare**: la calitate < 40, μ̂ se ingheata la o valoare conservatoare (0,7) si se semnaleaza.

### 37.3 `BrakeControlService` — 1 ms, SAFETY_CRITICAL, dual-channel
- **In**: cerere de deceleratie (de la sofer simulat sau de la `AdasAccService`), `onVehicleState`, `onFusedState`, starea lease-ului.
- **Algoritm, canal A (principal)**: regulator de deceleratie PI cu anti-windup + logica **ABS** per roata: daca `λ > λ_target` (implicit 0,15, calibrabil prin DID 0x0200), se reduce presiunea in trepte pana la reintrarea in domeniu, cu histereza si limitare de rata; distributie fata/spate dupa sarcina estimata; pe split-μ, limitarea diferentei stanga/dreapta pentru a nu genera moment de yaw (cerinta din SG-03).
- **Algoritm, canal B (diversitar)**: control tabelar in aritmetica cu virgula fixa (Q16.16), fara integrator, cu praguri de slip mai conservatoare. Intentionat mai simplu si mai prudent.
- **Comparator**: `|A − B| ≤ 8%` din domeniu, timp de minim 3 cicluri, altfel divergenta (§15.4). Iesirea nominala e canalul A; la divergenta se emite `min(A, B)` (mai putina frana = mai putin periculos in cazul H-01) si se ridica C0072.
- **Garda de siguranta** (`REQ-SAF-001`, cod separat de algoritm, revizuit distinct): comanda finala e emisa **doar daca** exista cerere valida cu E2E OK, lease valid, si plauzibilitate trecuta in ultimele 20 ms. Altfel: 0 si tranzitie de degradare.
- **Out**: CAN `0x201` (+ `0x202` SecOC) la 1 ms, `onBrakeState` la 10 ms.
- **La preluare (STANDBY → ACTIVE)**: limitator de rata (slew) 20%/ciclu in primele 10 cicluri (bumpless transfer, §13.5).

### 37.4 `TractionControlService` — 10 ms, HIGH
Limiteaza cuplul motor cand slip-ul de tractiune depaseste pragul; interactioneaza cu frana prin arbitrare: cererea cea mai restrictiva castiga. **Arbitrarea e explicita si testata** — doua functii care comanda acelasi actuator fara arbitru definit e un bug clasic de integrare automotive.

### 37.5 `SteeringAssistService` — 10 ms, HIGH
Cuplu de asistenta functie de viteza si unghi; limitare de rata; dezactivare automata la calitate scazuta a datelor. Foloseste acelasi mecanism de lease/fencing ca frana (al doilea actuator FENCED, ca sa demonstrezi ca mecanismul e generic, nu cusut pe un singur caz).

### 37.6 `AdasAccService` — 50 ms, MEDIUM
Adaptive cruise control simplu: mentine viteza tinta si distanta fata de un vehicul-tinta simulat (headway in secunde); genereaza cereri de deceleratie catre `BrakeControlService` prin RPC cu E2E. **Nu are voie sa comande direct actuatorul** — separarea asta (functie de confort vs. functie de siguranta) e o decizie de arhitectura care se explica la interviu.

### 37.7 Restul
`DiagnosticsService` (§16), `LoggingService` (drenaj + rotatie), `HealthService` (§8), `ThermalService` (§18), `GatewayService` (§40), `HmiService` (starea pentru dashboard, 100 ms, BEST_EFFORT).

**VERIFICARE.** Fiecare serviciu are: teste unitare pe algoritm cu vectori (inclusiv cazuri limita: viteza 0, divizare la zero, saturatii), un test de degradare pentru fiecare mod de defect al intrarilor sale, si participare la minim un scenariu SIL cu asertiuni numerice.

---

## 38. Lantul de timp end-to-end

**DE CE.** Deadline-urile pe task-uri nu spun nimic despre timpul total senzor → actuator, care e cerinta reala de control. Fara bugetul asta nu poti sti daca bucla e stabila si nu poti justifica perioadele alese.

**Lantul nominal (frana):**

| # | Etapa | Buget | Masurat prin |
|---|---|---|---|
| 1 | Esantionare encoder pe MCU + filtrare | 0,5 ms | timestamp MCU |
| 2 | Transmisie CAN `0x102` (arbitraj + 64B @ 2 Mbit) | 0,3 ms | timestamp hw RX |
| 3 | Receptie + decodare + E2E pe Node A/B | 0,2 ms | trace |
| 4 | Asteptarea urmatoarei activari `SensorFusion` (5 ms) | 5,0 ms | trace |
| 5 | Executie `SensorFusion` | 0,8 ms | trace |
| 6 | Publicare + receptie `BrakeControl` (shm) | 0,01 ms | trace |
| 7 | Asteptarea urmatoarei activari `BrakeControl` (1 ms) | 1,0 ms | trace |
| 8 | Executie `BrakeControl` (A + B + comparator) | 0,4 ms | trace |
| 9 | Serializare + SecOC + transmisie CAN `0x201/0x202` | 0,4 ms | timestamp hw TX |
| 10 | Receptie MCU + verificare MAC/E2E/epoch | 0,3 ms | timestamp MCU |
| 11 | PWM → tensiune stabilizata pe fir → citita de Uno R3 (mediere) | 15 ms | ADC pe Uno R3 |
| | **Total pana la comanda electrica (1-10)** | **8,9 ms** | K5 + trace end-to-end |
| | Total pana la efect fizic (cu 11) | ~29 ms | HIL |

Concluzii care trebuie scrise in `docs/PERFORMANCE.md`:
- **Latenta e dominata de asteptarea activarilor (pasii 4 si 7), nu de calcul.** De aceea alinierea fazelor conteaza: `SensorFusion` e programat cu offset astfel incat rezultatul lui sa fie gata cu ~200 µs inainte de activarea `BrakeControl`. Cu tabela time-triggered (§9.2) asta e explicit, nu noroc. Castig masurat: ~1 ms din lantul total.
- Calea analogica (PWM + mediere pe ADC) e cea mai lenta veriga si e o limitare a montajului improvizat, nu a software-ului — se spune explicit, cu masuratoare.
- Bugetul total (8,9 ms electric) e mult sub FTTI-urile din HARA (80-150 ms), deci exista margine si pentru un ciclu ratat.

**VERIFICARE.** `tests/hil/e2e_chain.cpp` masoara fiecare etapa separat, cu acelasi ceas global, si esueaza daca vreo etapa depaseste bugetul cu > 20%. Raportul apare in `make kpi`.

---

## 39. Boot, lifecycle si operare in conditii de pornire incompleta

**DE CE.** §8 pomeneste modulul de lifecycle, dar secventa de pornire e una dintre cele mai delicate parti ale unei platforme distribuite: cine porneste primul, ce faci daca lipseste un nod, si cum eviti sa pornesti pe jumatate si sa comanzi ceva.

### 39.1 Masina de stari de boot (per nod)
```
OFF → BOOTLOADER (verificare semnatura, §17.2)
    → PLATFORM_INIT   (PAL, memorie pre-alocata si blocata, cgroups, prioritati RT)
    → TIME_SYNC       (gPTP; se asteapta pana la 300 ms un master; altfel devine master provizoriu)
    → NETWORK_INIT    (interfete, VLAN, socketuri, SocketCAN up, filtre)
    → CLUSTER_JOIN    (SWIM, Raft, sincronizare config si registry)
    → SERVICES_SAFETY (pornire in ordinea grafului de dependinte)
    → SERVICES_CTRL
    → SERVICES_OPTIONAL
    → RUNNING
```
Fiecare tranzitie are timeout propriu si o actiune de esec definita. Timpul total tinta: **< 800 ms** (K11).

### 39.2 Graf de dependinte
Serviciile declara `requires: [ServiceX, ResourceY]`. Runtime-ul face sortare topologica, porneste in paralel ce se poate, si detecteaza ciclurile **la validarea configuratiei**, nu la runtime. Un serviciu care nu ajunge `READY` in timpul lui declarat: se marcheaza esuat, iar dependentii lui **nu pornesc** (nu pornesc "pe jumatate configurati").

### 39.3 Pornire incompleta — reguli explicite
| Situatie | Decizie |
|---|---|
| Un serviciu optional lipseste | RUNNING (DEGRADED), DTC informativ |
| Un serviciu de control lipseste, dar exista pe alt nod | plasare pe celalalt nod, RUNNING |
| Un serviciu SAFETY_CRITICAL nu porneste nicaieri | **nu se emite nicio comanda de actuator**; sistemul ramane in `SAFE_STATE`, cu diagnostic accesibil |
| Nu exista quorum (1 nod din 3) | modul `SINGLE_NODE_DEGRADED`: nodul poate rula servicii local, dar **nu poate obtine un lease FENCED** decat daca e singurul care a fost vreodata proprietar si niciun alt nod nu a fost vazut de la boot; altfel refuza sa comande. Regula e conservatoare intentionat si e testata explicit. |
| Ceasul nu se sincronizeaza (gPTP indisponibil) | resursele FENCED trec pe regula UNFENCED (asteptare de lease complet), toate marjele temporale se dubleaza, se ridica DTC si se intra in DEGRADED |
| Config diferita intre noduri (hash diferit) | nodul cu hash necunoscut **nu e admis in cluster**; DTC de configuratie. Fara "merge cumva cu versiuni amestecate". |

### 39.4 Oprire controlata
`SIGTERM` → oprire in ordine inversa dependentelor; serviciile FENCED cedeaza explicit lease-ul (mesaj de `RELEASE` + incrementarea epoch-ului la nivel de cluster, ca sa nu existe ambiguitate); NVM-ul se sincronizeaza; se scrie un `shutdown_reason`. Un `kill -9` trebuie sa produca exact acelasi rezultat *pentru siguranta* (doar mai lent pentru disponibilitate) — verificat prin test.

**VERIFICARE.** `tests/integration/startup_*.cpp`: 20 de scenarii de pornire (ordini diferite, noduri lipsa, servicii care esueaza, ceas indisponibil, config nepotrivita). `tests/dst/boot_chaos.cpp`: porniri in ordini aleatorii cu intarzieri aleatorii, 10.000 seeds; invariant: **nicio comanda de actuator inainte ca lantul complet de conditii sa fie indeplinit**.

---

## 40. Gateway CAN ↔ Ethernet — `services/gateway`

**DE CE.** In topologia din §3.1 Node C e gateway, dar comportamentul lui nu era specificat. Gateway-ul e o componenta automotive standard si e si punctul unde se aplica politica de securitate pe retea.

**CUM.**
- **Rutare bidirectionala** pe baza unei tabele declarative (`config/gateway.yaml`): `sursa (bus, id/topic) → destinatie (bus, id/topic) → transformare`.
- **Transformari**: CAN semnal → structura SOME/IP (si invers), cu conversie de unitati si de scalare, refacerea E2E pentru domeniul destinatie (**nu** se transmite mai departe un CRC calculat pentru alt format — se re-valideaza si se re-genereaza, si asta se documenteaza ca decizie).
- **Rate limiting** per ruta (token bucket) — un bus care o ia razna nu are voie sa inunde celalalt domeniu.
- **Firewall**: lista alba de ID-uri/servicii permise pe fiecare directie. Diagnostic dinspre exterior e permis doar catre serviciul de diagnostic, niciodata direct catre topicurile de control. Orice incalcare = eveniment IDS (§17.3).
- **Store-and-forward cu prioritati**: cozi separate pe clasa de trafic; la congestie se arunca intai traficul de logging.
- Latenta de gateway masurata si bugetata: **< 500 µs P99** pentru rutele de control.

**VERIFICARE.** `tests/integration/gateway_*.cpp`: fiecare ruta din tabel are un test generat automat (trimiti pe sursa, verifici pe destinatie, cu valoare si timing); teste negative pentru fiecare regula de firewall; test de rate limit; test de congestie cu verificarea ordinii de aruncare.

---

## 41. Starea sigura fizica — ce inseamna concret "safe state"

**DE CE.** Cea mai grea intrebare de siguranta din proiect: pentru o frana, ce e starea sigura? Presiune zero (masina nu franeaza, dar poate fi periculos daca soferul cere frana) sau presiune aplicata (masina franeaza, dar franarea neintentionata la 130 km/h e H-01, hazardul cel mai sever)? Un candidat care nu si-a pus intrebarea asta se vede imediat.

**Decizia, cu justificare (documentata in `docs/SAFETY.md`):**

| Actuator | Stare sigura | Justificare |
|---|---|---|
| **Frana (VOLT)** | **eliberare controlata** a presiunii comandate de VOLT, cu rampa (nu treapta), si predarea catre calea de baza | In arhitectura reala, franarea de baza ramane hidraulic/mecanic disponibila pentru sofer; VOLT adauga functii (ABS, ACC). H-01 (franare neintentionata la viteza mare, S3) e mai sever decat pierderea asistarii, iar pierderea functiei suplimentare e acoperita de calea de baza. **Deci: fail-silent, nu fail-active.** In montajul HIL, "calea de baza" e reprezentata de revenirea iesirii PWM la zero si de afisarea starii sigure pe matricea LED. |
| Directie (asistenta) | cuplu de asistenta la zero, cu rampa de 200 ms | Un salt in cuplu ar fi in sine un hazard; predarea catre sofer trebuie sa fie gradata. |
| ACC / functii de confort | dezactivare imediata + notificare HMI | Nu au autoritate directa asupra actuatoarelor. |
| Iesirile de diagnostic | inghetate, ultima valoare marcata invalida | Mai bine "nu stiu" decat o valoare veche prezentata ca valida. |

**Reguli comune pentru orice intrare in safe state:**
1. Se emite **o singura data** comanda de tranzitie, apoi calea de comanda e blocata (latch), ca sa nu existe oscilatii intre normal si safe.
2. Rampa de iesire e implementata **si in MCU**, nu doar in PC: daca PC-ul tace brusc, MCU-ul face el rampa in 20 ms, nu taie comanda instantaneu. Redundanta comportamentului la nivelul cel mai apropiat de actuator.
3. Se inregistreaza cauza (fault ID, snapshot, timestamp global) inainte de tranzitie — se scrie in NVM cu prioritate maxima.
4. Iesirea din safe state doar prin reset explicit (§15.3).

**VERIFICARE.** `tests/hil/safe_state_ramp.cpp` — se intrerupe legatura cu PC-ul in mijlocul unei franari si Uno R3 inregistreaza forma rampei de iesire; criteriu: fara treapta, revenire completa in 20 ±5 ms. `tests/unit/safe_state_latch.cpp` — nicio secventa de evenimente nu produce iesire automata din safe state.

---

## 42. Modelul de erori, threading si IO blocant

**DE CE.** "Folosim `std::expected`" nu e o strategie de tratare a erorilor. Iar un actor single-threaded care trebuie sa scrie pe disc are nevoie de o solutie explicita, altfel blocheaza bucla.

### 42.1 Taxonomie de erori
| Clasa | Exemple | Tratare |
|---|---|---|
| **Programare** (invariant incalcat) | index in afara domeniului, stare imposibila | `VOLT_ASSERT` → in Debug abort cu trace; in Release: fault intern, serviciu izolat, DTC U3000. Niciodata ignorate. |
| **Configuratie** | YAML invalid, dependinta ciclica, ID duplicat | Detectate la **validare**, inainte de pornire. Sistemul refuza sa porneasca, cu mesaj precis (fisier, linie, camp). |
| **Resursa indisponibila** | socket ocupat, shm lipsa, CAN down | Retry cu backoff marginit in faza de init; esec definitiv → serviciul nu devine READY. |
| **Tranzitorii de rulare** | pachet pierdut, timeout, esec E2E | `std::expected` + contorizare + debounce; devin fault-uri doar la depasirea pragului. |
| **Externe asteptate** | tester deconectat, NRC | Raspuns definit de protocol, fara efect asupra controlului. |

Reguli: nicio eroare nu se inghite in tacere (fiecare are contor si trace); niciun `catch(...)` gol; niciun cod de eroare ignorat (`[[nodiscard]]` peste tot); pe data plane nu se logheaza string-uri formatate, ci ID-uri de eveniment.

### 42.2 Threading
| Thread | Rol | Prioritate | CPU |
|---|---|---|---|
| `rt-control` | actorii SAFETY_CRITICAL si HIGH | FIFO 90/80 | izolate (2,3) |
| `rt-net-rx/tx` | receptie/transmisie CAN + Ethernet prioritar | FIFO 85 | izolate |
| `ctrl-plane` | SWIM, Raft, registry, RPC de control | OTHER (nice -5) | 0,1 |
| `io-offload` | scrieri pe disc (loguri, trace, record, NVM) | OTHER (nice 5) | 0,1 |
| `timer` | ceas si scheduler | FIFO 95 | izolat |

Reguli stricte:
- Un actor ruleaza **pe un singur thread**; nu exista partajare de stare intre actori (comunica doar prin mesaje).
- Tot IO-ul blocant se face in `io-offload`, alimentat prin cozi bounded; daca coada se umple, se arunca (si se contorizeaza) — **niciodata nu se blocheaza calea de control ca sa se scrie un log**.
- Orice mutex atins de un thread RT e `PTHREAD_PRIO_INHERIT` (mostenire de prioritate, contra inversiunii); mutex-urile pe calea de 1 ms sunt interzise prin design (doar structuri lock-free).
- TSan ruleaza pe suita de integrare; orice data race e blocanta pentru merge.

**VERIFICARE.** `tests/integration/io_backpressure.cpp` (disc plin / disc lent simulat cu `dm-delay`: bucla de control isi tine jitterul); `tests/unit/error_taxonomy.cpp`; verificare statica in CI ca nu exista `catch(...)` gol si ca fiecare `expected` e consumat.

### 42.3 Loguri si spatiu pe disc
Rotatie pe dimensiune (100 MB × 5 fisiere) + politica de "disc plin": se opreste inregistrarea (nu sistemul), se ridica DTC informativ, se pastreaza ultimele fisiere. Sesiunile de record au cota separata si se opresc singure la 2 GB.

---

## 43. Limitari cunoscute (le scrii tu primul, in README)

**DE CE.** Orice inginer senior va gasi limitele proiectului in 5 minute. Daca sunt deja scrise de tine, devin dovada de maturitate; daca le gaseste el, devin gaura in poveste.

> Lista de mai jos e cea **ramasa dupa** extinderile din Partea III (E-Gas pe 3 niveluri, monitor independent pe al treilea tip de siliciu, cross-check 2oo2 de senzori, analiza WCET statica, matricea de izolare, PKI cu UDS 0x29, CBMC). Ce era limitare si a fost rezolvat a fost mutat in scope, nu ascuns.

1. **Nu exista lockstep de nucleu in siliciu si nici logica dedicata** (Cortex-R5F dual-lockstep, ECC hardware, monitor implementat in FPGA). Nivelul 3 ruleaza pe un microcontroller de 8 biti cu cod minimal si watchdog hardware — mai slab decat logica dedicata, dar cu independenta reala de arhitectura. Interfata Nivelului 3 (frame-uri pe UART + linia ENABLE) e definita astfel incat o implementare in Verilog sa poata inlocui placa **fara nicio modificare in restul sistemului**, daca vreodata apare o placa FPGA disponibila.
2. **Alimentarea nu e redundanta** (toate placile alimentate prin USB de la acelasi laptop); caderea alimentarii duce la stare sigura pasiva, nu la continuarea functiei.
3. **Modelul de vehicul nu e validat impotriva unui vehicul real**, doar impotriva relatiilor teoretice, a literaturii si a co-simularii FMI cu un model tert.
4. **Nu e certificat ISO 26262 / ISO 21434** si nu poate fi de un individ — sunt aplicate metodele (HARA, FMEDA, ASIL decomposition, E-Gas), nu procesul de certificare cu evaluator independent.
5. **Cluster static de 3-4 noduri**, fara membership dinamic in Raft (decizie constienta, ADR-011).
6. **Portul QNX acopera PAL-ul, scheduler-ul si IPC-ul**, nu si stiva CAN (SocketCAN nu exista pe QNX; abstractizarea are backend QNX neimplementat).
7. **Analiza WCET statica e proprie si simplificata** (model de timp calibrat, nu analiza de microarhitectura ca aiT); e cross-validata cu masuratori si cu teorie a valorilor extreme, dar nu inlocuieste un tool calificat.
8. **PKI-ul e propriu si local** (CA de dezvoltare, chei in keystore software cu interfata pregatita pentru HSM), nu un PKI de productie cu ciclu de viata complet.
9. **Modelele TLA+ sunt marginite** (3-4 noduri, adancime limitata): verificare exhaustiva pe un model mic, nu demonstratie pentru orice configuratie. CBMC acopera functii, nu sistemul.
10. **Perceptia ADAS e minimala** (lidar 2D + detectie de banda clasica cu OpenCV), suficienta pentru a inchide bucla, nu comparabila cu un stack de conducere autonoma.

Fiecare limitare e insotita in `docs/` de o propozitie despre **ce ar insemna sa o rezolvi** — asta arata ca stii unde e granita si ca ai ales constient sa te opresti acolo.

---

## 44. Licentiere si dependinte tertiare

- Cod propriu: **Apache-2.0** (permisiva, compatibila cu utilizarea industriala; mai potrivita decat GPL daca vrei sa o arati unui angajator).
- Fiecare dependinta e listata in `docs/THIRD_PARTY.md` cu licenta si scopul; se evita dependintele GPL in codul care ajunge pe MCU sau in portul QNX.
- Fisierele DBC, configuratiile si cataloagele sunt proprii — fara materiale sub NDA, fara nimic luat de la Siemens sau din surse interne. Regula: **nimic din proiectul asta nu are voie sa provina din munca de la internship.** E si o chestiune de corectitudine, si de a putea vorbi liber despre proiect la interviuri.
- Diagramele si textele generate raman in repo ca surse (PlantUML/Markdown), fara imagini luate de pe internet.

---

## 45. Verificare finala inainte de a spune "gata"

Lista de control pe care o rulezi inainte de fiecare release si inainte de fiecare interviu:

- [ ] `make build test` curat pe gcc si clang, Debug si Release, fara warning-uri
- [ ] ASan/UBSan/TSan: 0 finding-uri pe suita de integrare
- [ ] DST: 10.000 seeds fara violari de invarianti; cele 12 mutante prinse
- [ ] `volt-replay verify` pe 50 de sesiuni: 100% determinism
- [ ] TLC: toate cele 3 modele trec
- [ ] `make kpi`: toate cele 18 KPI-uri masurate in ultimele 7 zile, pe hardware
- [ ] Traceability: 0 cerinte neacoperite, 0 teste orfane, toate SG-urile cu lant complet
- [ ] Fuzzing nightly: 0 crash-uri in ultimele 7 zile
- [ ] Toate cele 10 demo-uri ruleaza pe o masina curata, dupa `git clone`, urmand doar README-ul
- [ ] Suita HIL: toate testele trec cu hardware-ul conectat
- [ ] README: afirmatia centrala, GIF-ul cu matricea LED, tabelul KPI, limitarile cunoscute — in aceasta ordine
- [ ] Nimic din README nu promite mai mult decat demonstreaza codul

---
---

# PARTEA III — extinderi de maxima valoare

Nimic din partea asta nu e "nice to have". Fiecare sectiune adauga fie o capabilitate pe care platformele reale o au si proiectele de portofoliu nu, fie o dovada pe care nu o poti contesta la interviu. Ordinea de implementare e libera (ramurile din §29.2), dependenta e doar de trunchi.

---

## 46. Arhitectura de monitorizare pe 3 niveluri (conceptul E-Gas)

**DE CE.** Redundanta duala din §15.4 traieste in acelasi proces, pe acelasi nod, sub acelasi OS. Daca runtime-ul se strica in mod corelat, ambele canale se strica la fel. Industria a rezolvat asta demult cu **conceptul E-Gas** (EGAS Workgroup, folosit de toti producatorii pentru control de cuplu): trei niveluri de monitorizare, fiecare mai simplu si mai independent decat cel de deasupra. Implementarea lui te pune direct in vocabularul inginerilor de functional safety.

**CUM.**

```
┌──────────────────────────────────────────────────────────────┐
│ NIVEL 1 — FUNCTIE                                            │
│ BrakeControl complet: PI + ABS + distributie + arbitrare     │
│ ruleaza pe Node A/B (Linux, C++23), canal A + canal B        │
└──────────────────────┬───────────────────────────────────────┘
                       │ comanda + intentie declarata
┌──────────────────────▼───────────────────────────────────────┐
│ NIVEL 2 — MONITORIZAREA FUNCTIEI                             │
│ Model simplificat, independent: recalculeaza deceleratia      │
│ *permisa* din cerere + viteza + limite, si o compara cu       │
│ comanda. Cod separat, date separate, gandit separat.          │
│ Ruleaza pe ESP32 (Xtensa), NU pe nodul de control si NU pe    │
│ placa de actuare.                                             │
└──────────────────────┬───────────────────────────────────────┘
                       │ verdict + question/answer
┌──────────────────────▼───────────────────────────────────────┐
│ NIVEL 3 — MONITORIZAREA CONTROLLERULUI                       │
│ Uno R3 (AVR, 8 biti): verifica prin question/answer ca        │
│ Nivelul 2 e viu si calculeaza corect, in ferestre de timp     │
│ stricte, si detine **linia ENABLE** — calea de oprire         │
│ fizica a actuatorului (§47). Cel mai simplu dispozitiv din    │
│ sistem, cu cel mai simplu cod, pe a treia arhitectura.        │
└──────────────────────────────────────────────────────────────┘
```

- **Question/answer**: Nivelul 3 trimite periodic o intrebare (index intr-un tabel), Nivelul 2 trebuie sa raspunda cu valoarea corecta, calculata printr-un lant de operatii care trece prin toate ramurile importante ale codului sau. Raspuns gresit sau intarziat → oprire. Asta testeaza continuu **calea de executie**, nu doar prezenta.
- **Independenta reala**: trei arhitecturi de procesor complet diferite (x86_64 pentru Nivel 1, Xtensa pentru Nivel 2, AVR 8 biti pentru Nivel 3), trei ceasuri, trei toolchain-uri, trei baze de cod scrise separat. Nivelul 3 nu partajeaza nicio linie de cod cu Nivelul 1.
- **ASIL decomposition** explicat in `docs/SAFETY.md`: functia complexa (Nivel 1) poate fi tratata la un nivel de integritate mai mic pentru ca exista un monitor independent simplu (Nivelurile 2-3) care acopera modurile de defect periculoase. Asta e exact rationamentul care se face in industrie, si il poti explica pe tabla.

**VERIFICARE.** `tests/hil/egas_*.cpp`: injectezi pe rand (a) comanda excesiva de la Nivel 1 → oprita de Nivel 2 in < 5 ms; (b) Nivel 2 blocat → detectat de Nivel 3 prin question/answer in < 2 ms; (c) Nivel 3 blocat → watchdog hardware taie actuatorul. Fiecare cu masuratoare pe analizor logic.

**DEMO.** D11.

---

## 47. Nivelul 3: monitorul independent si calea de oprire — `firmware/uno_r3_l3monitor/`

**DE CE.** Nivelul 3 trebuie sa fie **cel mai simplu si cel mai independent** element din sistem: verifica doar ca Nivelul 2 e viu si corect, si detine calea de oprire. Simplitatea nu e un compromis, e cerinta: codul lui trebuie sa poata fi citit integral, linie cu linie, de o alta persoana, intr-o ora. Un ATmega328P cu 400 de linii de C e un Nivel 3 mai bun decat 20.000 de linii pe un procesor puternic.

**CUM.**

Uno R3 ruleaza o bucla fixa la 1 kHz, fara intreruperi complexe, fara alocari, fara biblioteci in afara de acces direct la registre:

| Functie | Implementare | De ce asa |
|---|---|---|
| **Question/answer** | la fiecare 5 ms trimite un index de intrebare pe UART catre ESP32; raspunsul corect e o valoare dintr-un tabel precalculat, obtinuta printr-un lant de operatii care trece prin toate ramurile importante din codul Nivelului 2 | testeaza continuu **calea de executie**, nu doar prezenta; un ESP32 blocat intr-o bucla gresita raspunde gresit, nu deloc |
| **Fereastra de timp** | raspunsul trebuie sa vina intre 1 si 4 ms; prea devreme e la fel de suspect ca prea tarziu | detecteaza si "raspunde din cache" |
| **Verificare de verdict** | ESP32 raporteaza si verdictul lui asupra comenzii; doua verdicte consecutive "comanda depaseste limita permisa" fara ca actuatorul sa fi scazut → Nivelul 3 intervine | acopera cazul in care Nivelul 2 detecteaza corect dar nu reuseste sa opreasca |
| **Linia ENABLE** | pin configurat open-drain catre pinul D7 al lui Uno R4; LOW = oprire | oprirea e un fir, nu o functie; nu poate fi ocolita din software-ul de pe laptop |
| **Latch** | odata trasa in LOW, linia ramane LOW pana la un reset explicit prin secventa dedicata | fara oscilatii intre normal si safe |
| **Watchdog hardware** | WDT-ul intern al AVR-ului la 15 ms; daca bucla lui se blocheaza, AVR-ul se reseteaza, iar la reset pinul e in stare de intrare cu pull-down extern logic → **ENABLE cade** | defectarea monitorului insusi duce la stare sigura, nu la pierderea protectiei |
| **Contor de evidenta** | pastreaza in EEPROM numarul de interventii, cauza si timestampul relativ | cutie neagra citibila dupa demo |

**Regula de proiectare cea mai importanta**: starea implicita a liniei ENABLE, la reset, la alimentare si la orice defect al lui Uno R3, e **oprit**. Nu exista niciun mod de defectare al monitorului care sa lase actuatorul activ. Asta e diferenta dintre un monitor si o decoratie, si e prima intrebare pe care ti-o va pune un inginer de safety.

**Interfata e definita ca sa fie inlocuibila.** Nivelul 3 comunica exclusiv prin: frame-uri pe UART (formatul din `firmware/shared/l3_protocol.h`) si o linie digitala. Daca vreodata ai acces la o placa FPGA, o implementare in Verilog a acelorasi doua interfete intra in locul lui Uno R3 fara nicio modificare in restul sistemului. Asta se scrie in `docs/SAFETY.md` ca punct de extensie, nu ca promisiune.

**VERIFICARE.**
- `firmware/uno_r3_l3monitor/tests/`: fiecare ramura din tabelul de intrebari, fiecare margine a ferestrei de timp (0,9 ms / 1,0 ms / 4,0 ms / 4,1 ms), comportamentul la reset si la watchdog.
- **K21** masurat de ESP32: marcheaza pe un GPIO momentul in care trimite deliberat un raspuns gresit si primeste intrerupere la caderea liniei ENABLE; contorul de 80 MHz da rezolutie de ~12,5 ns. Se ruleaza 1000 de repetari si se raporteaza histograma, nu media.
- **Test de nedistrugere**: 100.000 de cicluri de question/answer corecte, zero interventii false. Un monitor care se declanseaza singur e mai rau decat unul care lipseste, pentru ca te invata sa il ignori.
- **Test de defectare a monitorului**: scoti alimentarea lui Uno R3 in timpul unei comenzi active → ENABLE trebuie sa cada in mai putin de 10 ms si actuatorul sa intre in stare sigura. Verificat de 50 de ori.

**DEMO.** D11, D12.

---

## 48. Mixed-criticality si izolare — tot pe un singur laptop

**DE CE.** Platformele automotive pun domenii de criticalitati diferite pe acelasi SoC, separate prin partitionare. Nu ai nevoie de mai multe placi ca sa studiezi asta: ai nevoie de mai multe mecanisme de izolare pe aceeasi masina, si de masuratori care le compara.

**CUM.** Acelasi benchmark de bucla de control rulat in **cinci** configuratii, toate gratuite:

| Configuratie | Mecanism | Ce izoleaza |
|---|---|---|
| C0 | proces obisnuit, fara nimic | linia de baza (arata cat de rau e) |
| C1 | `SCHED_FIFO` + `isolcpus` + `mlockall` + IRQ mutate | scheduling si intreruperi |
| C2 | + cgroups v2 (`cpu.max`, `memory.max`, `io.max`) | resurse |
| C3 | network namespaces + `veth` + `tc netem` pentru nodurile logice | retea si defecte de retea |
| C4 | VM KVM cu vCPU pinuit, hugepages, `vhost-net` | domeniu complet separat |

- **Xen** ca a doua varianta de hypervisor, daca laptopul permite boot cu Xen (gratuit); daca nu, se documenteaza de ce si se ramane la KVM. Jailhouse ramane in afara scopului fara o placa dedicata — si se scrie explicit **de ce**, ceea ce e tot un semn de judecata inginereasca.
- **Yocto**: layer propriu `meta-volt` care produce o imagine minima bootabila, testata in **QEMU** (zero hardware). Ai povestea de BSP/integrare fara sa cumperi nimic.
- **Cross-compile pe ARM** ramane in scope prin QEMU user-mode + un rootfs aarch64: acelasi test suite ruleaza emulat, deci prinzi problemele de endianness, aliniere si atomics.
- **Livrabilul e tabelul**: jitter P99 al buclei de 1 ms in fiecare din cele cinci configuratii, cu si fara un `stress-ng` care satureaza CPU-ul si memoria. K23: degradare < 5% in C2 si C4.

**VERIFICARE.** `tests/perf/interference_matrix.cpp` ruleaza automat toate configuratiile posibile pe masina curenta si genereaza tabelul; configuratiile indisponibile sunt marcate `SKIPPED` cu motivul, nu inventate.

**DEMO.** D13.

---

## 49. Determinism pe retea: TSN in software, cu masuratori oneste

**DE CE.** TSN adevarat cere NIC-uri cu suport hardware. Nu le ai, si nu le cumperi. Dar **conceptele** (baza de timp comuna, ferestre de transmisie, prioritati, transmisie programata) se pot implementa si masura in software pe orice placa de retea, iar diferenta fata de hardware o explici — ceea ce e mai valoros decat sa o ascunzi.

**CUM.**
- **gPTP-lite cu timestamping software** (`SO_TIMESTAMPING` in mod software), disciplinand un ceas virtual comun. Tinta realista: offset RMS **< 50 µs** intre noduri pe loopback/veth, si sub 200 µs pe WiFi catre ESP32. Precizia se **masoara**, nu se presupune: un pin GPIO de pe ESP32 pulseaza la fiecare secunda de ceas global, iar Uno R3 masoara diferenta fata de propriul lui puls — ai un PPS improvizat, gratuit, care iti da o verificare independenta a sincronizarii intre doua dispozitive.
- **`taprio` in mod software** (fara offload hardware) pentru ferestrele de timp pe clase de trafic, plus `SO_PRIORITY` si `tc prio` pentru prioritizare — toate functioneaza pe orice NIC si pe `veth`.
- **Shaper time-aware propriu** in stratul de transmisie al platformei, aliniat cu tabela time-triggered a scheduler-ului: acelasi orar guverneaza si calculul, si trimiterea pe retea.
- **Masuratoarea care conteaza**: latenta P99 a clasei de control in timp ce saturezi legatura cu trafic de logging (generat local, `iperf`-style, gratuit). K20 devine: **P99 al clasei de control creste cu < 15%** la 100% incarcare pe clasele inferioare, cu prioritizare activata, fata de o crestere de cateva ordine de marime fara ea. Comparatia inainte/dupa e demonstratia.
- In `docs/PERFORMANCE.md` scrii explicit ce ar aduce hardware-ul TSN (timestamping in PHY, launch time per pachet, ordin de marime de precizie) si de ce cifrele tale sunt corecte pentru concluziile pe care le tragi.

**VERIFICARE.** K19 (redefinit: < 50 µs software, verificat incrucisat cu PPS-ul improvizat), K20 ca mai sus, plus capturi `tcpdump` care arata respectarea ferestrelor de transmisie.

**DEMO.** D14.

---

## 50. Analiza WCET si sinteza de orar prin SMT

**DE CE.** "WCET observat" e limita obisnuita a proiectelor de portofoliu. Daca faci si o estimare statica proprie, si o sinteza de orar demonstrata cu un solver, ai raspunsul complet la intrebarea "de unde stii ca task-urile isi tin deadline-urile".

**CUM.**
- **`volt-wcet`**: tool care ia IR-ul LLVM al functiilor din calea critica, construieste graful de flux de control, cere adnotari de limite de bucla (`VOLT_LOOP_BOUND(n)`, verificate la runtime in Debug), atribuie costuri instructiunilor dintr-un model de timp **calibrat prin masuratori pe masina tinta** (micro-benchmarks per clasa de instructiune, plus penalizari de cache configurabile), si rezolva drumul cel mai lung ca problema de programare liniara intreaga (IPET, cu `lp_solve`/Z3).
- Rezultatul se compara cu maximul observat si cu o estimare din **teoria valorilor extreme** (potrivire Gumbel pe maximele pe blocuri, ca in analiza probabilistica de timp). Trei estimari independente ale aceleiasi marimi, cu discutia diferentelor — asta e o sectiune de calitate academica in `docs/PERFORMANCE.md`.
- **Sinteza de orar time-triggered cu Z3**: codifici perioadele, precedentele, exclusivitatile, ferestrele de retea (Qbv) si constrangerile de nod ca formula SMT; solverul produce un orar global (calcul + retea) sau **demonstreaza ca nu exista**. Diferenta fata de greedy: ai o garantie, nu o solutie plauzibila.

**VERIFICARE.** K22: pe 10^8 executii reale, maximul observat nu depaseste niciodata bound-ul static (zero subestimari), iar raportul bound/observat < 2,5. Orarul sintetizat e verificat de un checker independent si rulat efectiv pe hardware.

**DEMO.** D14 (partea de determinism).

---

## 51. Securitate de nivel superior: PKI, UDS 0x29, cripto hibrida, fault injection fizic

**DE CE.** Seed & key e ce se facea acum 15 ani. Standardul actual (ISO 14229-1:2020) are serviciul **0x29 Authentication**, bazat pe certificate. Si, pentru ca teza ta e pe criptografie post-cuantica, ai o ocazie perfecta de a lega cele doua proiecte.

**CUM.**
- **PKI propriu**: CA de dezvoltare, certificate per rol (`SERVICE`, `ENGINEERING`, `PRODUCTION`) si per nod, cu lanturi si expirare. Chei stocate in keystore cu interfata de HSM.
- **UDS 0x29** implementat conform: `verifyCertificateUnidirectional/Bidirectional`, `proofOfOwnership`, `transmitCertificate`, gestiunea rolurilor si a duratei sesiunii autentificate. Testerul tau (`volt-diag`) prezinta un certificat; ECU-ul verifica lantul, cere dovada de posesie a cheii private (challenge semnat), si abia apoi acorda drepturi. Seed & key ramane implementat ca **mecanism vechi**, ca sa poti compara si explica de ce s-a schimbat.
- **Semnaturi hibride pentru secure boot**: ECDSA P-256 **+ ML-DSA (Dilithium)** in paralel; imaginea e acceptata doar daca ambele verifica. Masori dimensiunea semnaturii si timpul de verificare pe x86 si pe Cortex-M4, si discuti compromisul. Asta e o legatura directa cu teza ta de licenta si un subiect despre care foarte putini candidati pot vorbi din experienta proprie.
- **Rotatie de chei SecOC**: distributie de chei noi prin canalul diagnostic autentificat, cu tranzitie fara pierderea comunicatiei (acceptare a doua chei intr-o fereastra).
- **Fault injection fizic, fara componente cumparate**: resetari repetate declansate software (pinul RESET al lui Uno R4 comandat de un GPIO al ESP32 — un singur fir), intreruperi de comunicatie prin scoaterea firelor, si perturbatii injectate pe linia analogica. Verifici ca sistemul nu ajunge niciodata intr-o stare in care accepta o comanda nevalidata. Daca vreodata ai un tranzistor si un condensator, acelasi harness suporta si glitch pe alimentare — interfata de campanie e aceeasi.

**VERIFICARE.** K28; campanie de 5.000 de resetari si intreruperi, cu inregistrarea fiecarui rezultat (boot corect / stare sigura / refuz), criteriu: **zero** cazuri de comanda acceptata fara verificare completa.

**DEMO.** D12, D15.

---

## 52. XCP + A2L: masurare si calibrare cu tooling industrial

**DE CE.** In automotive, calibrarea si masurarea se fac cu XCP (CANape, INCA, PyXCP). Un ECU care vorbeste XCP se conecteaza la lantul de tooling real. E o capabilitate pe care o cauta explicit rolurile de calibrare/validare.

**CUM.**
- **XCP slave** peste Ethernet (XCP-on-UDP) si peste CAN, cu: `CONNECT`, `GET_STATUS`, `SET_MTA`, `UPLOAD/DOWNLOAD` (calibrare), si — partea valoroasa — **DAQ lists**: configurezi liste de semnale masurate ciclic (1 ms / 10 ms / 100 ms), cu marcaj de timp, transmise catre tool fara polling.
- **Generare A2L** automata din metadatele parametrilor si semnalelor (`tools/a2l_gen`): adrese, tipuri, conversii, limite, grupuri. Fisierul se incarca in `pyxcp` (si in CANape, daca ai acces la unul).
- Calibrare la cald a parametrilor non-safety (praguri ABS, constante de filtru) in timp ce sistemul ruleaza, cu verificarea limitelor si jurnalizarea fiecarei modificari.

**VERIFICARE.** K24: 32 de semnale la 1 kHz timp de 10 minute, zero pierderi, verificat cu `pyxcp` in CI (peste UDP loopback) si pe hardware.

**DEMO.** D14.

---

## 53. Co-simulare standardizata: FMI/FMU + bridge ROS 2

**DE CE.** Doua ecosisteme, doua audiente. **FMI** e standardul de co-simulare din automotive (Simulink, dSPACE, CarMaker vorbesc FMU) — daca modelul tau de vehicul e un FMU, devine interoperabil cu tot ce e industrial. **ROS 2** e limba franca a robotilor si a conducerii autonome, iar AROBS si companiile de autonomous driving lucreaza cu el.

**CUM.**
- **Export FMU 2.0/3.0 (Co-Simulation)** al modelului de vehicul: interfata `fmi3DoStep`, variabile de intrare/iesire descrise in `modelDescription.xml`, impachetat ca `.fmu`. Poate fi rulat de orice master FMI.
- **Import FMU**: poti inlocui modelul tau cu un model tert (de exemplu un model de anvelopa mai bun) fara sa schimbi nimic in platforma. Demonstrezi asta ruland acelasi scenariu cu doua modele diferite.
- **Bridge ROS 2** (`volt-ros2-bridge`): publica starea vehiculului, comenzile si diagnosticul ca topicuri ROS 2, si accepta comenzi de la noduri ROS 2. Asa poti folosi `rviz2` pentru vizualizare, `ros2 bag` pentru inregistrare, si poti conecta stack-uri de perceptie existente. Bridge-ul e strict izolat: e un client SOME/IP normal, fara acces privilegiat.

**VERIFICARE.** FMU-ul validat cu `fmpy` (checker de conformitate) in CI; rularea aceluiasi scenariu prin FMU si nativ trebuie sa dea aceleasi rezultate in limita tolerantei de integrare. Bridge-ul ROS 2 testat cu un nod de test care conduce un scenariu complet.

**DEMO.** D15.

---

## 54. Verificare la nivel de cod (CBMC) si analiza cantitativa de siguranta

**DE CE.** TLA+ verifica protocolul; nu spune nimic despre codul C++ care il implementeaza. CBMC (bounded model checking) e folosit in industrie exact pentru asta, si acopera exact tipurile de bug care produc DTC-uri fantoma: overflow, indexare, stari imposibile.

**CUM.**
- **CBMC** pe 12+ functii critice, izolate ca unitati verificabile: masina de stari E2E, reasamblarea ISO-TP, debounce-ul de DTC, comparatorul dual-channel, `epoch_guard`-ul software, parserul de DBC, aritmetica de slip. Se demonstreaza: absenta overflow-ului, absenta accesului in afara limitelor, respectarea pre/postconditiilor scrise ca `__CPROVER_assert`.
- **Frama-C/ACSL** pe firmware-ul MCU (C), pentru functiile de verificare a comenzii.
- **FMEDA** (`tools/fmeda`): tabel cu modurile de defect ale fiecarei componente, rata de defectare presupusa, mecanismul de diagnostic care le acopera si acoperirea estimata → calculezi **SPFM** si **LFM** pentru functia de frana. Cifrele sunt ilustrative (nu ai date de fiabilitate reale) si o spui, dar **metoda e corecta**, iar tabelul arata ca intelegi de ce exista fiecare mecanism de diagnostic din sistem.
- **FTA generata automat** din modelul de propagare a defectelor: din tabelul de fault-uri si reactii, `tools/fta_gen` construieste arborele de defect pentru fiecare safety goal si il randeaza. Documentatia de siguranta devine astfel derivata din configuratia reala, nu desenata separat.

**VERIFICARE.** K25 in CI; tabelul FMEDA regenerat de `make kpi`; arborii FTA verificati impotriva scenariilor de fault injection (fiecare cale din arbore trebuie sa aiba un test care o parcurge — sau e marcata explicit ca neacoperita).

**DEMO.** D11 (partea de rigoare).

---

## 55. ADAS cu senzori pe care ii ai deja

**DE CE.** Un serviciu ADAS care primeste un vehicul-tinta inventat e o cutie goala. Cu un senzor real ai o functie reala, cu deadline-uri, incertitudine si moduri de defect adevarate. Nu ai nevoie de lidar ca sa obtii asta.

**CUM — trei surse reale, toate gratuite:**

1. **Camera laptopului** (`services/perception_lane/`): detectie de banda clasica cu OpenCV — transformare de perspectiva, prag adaptiv, Hough, potrivire de polinom, estimarea offsetului lateral si a curburii. Fara retele neuronale: determinist, explicabil, testabil pe cadre inregistrate. Ruleaza la 20 Hz, in clasa MEDIUM, si **nu are voie** sa influenteze jitterul buclei de 1 ms — inca o demonstratie de freedom from interference, de data asta cu o sarcina grea si reala.
2. **Distanta prin camera**: un obiect de dimensiune cunoscuta (o foaie A4 tinuta in mana, un marcaj imprimat) da o estimare de distanta prin dimensiunea aparenta. Il apropii de camera → ACC-ul cere deceleratie → matricea LED de pe R4 se aprinde. Demo fizic, complet, cu zero lei.
3. **RSSI-ul WiFi / BLE de pe ESP32 sau Uno R4** ca senzor de proximitate grosier catre un "vehicul-tinta" (celalalt modul, tinut in mana si mutat). E zgomotos si nefiabil — **exact de aceea e util**: iti forteaza sa implementezi corect calitatea datelor, filtrarea si degradarea, si iti da un mod de defect natural pentru demo.

**Regula de proiectare care conteaza**: lipsa datelor **nu** inseamna "drum liber". Fiecare detectie are calitate si varsta; obiectele pierdute se propaga cu timp de expirare; la expirare se trece in mod degradat, nu se presupune drum liber. E o greseala clasica si periculoasa, si faptul ca o tratezi explicit se remarca.

**Arhitectural**: `AdasAccService` genereaza doar **cereri** de deceleratie catre `BrakeControlService`, care ramane singurul cu autoritate asupra actuatorului. Separarea functie de confort / functie de siguranta e o decizie pe care o explici la interviu.

**VERIFICARE.** K27 redefinit: latenta camera → cerere de deceleratie, P99 < 120 ms; set de 50 de cadre inregistrate cu adevar de referinta masurat manual; teste de regresie pe detectia de banda si pe estimarea distantei; test care demonstreaza ca activarea perceptiei nu misca K1.

**DEMO.** D15.

---

## 56. API in stil Adaptive AUTOSAR (`ara::com`) + manifest de executie

**DE CE.** Daca middleware-ul tau are aceeasi forma ca `ara::com`, un inginer care lucreaza cu Adaptive AUTOSAR intelege codul tau in 30 de secunde si vede ca stii vocabularul. Nu implementezi AUTOSAR — imprumuti modelul de programare, care e bun oricum.

**CUM.**
- **Proxy / Skeleton** generate din catalogul de servicii (§36): `VehicleStateProxy` cu `FindService`, handler de disponibilitate, `Events` cu `Subscribe`/`GetNewSamples`, `Methods` care intorc `ara::core::Future`-like (`volt::Future`), si **Fields** (valoare cu getter/setter/notificare) — exact triada metode/evenimente/campuri.
- **Manifest de executie** (JSON): pentru fiecare aplicatie — instante de servicii oferite si cerute, mapare pe porturi si retele, grupuri de resurse, dependinte de pornire, moduri de functionare. Runtime-ul citeste manifestul, nu are nimic cablat in cod. Asta e chiar filozofia Adaptive.
- **Modele de functionare (Machine/Function Groups)**: `Startup`, `Driving`, `Diagnostics`, `Update`, `Shutdown` — cu reguli despre ce servicii ruleaza in fiecare mod; schimbarea modului e o operatie de platforma, testata.
- Se documenteaza clar in `docs/ARCHITECTURE.md` ce a fost imprumutat conceptual si ce nu exista (persistenta AUTOSAR, IAM complet, ara::phm) — onestitate care iti creste, nu iti scade, credibilitatea.

**VERIFICARE.** Generatorul de proxy/skeleton are teste de aur (cod generat comparat cu referinta); un test verifica faptul ca aplicatiile pornesc **exclusiv** pe baza manifestului (schimbi manifestul, se schimba comportamentul, fara recompilare).

---

## 57. Demo-uri suplimentare

### D11 — Monitorul independent castiga (2 min) ★
```bash
make demo-egas
volt-inject rogue_command --value 100% --bypass-guard   # Nivel 1 "innebuneste"
```
Trei acte, fiecare cu o bariera in minus:
1. Nivelul 1 comanda deliberat frana la maxim la 120 km/h → **Nivelul 2** (ESP32) recalculeaza comanda permisa si o respinge; matricea LED nu urca.
2. `--kill-l2` opreste raspunsurile Nivelului 2 → **Nivelul 3** (Uno R3) constata ca question/answer-ul nu mai vine in fereastra si **trage linia ENABLE in LOW**; matricea LED se stinge instantaneu si ramane stinsa (latch).
3. `--kill-l3`, adica scoti fizic USB-ul lui Uno R3 → watchdog-ul lui cade, linia ENABLE cade, actuatorul intra tot in stare sigura. **Nu exista mod de defectare care sa lase actuatorul activ.**

Arati histograma K21 masurata de contorul de 80 MHz al ESP32 si contoarele din EEPROM-ul lui Uno R3.
Ce dovedeste: intelegi ca siguranta nu poate depinde de corectitudinea functiei complexe. Asta e demo-ul de care isi vor aminti.

### D12 — Atac fizic (2 min)
Resetari repetate ale placii de actuare in timpul verificarii MAC-ului, replay de frame-uri inregistrate pe tunel, si comenzi cu epoch vechi injectate de pe un al doilea proces care se pretinde primary. Arati: toate respinse, evenimentele in log-ul de securitate cu inlantuire de hash-uri, si contoarele de refuz din EEPROM-ul placilor.

### D13 — Doua sisteme de operare, un SoC (2 min)
Rulezi acelasi benchmark de bucla de 1 ms in cele cinci configuratii de izolare din §48, cu `stress-ng` care satureaza CPU-ul si memoria in paralel. In C0 jitterul explodeaza, in C2 si C4 nu se clinteste (K23). Tabelul comparativ se genereaza pe loc, pe masina pe care ruleaza demo-ul.

### D14 — Determinism, demonstrat pe ce ai (3 min)
Orar time-triggered sintetizat cu Z3 si rulat efectiv, sincronizarea de ceas verificata independent prin PPS-ul improvizat (ESP32 pulseaza, Uno R3 masoara), si tabelul WCET (bound static / maxim observat / estimare din teoria valorilor extreme) unul langa altul. Apoi pornesti trafic parazit pana la saturare si arati efectul prioritizarii: cu shaper pornit vs. oprit (K20).

### D15 — Vehiculul complet (3 min)
Apropii un obiect de camera laptopului; perceptia estimeaza distanta si viteza de apropiere; ACC-ul cere deceleratie; `BrakeControlService` o executa; **matricea LED de pe Uno R4 urca**; Uno R3 citeste iesirea analogica si inchide bucla; `rviz2` arata scena prin bridge-ul ROS 2, iar modelul de vehicul ruleaza ca FMU. In mijlocul manevrei omori un nod — bara de pe matrice nu tresare. Un singur demo care atinge perceptie, control, distributie, siguranta si tooling standard, cu hardware de zero lei.

---

## 58. Bullet-uri de CV suplimentare (din Partea III)

- Implemented a three-level EGAS-style monitoring architecture distributed across three different processor architectures (x86_64 control function, Xtensa function monitor, 8-bit AVR controller monitor holding the physical actuator enable line), with challenge-response supervision and a fail-safe-by-default shutdown path; no single failure mode leaves the actuator active.
- Implemented output-stage readback diagnosis and 2-out-of-2 sensor cross-checking across two independent ADCs on separate microcontrollers, closing the control loop physically and exposing real quantization, offset and noise divergence.
- Deployed and benchmarked the platform across five isolation configurations (plain process, SCHED_FIFO with isolated CPUs, cgroups v2, network namespaces, KVM) and quantified their interference on real-time jitter; built a Yocto layer producing a bootable minimal image validated in QEMU.
- Implemented a gPTP-derived global time base and a time-aware traffic shaper aligned with the time-triggered task schedule, achieving cluster-wide synchronization within 50 µs and keeping control-class P99 latency within 15% under full network saturation; synchronization independently cross-checked with a microcontroller-generated PPS reference.
- Built a static WCET estimator over LLVM IR using IPET with a measurement-calibrated timing model, cross-validated against extreme-value analysis and 10^8 measured executions with zero underestimations; synthesized global time-triggered schedules using an SMT solver.
- Implemented certificate-based diagnostic authentication (UDS 0x29) with an internal PKI, SecOC key rotation, and hybrid ECDSA + ML-DSA secure boot signatures; validated against thousands of injected reset, replay and stale-epoch attacks on real microcontrollers.
- Implemented an XCP slave with DAQ lists and automatic A2L generation, enabling live measurement and calibration with standard automotive tooling.
- Exported the vehicle model as an FMI co-simulation FMU and built a ROS 2 bridge, enabling interoperability with industrial simulation tools and robotics stacks; closed the control loop through three physically separate microcontrollers with an independent hardware shutdown line.
- Applied bounded model checking (CBMC) to safety-critical functions and produced FMEDA diagnostic-coverage metrics and automatically generated fault trees derived from the live fault-propagation configuration.
- Implemented an `ara::com`-style service API (proxies, skeletons, events, methods, fields) driven entirely by execution manifests, with machine and function-group operating modes.
