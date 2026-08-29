# Deviatii de la regulile de analiza statica

Fiecare `// NOLINT(regula) — deviation: DEV-NNN` din cod are aici o intrare cu
justificarea. Fara intrare, deviatia nu e acceptata la recenzie.

---

## DEV-001 — `cert-dcl58-cpp` in `platform/core/include/volt/core/strong_id.hpp`

**Regula semnalata:** `cert-dcl58-cpp` (modification of 'std' namespace can
result in undefined behavior).

**Unde:** specializarea `std::hash<volt::core::StrongId<Tag, Repr>>`.

**De ce se deviaza:** standardul permite explicit specializarea sabloanelor din
`std` pentru tipuri definite de program ([namespace.std]), iar `std::hash` e
mecanismul prin care un tip devine cheie in containerele neordonate. Regula
semnaleaza deschiderea namespace-ului `std`, fara sa distinga cazul permis de
cel interzis.

**De ce nu exista alternativa mai buna:** un hasher separat ar obliga fiecare
`std::unordered_map` cu cheie `StrongId` sa il numeasca explicit la fiecare
declaratie, ceea ce ar face usor de uitat si ar duce la doua tipuri de harti
incompatibile pentru aceeasi cheie.

**Limitele deviatiei:** se aplica exclusiv acestei specializari. Orice alta
adaugare in namespace-ul `std` ramane interzisa.

---

## DEV-002 — `readability-magic-numbers` in directoarele de teste

**Regula semnalata:** `readability-magic-numbers`.

**Unde:** dezactivata prin `.clang-tidy` in directoarele de teste, nu prin
`NOLINT` in cod.

**De ce se deviaza:** un test isi scrie valoarea asteptata ca literal. Intr-un
tabel de cazuri, a da nume fiecarei valori ascunde exact ce arata tabelul:
perechea intrare-rezultat devine doua nume care trebuie urmarite in alta parte.

**Limitele deviatiei:** doar codul de test. In cod de productie regula ramane
activa, unde un numar neexplicat chiar e un defect (vezi si regula 7.4).

---

## DEV-003 — `cert-msc32-c` / `cert-msc51-cpp` in directoarele de teste

**Regula semnalata:** generator de numere aleatorii initializat cu o valoare
constanta.

**Unde:** dezactivata prin `.clang-tidy` in directoarele de teste.

**De ce se deviaza:** AGENTS.md 8.5 cere explicit ca orice sursa de aleator
dintr-un test sa primeasca seed explicit, iar seed-ul sa fie tiparit la esec,
ca rularea care a gasit problema sa poata fi repetata identic. Regula
clang-tidy protejeaza impotriva predictibilitatii in context de securitate —
exact proprietatea pe care un test o cere.

**Limitele deviatiei:** doar codul de test. In cod de productie, orice sursa de
aleator trece prin `Environment` (SPEC 4/D1), unde seed-ul e injectat, nu fixat
in cod.

---

## DEV-004 — `readability-redundant-member-init`, dezactivata global

**Regula semnalata:** initializatorul unui membru e redundant cand tipul are
deja un constructor implicit care il aduce in aceeasi stare.

**Unde:** dezactivata in `.clang-tidy` din radacina.

**De ce se deviaza:** regula intra in conflict direct cu un warning al
compilatorului, care e eroare prin setul din SPEC 7.1. Un membru fara
initializator explicit face ca orice construire cu designated initializers care
nu il numeste sa produca `-Wmissing-designated-field-initializers`. Cum
`-Werror` e activ peste tot, respectarea regulii clang-tidy ar opri build-ul.
Exemplu concret: `ThreadConfig{.name = "x"}` in suita de conformanta PAL.

**De ce compilatorul are prioritate:** un initializator explicit pe fiecare
camp al unei structuri de configuratie spune care e valoarea implicita la locul
declaratiei, ceea ce e util oricum; regula clang-tidy castiga doar o linie mai
scurta.

**Limitele deviatiei:** doar acest check. Membrii tot trebuie initializati; ce
se accepta e ca initializarea sa fie scrisa chiar si acolo unde constructorul
implicit ar fi facut acelasi lucru.

---

## DEV-005 — `bugprone-reserved-identifier` in `platform/log/src/format_entry.cpp`

**Regula semnalata:** identificator rezervat implementarii (`__start_...`,
`__stop_...`).

**Unde:** declaratiile celor doua simboluri care delimiteaza sectiunea
`volt_log_formats`.

**De ce se deviaza:** numele nu sunt alese de noi. Linker-ul genereaza exact
`__start_<sectiune>` si `__stop_<sectiune>` pentru orice sectiune al carei nume
e identificator C valid; sunt singurul mod de a citi tabela de formate fara cod
de inregistrare care sa ruleze la pornire. Orice alt nume nu s-ar lega.

**De ce nu exista alternativa mai buna:** varianta fara sectiune ar cere ca
fiecare format sa se inregistreze la initializare, ceea ce inseamna cod care
ruleaza inainte de `main`, ordine de initializare nedefinita intre unitati de
translatie, si un cost la pornire proportional cu numarul de mesaje din
program.

**Limitele deviatiei:** exclusiv aceste doua declaratii. Niciun alt identificator
din proiect nu incepe cu doua underscore-uri.

---

## DEV-006 — `readability-function-size` si `readability-function-cognitive-complexity` in directoarele de teste

**Regulile semnalate:** lungime si complexitate cognitiva peste prag.

**Unde:** dezactivate prin `.clang-tidy` in directoarele de teste.

**De ce se deviaza:** o aserttiune GoogleTest (`ASSERT_TRUE`, `EXPECT_EQ`) se
expandeaza in `if`/`else` si intr-un bloc de streaming a mesajului. Ambele
metrici numara acele ramuri, desi cititorul testului nu le vede: un test cu opt
aserttiuni liniare depaseste pragul fara sa aiba nicio ramura scrisa de mana.
Rezultatul e ca regula recompenseaza scoaterea aserttiunilor, adica exact
opusul a ce trebuie sa faca un test.

**Limitele deviatiei:** doar codul de test. In productie ambele raman active,
si regula 3.11 din AGENTS.md (maxim ~50 de linii, maxim 3 niveluri) se aplica
in continuare — mai multe functii din `platform/log` au fost impartite tocmai
fiindca le depaseau.

---

## DEV-007 — `performance-enum-size` in `platform/trace/include/volt/trace/trace_event.hpp`

**Regula semnalata:** enum-ul foloseste un tip de baza mai larg decat i-ar
trebui pentru valorile pe care le are.

**Unde:** `TraceEvent`, declarat pe `std::uint16_t` desi lista actuala incape pe
opt biti.

**De ce se deviaza:** latimea nu e aleasa dupa valorile de azi, ci dupa formatul
inregistrarii. `TraceRecord` rezerva doi octeti pentru identificator si isi
imparte restul bugetului de saisprezece octeti in jurul lor. Ingustarea
enum-ului ar muta fiecare camp de dupa el, deci ar face ilizibila orice captura
luata inainte — exact ce evita rezervarea.

**De ce nu exista alternativa mai buna:** un enum pe opt biti cu un camp de
umplutura alaturi ar ocupa aceiasi doi octeti si ar ascunde motivul.

**Limitele deviatiei:** exclusiv acest enum. Orice alt enum care nu ajunge pe
disc isi ia latimea minima.
