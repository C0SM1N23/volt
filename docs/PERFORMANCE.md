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

**Cum:** `platform/trace/tests/trace_benchmark_test.cpp`, 20 de loturi × 5
cicluri, mediana, cu incalzire aruncata explicit.

| Metrica | Valoare |
|---|---|
| Ciclu cu tracing oprit | ~89.0 µs |
| Ciclu cu tracing pornit | ~89.1 µs |
| **Overhead** | **0,1 – 0,2 %** (5 rulari) |
| Cost per eveniment | 18 – 30 ns |

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
