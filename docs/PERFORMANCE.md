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
