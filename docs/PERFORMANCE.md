# Performanta VOLT — metodologie si masuratori

Fiecare numar din acest fisier are testul care il regenereaza. Un numar fara
metodologie nu inseamna nimic, asa ca fiecare masuratoare spune pe ce a fost
facuta si cum.

---

## Metodologie

Regulile din SPEC §25, aplicate la ce exista deja in repo:

- **Ce se raporteaza:** P50 si P99, niciodata doar media. O medie ascunde exact
  cozile care rup un deadline.
- **Warm-up:** primele iteratii sunt aruncate explicit, ca masuratoarea sa nu
  contina page fault-urile de prima atingere si invatarea predictorului de
  salturi.
- **Cum se cronometreaza:** ceasul monoton prin PAL. O citire de ceas costa
  zeci de nanosecunde, adica acelasi ordin de marime cu ce se masoara, deci
  citirile incadreaza un lot de apeluri, nu fiecare apel, iar costul se imparte.
- **Ce nu e inca aici:** mediul din SPEC §25 (kernel PREEMPT_RT, `isolcpus`,
  guvernor `performance`, turbo si C-states oprite, `cyclictest` publicat ca
  baseline). Masuratorile de mai jos sunt facute pe o masina de dezvoltare
  obisnuita, deci sunt limite superioare, nu cifre de referinta.

---

## L1 — Costul unui apel de logare pe calea apelantului

**Tinta:** SPEC §8.4 bugeteaza ~40-80 ns; P05 cere sub 100 ns.

**Ce se masoara:** un `VOLT_LOG_INFO` cu doua argumente intregi, de la intrarea
in macro pana la iesire: verificarea filtrului, citirea ceasului, rezervarea
unui slot in ring si codificarea inregistrarii. Nu include drenajul, care
ruleaza pe alt thread.

**Cum:** `platform/log/tests/log_benchmark_test.cpp`, 2000 de loturi × 1000 de
apeluri, cu firul de drenaj pornit ca ring-ul sa nu se umple.

| Metrica | Valoare |
|---|---|
| P50 | **56 ns** |
| P99 | 320 ns |

**Mediu:** AMD Ryzen 7 7435HS, 16 fire logice, Ubuntu 26.04, GCC 14.3, build
`dev` (`-O0 -g`). Kernel generic, fara izolare de CPU.

**Observatii oneste:**
- Build-ul `dev` e neoptimizat. Pe `release` cifra scade, dar testul ruleaza pe
  `dev` fiindca acolo ruleaza si restul suitei; ce se verifica e ca bugetul e
  respectat chiar si in cel mai defavorabil build.
- P99 de 320 ns e zgomot de planificare pe o masina partajata, nu cost al
  codului: nu exista alocare, blocare sau syscall pe calea masurata in afara
  citirii de ceas prin vDSO.
- Asertiunea din test e pe P50. P99 pe un runner partajat depinde de ce mai
  ruleaza pe masina, iar un prag pe el ar produce un test instabil, ceea ce
  regula 8.9 interzice.

**Ce a schimbat cifra:** codificarea scria campurile octet cu octet prin
`span_utils`, cu verificare de limite la fiecare. Rezervarea se face oricum o
data pentru toata inregistrarea, asa ca scrierile au devenit cate o singura
copiere: P50 a scazut de la 87 ns la 56 ns.

---

## L2 — Conservarea inregistrarilor sub incarcare

**Tinta:** nicio inregistrare pierduta fara sa fie contorizata (SPEC §42.1).

**Ce se masoara:** 8 fire × 1.000.000 de inregistrari, cu drenaj concurent.
Invariantul verificat este `scrise + aruncate == produse`.

**Cum:** `platform/log/tests/log_concurrency_test.cpp`, rulat si sub TSan.

| Metrica | Valoare |
|---|---|
| Inregistrari produse | 8.000.000 |
| Pierdute necontorizate | **0** |
| Data races raportate de TSan | **0** |

Aruncarea sub incarcare e comportament proiectat, nu defect: un producator care
gaseste ring-ul plin arunca inregistrarea si o numara, fiindca a bloca un ciclu
de control ca sa incapa o linie de log nu e niciodata compromisul corect.

---

## K12 — Overhead-ul tracing-ului activat

**Tinta:** SPEC §0.2 K12 cere sub 2% cand tracing-ul e pornit.

**Ce se masoara:** acelasi binar ruleaza acelasi ciclu de control cu tracing-ul
oprit si pornit; diferenta e overhead-ul. Comutarea e la rulare, nu la
compilare, fiindca intrebarea din K12 e cat costa tracing-ul *activat*, nu cat
costa codul care nu exista.

**Ciclul folosit:** ~89 µs de lucru cu 10 puncte de trasare, dimensionat din
SPEC §8.1 (task safety-critical pe perioada de 1 ms, cu buget de cateva sute de
microsecunde). Masurarea fata de o bucla care se termina in nanosecunde ar
raporta costul unui punct de trasare raportat la nimic, ceea ce nu e intrebarea.

**Cum:** `platform/trace/tests/trace_benchmark_test.cpp`, 20 de perechi de
loturi × 5 cicluri, in ordine A/B si B/A alternata, cu mediana diferentelor
pereche si cu incalzire aruncata explicit. Intercalarea impiedica deriva de
frecventa CPU sau de incarcare a hostului dintre doua faze lungi sa fie
raportata drept cost de tracing.

| Metrica | Valoare |
|---|---|
| Ciclu cu tracing oprit | 90,7 µs |
| Ciclu cu tracing pornit | 91,4 µs |
| **Overhead** | **0,81 %** (cel mai slab; interval 0,02 – 0,81 %, 2 rulari) |
| Cost per eveniment | 73 ns (cel mai slab; interval 2 – 73 ns) |

**Mediu:** identic cu L1 — AMD Ryzen 7 7435HS, Ubuntu 26.04, GCC 14.3, build
`dev` (`-O0 -g`), kernel generic.

**Observatii oneste:**
- Cifrele sunt de pe un build neoptimizat. Pe `release` costul per eveniment
  scade; se raporteaza cel mai defavorabil caz.
- Prima masuratoare dupa pornire da un cost per eveniment mai mare (~90 ns),
  fiindca liniile de cache ale ring-ului sunt reci. De aceea exista incalzire.
- Testul sare sub sanitizer sau sub coverage: instrumentarea numara fiecare
  acces la memorie, deci diferenta masurata acolo e a uneltei, nu a codului.

**Ce a schimbat cifra:** variabila thread-local a punctului de trasare folosea
modelul TLS implicit, care intr-o biblioteca inseamna un apel in loader la
fiecare acces. Cu `initial-exec` accesul devine o citire relativa la registru.

---

## T1 — Conservarea evenimentelor de trasare sub incarcare

**Tinta:** niciun eveniment pierdut fara sa fie contorizat.

**Ce se masoara:** 4 fire × 2.500.000 de evenimente (10 milioane in total), cu
un colector care goleste ring-urile in paralel. Invariantul verificat este
`colectate + aruncate == produse`.

**Cum:** `platform/trace/tests/trace_benchmark_test.cpp`, rulat si sub TSan.

| Metrica | Valoare |
|---|---|
| Evenimente produse | 10.000.000 |
| Pierdute necontorizate | **0** |
| Data races raportate de TSan | **0** |

---

## M1 — Latenta si throughput-ul cozilor marginite

**Tinta:** P08 cere costul publicat pentru SPSC si MPSC, iar SPEC §8.3 cere
cozi lock-free fara alocare pe calea de date.

**Ce se masoara:** latenta este un round-trip local `try_push` + `try_pop`,
masurat in 10.000 de loturi a cate 1.000 de operatii dupa incalzire. Citirile
ceasului PAL incadreaza lotul, iar valorile sunt P50/P99 pe costul unei
operatii din lot. Throughput-ul este end-to-end, cu producator si consumator pe
fire PAL distincte: un producator pentru SPSC, patru pentru MPSC si 10 milioane
de transferuri reusite in fiecare caz.

**Cum:** `platform/memory/tests/memory_benchmark.cpp`, Google Benchmark, cinci
repetitii. Comanda care regenereaza tabelul:

```sh
cmake --preset release
cmake --build --preset release --target memory_benchmarks
build/release/platform/memory/tests/memory_benchmarks \
  --benchmark_min_time=0.5s \
  --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true
```

| Coada | Latenta P50 | Latenta P99 | Cost transfer concurent | Throughput median |
|---|---:|---:|---:|---:|
| SPSC, 1 producator | **1,372 ns** | **1,383 ns** | **13,0 ns** | **76,88 M mesaje/s** |
| MPSC, 4 producatori | **2,905 ns** | **2,936 ns** | **74,5 ns** | **13,42 M mesaje/s** |

Latentele locale sunt stabile intre rulari (cv sub 0,3%). Cifrele concurente se
misca cu incarcarea masinii: pe doua rulari consecutive, SPSC a dat 11,0-13,0 ns
(76,9-91,1 M mesaje/s) si MPSC 74,5-78,6 ns (12,7-13,4 M mesaje/s). Tabelul
contine rularea cea mai lenta din cele doua, nu cea mai favorabila.

**Mediu:** AMD Ryzen 7 7435HS, 16 fire logice, Ubuntu 26.04.1, GCC 14.3,
build `release` (`-O3`), kernel generic. CPU scaling si boost au ramas active,
fara izolare de CPU; biblioteca Google Benchmark a distributiei este construita
in mod debug. Acestea sunt rezultate reproductibile pe masina de dezvoltare,
nu cifre pentru tinta PREEMPT_RT din SPEC §25.

**Observatii oneste:**

- Latenta locala izoleaza costul structurii si nu include transferul intre
  cache-urile a doua nuclee; masurarea concurenta include acel cost si este
  cifra corecta pentru capacitatea end-to-end.
- MPSC plateste compare-exchange pe cursor si secventa per slot, iar cei patru
  producatori contesta aceeasi linie de cache. Diferenta fata de SPSC este
  costul asteptat al topologiei, nu o pierdere de mesaje.
- Inainte de P08 aceste primitive si benchmark-ul nu existau. Dupa P08, ambele
  cozi ruleaza fara heap si fara blocare pe calea masurata, cu cifrele de mai sus.

## M2 — Verificarea concurentei pentru memorie

**Ce se masoara:** SPSC si MPSC transfera fiecare 10 milioane de valori sub
TSan. Acelasi binar exercita un milion de publicari `SeqLock` cu trei cititori
si un milion de transferuri de proprietate prin pool-ul atomic cu patru
lucratori.

**Cum:** `platform/memory/tests/queue_concurrency_test.cpp`, prin PAL POSIX.

| Campanie | Operatii reusite | Pierderi sau dublari | Data races TSan |
|---|---:|---:|---:|
| SPSC | 10.000.000 | **0** | **0** |
| MPSC, 4 producatori | 10.000.000 | **0** | **0** |
| SeqLock, 3 cititori | 1.000.000 publicari | **0 valori mixte** | **0** |
| AtomicFixedPool, 4 lucratori | 1.000.000 transferuri | **0 sloturi duble** | **0** |

---

## K10 — Alocari dinamice pe calea de siguranta

**Tinta:** SPEC 0.2 cere exact 0 alocari dinamice pe calea de siguranta in
regim stationar, masurate cu hook pe alocator si `no_alloc_scope`.

**Ce se masoara:** o bucla de control care ruleaza 60 s intr-un
`volt::no_alloc_scope`. Fiecare ciclu ia opt esantioane dintr-o coada SPSC, le
reduce, imprumuta si elibereaza un slot dintr-un `FixedPool`, taie o zona de
lucru dintr-un `Arena` cu `FrameScope`, si publica rezultatul intr-un
`SeqLock` — adica formele pe care le foloseste chiar calea de date.

**Cum:** `platform/memory/tests/no_alloc_soak_test.cpp`, prin ceasul PAL.
Contoarele vin din `AllocationTracker`, alimentat de operatorii de alocare
inlocuiti din `platform/memory/src/allocation_hooks.cpp`.

```sh
cmake --preset dev
ctest --preset dev -R memory_no_alloc_soak
```

| Masuratoare | Debug | Release |
|---|---:|---:|
| Durata buclei | 60,0 s | 60,0 s |
| Alocari in `no_alloc_scope` | **0** | **0** |
| Violari raportate de garda | **0** | **0** |
| Reactia la o violare | abort cu backtrace | contor + `TRACE(AllocationViolation)` |

**Observatii oneste:**

- Sub AddressSanitizer si ThreadSanitizer, runtime-ul sanitizer-ului defineste
  el operatorii de alocare, iar contoarele VOLT raman la zero fiindca nu vad
  nimic. Acolo testele **se sar explicit**, nu trec: un zero nemasurat arata
  identic cu un zero real, si numai unul dintre ele inseamna ceva.
  `AllocationTracker::hooks_installed()` spune care dintre cele doua e cazul.
- 60 s este durata ceruta de P09. SPEC 8.3 cere un soak de 1 h cu
  `--check-no-alloc` la nivel de aplicatie; acela vine cand exista aplicatia,
  si va folosi acelasi contor.
- Cifra e masurata pe masina de dezvoltare, nu pe tinta PREEMPT_RT din SPEC 25.
  Numarul de alocari nu depinde insa de masina: e o proprietate a codului.

---

## K3 — Latenta IPC intra-nod, toate mecanismele din SPEC §10.1

**Tinta:** shm one-way P50 < 2 µs si P99 < 8 µs (K3), masurate prin ping-pong.

**Ce se masoara:** acelasi payload de 64 de octeti trece dus-intors intre doua
fire; o latenta one-way este jumatate dintr-un dus-intors. Percentilele vin din
esantioane per-iteratie, cu 2.000 de iteratii de incalzire aruncate. Throughput:
pentru transporturile cu coada (shm ring), 1.000.000 de mesaje intr-o singura
directie cu drenaj concurent, contorizate la destinatie cu conservare verificata
(`primite + pierdute == produse`); pentru seqlock, rata sustinuta de `store`;
pentru socketuri si mq, inversul latentei dus-intors — costul pe mesaj domina
complet rata sustenabila a unui request/response strict alternant.

**Cum:** `platform/ipc/tests/ipc_benchmark_test.cpp` — 100.000 de runde pentru
mecanismele shm, 20.000 pentru cele cu syscall. Coada de mesaje POSIX intra prin
PAL (`IMessageQueue`), adaugata exact pentru rolul pe care SPEC §10.1 i-l da:
termen de comparatie si fallback; regula 2.15 interzice apeluri POSIX in afara
`platform/pal/`, deci benchmark-ul nu avea alt drum legal catre `mq_*`.

| Mecanism | P50 | P99 | P99.9 | max | Throughput |
|---|---|---|---|---|---|
| shm SPSC ring (loan/publish) | **1,02 µs** | **2,37 µs** | 4,11 µs | 31,1 µs | 1,95 M msg/s |
| shm seqlock (ultima valoare) | **0,38 µs** | 0,66 µs | 1,14 µs | 12,3 µs | 15,2 M store/s |
| Unix domain socket | 3,90 µs | 8,05 µs | 11,5 µs | 28,2 µs | ~0,13 M msg/s |
| POSIX mq | 3,52 µs | 6,57 µs | 10,9 µs | 29,2 µs | ~0,14 M msg/s |
| TCP loopback | 6,61 µs | 14,2 µs | 31,3 µs | 75,7 µs | ~0,08 M msg/s |
| UDP loopback | 5,87 µs | 11,9 µs | 21,8 µs | 44,2 µs | ~0,09 M msg/s |

K3 cere P50 < 2 µs si P99 < 8 µs pe mediul reglat; ring-ul le tine deja pe un
build neoptimizat fara isolcpus, cu marja. Ierarhia dintre mecanisme este cea
asteptata din SPEC §10.1; mq iese putin inaintea UDS pe kernelul asta, diferenta
e in zgomotul dintre doua syscall-uri.

**Mediu:** AMD Ryzen 7 7435HS, 16 fire logice, Ubuntu 26.04, GCC 14.3, build
`dev` (`-O0 -g`), kernel generic, fara izolare de CPU. Tabelul de referinta din
SPEC §10.1 presupune PREEMPT_RT + isolcpus; cifrele de aici sunt limite
superioare pe o masina de dezvoltare, remasurabile cu acelasi test pe mediul
reglat la P19.

**Observatii oneste:**
- Fiecare esantion include doua citiri de ceas prin vDSO (~40-60 ns pe masina
  asta); la nivelul shm citirea de ceas e o fractiune vizibila din numar, deci
  cifrele shm sunt supraevaluate cu zeci de nanosecunde, nu subevaluate.
- Ping-pong-ul strict alternant tine ringurile la adancime 1, deci masoara
  calea, nu presiunea pe coada; testul de conservare de la T-sarcina acopera
  restul.
- Cifrele socket depind de ce face kernelul cu wakeup-urile intre fire; pe un
  runner incarcat cozile P99.9 si max cresc cu un ordin de marime fara ca vreo
  linie de cod VOLT sa se schimbe.

**Ce mai spune throughput-ul:** rata shm ring de ~2 M msg/s include, per mesaj,
imprumut + refcount + fanout + conservare contorizata, pe `-O0`; seqlock-ul, care
nu tine evidenta nimanui, arata plafonul structurii pure. Diferenta este pretul
contabilitatii care face crash-recovery-ul posibil, platit o data pe mesaj.

Histograma log2 a distributiei one-way pentru shm ring (generata de acelasi
test, valorile sunt numar de esantioane):

```
      512 ns | ####################################   57114
     1024 ns | ##########################             41129
     2048 ns | ##                                      1656
     4096 ns | #                                         85
     8192 ns | #                                          12
    16384 ns | #                                           4
```
