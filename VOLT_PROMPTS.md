# VOLT — carte de prompturi pentru AI

Fisier tovaras pentru `VOLT_SPEC.md`. Fiecare prompt de aici e o **unitate de lucru completa**: spune ce se implementeaza, ce nu, ce trebuie citit inainte, cu ce se leaga din codul existent, si cum se verifica. Ordinea e cea de dependenta din SPEC §29.

**Se incepe cu PROMPT 0.** El nu scrie cod: creeaza `CLAUDE.md`, fisierul de reguli pe care orice sesiune ulterioara il citeste primul. Regulile de calitate a codului si a comentariilor traiesc acolo, o singura data, si nu se mai repeta in fiecare prompt.

---

## Cuprins

- **PROMPT 0** — genereaza `CLAUDE.md` (regulile stricte)
- **Preambul** — ce lipesti inaintea fiecarui prompt de implementare
- **Sablonul unui prompt** — cum arata si de ce
- **Definitia de gata** · **Prompt de revizuire** · **Prompt de depanare**
- **P00-P68** — trunchiul (T0-T12 + tooling)
- **P69-P86** — ramurile de aprofundare
- **Anexa A** — ordinea recomandata · **Anexa B** — cum o ia razna o sesiune

---

# PROMPT 0 — genereaza `CLAUDE.md`

Se ruleaza **o singura data**, la inceputul proiectului, intr-un repo gol. Nu produce cod. Produce fisierul de reguli.

```
Creeaza in radacina repo-ului un singur fisier, CLAUDE.md, cu exact continutul de mai jos.
Nu adauga nimic, nu scoate nimic, nu reformula, nu "imbunatati". Nu crea alte fisiere.
Nu scrie cod. Raspunsul tau contine doar fisierul CLAUDE.md.

--- INCEPUT CONTINUT CLAUDE.md ---

# CLAUDE.md — reguli obligatorii pentru VOLT

Acest fisier se citeste integral la inceputul fiecarei sesiuni, inainte de orice altceva.
Regulile sunt normative: MUST / NEVER. Nu sunt sugestii si nu se negociaza in interiorul
unei sesiuni de implementare. Daca o regula pare gresita pentru un caz concret, te opresti
si intrebi; nu o incalci si nu o interpretezi larg.

Sursa de adevar pentru CE se construieste este `docs/VOLT_SPEC.md`.
Sursa de adevar pentru CUM se scrie este acest fisier.

---

## 1. Reguli de proces

1.1 MUST: implementezi exact ce cere prompt-ul curent. Nimic in plus.
1.2 MUST: ce observi si nu e cerut se scrie la finalul raspunsului, la "Observatii pentru
    viitor". NEVER implementezi acele lucruri in aceeasi sesiune.
1.3 MUST: daca specificatia e ambigua, incompleta sau contradictorie pentru ce ti se cere,
    pui intrebarea si te opresti. NEVER alegi in locul autorului si NEVER presupui.
1.4 NEVER modifici o interfata definita in SPEC. Daca o consideri gresita, propui modificarea
    SPEC-ului ca pas separat, si astepti confirmarea.
1.5 NEVER refactorizezi cod care nu e in scopul prompt-ului, nici macar pentru a-l "curata".
1.6 NEVER adaugi dependinte externe. Lista permisa e in SPEC §7.2.
1.7 MUST: fiecare raspuns contine fisiere complete, cu calea lor, gata de copiat. NEVER
    fragmente, NEVER "restul ramane la fel", NEVER diff-uri partiale fara context.
1.8 MUST: raspunsul se incheie cu comenzile exacte de build si de test, si cu sectiunile
    "Observatii pentru viitor" si "Intrebari".
1.9 MUST: codul livrat e bine gandit si eficient. Structura minima care rezolva problema,
    fara duplicare, fara cod speculativ, cu cost la rulare justificat pe calea critica.

---

## 2. Interzis fara exceptie

NEVER apar in cod, in niciun modul, sub nicio justificare:

2.1  TODO, FIXME, XXX, HACK, "for now", "temporary", "will be implemented later".
     Cod incomplet nu se comite. Ori e terminat, ori nu exista.
2.2  Stub-uri care intorc valori fictive ca sa compileze.
2.3  Cod comentat. Istoricul e in git.
2.4  `new`, `delete`, `malloc`, `free` scrise direct. Alocarea se face prin alocatoarele
     din `platform/memory`, si doar unde e permisa (regula 5.1).
2.5  `iostream`, `printf`, `std::cout`, `std::endl`. Iesirea se face prin `platform/log`
     sau, in tooling, prin `fmt`.
2.6  `using namespace` in headere. In .cpp, doar `using` punctual pentru un simbol.
2.7  Cast-uri in stil C. Se folosesc `static_cast`, si `reinterpret_cast` doar in codul de
     serializare, cu un comentariu care justifica aliasing-ul.
2.8  `#define` pentru constante sau functii. Se folosesc `constexpr` si functii `inline`.
     Macro-uri permise: doar cele definite in `platform/core` (`VOLT_TRY`, `VOLT_ASSERT`,
     `VOLT_LOG_*`, `VOLT_TRACE`, `VOLT_CHECKPOINT`, `VOLT_LOOP_BOUND`).
2.9  Stare globala mutabila. Singletonul e permis exclusiv pentru registrul de metrici si
     pentru registrul de loguri, ambele deja existente.
2.10 `catch (...)` fara re-aruncare, si orice bloc catch gol.
2.11 Valori de retur ignorate. Orice functie care poate esua e `[[nodiscard]]`.
2.12 `rand`, `srand`, `strcpy`, `strcat`, `sprintf`, `gets`, `atoi`.
2.13 Numere magice. Orice constanta numerica cu semnificatie are nume, unitate si sursa
     (regula 7.4).
2.14 Parametri de tip `bool` in API-uri publice. Se foloseste un enum cu doua valori, ca
     apelul sa fie lizibil la locul apelului.
2.15 Apeluri directe la API-ul sistemului de operare in afara `platform/pal/`.
2.16 Dependinte intre module care incalca ordinea straturilor din SPEC §5. Un strat inferior
     NEVER include un strat superior.

---

## 3. Limbaj si stil

3.1  Standard: C++23. Compilatoare tinta: GCC 14 si Clang 19, ambele obligatoriu curate.
3.2  MUST: totul compileaza fara niciun warning cu setul de flag-uri din `cmake/CompilerWarnings.cmake`.
     Un warning e o eroare, nu o observatie.
3.3  MUST: ID-urile si marimile fizice circula ca tipuri tari (`NodeId`, `Duration`, ...),
     NEVER ca `uint32_t` sau `int` gol.
3.4  MUST: `const` peste tot unde e posibil, inclusiv pe variabile locale.
3.5  MUST: `noexcept` pe orice functie care nu poate arunca. Destructorii sunt intotdeauna
     `noexcept`.
3.6  MUST: clasele care nu sunt gandite pentru derivare sunt `final`. Mostenirea se foloseste
     doar pentru interfete pure.
3.7  MUST: rule of zero. Daca ai nevoie de rule of five, scrii de ce, intr-un comentariu.
3.8  MUST: `auto` doar cand tipul e evident din partea dreapta (`auto x = Foo{}`) sau cand e
     nescriptibil (iteratori, lambda). NEVER `auto` pe valorile de retur ale functiilor din
     API-uri publice.
3.9  MUST: parametri de intrare mari se transmit prin `std::span`, `std::string_view` sau
     referinta const. NEVER copii tacute.
3.10 MUST: maxim 5 parametri per functie. Peste, se grupeaza intr-o structura cu nume.
3.11 MUST: maxim ~50 de linii per functie si maxim 3 niveluri de imbricare. Peste, se extrage.
3.12 MUST: un tip public per fisier header; numele fisierului = numele tipului, in snake_case.
3.13 MUST: ordinea includerilor: headerul propriu, apoi headere din acelasi modul, apoi din
     alte module VOLT, apoi biblioteci externe, apoi standard. Grupuri separate de o linie goala.
3.14 MUST: `#pragma once`.
3.15 MUST: namespace `volt::<modul>`. NEVER simboluri in namespace global.
3.16 MUST: nume in engleza, in cod si in comentarii. Documentatia de proiect poate fi in romana;
     codul nu.
3.17 Conventii: `PascalCase` pentru tipuri, `snake_case` pentru functii si variabile,
     `snake_case_` pentru membri privati, `kPascalCase` pentru constante `constexpr`,
     `SCREAMING_SNAKE` doar pentru macro-urile permise.

---

## 4. Erori

4.1  MUST: erorile asteptate se intorc ca `volt::expected<T>`. NEVER exceptii pentru control de flux.
4.2  Exceptiile sunt permise exclusiv in faza de initializare a aplicatiilor si in tooling.
     NEVER pe data plane. Bibliotecile din `platform/` si `services/` MUST compila si cu
     `-fno-exceptions`.
4.3  MUST: incalcarea unui invariant intern se trateaza cu `VOLT_ASSERT`. In Debug opreste
     executia cu backtrace; in Release ridica un fault intern. NEVER se ignora si NEVER se
     "repara" prin continuare cu valori implicite.
4.4  MUST: erorile de configuratie se detecteaza la validare, inainte de pornire, si au mesaj
     precis: fisier, linie, camp, valoare gasita, valoare asteptata.
4.5  MUST: fiecare eroare are un contor. NEVER o eroare inghitita in tacere.
4.6  NEVER se logheaza si se arunca aceeasi eroare de doua ori pe acelasi nivel.
4.7  MUST: mesajele de eroare descriu ce s-a intamplat si ce se poate face. NEVER "error 5".

---

## 5. Memorie si timp real

5.1  NEVER alocare dinamica pe data plane (`services/`, `safety/`, si calea de transmisie din
     `communication/`) dupa initializare. Se foloseste `volt::no_alloc_scope` ca sa fie impus
     mecanic, nu prin buna intentie.
5.2  NEVER syscall-uri, blocari, sau I/O in bucla de control. Scrierile pe disc trec prin
     `io-offload`.
5.3  NEVER mutex pe calea de 1 ms. Se folosesc structurile lock-free din `platform/memory`.
     Orice mutex atins de un thread RT MUST fi `PTHREAD_PRIO_INHERIT`.
5.4  MUST: fiecare atomic are `memory_order` explicit si un comentariu care il justifica.
     `seq_cst` peste tot inseamna ca nu ai gandit problema.
5.5  MUST: toate cozile, buffere si recursiile sunt marginite. NEVER structuri care cresc
     nelimitat in functie de intrare.
5.6  NEVER recursie pe data plane. NEVER VLA. NEVER `alloca`.
5.7  MUST: fiecare bucla din calea critica are limita cunoscuta, marcata cu `VOLT_LOOP_BOUND`.
5.8  MUST: functiile din bucla de control declara in comentariu bugetul de timp asumat si de
     unde vine (SPEC §38 sau tabelul de task-uri).

---

## 6. Concurenta

6.1  MUST: fiecare structura de date are un proprietar unic si un singur thread care o modifica.
     Proprietarul se scrie in comentariul de la declaratie.
6.2  NEVER stare partajata mutabila intre actori. Actorii comunica exclusiv prin mesaje.
6.3  MUST: orice cod nou care implica doua thread-uri e rulat sub TSan inainte de commit.
6.4  MUST: fiecare thread creat are nume (`pthread_setname_np`), prioritate si afinitate
     stabilite explicit si documentate.
6.5  NEVER `std::thread` direct. Se folosesc abstractiile din `platform/pal`.

---

## 7. Comentarii

Comentariile sunt parte din cod si se recenzeaza la fel de sever.

7.1  MUST: comentariul explica DE CE, NEVER CE. Daca ai nevoie sa explici ce face codul,
     codul e prost scris — repara codul, nu adauga comentariu.
     Interzis: `// increment counter`, `// loop over wheels`, `// return the result`.
7.2  NEVER comentarii-jurnal (`// modified by X on date`), NEVER bannere ASCII decorative,
     NEVER comentarii care repeta semnatura functiei.
7.3  MUST: comentariul care ar deveni fals dupa o modificare rezonabila a codului e un bug.
     Se scrie astfel incat sa ramana adevarat, sau nu se scrie deloc.
7.4  MUST: orice constanta numerica are, la locul definirii, un comentariu cu: unitatea,
     de unde vine valoarea (sectiune din SPEC, standard, masuratoare, calcul), si ce se
     intampla daca se schimba.
     Exemplu corect:
       // SWIM suspect timeout. Derived from the 15 ms detection budget in SPEC 13.1,
       // which itself comes from the 100 ms FTTI of SG-06. Increasing this delays
       // failover; decreasing it raises the false-positive rate measured in tests/dst.
       constexpr Duration kSwimSuspectTimeout = Duration::from_ms(5);
7.5  MUST: fiecare `memory_order` non-relaxed are comentariu cu ce ordonare garanteaza si
     fata de ce alt acces.
7.6  MUST: fiecare deviatie de la o regula de clang-tidy are `// NOLINT(regula) — deviation: DEV-NNN`
     si o intrare corespunzatoare in `docs/DEVIATIONS.md`.
7.7  MUST: codul care implementeaza o cerinta poarta `// @satisfies REQ-XXX-NNN`, iar testul
     care o verifica poarta `// @verifies REQ-XXX-NNN`.
7.8  MUST: codul care implementeaza o actiune modelata formal poarta `// @tla Modul!Actiune`.
7.9  MUST: orice algoritm neevident are in comentariu sursa (standard, articol, sectiune din
     SPEC). Un algoritm fara sursa si fara explicatie nu trece de recenzie.
7.10 MUST: comentariu de documentatie pe fiecare functie publica, in aceasta forma, si doar
     cu campurile care se aplica:
       /// Short imperative sentence describing the effect.
       /// @pre    conditions the caller must guarantee
       /// @post   what holds after a successful call
       /// @thread which thread(s) may call this
       /// @rt     realtime: allocation-free / may block / not for data plane
       /// @errors which ErrorCode values can be returned and when
7.11 MUST: la fiecare presupunere despre durata de viata sau proprietate a unui pointer sau
     span primit ca parametru, comentariul spune cine detine memoria si cat trebuie sa
     traiasca.
7.12 NEVER comentarii la persoana intai, glume, sau observatii despre proces
     (`// this is ugly but`, `// not sure why this works`). Daca nu stii de ce merge,
     nu e gata.
7.13 MUST: unitatile apar in numele identificatorului, nu doar in comentariu:
     `timeout_ms`, `speed_mps`, `pressure_bar`, `period_us`. NEVER `timeout`, `speed`.

---

## 8. Teste

8.1  MUST: fiecare functie publica are teste. Cod fara teste nu e terminat.
8.2  MUST: testul verifica un singur comportament si numele lui il descrie:
     `BrakeGuard_RejectsCommand_WhenLeaseInvalid`.
8.3  MUST: inainte de a considera un test valid, strici intentionat codul si verifici ca
     testul pica. Un test care trece si cu codul stricat nu e test.
8.4  NEVER `sleep` sau asteptari cu durata fixa in teste. Sincronizarea se face pe evenimente
     sau pe ceasul virtual.
8.5  NEVER teste nedeterministe. Orice sursa de aleator primeste seed explicit, iar seed-ul
     se tipareste la esec.
8.6  NEVER logica in teste (if/else, bucle care decid ce se verifica). Cazurile se enumera
     ca date, in tabel.
8.7  MUST: fiecare ramura a codului nou e acoperita, inclusiv caile de eroare.
8.8  MUST: fiecare bug reparat primeste un test de regresie, in `tests/regressions/`,
     numit dupa simptom.
8.9  MUST: un test instabil se repara sau se sterge in aceeasi zi. NEVER se reruleaza pana trece.
8.10 MUST: testele de fault injection asteapta confirmarea injectiei inainte sa verifice
     reactia. Altfel testul poate trece fara sa fi injectat nimic.

---

## 9. Configuratie

9.1  MUST: orice valoare care ar putea fi ajustata sta in configuratie, nu in cod.
9.2  MUST: fiecare parametru de configuratie are tip, interval valid, unitate si valoare
     implicita, verificate la incarcare.
9.3  MUST: un camp necunoscut in fisierul de configuratie e eroare, NEVER ignorat in tacere.
9.4  NEVER comportament diferit intre build-uri in functie de flag-uri nedocumentate.

---

## 10. Git si documentatie

10.1 MUST: un commit = o schimbare logica. Mesaj in format `tip(scop): descriere la imperativ`.
10.2 MUST: commit-urile care ating performanta contin in corp masuratoarea inainte/dupa.
10.3 NEVER merge cu CI rosu. NEVER dezactivarea unui test ca sa treaca pipeline-ul.
10.4 MUST: o schimbare de comportament implica actualizarea `docs/VOLT_SPEC.md` in acelasi PR.
10.5 NEVER se editeaza manual fisierele generate (`docs/TRACEABILITY.md`, `docs/KPI_REPORT.md`,
     cod generat din DBC sau din cataloage). Se modifica sursa si se regenereaza.
10.6 NEVER se scrie in README ceva ce codul nu demonstreaza.

---

## 11. Autoverificare inainte de a raspunde

Inainte sa trimiti raspunsul, verifici, in ordine:

- [ ] Am implementat exact ce s-a cerut si nimic altceva?
- [ ] Exista in diff ceva din lista "Ce NU implementezi" a prompt-ului?
- [ ] Am incalcat vreo regula din sectiunea 2?
- [ ] Fiecare constanta numerica are nume, unitate si sursa?
- [ ] Fiecare comentariu explica de ce, si ramane adevarat dupa o modificare rezonabila?
- [ ] Fiecare functie publica are documentatie in formatul 7.10?
- [ ] Fiecare atomic are memory order justificat?
- [ ] Exista alocare, blocare sau syscall pe o cale de data plane?
- [ ] Fiecare test pica daca stric intentionat codul?
- [ ] Am lasat vreun TODO, stub sau cod comentat?

Daca vreun raspuns e gresit, corectezi inainte sa trimiti. Nu trimiti cu observatia
"as putea imbunatati mai tarziu".

--- SFARSIT CONTINUT CLAUDE.md ---
```

**Verificare PROMPT 0:** exista `CLAUDE.md` in radacina, contine toate cele 11 sectiuni, si nu s-a creat niciun alt fisier.

**Cand se modifica `CLAUDE.md`:** doar cand descoperi o regula noua din experienta (ex: un tip de bug care s-a repetat). Se adauga cu numar nou, la sectiunea potrivita, si se noteaza in `docs/devlog/`. NEVER se relaxeaza o regula ca sa treaca un prompt.

---

# Preambul (se lipeste inaintea fiecarui prompt de implementare)

```
Proiect: VOLT — platforma automotive distribuita de compute in C++23.

Inainte de orice, citeste integral CLAUDE.md din radacina repo-ului. Regulile de acolo sunt
obligatorii si au prioritate fata de obiceiurile tale de scriere a codului. Specificatia
functionala e in docs/VOLT_SPEC.md si e sursa de adevar pentru ce se construieste.

Confirma in primul rand al raspunsului ca ai citit CLAUDE.md, apoi executa prompt-ul de mai jos.
La final, parcurge lista de autoverificare din CLAUDE.md sectiunea 11 si raporteaza rezultatul
fiecarui punct pe o linie.
```

Atat. Regulile nu se mai repeta: daca le lipesti de fiecare data, se dilueaza si incepe sa
le ignore selectiv. Un fisier, citit de fiecare data, la care te intorci cand ceva iese prost.

---

# Sablonul unui prompt

Fiecare prompt din acest document are aceleasi sectiuni, in aceeasi ordine, si fiecare exista
dintr-un motiv:

| Sectiune | De ce exista |
|---|---|
| **Citeste inainte** | sectiunile exacte din SPEC + fisierele de cod de care depinde. Fara ele, AI-ul inventeaza interfete. |
| **Se leaga de** | ce prompturi anterioare trebuie sa fie deja terminate. |
| **Implementezi** | lista concreta de fisiere si functionalitati. |
| **NU implementezi** | cea mai importanta sectiune. Fara ea, un prompt se transforma in trei. |
| **Verificare** | testele si comenzile exacte care dovedesc ca merge. Nu "testeaza bine". |
| **Done cand** | criteriul binar de terminare. |
| **Capcane** | greseala pe care o va face daca nu i-o spui. |
| **Commit** | mesajul, ca sa ai istoric coerent. |

Daca scrii prompturi noi, respecta sablonul. Un prompt fara "NU implementezi" nu e prompt,
e o dorinta.

---

# Definitia de gata (valabila pentru orice prompt)

```
[ ] cmake --preset dev && cmake --build --preset dev      → 0 warning-uri
[ ] ctest --preset dev                                     → toate testele trec
[ ] cmake --build --preset asan && ctest --preset asan     → curat
[ ] cmake --build --preset tsan && ctest --preset tsan     → curat (daca atinge concurenta)
[ ] clang-tidy pe fisierele noi                            → 0 finding-uri sau deviatie DEV-NNN
[ ] clang-format --dry-run --Werror                        → curat
[ ] fiecare test nou pica daca strici intentionat codul
[ ] lista de autoverificare din CLAUDE.md §11              → toate bifate
[ ] nimic din "Ce NU implementezi" nu apare in diff
```

Ultimele doua randuri sunt cele mai des incalcate. Le verifici manual, cu diff-ul in fata.

---

# Prompt de revizuire (dupa fiecare prompt de implementare)

```
Ai in fata CLAUDE.md, sectiunile relevante din docs/VOLT_SPEC.md si diff-ul de mai jos.
Nu implementa nimic si nu propune rescrieri. Raspunde in liste scurte:

1. Ce din diff NU era cerut in prompt?
2. Ce din prompt lipseste din diff?
3. Ce regula din CLAUDE.md e incalcata? Citeaza numarul regulii si linia din cod.
4. Unde se abate codul de la SPEC? Citeaza sectiunea.
5. Ce comentariu explica CE in loc de DE CE, sau ar deveni fals dupa o modificare rezonabila?
6. Ce constanta numerica nu are unitate si sursa?
7. Ce ramura de cod nu are test?
8. Numeste 3 moduri in care codul asta se strica in productie si spune daca sunt tratate.
```

---

# Prompt de depanare

```
Simptom: <ce vezi, exact>
Ce am incercat: <ce ai incercat>
Context: docs/VOLT_SPEC.md §<sectiuni>, fisierele <lista>

Nu propune rescrieri si nu ghici. Procedeaza asa:
1. Enumera 5 ipoteze, ordonate dupa probabilitate.
2. Pentru fiecare, spune EXACT ce comanda sau ce log o confirma sau o infirma.
3. Opreste-te si asteapta rezultatele de la mine.
```

---

# FAZA T0 — Bootstrap

## P00 — Schelet de repo si sistem de build

- [x] Done

**Citeste inainte:** `CLAUDE.md`, SPEC §6 (structura), §7.1-7.2 (build, dependinte), §7.3 (standard de cod).
**Se leaga de:** PROMPT 0 (`CLAUDE.md` exista deja in radacina; nu il modifica).

**Implementezi:**
- Arborele de directoare complet din SPEC §6, cu `.gitkeep` in cele goale.
- `CMakeLists.txt` radacina: C++23, targeturi exportate ca `volt::<modul>`, optiuni `VOLT_BUILD_TESTS`, `VOLT_BUILD_TOOLS`, `VOLT_ENABLE_TRACING`, `VOLT_SANITIZER=none|asan|ubsan|tsan`.
- `CMakePresets.json` cu preseturile: `dev`, `release`, `rt`, `asan`, `ubsan`, `tsan`, `coverage`, `sim`.
- `cmake/` cu: `CompilerWarnings.cmake` (lista exacta de flag-uri din SPEC §7.1), `Sanitizers.cmake`, `VoltAddLibrary.cmake` (functie helper care creeaza un modul cu conventiile proiectului), `VoltAddTest.cmake`.
- `.clang-format`, `.clang-tidy` (setul din SPEC §7.3), `.gitignore`, `.editorconfig`.
- Un modul minim `platform/core` cu un singur header si un test, ca sa dovedesti ca lantul functioneaza cap-coada.
- **Impunerea mecanica a regulilor din `CLAUDE.md` care se pot verifica automat**: `.clang-tidy` cu regulile pentru §2 si §3 (interzicerea cast-urilor C, a `using namespace` in headere, a functiilor prea lungi, a numarului de parametri, a adancimii de imbricare, `[[nodiscard]]`, `readability-magic-numbers`), plus `ci/check_banned_patterns.sh` care cauta in tot repo-ul: `TODO`, `FIXME`, `XXX`, `HACK`, `std::cout`, `printf`, `new `, `delete `, `#define` in afara listei permise, si cod comentat (linii care incep cu `//` si contin `;` sau `{`).
- `README.md` cu titlul, o fraza si instructiuni de build. Nimic promis in plus.

**NU implementezi:** niciun modul functional, niciun CI (vine la P01), niciun cod de aplicatie, niciun `find_package` pentru dependinte pe care inca nu le folosim.

**Verificare:**
```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
./ci/check_banned_patterns.sh
```
Toate trei verzi, zero warning-uri. Apoi adauga intentionat un `// TODO` si un `printf` intr-un fisier si verifica faptul ca scriptul le prinde pe amandoua.

**Done cand:** un om care cloneaza repo-ul poate rula comenzile de mai sus fara sa citeasca nimic altceva, iar regulile mecanizabile din `CLAUDE.md` pica build-ul cand sunt incalcate.

**Capcane:** nu pune `-Werror` doar pe Release; e nevoie peste tot. Nu folosi `include_directories()` global — doar `target_include_directories(... PUBLIC ...)`.

**Commit:** `build: repository skeleton, CMake presets and toolchain configuration`

---

## P01 — CI cu toate gate-urile

- [x] Done

**Citeste inainte:** SPEC §24 (CI/CD), §23 (niveluri de testare), §0.2 (KPI).
**Se leaga de:** P00.

**Implementezi:** `.github/workflows/ci.yml` cu job-urile din SPEC §24, plus rularea lui `ci/check_banned_patterns.sh` ca prim pas (e cel mai rapid si prinde cele mai multe abateri), dar **doar pasii care au sens acum**: format, tidy, build matrix (gcc-14 × clang-19 × Debug/RelWithDebInfo), unit tests, build+run ASan, UBSan, TSan, coverage cu gate (pornit la 0% si crescut pe masura ce apar module — vezi mai jos).
- `coverage` gate configurabil dintr-un fisier `ci/coverage_gate.txt`, ca sa il urci progresiv fara sa editezi workflow-ul.
- Cache pentru build.
- Job-uri separate, ca sa vezi imediat ce a picat.
- `ci/check_forbidden_symbols.sh` — momentan doar scheletul care nu gaseste nimic (lista se umple la P12).

**NU implementezi:** nightly, fuzzing, DST, perf, HIL (vin cand exista ce sa ruleze). Nu adauga runner self-hosted.

**Verificare:** push pe un branch de test → toate job-urile verzi. Introdu intentionat un warning si verifica ca CI-ul pica; apoi scoate-l.

**Done cand:** CI-ul pica pentru fiecare din: format gresit, warning de compilare, test picat, finding de sanitizer, tipar interzis din `CLAUDE.md` §2.

**Commit:** `ci: build matrix, sanitizers, formatting and coverage gates`

---

## P02 — Tipuri de baza si model de erori

- [x] Done

**Citeste inainte:** SPEC §42.1 (taxonomie de erori), §8.2 (timp), §4 (D1).
**Se leaga de:** P00.

**Implementezi in `platform/core/`:**
- `types.hpp`: `NodeId`, `ServiceId`, `InstanceId`, `TaskId`, `TopicId`, `Epoch`, `Priority` — toate tipuri tari (strong typedef), nu alias-uri de `uint32_t`. Comparabile, hash-abile, `constexpr`.
- `time.hpp`: `Duration` si `Timestamp` conform SPEC §8.2 (nanosecunde, `operator<=>`, aritmetica sigura, fara conversii implicite).
- `error.hpp`: `enum class ErrorCode` cu categoriile din SPEC §42.1 si valori grupate pe intervale (`0x1xxx` config, `0x2xxx` resursa, `0x3xxx` tranzitoriu, ...); `volt::expected<T>` = alias peste `std::expected<T, ErrorCode>`; macro-uri `VOLT_TRY(expr)` (propagare) si `VOLT_ASSERT(cond, msg)`.
- `hash.hpp`: xxhash64 `constexpr`, folosit pentru `state_hash` si pentru ID-uri de format de log.
- `endian.hpp`: conversii explicite, cu teste pe ambele ordini.
- `span_utils.hpp`: helperi de citire/scriere sigura in `std::span<std::byte>` cu verificare de lungime, care intorc `expected`.

**NU implementezi:** logging, alocatoare, nimic care depinde de OS.

**Verificare:** teste unitare pentru fiecare tip, inclusiv: overflow la aritmetica de timp, conversii refuzate la compilare (test cu `static_assert` si cu `requires`), round-trip endian pe 10.000 de valori aleatorii, `VOLT_TRY` care propaga corect prin 3 niveluri.

**Done cand:** `git grep -n "uint32_t node_id"` nu gaseste nimic — ID-urile circula doar ca tipuri tari.

**Capcane:** nu face `Timestamp` un alias de `std::chrono::time_point` — ai nevoie de control asupra reprezentarii pentru serializare si pentru ceasul global.

**Commit:** `core: strong types, time primitives and expected-based error model`

---

# FAZA T1 — Platform

## P03 — PAL: interfata si backend POSIX

- [x] Done

**Citeste inainte:** SPEC §5 (straturi), §8.1 (proces/thread), §27.1 (portabilitate), §42.2 (threading).
**Se leaga de:** P02.

**Implementezi in `platform/pal/`:**
- `include/volt/pal/`: interfetele pure `IClock`, `IThread`, `IProcess`, `ISharedMemory`, `ISocket`, `IFile`, `ITimer`, `IWatchdogDevice`. Toate metodele intorc `expected`, niciuna nu arunca.
- `posix/`: implementarea completa pentru Linux: `clock_gettime`, `pthread_create` cu atribute (politica, prioritate, afinitate, dimensiune de stiva), `fork/exec`, `shm_open`/`mmap`, socketuri, `timerfd`, `/dev/watchdog`.
- Setari RT: `mlockall`, `sched_setscheduler`, `pthread_setaffinity_np`, `PTHREAD_PRIO_INHERIT` pe mutexurile expuse.
- `tests/pal_conformance/`: **o singura suita de teste** care se ruleaza contra oricarui backend (parametrizata pe implementare). Asta e livrabilul cel mai important — ea va valida ulterior si backendul sim, si cel QNX.

**NU implementezi:** backend sim (P04), backend QNX, CAN, nimic din stiva de comunicatie, nimic care are logica de aplicatie.

**Verificare:**
```bash
ctest --preset dev -R pal_conformance
```
Plus un test care verifica explicit ca setarea prioritatii RT esueaza *elegant* (cu `ErrorCode`, nu cu crash) cand nu ai drepturi.

**Done cand:** suita de conformanta are minim 40 de teste si trece; nicio functie POSIX nu e apelata din afara lui `platform/pal/posix/` (verificat cu `git grep`).

**Commit:** `pal: platform abstraction interfaces and POSIX backend with conformance suite`

---

## P04 — PAL: backend de simulare

- [x] Done

**Citeste inainte:** SPEC §4 (D1), §21.1 (DST), §8.2.
**Se leaga de:** P03 (aceeasi suita de conformanta trebuie sa treaca).

**Implementezi in `platform/pal/sim/`:** implementarea deterministaa a acelorasi interfete: ceas virtual care avanseaza doar la comanda, timere ordonate intr-o coada de evenimente, "thread-uri" cooperative rulate de un planificator determinist, shared memory ca buffere in proces, socketuri ca cozi in memorie cu model de latenta/pierdere/reordonare parametrizat de un RNG cu seed.

**NU implementezi:** motorul DST (P62), injectia de fault (P26), invarianti. Aici doar substratul.

**Verificare:** aceeasi `pal_conformance` trece si pe backendul sim. Plus: doua rulari cu acelasi seed produc exact aceeasi secventa de evenimente (compara un log de evenimente hash-uit).

**Done cand:** `ctest -R pal_conformance` trece pe ambele backend-uri, si testul de determinism trece de 100 de ori la rand.

**Capcane:** ceasul virtual nu are voie sa consulte niciodata ceasul real, nici macar pentru "seed". Seed-ul e parametru explicit.

**Commit:** `pal: deterministic simulation backend passing the shared conformance suite`

---

## P05 — Logging lock-free

- [x] Done

**Citeste inainte:** SPEC §8.4 (logging), §42.2 (io-offload), §42.3 (rotatie).
**Se leaga de:** P02, P03.

**Implementezi in `platform/log/`:**
- Ring MPSC per-thread, inregistrari binare: `{format_id, timestamp, level, module, args...}`; `format_id` calculat `consteval` din sirul de format, tabela exportata intr-o sectiune dedicata a binarului.
- API: `VOLT_LOG_INFO("wheel {} slip {}", idx, slip)` — fara alocari, fara formatare in calea apelantului.
- Thread de drenaj in `io-offload`, cu rotatie pe dimensiune (100 MB × 5) si politica de disc plin din SPEC §42.3.
- Filtrare pe modul si nivel, schimbabila la runtime.
- `tools/volt-logdec`: decodorul care transforma logul binar in text folosind tabela de formate din binar.

**NU implementezi:** tracing (P06), metrici (P08), transport pe retea al logurilor.

**Verificare:**
- Test de concurenta: 8 thread-uri × 1M mesaje, zero pierderi neanuntate, zero data race sub TSan.
- Benchmark: cost pe calea apelantului < 100 ns (raporteaza P50/P99).
- Test de disc plin (`tmpfs` mic): sistemul continua, logarea se opreste, apare contorul.
- Round-trip: 10.000 de mesaje cu tipuri variate → decodate identic.

**Done cand:** benchmark-ul e in `docs/PERFORMANCE.md` si TSan e curat.

**Capcane:** nu folosi `std::string` in inregistrare. Argumentele de tip sir se logheaza ca ID sau ca `string_view` catre memorie cu durata de viata garantata (literal).

**Commit:** `log: lock-free binary logging with offline decoder`

---

## P06 — Tracing si export Perfetto

- [x] Done

**Citeste inainte:** SPEC §8.4 (tracing), §22.3, §0.2 (K12).
**Se leaga de:** P05.

**Implementezi in `platform/trace/`:**
- Ring per-CPU, eveniment de 16 octeti, timestamp din `rdtsc` calibrat pe ceasul PAL.
- Enum de evenimente din SPEC §8.4, extensibil, cu ID-uri stabile (nu reordonabile).
- `VOLT_TRACE(event, arg)` si `TraceScope` (RAII pentru intervale).
- `volt-trace export --perfetto` → fisier deschis corect in `ui.perfetto.dev`, cu procese, thread-uri si `flow events` intre mesaje.

**NU implementezi:** corelare intre noduri (vine cu gPTP la P42) — dar lasa campul de `node_id` in format, ca sa nu schimbi formatul mai tarziu.

**Verificare:**
- K12: benchmark A/B cu tracing pornit/oprit → overhead < 2%.
- Fisierul exportat validat de un parser de referinta (script Python care il incarca si verifica structura).
- Test: 4 thread-uri emit 10M evenimente, niciunul pierdut fara sa fie contorizat.

**Done cand:** ai un screenshot din Perfetto in `docs/` cu un profil real de la testele tale.

**Commit:** `trace: per-CPU event tracing with Perfetto export`

---

## P07 — Configuratie: schema, validare, incarcare

- [x] Done

**Citeste inainte:** SPEC §8.5, §34 anexa B (exemplu complet), §39.3 (config nepotrivita intre noduri).
**Se leaga de:** P02.

**Implementezi in `platform/config/`:**
- Parser YAML (yaml-cpp) + **validare de schema declarativa**: tip, interval, camp obligatoriu, dependinte intre campuri, valori enum.
- Erorile de config sunt precise: fisier, linie, camp, valoare gasita, valoare asteptata. Un mesaj de eroare vag e un bug.
- Structuri C++ tipizate pentru `cluster.yaml`, `node_X.yaml`, generate manual (nu reflectie).
- `config_hash()` — hash stabil peste configuratia efectiva, folosit la P55 pentru admiterea in cluster.
- Hot-reload doar pentru campurile marcate `calibratable: true`, cu callback `on_config_change`.

**NU implementezi:** distribuirea configuratiei prin Raft (P56), UDS WriteDataByIdentifier (P46).

**Verificare:** 25 de fisiere de config invalide in `tests/data/bad_configs/`, fiecare cu mesajul de eroare asteptat verificat exact. Plus: configuratia exemplu din SPEC anexa B se incarca fara eroare.

**Done cand:** niciun camp din exemplul din SPEC nu e ignorat in tacere (test care compara setul de campuri consumate cu setul de campuri din fisier).

**Capcane:** un camp scris gresit trebuie sa fie **eroare**, nu ignorat. Asta prinde 90% din problemele de configurare.

**Commit:** `config: schema-validated YAML configuration with precise diagnostics`

---

## P08 — Memorie: pool-uri, arene, cozi

- [x] Done

**Citeste inainte:** SPEC §8.3, §10.1-10.2, §0.2 (K10).
**Se leaga de:** P02, P03.

**Implementezi in `platform/memory/`:**
- `FixedPool<T, N>`: free-list, O(1), indexata (utilizabila in shared memory), thread-safe pe varianta atomica si non-atomica.
- `Arena`: bump allocator cu resetare, aliniere configurabila, `FrameScope` RAII.
- `BoundedQueue<T, N>`: SPSC lock-free cu padding la linie de cache; varianta MPSC separata.
- `SeqLock<T>` pentru date de tip "ultima valoare valida" cu cititori multipli.
- Toate cu `static_assert` pe trivially copyable acolo unde e necesar.

**NU implementezi:** transportul shm (P13), `no_alloc_scope` (P09).

**Verificare:**
- Property-based (rapidcheck): pentru orice secventa de alocari/eliberari, pool-ul nu pierde si nu dubleaza sloturi.
- TSan pe SPSC/MPSC cu 10M operatii.
- Un model de verificare a cozii: ruleaza `BoundedQueue` sub un test care compara cu o coada de referinta protejata cu mutex, pe 10M operatii intercalate aleator.
- Benchmark: throughput si latenta, in `docs/PERFORMANCE.md`.

**Done cand:** cozile trec TSan si testul de model, iar benchmark-ul e publicat.

**Capcane:** memory order-ul se scrie explicit si se justifica intr-un comentariu la fiecare atomic. `seq_cst` peste tot inseamna ca nu ai gandit.

**Commit:** `memory: fixed pools, arenas and lock-free bounded queues`

---

## P09 — no_alloc_scope si urmarirea alocarilor

- [x] Done

**Citeste inainte:** SPEC §8.3 punctele 5-6, §0.2 (K10).
**Se leaga de:** P08.

**Implementezi:**
- Override global de `operator new/delete` care incrementeaza contoare per-thread si consulta un flag thread-local.
- `volt::no_alloc_scope`: RAII; in Debug `abort()` cu backtrace la prima alocare; in Release contorizeaza, emite `TRACE(ALLOC_VIOLATION)` si ridica un fault intern.
- `AllocationTracker`: count, bytes, peak, high-water mark per thread, expus ca metrica.

**NU implementezi:** fault manager (P53) — deocamdata doar contor si trace.

**Verificare:** test care aloca intentionat intr-un `no_alloc_scope` si verifica detectia in ambele moduri de build; test care ruleaza o bucla realista 60 s si raporteaza 0 alocari.

**Done cand:** K10 poate fi masurat pe orice bucata de cod cu o singura linie.

**Commit:** `memory: allocation guard and per-thread allocation tracking`

---

## P10 — Metrici si endpoint Prometheus

- [x] Done

**Citeste inainte:** SPEC §22.4.
**Se leaga de:** P05, P06.

**Implementezi in `platform/trace/metrics/`:** `Counter`, `Gauge`, `Histogram` (HDR, cu percentile corecte), registru global cu nume si etichete, export text si in format Prometheus pe un port HTTP configurabil, in threadul de control plane.

**NU implementezi:** dashboard (P71), colectare intre noduri (P21).

**Verificare:** percentile comparate cu un calcul de referinta pe 1M de esantioane (eroare < 1%); formatul Prometheus validat cu un parser de referinta; test ca inregistrarea unei metrici din data plane nu aloca (foloseste `no_alloc_scope`).

**Commit:** `metrics: counters, gauges, HDR histograms and Prometheus exposition`

---

# FAZA T2 — Model de actori si IPC

## P11 — IActor, Environment, mailbox, dispatcher

- [x] Done

**Citeste inainte:** SPEC §4 (D1 — cel mai important prompt din proiect), §12.1, §42.2.
**Se leaga de:** P02-P10.

**Implementezi in `platform/actor/`:**
- `Environment` si `IActor` exact cu semnaturile din SPEC §4 (nu le modifica; daca ceva lipseste, intreaba).
- `Mailbox`: coada bounded de mesaje, cu politica de plin configurabila (DROP_OLDEST / DROP_NEW / FAULT).
- `Dispatcher`: bucla single-thread care ia mesaje si timere si le livreaza actorului, in ordine determinista (mesajele inainte de timere la acelasi timestamp, si documenteaza de ce).
- `RealEnvironment` peste PAL POSIX; `SimEnvironment` peste PAL sim (doar substratul, motorul DST vine la P62).
- `StateWriter`/`StateReader` pentru `serialize`/`deserialize`, cu format binar versionat.
- `ci/forbidden_symbols.txt` + activarea verificarii din P01: `clock_gettime`, `gettimeofday`, `malloc`, `free`, `socket`, `rand`, `printf`, `std::cout` interzise in `services/` si `safety/`.

**NU implementezi:** niciun serviciu concret, transportul shm (P12), rutarea intre noduri.

**Verificare:**
- Un actor de test (`EchoActor`) rulat identic peste `RealEnvironment` si `SimEnvironment`, cu aceleasi intrari → acelasi `state_hash()` la fiecare pas.
- Test: mailbox plin → politica aplicata si contorizata.
- CI-ul pica daca adaugi `printf` intr-un fisier din `services/`.
- Property: `deserialize(serialize(x)) == x` si hash identic, pentru 10.000 de stari generate.

**Done cand:** regula D1 e impusa mecanic, nu prin disciplina.

**Capcane:** `Environment` nu are voie sa expuna nimic care sa permita accesul direct la OS. Daca un actor are nevoie de ceva nou, se adauga o metoda in `Environment`, nu o portita.

**Commit:** `actor: deterministic actor runtime with injected environment`

---

## P12 — Transport zero-copy in memorie partajata

**Citeste inainte:** SPEC §10.1-10.2, §8.3, §0.2 (K3).
**Se leaga de:** P08, P11.

**Implementezi in `platform/ipc/`:**
- Segment shm cu antet (magic, versiune, layout), pool-uri de mesaje per tip, ring-uri SPSC per pereche producator-consumator.
- `Publisher<T>::loan()` → `Loan<T>` (RAII, mutabil), `publish(std::move(loan))`; `Subscriber<T>` primeste `Sample<T>` read-only cu refcount atomic.
- Politici QoS din SPEC §12.2 la nivel de transport: `KEEP_LAST(n)`, `DROP_OLDEST`, detectie de consumator lent cu contor.
- Backend alternativ: Unix domain sockets pentru control plane, cu `SO_PEERCRED`.
- Recuperare: daca un proces moare tinand un `Loan`, buffer-ul se recupereaza (contor de proprietar + curatare la deschidere).

**NU implementezi:** serializare pe fir (P28/P39), transport intre noduri, discovery.

**Verificare:**
- K3: benchmark ping-pong, tabelul complet din SPEC §10.1 (shm ring, seqlock, UDS, mq, TCP, UDP) publicat in `docs/PERFORMANCE.md`.
- Test de crash: `kill -9` unui consumator care tine 100 de sample-uri → producatorul isi revine, fara scurgeri.
- Test cu 1 producator la 1 kHz si 1 consumator care doarme 50 ms → producatorul nu se blocheaza niciodata (masoara jitterul lui).
- Zero copii pe calea intra-nod, demonstrat prin contorizarea `memcpy` intr-un build instrumentat.

**Commit:** `ipc: zero-copy shared-memory transport with loan/sample semantics`

---

# FAZA T3 — Runtime real-time

## P13 — Scheduler: clasa rate-monotonic si monitorizarea deadline-urilor

**Citeste inainte:** SPEC §9.1, §9.2 (RM), §9.4, §0.2 (K1, K2).
**Se leaga de:** P03, P06, P11.

**Implementezi in `platform/sched/`:**
- `TaskSpec` exact ca in SPEC §9.1.
- Planificator RM peste `SCHED_FIFO`, cu maparea prioritatilor documentata.
- Activare periodica precisa (`timerfd` sau `clock_nanosleep` cu `TIMER_ABSTIME` — alege si justifica), fara deriva cumulativa.
- Instrumentare: jitter de activare, timp de raspuns, timp de executie (`CLOCK_THREAD_CPUTIME_ID`), deadline miss, overrun fata de bugetul WCET, toate in HDR histograms si ca evenimente de trace.
- Politici de overrun din SPEC §9.5.

**NU implementezi:** clasa time-triggered si EDF (P14), analiza RTA (P15), watchdog (P16).

**Verificare:**
- K1/K2 pe masina de dezvoltare, plus aceleasi masuratori dupa aplicarea setarilor RT din SPEC §25 (raporteaza ambele; diferenta e o poveste buna).
- Test: un task care depaseste bugetul → politica corecta aplicata si evenimentul emis.
- Test de deriva: 1 ora la 1 kHz → numarul de activari e exact 3.600.000 ± 0.

**Capcane:** somnul relativ acumuleaza eroare. Foloseste timp absolut si calculeaza urmatoarea activare din origine, nu din "acum".

**Commit:** `sched: rate-monotonic scheduler with deadline and jitter instrumentation`

---

## P14 — Scheduler: clasa time-triggered si EDF

**Citeste inainte:** SPEC §9.2, §38 (lantul de timp), §8.2.
**Se leaga de:** P13.

**Implementezi:**
- Tabela TT: sloturi cu offset in cadrul hiperperioadei, executata aliniat la o origine de timp comuna (deocamdata ceasul local; gPTP se conecteaza la P42 fara sa schimbi interfata).
- `tools/tt_schedule_gen`: generator greedy + backtracking din constrangeri (perioade, precedente, exclusivitati), plus un **checker independent** care valideaza orice tabela produsa.
- Clasa EDF peste `SCHED_DEADLINE`, cu documentarea limitarilor kernelului.
- Comparatie experimentala RM vs EDF vs TT pe acelasi set de task-uri, in `docs/PERFORMANCE.md`.

**NU implementezi:** sinteza cu Z3 (ramura R-TIME, P73), sincronizare intre noduri.

**Verificare:** checker-ul respinge cel putin 5 tabele invalide construite manual; masuratori de jitter pentru fiecare din cele trei clase; testul de aliniere a fazelor din SPEC §38 (rezultatul lui SensorFusion e gata cu ~200 µs inainte de activarea lui BrakeControl).

**Commit:** `sched: time-triggered and EDF scheduling classes with schedule generator`

---

## P15 — Analiza de schedulabilitate (RTA)

**Citeste inainte:** SPEC §9.3.
**Se leaga de:** P13.

**Implementezi:** `platform/sched/rta/` — analiza de timp de raspuns cu iteratia din SPEC §9.3, cu factor de blocare si jitter de release; CLI `volt-sched analyze <config>` care produce exact tabelul din SPEC §9.3, inclusiv verdictul si marginea.

**NU implementezi:** admisia la runtime (o adaugi la P20, in lifecycle), analiza de cache sau de microarhitectura.

**Verificare:** 10 seturi de task-uri cu raspuns calculat manual (pune calculul in comentariu) — rezultatele trebuie sa coincida; un set peste bound-ul Liu&Layland dar schedulabil prin RTA, cu explicatia in `docs/PERFORMANCE.md`.

**Commit:** `sched: response-time schedulability analysis and CLI`

---

## P16 — Watchdog pe trei niveluri

**Citeste inainte:** SPEC §8.6.
**Se leaga de:** P13, P06.

**Implementezi in `platform/watchdog/`:**
- Alive supervision cu fereastra `[min, max]` (prea des e la fel de suspect ca prea rar).
- Deadline supervision (consuma evenimentele scheduler-ului).
- **Program flow monitoring**: `VOLT_CHECKPOINT(id)` verificat contra unui graf de flux permis, generat dintr-un fisier de configurare per serviciu.
- Node watchdog: kick la `/dev/watchdog` (softdog in dev).
- Escaladarea exacta din SPEC §8.6.

**NU implementezi:** reactia de degradare (P54), migrarea serviciilor (P60).

**Verificare:** pentru fiecare nivel, un test care injecteaza defectul (task care raporteaza prea des, prea rar, deloc; sarirea unui checkpoint) si verifica escaladarea si timpul de reactie.

**Commit:** `watchdog: alive, deadline and program-flow supervision with escalation`

---

## P17 — Lifecycle, graf de dependinte, masina de boot

**Citeste inainte:** SPEC §39 (integral), §8.1.
**Se leaga de:** P07, P13, P16.

**Implementezi in `platform/lifecycle/`:**
- Masina de stari de boot din SPEC §39.1, cu timeout si actiune de esec per tranzitie.
- Rezolvator de dependinte: sortare topologica, pornire in paralel unde se poate, detectia ciclurilor **la validarea configuratiei**.
- Regulile de pornire incompleta din SPEC §39.3, toate cinci, inclusiv `SINGLE_NODE_DEGRADED` si comportamentul fara ceas sincronizat.
- Oprire controlata din SPEC §39.4.

**NU implementezi:** cluster join real (P55), lease-uri (P57) — deocamdata puncte de extensie clar marcate cu interfete, nu stub-uri care mint.

**Verificare:** cele 20 de scenarii de pornire din SPEC §39; K11 masurat; test ca `kill -9` produce aceeasi stare de siguranta ca `SIGTERM`.

**Capcane:** "porneste pe jumatate" e cel mai periculos rezultat. Testul care conteaza: niciun actuator comandat inainte ca tot lantul de conditii sa fie indeplinit.

**Commit:** `lifecycle: boot state machine, dependency graph and degraded startup policies`

---

## P18 — Health monitoring si raportare de resurse

**Citeste inainte:** SPEC §22 (observabilitate), §15.5, §12.1 (campul health).
**Se leaga de:** P10, P17.

**Implementezi:** colectare per proces si per nod (CPU din `/proc`, memorie RSS si peak, adancimea cozilor, latente, stare de deadline), agregare intr-un `HealthReport` publicat periodic, si un `HealthService` care il expune.

**NU implementezi:** detectia de cadere de nod (P55) — health e local, detectia e alta problema.

**Verificare:** valorile raportate comparate cu `top`/`/proc` in test (toleranta declarata); test ca sampling-ul nu misca K1.

**Commit:** `health: per-node and per-service resource sampling and reporting`

---

## P19 — Reglaj RT si baseline de latenta

**Citeste inainte:** SPEC §25 (metodologie).
**Se leaga de:** P13.

**Implementezi:** `tools/rt_setup.sh` (isolcpus, nohz_full, IRQ affinity, guvernator, verificari de precondtii cu mesaje clare), `tools/kpi/cyclictest_baseline.sh`, si sectiunea de metodologie in `docs/PERFORMANCE.md` completata cu rezultatele de pe masina ta.

**NU implementezi:** raportul KPI complet (P72).

**Verificare:** scriptul refuza sa ruleze daca kernelul nu are PREEMPT_RT si spune exact ce lipseste; baseline-ul e publicat inainte de orice alta masuratoare.

**Commit:** `tools: real-time system tuning and latency baseline measurement`

---

# FAZA T4 — Simulare de vehicul si injectie de defecte

## P20 — Model de dinamica a vehiculului

**Citeste inainte:** SPEC §19.1, §37.2.
**Se leaga de:** P02, P11.

**Implementezi in `simulation/vehicle/`:** modelul din SPEC §19.1 — bicycle 3-DOF, dinamica de roata individuala, Pacejka simplificata, transfer de sarcina, integrare RK4 cu pas fix de 1 ms, `mu` configurabil per roata. Complet determinist, fara dependenta de wall-clock, cu suport de rulare accelerata.

**NU implementezi:** senzori (P21), scenarii (P22), grafica.

**Verificare:** cele trei validari din SPEC §19: conservarea energiei fara frecare (< 0,1% pe 10 s), distanta de oprire vs `v²/(2μg)` (< 5%), raspuns la treapta de volan comparat cu valori din literatura (citeaza sursa in `docs/SIMULATION.md`). Plus: doua rulari cu acelasi input dau rezultate bit-identice.

**Commit:** `sim: vehicle dynamics model with tire and load transfer`

---

## P21 — Modele de senzori cu moduri de defect

**Citeste inainte:** SPEC §19.2, §37.1.
**Se leaga de:** P20.

**Implementezi:** pipeline-ul `real → cuantizare → zgomot → bias/drift → latenta → rata → mod de defect`, cu toate cele 7 moduri din SPEC §19.2, fiecare parametrizabil si activabil la runtime.

**NU implementezi:** detectia lor (P52) — aici doar producerea.

**Verificare:** pentru fiecare mod de defect, un test care verifica statistic ca semnalul produs are proprietatea asteptata (ex: `FROZEN_PLAUSIBLE` ramane in interval si nu declanseaza range check).

**Commit:** `sim: sensor models with configurable fault modes`

---

## P22 — Motor de scenarii cu asertiuni

**Citeste inainte:** SPEC §19.3.
**Se leaga de:** P20, P21, P07.

**Implementezi:** parser si executor pentru formatul de scenariu din SPEC §19.3 (timeline de actiuni + expectations), `volt-sim run <scenariu> --assert`, raport clar la esec (ce asteptare, ce valoare, cand).

**NU implementezi:** injectii care tin de platforma (node_failure etc.) — vin la P23 si se conecteaza aici prin acelasi mecanism de actiuni.

**Verificare:** 5 scenarii reale in `simulation/scenarios/`, rulate in CI; un scenariu care esueaza intentionat, cu mesaj verificat.

**Commit:** `sim: declarative scenario engine with assertions`

---

## P23 — Framework de injectie de defecte

**Citeste inainte:** SPEC §20 (tabelul complet), §22.
**Se leaga de:** P22, P11.

**Implementezi:** `simulation/faults/` cu punctele de injectie pentru categoriile disponibile acum (compute, date; retea si securitate se completeaza cand exista modulele), `volt-inject` CLI cu sintaxa din SPEC §20, si **confirmarea injectiei**: fiecare injectie emite un eveniment de trace pe care testele il asteapta inainte sa verifice reactia.

**NU implementezi:** injectiile care depind de module inexistente. Comanda trebuie sa raspunda explicit "not supported yet in this build", nu sa taca.

**Verificare:** pentru fiecare fault implementat, un test care verifica (a) ca s-a produs, (b) ca a fost confirmat prin trace, (c) reactia sistemului.

**Capcane:** un test de fault injection care nu confirma injectia poate trece cu succes fara sa fi injectat nimic. Asta e capcana numarul unu din toata testarea de fault tolerance.

**Commit:** `faults: injection framework with confirmed-injection semantics`

---

# FAZA T5 — CAN-FD

## P24 — Abstractizarea de driver CAN si backendul SocketCAN

**Citeste inainte:** SPEC §11.1 (nivel 0-1), §26.2 (tunelul), §11.1 bus-off.
**Se leaga de:** P03.

**Implementezi in `communication/can/`:**
- `ICanDriver`: `open`, `send`, `receive` (cu timestamp), `set_filters`, `bus_state`, `statistics`. Toate cu `expected`.
- Backend SocketCAN: `CAN_RAW`, `CAN_RAW_FD_FRAMES`, filtre in kernel, `SO_TIMESTAMPING`, `CAN_ERR_FILTER`.
- Frame layer: encode/decode CAN si CAN-FD (DLC ↔ lungime pentru 0..64 octeti), extended ID, BRS, ESI.
- Masina de stari de bus: error-active → warning → passive → bus-off → recuperare cu backoff, cu evenimente.

**NU implementezi:** DBC (P25), tunelul serial (P50), semnale, E2E.

**Verificare:** teste pe `vcan0` in CI; `cansend`/`candump` din can-utils trebuie sa vada exact ce trimiti si invers; tabelul DLC↔lungime verificat exhaustiv pentru toate cele 16 valori.

**Commit:** `can: driver abstraction, SocketCAN backend and CAN-FD frame layer`

---

## P25 — Parser DBC

**Citeste inainte:** SPEC §11.1 (nivel 2).
**Se leaga de:** P24.

**Implementezi:** parser complet pentru subsetul folosit: `BO_`, `SG_`, `BA_`, `CM_`, `VAL_`, `VAL_TABLE_`, multiplexoare simple si extinse, byte order Intel/Motorola, factor/offset, min/max, unitati. Erorile de parsare sunt precise (linie, coloana, ce s-a asteptat).

**NU implementezi:** codegen (P26), scriere de DBC.

**Verificare:** `config/vehicle.dbc` (scris de tine, cu matricea din SPEC §11.1) se parseaza complet; 15 fisiere DBC invalide dau erorile asteptate; **comparatie cu `cantools`**: pentru fiecare mesaj, definitiile citite trebuie sa coincida.

**Commit:** `can: DBC parser with cross-validation against cantools`

---

## P26 — Generator de cod din DBC si stratul de semnale

**Citeste inainte:** SPEC §11.1 (nivel 2, exemplul de cod generat).
**Se leaga de:** P25.

**Implementezi:** `tools/dbc2cpp` — genereaza headere `constexpr` cu accesori type-safe, exact in forma din SPEC; integrare in CMake (regenerare la modificarea DBC-ului); impachetare/despachetare de biti corecta pentru ambele ordini de octeti si pentru semnale care traverseaza octeti.

**NU implementezi:** transmisie ciclica (P27).

**Verificare:** **testul care conteaza** — 100.000 de vectori aleatorii encodati/decodati cu codul tau si cu `cantools`, rezultate identice. Plus teste pentru cazuri limita: semnal de 1 bit, semnal de 64 de biti, semnal la limita de octet, valori negative cu scalare.

**Commit:** `can: DBC-to-C++ code generation with 100k-vector cantools equivalence`

---

## P27 — Managementul comunicatiei CAN

**Citeste inainte:** SPEC §11.1 (nivel 3).
**Se leaga de:** P26, P13.

**Implementezi:** transmisie ciclica cu fazare automata (evitarea burst-urilor), transmisie event-triggered cu debounce si rate limit, supervizare de timeout la receptie per mesaj, calculul incarcarii de bus si raportarea lui ca metrica.

**NU implementezi:** E2E (P28), SecOC (P29), gateway (P59).

**Verificare:** test ca 12 mesaje ciclice cu perioade diferite nu se aglomereaza (masoara distributia in timp); test de timeout care produce evenimentul asteptat; incarcarea de bus calculata verificata contra unui calcul manual pentru un set fix de mesaje.

**Commit:** `can: cyclic and event-triggered transmission with timeout supervision`

---

## P28 — Protectie E2E

**Citeste inainte:** SPEC §11.6, §14 (modelul TLA+ al receptorului).
**Se leaga de:** P27.

**Implementezi in `communication/e2e/`:** CRC peste payload + Data ID, alive counter pe 4 biti, si **masina de stari a receptorului cu toate cele 6 stari** din SPEC §11.6, plus fereastra de monitorizare (X din ultimele Y esantioane).

**NU implementezi:** SecOC (P29). E2E protejeaza impotriva erorilor, SecOC impotriva atacurilor — nu le amesteca.

**Verificare:** tabel exhaustiv de tranzitii (secventa de counter-e → stare asteptata la fiecare pas), inclusiv wrap-around, repetare, salt, pierdere; property-based: orice bit flip intr-un mesaj e detectat (10^6 incercari); testul de masquerading: acelasi payload cu alt Data ID e respins.

**Commit:** `e2e: end-to-end protection with full receiver state machine`

---

## P29 — SecOC si managerul de prospetime

**Citeste inainte:** SPEC §11.7, §17.1 (T-01, T-02).
**Se leaga de:** P28.

**Implementezi in `communication/secoc/`:** AES-128-CMAC (prin mbedTLS, nu implementat de tine), trunchiere la 24 de biti, freshness value pe 8 biti pe fir + contor de trip pe 16 biti, Freshness Value Manager cu sincronizare periodica, verificare in timp constant, politica per mesaj din configuratie.

**NU implementezi:** PKI, rotatie de chei (ramura R-SEC), secure boot.

**Verificare:** vectori de test CMAC din RFC 4493 (obligatoriu — daca nu ii ai, cripto ta e gresita); test de replay: un frame valid retrimis e respins; test de MAC falsificat; masurarea timpului de verificare pe x86 (si mai tarziu pe MCU), publicata.

**Commit:** `secoc: truncated CMAC authentication with freshness management`

---

# FAZA T6 — Servicii si bucla de control

## P30 — SensorFusionService

**Citeste inainte:** SPEC §37.1, §15.4 (plauzibilitate), §36 (interfata).
**Se leaga de:** P11, P26, P28.

**Implementezi:** serviciul complet din SPEC §37.1 — filtru complementar, estimare de bias, indicator de calitate, masca de surse valide, si cele 3 trepte de degradare.

**NU implementezi:** verificarile de plauzibilitate ca modul separat (P52) — aici doar le consumi prin interfata lor; daca modulul nu exista inca, defineste interfata si implementeaza minimul, dar marcheaza clar.

**Verificare:** teste cu date generate de simulator, cu adevar de referinta cunoscut: eroarea de estimare a vitezei < 2% in regim normal, < 8% cu o roata invalida; test pentru fiecare treapta de degradare; test ca serviciul nu aloca (K10).

**Commit:** `services: sensor fusion with quality indication and graceful degradation`

---

## P31 — BrakeControlService dual-channel

**Citeste inainte:** SPEC §37.3 (integral), §15.4 (redundanta), §15.2 (REQ-SAF-001), §41 (starea sigura).
**Se leaga de:** P30, P13.

**Implementezi:**
- Canalul A: PI cu anti-windup + ABS per roata + distributie fata/spate + limitare split-mu.
- Canalul B: **algoritm diferit**, tabelar, in virgula fixa Q16.16, mai conservator. Nu copia canalul A.
- Comparator cu toleranta, durata si histereza; la divergenta emite `min(A,B)`.
- **Garda de siguranta ca modul separat**, cu `// @satisfies REQ-SAF-001`: comanda iese doar cu cerere valida E2E OK + lease valid + plauzibilitate recenta.
- Slew limiter pentru preluarea din STANDBY.

**NU implementezi:** lease-urile (P57) — foloseste o interfata `ILeaseProvider` care deocamdata intoarce mereu valid, si marcheaza cu un comentariu unde se conecteaza. Nu implementa arbitrarea cu traction control (P32).

**Verificare:** scenariu de franare pe suprafata uniforma si pe split-mu, cu asertiuni numerice (distanta de oprire, yaw maxim); test ca fara cerere valida comanda e exact 0 in toate combinatiile posibile de intrari (exhaustiv pe conditii); test de divergenta cu canal B corupt.

**Capcane:** daca ambele canale ajung sa foloseasca aceleasi constante si aceeasi structura, redundanta e decorativa. Diversitatea e cerinta, nu stil.

**Commit:** `services: dual-channel brake control with independent safety guard`

---

## P32 — Servicii de vehicul si arbitrare

**Citeste inainte:** SPEC §37.2, §37.4, §37.5.
**Se leaga de:** P31.

**Implementezi:** `VehicleDynamicsService`, `TractionControlService`, `SteeringAssistService`, si **arbitrul explicit** intre cererile care ating acelasi actuator (cea mai restrictiva castiga), ca modul separat si testabil.

**NU implementezi:** ACC (vine la ramura R-ADAS), lease pentru directie (P57).

**Verificare:** teste tabelate pentru arbitru (toate combinatiile de cereri concurente); test ca doua functii nu pot comanda simultan acelasi actuator fara sa treaca prin arbitru (verificare structurala + test).

**Commit:** `services: vehicle dynamics, traction control, steering assist and actuator arbitration`

---

## P33 — Bucla inchisa in SIL

**Citeste inainte:** SPEC §3.2, §38 (lantul de timp), §19.3.
**Se leaga de:** P20-P32.

**Implementezi:** legarea completa simulator ↔ servicii ↔ CAN virtual, scenariul `emergency_brake_split_mu` cu asertiunile din SPEC §19.3, si masurarea lantului end-to-end din SPEC §38 (fiecare etapa separat).

**NU implementezi:** noduri multiple (T10), hardware (T12).

**Verificare:** scenariul trece cu asertiuni; tabelul de timp end-to-end e completat cu masuratori reale in `docs/PERFORMANCE.md`; K1 respectat cu tot sistemul pornit.

**Done cand:** ai primul demo real (D2) care ruleaza cu o singura comanda.

**Commit:** `sil: closed-loop integration with end-to-end timing measurement`

---

# FAZA T7 — Ethernet si SOME/IP

## P34 — Reactor de retea si transporturi

**Citeste inainte:** SPEC §11.3, §12.2 (QoS), §42.2 (threading).
**Se leaga de:** P03, P11.

**Implementezi in `communication/eth/`:** reactor `epoll` edge-triggered, UDP unicast si multicast (cu IGMP), TCP, `SO_PRIORITY` mapat pe PCP, tagging VLAN, un thread de retea per clasa de prioritate, si politicile QoS din SPEC §12.2 la nivel de transport.

**NU implementezi:** SOME/IP (P35), shaper time-aware (ramura R-TIME), gPTP (P38).

**Verificare:** teste peste `veth` intre doua network namespace-uri (asa testezi retea reala fara hardware); test de prioritizare: trafic de fundal saturant nu afecteaza clasa de control cu mai mult de X% (masoara si documenteaza X); TSan curat.

**Commit:** `eth: epoll reactor with UDP/TCP/multicast transports and traffic classes`

---

## P35 — SOME/IP compatibil pe fir

**Citeste inainte:** SPEC §11.4 (antetul, tipurile, codurile), §36 (catalogul).
**Se leaga de:** P34.

**Implementezi:** antetul de 16 octeti exact ca in spec, toate tipurile de mesaj si codurile de retur, SOME/IP-TP (segmentare), si serializarea conforma (big-endian, aliniere, tablouri dinamice cu camp de lungime).

**NU implementezi:** service discovery (P36), generarea de proxy/skeleton (P37).

**Verificare (obligatorie, altfel nu e terminat):**
1. Capturi `pcap` cu traficul tau deschise in **Wireshark** → dissectorul standard SOME/IP le decodeaza corect. Pune fisierul `.pcap` in repo.
2. Teste de round-trip pentru toate tipurile de date din catalog.
3. Fuzzing pe decodor (adauga tinta la P49).

**Commit:** `someip: wire-compatible protocol implementation validated with Wireshark`

---

## P36 — SOME/IP Service Discovery

**Citeste inainte:** SPEC §11.4 (SD), §12.1 (registry).
**Se leaga de:** P35.

**Implementezi:** entries (Find, Offer, StopOffer, Subscribe, SubscribeAck, Nack), options (IPv4 endpoint, multicast, configuration), fazele initial wait / repetition cu backoff / main phase ciclica, TTL, detectie de reboot.

**NU implementezi:** registry replicat (P55/P56) — SD e protocolul pe fir, registry-ul e sursa de adevar; tine-le separate.

**Verificare:** 3 procese, 12 servicii, toate descoperite in < 500 ms de la pornire; test de TTL expirat; test de reboot detectat; captura Wireshark cu SD decodat.

**Commit:** `someip: service discovery with repetition phases and reboot detection`

---

## P37 — Generator de proxy si skeleton din catalog

**Citeste inainte:** SPEC §36 (catalogul), §56 (stilul ara::com), §11.4.
**Se leaga de:** P35, P36.

**Implementezi:** `tools/serdes_gen` care citeste `config/services.yaml` si genereaza: structuri de mesaj, serializare, `<Service>Proxy` (find, handler de disponibilitate, evenimente, metode cu `volt::Future`) si `<Service>Skeleton`. Stilul e cel din SPEC §56.

**NU implementezi:** manifest de executie complet (ramura R-AUTOSAR) — deocamdata doar catalogul de servicii.

**Verificare:** teste de aur (cod generat comparat cu referinta versionata); un serviciu real folosit prin proxy generat; test ca schimbarea versiunii de interfata produce `E_WRONG_INTERFACE_VERSION`.

**Commit:** `someip: proxy/skeleton code generation from the service catalog`

---

## P38 — gPTP-lite: baza de timp globala

**Citeste inainte:** SPEC §8.2 (integral), §13.3 (marja de ceas), §49.
**Se leaga de:** P34, P13.

**Implementezi:** master ales prin BMCA simplificat cu fallback, mesaje SYNC/FOLLOW_UP/DELAY_REQ/DELAY_RESP peste UDP multicast, timestamping software (`SO_TIMESTAMPING`), servo PI pe offset + estimare de drift prin regresie, si `Clock::synced()` / `offset_ns()` / `drift_ppb()` expuse.

**NU implementezi:** timestamping hardware (ramura R-TIME), PPS (P66).

**Verificare:** K19 (< 50 µs RMS intre doua namespace-uri); test de cadere a masterului → alegerea altuia in < 1 s; **test care conteaza pentru siguranta**: cand `synced()` e fals, marjele temporale se dubleaza si se ridica evenimentul (verificat impreuna cu P57).

**Commit:** `time: gPTP-derived global time base with drift estimation`

---

## P39 — DoIP

**Citeste inainte:** SPEC §11.5.
**Se leaga de:** P34.

**Implementezi:** antetul si tipurile de payload din SPEC §11.5, discovery pe UDP 13400, sesiuni pe TCP 13400, routing activation cu tipuri de activare, alive check.

**NU implementezi:** UDS (P42-P44) — DoIP e doar transport.

**Verificare:** interop automat in CI cu biblioteca Python `doipclient`: descopera entitatea, activeaza rutarea, transporta un mesaj. Captura Wireshark decodata.

**Commit:** `doip: ISO 13400 transport with routing activation`

---

# FAZA T8 — Diagnostic

## P40 — ISO-TP

**Citeste inainte:** SPEC §11.2.
**Se leaga de:** P24.

**Implementezi:** SF/FF/CF/FC, block size, STmin (inclusiv valorile in microsecunde), FC.WAIT, FC.OVFLW, toate timerele N_As/N_Ar/N_Bs/N_Br/N_Cs/N_Cr cu valorile din standard, adresare normala si extinsa, suport CAN-FD pentru lungimi mari.

**NU implementezi:** UDS (P41).

**Verificare (obligatorie):** interop cu modulul kernel `can-isotp` pe `vcan0` — `isotpsend` catre stiva ta si invers, pentru payload-uri de 1, 7, 8, 100, 4095 octeti. Plus: test pentru fiecare timeout, cu verificarea codului de eroare.

**Commit:** `isotp: ISO 15765-2 transport validated against the Linux can-isotp module`

---

## P41 — Server UDS: nucleu, sesiuni, NRC-uri

**Citeste inainte:** SPEC §16.1 (tabelul de servicii), §16.3.
**Se leaga de:** P40, P39.

**Implementezi:** dispecerul de servicii cu verificarea conditiilor (sesiune, securitate, stare), sesiunile (default / programming / extended), timerul S3, `0x3E TesterPresent`, `0x10`, `0x11`, `0x28`, `0x85`, si **toate** NRC-urile din SPEC §16.1, inclusiv `0x78` response pending cu retrimitere.

**NU implementezi:** serviciile de date (P42), DTC (P43), security access (P44), flashing (ramura R-SEC).

**Verificare:** vectori request/response octet cu octet pentru fiecare serviciu si fiecare NRC; test de S3 (revenire la default dupa 5 s); interop partial cu `udsoncan`.

**Commit:** `uds: diagnostic session management, service dispatch and negative responses`

---

## P42 — UDS: servicii de date si rutine

**Citeste inainte:** SPEC §16.1 (tabelul de DID-uri), §8.5 (calibrare).
**Se leaga de:** P41, P07.

**Implementezi:** `0x22` (multi-DID), `0x2E`, `0x2F`, `0x31` cu rutinele din spec, si registrul de DID-uri generat din `config/dids.yaml` (`tools/did_gen`), cu tipuri, limite si drepturi de acces.

**NU implementezi:** rutina de fault injection cu drepturi reale (are nevoie de P44), flashing.

**Verificare:** fiecare DID din tabelul SPEC §16.1 citit prin `udsoncan` cu valoarea asteptata; scrierea unui DID in afara limitelor → `0x31`; test ca DID-urile de calibrare chiar schimba comportamentul serviciului.

**Commit:** `uds: data identifiers, IO control and routine control`

---

## P43 — DTC manager si NVM

**Citeste inainte:** SPEC §16.2 (integral, inclusiv catalogul), §15.5 (debounce).
**Se leaga de:** P41.

**Implementezi:** format pe 3 octeti + status byte cu **toti** cei 8 biti, ciclu de operare, debounce (counter si time-based), aging, snapshot records (max 3), extended data records, `0x19` cu sub-functiile din spec, `0x14`, si persistenta NVM cu doua copii + CRC32, scriere atomica write-then-swap, detectie de coruptie la boot.

**NU implementezi:** reactiile de degradare (P54) — DTC-ul e evidenta, reactia e alta responsabilitate.

**Verificare:** pentru fiecare DTC din catalog, ciclul complet aparitie → pending → confirmed → snapshot → aging → sters, cu verificarea fiecarui bit la fiecare pas; test de intrerupere a alimentarii: 200 de `kill -9` in mijlocul scrierii → NVM mereu valid (una din cele doua stari, niciodata corupt).

**Commit:** `dtc: fault memory with debounce, aging, snapshots and robust NVM`

---

## P44 — Security access si control de acces

**Citeste inainte:** SPEC §16.3, §17.1 (T-04).
**Se leaga de:** P41, P29.

**Implementezi:** seed & key cu CMAC (nu XOR), doua niveluri, delay timer exponential dupa esecuri, limita de incercari, roluri si maparea lor pe servicii si DID-uri din `config/access_control.yaml`, keystore separat de cod.

**NU implementezi:** UDS 0x29 cu certificate (ramura R-SEC) — asta il inlocuieste mai tarziu, nu il dubleaza acum.

**Verificare:** cheie gresita → `0x35`; a 4-a incercare → `0x36`, apoi `0x37` pana la expirare; seed diferit la fiecare cerere (verificat statistic pe 10.000 de cereri); test ca o rutina protejata e refuzata fara acces.

**Commit:** `security: seed-and-key access control with role-based permissions`

---

## P45 — Tester `volt-diag` si interop

**Citeste inainte:** SPEC §16 (integral), §30 (D8).
**Se leaga de:** P39-P44.

**Implementezi:** CLI-ul complet cu sintaxa din SPEC §30 (D8), peste ambele transporturi (ISO-TP pe CAN si DoIP), cu output citibil si mod `--raw` pentru octeti.

**NU implementezi:** flashing (ramura R-SEC).

**Verificare:** scriptul `tests/integration/uds_interop.py` (cu `udsoncan` si `doipclient`) executa aceeasi secventa ca `volt-diag` si obtine aceleasi raspunsuri — rulat in CI.

**Commit:** `tools: volt-diag diagnostic tester over ISO-TP and DoIP`

---

## P46 — Tinte de fuzzing

**Citeste inainte:** SPEC §17.4, §23.
**Se leaga de:** P25, P28, P35, P36, P39, P40, P41.

**Implementezi:** tinte libFuzzer pentru: decodor de frame CAN, parser DBC, reasamblare ISO-TP, antet si payload SOME/IP, entries/options SD, antet DoIP, request UDS. Corpus minimizat, versionat. Integrare in CI (60 s per tinta) si nightly (1 h).

**NU implementezi:** fuzzing structurat cu gramatica (mai tarziu, daca e cazul).

**Verificare:** 0 crash-uri, 0 leak-uri; fiecare finding devine test in `tests/fuzz/regressions/`. Introdu intentionat un bug de indexare si verifica ca fuzzer-ul il gaseste in < 60 s.

**Commit:** `fuzz: libFuzzer targets for all externally-facing parsers`

---

# FAZA T9 — Siguranta

## P47 — Cerinte, HARA si tool de traceability

**Citeste inainte:** SPEC §15.1, §15.2, §28 (integral).
**Se leaga de:** tot ce exista.

**Implementezi:**
- `requirements/HARA.md` cu cele 6 hazarde din SPEC §15.1 si derivarea bugetelor.
- `requirements/REQ-SAF.md`, `REQ-RT.md`, `REQ-COM.md`, `REQ-DIA.md`, `REQ-SEC.md`, `REQ-DST.md` — cerinte testabile, in formatul din SPEC §15.2.
- `tools/traceability/` — scriptul din SPEC §28.2, care genereaza `docs/TRACEABILITY.md` si **esueaza** la cerinte neacoperite, teste orfane, sau safety goal fara lant complet.
- Adnotarea codului deja scris cu `// @satisfies` si a testelor cu `// @verifies`.

**NU implementezi:** cerinte pentru module inexistente (le scrii cand ajungi la ele) — dar lasa ID-urile rezervate.

**Verificare:** tool-ul are propriile teste pe repo-uri sintetice; ruleaza pe repo-ul real si raporteaza onest ce lipseste (e in regula sa lipseasca acum — nu falsifica acoperirea).

**Commit:** `requirements: hazard analysis, safety requirements and automated traceability`

---

## P48 — Plauzibilitate si cross-check

**Citeste inainte:** SPEC §15.4, §37.1, §26.3 (cele doua canale de senzor).
**Se leaga de:** P30, P21.

**Implementezi in `safety/plausibility/`:** cele patru familii de verificari (range, rate, temporal, cross), configurabile per semnal, cu histereza si durata; verificarea incrucisata roti ↔ IMU ↔ estimare; votare 2oo3 acolo unde exista trei surse; marcarea sursei eliminate.

**NU implementezi:** reactia (P50) — plauzibilitatea produce verdicte, nu decizii.

**Verificare:** property-based: pentru orice secventa cu defect de tip X injectat, detectie in ≤ D ms; **si** rata de fals pozitiv zero pe 10^7 esantioane fara defect. Testul cu `FROZEN_PLAUSIBLE` e obligatoriu — e singurul care nu poate fi prins fara cross-check.

**Commit:** `safety: plausibility checks and cross-source verification`

---

## P49 — Redundanta duala si comparator

**Citeste inainte:** SPEC §15.4 (redundanta), §37.3.
**Se leaga de:** P31.

**Implementezi in `safety/redundancy/`:** comparatorul generic (toleranta, durata, histereza), politica de iesire conservatoare, si raportarea divergentei. Extrage din P31 logica comparatorului si generalizeaz-o, ca sa fie folosibila si pentru directie.

**NU implementezi:** monitorul de Nivel 2 pe MCU (P56) — acela e alt nivel, nu acelasi mecanism.

**Verificare:** teste tabelate cu perechi de valori si verdicte asteptate; test ca divergenta sub durata minima **nu** declanseaza (altfel ai fals pozitive la fiecare tranzitie).

**Commit:** `safety: generic dual-channel comparator with conservative output policy`

---

## P50 — Fault manager

**Citeste inainte:** SPEC §15.5, §16.2 (legatura cu DTC).
**Se leaga de:** P43, P48, P16.

**Implementezi in `safety/fault_manager/`:** structura `Fault`, debounce configurabil per fault, healing/aging, tabelul de reactii **in configuratie, nu in cod**, si snapshot-ul la confirmare. Toate modulele care detecteaza ceva raporteaza aici, printr-o singura interfata.

**NU implementezi:** masina de degradare (P51) — fault manager-ul decide *ce e stricat*, degradarea decide *ce facem*.

**Verificare:** test generat automat din tabelul de fault-uri: pentru fiecare fault injectabil, DTC-ul documentat trebuie sa apara. Asta face imposibil sa adaugi un fault nou fara sa il legi corect.

**Commit:** `safety: central fault manager with configurable debounce and reactions`

---

## P51 — Masina de degradare si starea sigura

**Citeste inainte:** SPEC §15.3 (tabelul de tranzitii), §41 (starea sigura fizica).
**Se leaga de:** P50, P31.

**Implementezi:**
- `tools/fsm_gen`: tabel YAML → cod C++ + diagrama PlantUML + tabel in documentatie. Codul si documentatia **nu pot** diverge.
- Cele doua dimensiuni ortogonale (system state si safety state) din SPEC §15.3.
- Regulile de safe state din SPEC §41: rampa, latch, inregistrarea cauzei inainte de tranzitie, iesire doar prin reset explicit.

**NU implementezi:** partea de rampa din firmware (P57).

**Verificare:** parcurgere exhaustiva a tuturor perechilor (stare × eveniment) — nicio combinatie nedefinita; test ca nicio secventa de evenimente nu produce iesire automata din SAFE_STATE; testul de timp: fiecare tranzitie in deadline-ul ei.

**Commit:** `safety: generated degradation state machine with latched safe state`

---

# FAZA T10 — Runtime distribuit

## P52 — SWIM si detectia pe doua retele

**Citeste inainte:** SPEC §13.1 (integral), §0.2 (K6).
**Se leaga de:** P34, P27, P38.

**Implementezi in `distributed/membership/`:** protocolul SWIM cu parametrii din spec (3/2/3/5 ms), probing indirect, incarnation numbers, gossip piggyback, **si confirmarea independenta pe CAN** prin `0x400 NodeHeartbeat`. Regula: FAULTY doar cand tac ambele cai; daca tace una, e defect de retea si are alta reactie.

**NU implementezi:** decizia de failover (P54) — detectorul doar raporteaza.

**Verificare:** in `SimEnvironment`, cu pierderi 0-60%, partitii, noduri lente si restarturi: (a) un nod cazut e detectat de toti in < 50 ms; (b) rata de fals pozitiv masurata si raportata; (c) K6 pe procese reale, 100 de repetari, histograma.

**Commit:** `membership: SWIM failure detection with dual-network confirmation`

---

## P53 — Raft

**Citeste inainte:** SPEC §13.2, §14 (ce se verifica formal).
**Se leaga de:** P52, P11.

**Implementezi in `distributed/consensus/`:** leader election cu termeni si timeout randomizat, `AppendEntries`, commit index, aplicare in ordine pe masina de stari (registry + tabela de proprietate), persistenta `currentTerm`/`votedFor`/log cu `fsync` si CRC, snapshot si compactare. Cluster static de 3 noduri, fara membership dinamic (documenteaza in ADR).

**NU implementezi:** lease-uri (P54), placement (P55).

**Verificare:** in DST, 2000 de seeds cu partitii, crash-uri si mesaje reordonate; invarianti verificati mecanic: Election Safety, Log Matching, Leader Completeness, State Machine Safety. Test de recuperare dupa `kill -9` in mijlocul unui `fsync` (simulat prin scriere partiala in PAL sim).

**Capcane:** nu incepe cu optimizari (batching, pipelining). Corectitudinea intai, si invariantii verificati la fiecare pas.

**Commit:** `consensus: Raft implementation for the replicated configuration store`

---

## P54 — Lease-uri, epoch-uri, fencing, succesiune preautorizata

**Citeste inainte:** SPEC §13.3 (integral — inclusiv tabelul FENCED/UNFENCED si punctul 6), §15.1 (SG-05).
**Se leaga de:** P53, P52, P38.

**Implementezi in `distributed/lease/`:**
- Lease 30 ms / reinnoire 10 ms / retragere proprie la 20 ms, cu marja legata de `max_clock_error` din gPTP.
- Clasificarea resurselor FENCED / UNFENCED si cele doua reguli de acordare.
- **Succesiunea preautorizata**: intrarea conditionata comisa odata cu acordarea, activata la constatarea quorumului.
- Epoch-ul in fiecare comanda catre actuator, si refuzul comenzilor cu epoch mai vechi la client.

**NU implementezi:** verificarea in firmware (P58) — dar formatul de mesaj trebuie sa fie deja final.

**Verificare:**
- Invariantul central in DST: niciodata doi detinatori simultani, pe 5000 de seeds cu ceasuri care deriva si partitii.
- Test: fara quorum nu se preia nimic si se intra in safe state.
- Test: nodul izolat tace **inainte** de expirarea lease-ului, masurat.

**Commit:** `lease: fenced ownership with pre-authorized succession and self-yield`

---

## P55 — Placement si orchestrare

**Citeste inainte:** SPEC §13.4, §39.3.
**Se leaga de:** P53.

**Implementezi:** rezolvatorul determinist din SPEC §13.4 (greedy cu prioritati + backtracking limitat), constrangerile hard, ordonarea pe criticalitate, oprirea serviciilor optionale de jos in sus, planul comis prin Raft, si CLI-ul `volt-monitor placement`.

**NU implementezi:** migrarea efectiva (P57) — aici doar planul.

**Verificare:** teste tabelate (intrare → plan exact asteptat); in DST, 200 de cicluri de cadere/revenire cu invariantii: niciodata doua ACTIVE, niciun serviciu safety oprit cat timp exista un nod capabil.

**Commit:** `placement: constraint-based service placement with criticality ordering`

---

## P56 — Replicarea starii si transferul fara salt

**Citeste inainte:** SPEC §13.5, §37.3 (slew limiter).
**Se leaga de:** P11, P54.

**Implementezi:** checkpoint periodic prin `serialize()`, log shipping intre checkpoint-uri, detectia divergentei prin `state_hash`, starea `STANDBY_STALE` si regula de preluare cu reinitializare controlata, si transferul cu limitare de rata.

**NU implementezi:** orchestrarea failover-ului (P57).

**Verificare:** property: `deserialize(serialize(s))` identic pe 10.000 de stari; test de discontinuitate: K9 (< 2 cicluri, salt < 5%); test ca divergenta injectata pe canalul de sincronizare e **detectata**, nu ignorata.

**Commit:** `replication: state checkpointing, log shipping and bumpless transfer`

---

## P57 — Orchestrarea failover-ului si raportul de cronologie

**Citeste inainte:** SPEC §13.6 (cronologia exacta), §13.7 (rejoin), §0.2 (K7).
**Se leaga de:** P52-P56.

**Implementezi:** legarea completa detectie → constatare de quorum → activarea succesiunii → restore → prima comanda; generarea automata a raportului de failover din SPEC §13.6 dupa fiecare eveniment; rejoin ca `OBSERVER` si failback **manual sau cu histereza**, niciodata automat imediat.

**NU implementezi:** partea de firmware (P58).

**Verificare:** `failover_timeline` cu 100 de repetari si raportul statistic complet din SPEC §13.6; test `rejoin_zombie` in DST; K7 si K9 masurate.

**Done cand:** ai demo-ul D3 rulabil cu o comanda.

**Commit:** `recovery: failover orchestration with measured phase-by-phase timeline`

---

# FAZA T11 — Testare determinista si replay

## P58 — Motorul DST

**Citeste inainte:** SPEC §21.1 (integral).
**Se leaga de:** P04, P11, P52-P57.

**Implementezi:** `apps/volt-dst` — intreg clusterul intr-un thread peste `SimEnvironment`, ceas virtual pe coada de evenimente, model de retea derivat din seed, cei 6 invarianti din SPEC §21.1 verificati **la fiecare pas**, si comenzile `run` / `sweep`.

**NU implementezi:** minimizarea (P59), replay (P60).

**Verificare:** acelasi seed → aceeasi executie (hash al secventei de evenimente), de 100 de ori; o rulare de 10 minute simulate in < 5 s reale; incalcarea deliberata a unui invariant e prinsa.

**Commit:** `dst: single-threaded deterministic simulation of the full cluster`

---

## P59 — Minimizare si mutanti

**Citeste inainte:** SPEC §21.1 (minimizare, mutanti).
**Se leaga de:** P58.

**Implementezi:** delta debugging pentru reducerea scenariului la pasii minimi care reproduc esecul, salvarea automata ca test de regresie, si `tests/dst/mutants/` cu **12 mutatii cunoscute** ale protocolului (lease acordat prea devreme, epoch necomparat, snapshot aplicat in ordine gresita, quorum calculat gresit, etc.), fiecare cu un flag de compilare.

**NU implementezi:** mutation testing generic pe tot codul.

**Verificare:** toate cele 12 mutante sunt prinse in < 1000 de seeds; timpul mediu pana la detectie e raportat per mutanta (unele vor fi grele — asta e informatie utila).

**Commit:** `dst: counterexample minimization and protocol mutation suite`

---

## P60 — Record, replay, time-travel

**Citeste inainte:** SPEC §21.2, §0.2 (K14).
**Se leaga de:** P11, P58.

**Implementezi:** modul `--record` (toate intrarile fiecarui actor, cu overhead < 5%), `ReplayEnvironment`, compararea `state_hash` la fiecare pas, si comenzile `verify` / `goto` / `step` / `watch` / `why` / `export --perfetto`. `why` construieste subtrace-ul cauzal minim.

**NU implementezi:** interfata grafica de debugging.

**Verificare:** K14 — 50 de sesiuni rejucate de 3 ori, 100% hash-uri identice; orice divergenta e tratata ca bug de arhitectura (violare D1), nu ca toleranta acceptabila.

**Commit:** `replay: deterministic session recording with causal trace analysis`

---

# FAZA T12 — Hardware (buget zero)

## P61 — Tunelul CAN catre placi

**Citeste inainte:** SPEC §26.2 (integral), §26.1.
**Se leaga de:** P24.

**Implementezi:** `apps/volt-cantun` — citeste `vcan0`, incapsuleaza in formatul `SOF | len | can_id | flags | payload | CRC16`, trimite pe UART (1 Mbit) si pe UDP; in sens invers, la fel. Acelasi codec compilat si pentru MCU-uri, in `firmware/shared/`.

**NU implementezi:** firmware-ul (P62-P64).

**Verificare:** `cantun_fidelity` — 100.000 de frame-uri aleatorii dus-intors bit-identice, cu latenta masurata si publicata; test de resincronizare dupa octeti pierduti (taie intentionat din flux).

**Commit:** `hil: CAN tunnel over UART and UDP with shared codec`

---

## P62 — Firmware actuator (Arduino Uno R4 WiFi)

**Citeste inainte:** SPEC §26.3, §26.4, §41 (starea sigura), §13.3 (epoch).
**Se leaga de:** P61, P28, P29, P54.

**Implementezi in `firmware/uno_r4_actuator/`:** receptie pe UART **si** WiFi, verificare CRC E2E + alive counter + CMAC trunchiat + epoch (`max_epoch_seen` persistat), timeout de 20 ms → **rampa** catre starea sigura (rampa e in firmware, nu doar pe PC), matricea LED 12×8 ca bara de presiune, iesire PWM, readback pe A0 cu comparatie comanda/readback, verificarea pinului ENABLE inainte de fiecare scriere, raportare de stare la 100 ms.

**NU implementezi:** logica de control (ramane pe PC), Nivelul 2 (P63).

**Verificare:** teste pe placa: comanda cu epoch vechi refuzata si contorizata; ENABLE LOW → iesire zero indiferent de comanda; deconectarea PC-ului → rampa in 20 ±5 ms masurata de Uno R3; 200 de resetari in timpul comenzilor → epoch pastrat.

**Capcane:** nu exista cale de cod care sa scrie PWM fara sa treaca prin verificarea ENABLE. Verifica asta citind codul, nu presupunand.

**Commit:** `firmware: actuator ECU with E2E, SecOC, epoch fencing and safe-state ramp`

---

## P63 — Firmware monitor de Nivel 2 si al doilea canal de senzor (ESP32)

**Citeste inainte:** SPEC §46 (Nivelul 2), §26.3 (senzori), §47 (question/answer).
**Se leaga de:** P62.

**Implementezi in `firmware/esp32_monitor/`:** pe un core — modelul simplificat de comanda permisa (independent de algoritmul de pe PC), verdictul si raportarea; pe celalalt core — citirea ADC a liniei analogice, conversia in "viteza roata", trimiterea ca frame `0x102` cu E2E; plus raspunsul la question/answer de la Uno R3, si contorul de 80 MHz pentru masurarea K21.

**NU implementezi:** linia ENABLE (o detine Uno R3, P64), logica de control completa.

**Verificare:** test ca o comanda excesiva e respinsa in < 5 ms; comparatia celor doua canale de senzor (ESP32 vs readback-ul lui R4) intr-un test de cross-check; masurarea si publicarea timpului de calcul CMAC pe ESP32.

**Commit:** `firmware: ESP32 level-2 function monitor and independent sensor channel`

---

## P64 — Firmware Nivel 3 si calea de oprire (Arduino Uno R3)

**Citeste inainte:** SPEC §47 (integral), §46.
**Se leaga de:** P63, P62.

**Implementezi in `firmware/uno_r3_l3monitor/`:** bucla fixa la 1 kHz fara biblioteci grele, question/answer cu tabel precalculat si fereastra 1-4 ms, verificarea verdictului Nivelului 2, linia ENABLE open-drain cu latch, watchdog hardware AVR la 15 ms, contoare in EEPROM. **Starea implicita la reset si la orice defect: ENABLE oprit.**

**NU implementezi:** nimic altceva. Acest firmware trebuie sa poata fi citit integral in mai putin de o ora. Daca depaseste ~500 de linii, ceva nu apartine aici.

**Verificare:** fiecare ramura din tabelul de intrebari; marginile ferestrei (0,9 / 1,0 / 4,0 / 4,1 ms); K21 masurat de ESP32, 1000 de repetari, histograma; 100.000 de cicluri corecte fara interventie falsa; **scoaterea alimentarii lui Uno R3 → ENABLE cade in < 10 ms**, verificat de 50 de ori.

**Commit:** `firmware: level-3 controller monitor with fail-safe enable line`

---

## P65 — Suita HIL si masuratori pe hardware

**Citeste inainte:** SPEC §26.5, §38, §30.
**Se leaga de:** P61-P64.

**Implementezi:** cele 9 teste din SPEC §26.5, harness-ul care le ruleaza (inclusiv pasii manuali, clar semnalati), si masurarea lantului end-to-end din SPEC §38 pe hardware real.

**NU implementezi:** automatizarea pasilor fizici (scoaterea firelor ramane manuala; testul cere confirmare).

**Verificare:** toate cele 9 trec; `docs/HIL_REPORT.md` generat; video de 60 s cu D3 pe hardware.

**Commit:** `hil: hardware test suite and end-to-end timing measurements`

---

# Observabilitate si tooling

## P66 — TUI `volt-monitor`

**Citeste inainte:** SPEC §22.1 (macheta exacta).
**Se leaga de:** P10, P18, P57.

**Implementezi:** ecranul din SPEC §22.1 cu ftxui, alimentat de fluxul de telemetrie, cu refresh la 10 Hz si fara sa influenteze data plane-ul.

**Verificare:** test ca pornirea TUI-ului nu misca K1; test de randare cu date sintetice (snapshot testing pe text).

**Commit:** `tools: terminal system monitor`

---

## P67 — Dashboard web

**Citeste inainte:** SPEC §22.2.
**Se leaga de:** P66.

**Implementezi:** bridge WebSocket in C++ (in control plane) + SPA cu cele patru panouri din SPEC §22.2. Dashboard-ul e **doar consumator**; orice comanda trece prin canalul de diagnostic autentificat.

**NU implementezi:** control direct al data plane-ului din browser. Niciodata.

**Verificare:** test ca bridge-ul nu poate publica pe topicuri de control (verificare structurala + test negativ); test de incarcare: 10 clienti conectati nu misca K1.

**Commit:** `tools: web dashboard with read-only telemetry bridge`

---

## P68 — Generator de raport KPI

**Citeste inainte:** SPEC §0.2, §25.
**Se leaga de:** tot.

**Implementezi:** `make kpi` → ruleaza fiecare masuratoare, colecteaza rezultatele, genereaza `docs/KPI_REPORT.md` cu tabelul complet, graficele, data si commit-ul fiecarei masuratori. KPI-urile nemasurabile in mediul curent apar ca `NOT MEASURED`, niciodata cu valori vechi prezentate ca actuale.

**Verificare:** raportul generat pe o masina curata; README-ul citeste din raport, nu contine numere scrise de mana.

**Commit:** `tools: automated KPI report generation`

---

# Ramuri de aprofundare

Prompturile de mai jos sunt independente si se pot lua in orice ordine dupa T12. Formatul e mai scurt pentru ca acum stii cum arata un prompt bun — completeaza sectiunile lipsa dupa modelul de mai sus.

## P69 — TLA+ pentru lease si failover
**Citeste:** SPEC §14, §13.3. **Implementezi:** `Lease.tla`, `Failover.tla`, `E2E.tla` cu invariantii si liveness-ul din spec, configuratii TLC marginite (3 noduri, 2 servicii), maparea actiunilor la cod prin `// @tla`, si rularea in CI. **NU:** nu modela tot sistemul; modeleaza doar protocolul. **Verificare:** TLC trece; strica intentionat o regula din model si verifica ca TLC produce contraexemplu. **Livrabil bonus:** un contraexemplu real gasit, documentat in `docs/SAFETY.md`.

## P70 — CBMC pe functiile critice
**Citeste:** SPEC §54. **Implementezi:** harness-uri CBMC pentru cele 12 functii listate, cu pre/postconditii `__CPROVER_assert`, rulate in CI cu limite de adancime documentate. **NU:** nu incerca sa verifici cod cu alocari dinamice sau bucle nemarginite — extrage functia pura intai. **Verificare:** K25.

## P71 — FMEDA si arbori de defect generati
**Citeste:** SPEC §54, §15.1. **Implementezi:** `tools/fmeda` (tabel de moduri de defect, rate presupuse, mecanism de diagnostic, acoperire → SPFM/LFM) si `tools/fta_gen` (arbore de defect derivat din tabelul de fault-uri si reactii). **NU:** nu prezenta cifrele ca reale — sunt ilustrative si scrie asta in document. **Verificare:** fiecare cale din arbore are un test de fault injection care o parcurge, sau e marcata explicit ca neacoperita.

## P72 — WCET static
**Citeste:** SPEC §50. **Implementezi:** `volt-wcet` peste LLVM IR (CFG, adnotari `VOLT_LOOP_BOUND`, model de timp calibrat prin microbenchmarks, IPET rezolvat cu ILP), plus estimarea prin teoria valorilor extreme, plus tabelul comparativ. **Verificare:** K22 — zero subestimari pe 10^8 executii masurate.

## P73 — Sinteza de orar cu Z3
**Citeste:** SPEC §50, §9.2. **Implementezi:** codificarea constrangerilor (perioade, precedente, exclusivitati, ferestre de retea) in SMT si generarea unui orar global, sau demonstratia de infezabilitate. **NU:** nu inlocui generatorul greedy — coexista, si compari rezultatele. **Verificare:** checker-ul independent valideaza orarul; comparatie greedy vs Z3 pe 10 seturi.

## P74 — Matricea de izolare
**Citeste:** SPEC §48. **Implementezi:** `tests/perf/interference_matrix` care ruleaza acelasi benchmark in cele cinci configuratii (C0-C4) si genereaza tabelul; configuratiile indisponibile apar `SKIPPED` cu motiv. **Verificare:** K23.

## P75 — Portul QNX
**Citeste:** SPEC §27.1. **Implementezi:** `platform/pal/qnx/` cu maparea documentata a prioritatilor si backendul IPC nativ (`MsgSend`/`MsgReceive`), rulat in VM cu QNX SDP (licenta non-comerciala). **NU:** nu incerca CAN pe QNX; documenteaza backendul lipsa. **Verificare:** aceeasi `pal_conformance` trece; tabel comparativ de latenta Linux vs QNX in `docs/PORTABILITY.md`.

## P76 — Shaper time-aware si prioritizare masurata
**Citeste:** SPEC §49, §11.3. **Implementezi:** shaper software aliniat cu tabela TT, `taprio` software, mapari PCP, si campania de masurare cu si fara prioritizare. **Verificare:** K20; capturi care arata respectarea ferestrelor.

## P77 — PPS improvizat pentru verificarea ceasului
**Citeste:** SPEC §49. **Implementezi:** ESP32 pulseaza pe GPIO la fiecare secunda de ceas global, Uno R3 masoara diferenta fata de propriul puls si raporteaza; scriptul care agrega si publica distributia. **Verificare:** K19 verificat independent de propria ta implementare de gPTP (asta e ideea).

## P78 — PKI si UDS 0x29
**Citeste:** SPEC §51, §16.3. **Implementezi:** CA de dezvoltare, certificate per rol si per nod, serviciul `0x29` cu verificare de lant si dovada de posesie, si rotatia cheilor SecOC prin canalul autentificat. Seed & key ramane, ca mecanism vechi, pentru comparatie. **Verificare:** K28; zero acceptari de certificate invalide pe 1000 de variante generate.

## P79 — Secure boot si semnaturi hibride
**Citeste:** SPEC §17.2, §51. **Implementezi:** lantul de masuratori pe 4 etape, manifest semnat cu hash per artefact, si verificare hibrida ECDSA P-256 + ML-DSA. **Verificare:** modifica un octet in fiecare artefact pe rand (4 cazuri) → pornirea refuzata de fiecare data, cu eroarea corecta; masoara si publica dimensiunea semnaturii si timpul de verificare pe PC si pe MCU.

## P80 — Flashing UDS si OTA cu A/B
**Citeste:** SPEC §16.4. **Implementezi:** `0x34`/`0x36`/`0x37`, partitii A/B, health check post-boot cu rollback automat, si orchestrarea pe cluster (niciodata ACTIVE si STANDBY simultan). **Verificare:** 200 de intreruperi in puncte diferite → mereu bootabil; imagine care crapa intentionat → rollback in < 60 s.

## P81 — IDS
**Citeste:** SPEC §17.3, §17.1. **Implementezi:** cele 6 detectoare deterministe, corelatorul, si logul de securitate cu inlantuire de hash-uri. **NU:** fara ML — detectoarele trebuie sa fie explicabile si testabile. **Verificare:** fiecare vector din TARA declanseaza alerta corecta in < 200 ms; zero alerte in 1 h de trafic normal.

## P82 — XCP si A2L
**Citeste:** SPEC §52. **Implementezi:** slave XCP peste UDP si CAN, cu DAQ lists, si `tools/a2l_gen`. **Verificare:** K24 cu `pyxcp`; fiecare parametru din A2L corespunde unui DID expus prin UDS.

## P83 — FMI si bridge ROS 2
**Citeste:** SPEC §53. **Implementezi:** export FMU 2.0/3.0 al modelului de vehicul, import FMU tert, si `volt-ros2-bridge` ca simplu client SOME/IP fara privilegii. **Verificare:** FMU validat cu `fmpy`; acelasi scenariu rulat nativ si prin FMU da rezultate in limita tolerantei de integrare.

## P84 — Perceptie ADAS si ACC
**Citeste:** SPEC §55, §37.6. **Implementezi:** detectia de banda cu OpenCV, estimarea distantei dintr-un obiect de dimensiune cunoscuta, si `AdasAccService` care genereaza doar **cereri** catre BrakeControl. **NU:** ACC-ul nu atinge niciodata actuatorul direct. **Verificare:** K27; 50 de cadre cu adevar de referinta; test ca activarea perceptiei nu misca K1.

## P85 — Manifeste de executie in stil ara::com
**Citeste:** SPEC §56. **Implementezi:** manifestul JSON per aplicatie, modurile de functionare (Startup/Driving/Diagnostics/Update/Shutdown), si eliminarea oricarei configurari cablate in cod. **Verificare:** schimbi manifestul → se schimba comportamentul, fara recompilare (test automat).

## P86 — Yocto si build pentru ARM64
**Citeste:** SPEC §48. **Implementezi:** layer `meta-volt` cu reteta pentru runtime si servicii systemd, imagine testata in QEMU; cross-build aarch64 rulat sub QEMU user-mode. **Verificare:** imaginea booteaza in QEMU si ajunge la RUNNING; suita de teste trece pe aarch64 emulat.

---

# Anexa A — Ordinea recomandata

```
PROMPT 0 (CLAUDE.md)  ← o singura data, inainte de orice
T0:  P00 → P01 → P02
T1:  P03 → P04 → P05 → P06 → P07 → P08 → P09 → P10
T2:  P11 → P12
T3:  P13 → P14 → P15 → P16 → P17 → P18 → P19
T4:  P20 → P21 → P22 → P23
T5:  P24 → P25 → P26 → P27 → P28 → P29
T6:  P30 → P31 → P32 → P33          ← primul demo serios (D2)
T7:  P34 → P35 → P36 → P37 → P38 → P39
T8:  P40 → P41 → P42 → P43 → P44 → P45 → P46
T9:  P47 → P48 → P49 → P50 → P51
T10: P52 → P53 → P54 → P55 → P56 → P57   ← demo-ul principal (D3, D4)
T11: P58 → P59 → P60
T12: P61 → P62 → P63 → P64 → P65         ← hardware, demo D11
Tooling: P66 → P67 → P68   (se pot lua oricand dupa T3)
Ramuri:  P69-P86, in orice ordine
```

# Anexa B — Cele mai frecvente moduri in care o sesiune cu AI o ia razna

| Simptom | Ce s-a intamplat | Ce faci |
|---|---|---|
| Ti-a dat cod care "ar trebui sa mearga" cu TODO-uri | prompt prea larg | imparte prompt-ul in doua si reia |
| A inventat o functie din alt modul | nu i-ai dat interfata | ataseaza headerele de care depinde, nu descrierea lor |
| A refactorizat cod care nu era in scop | lipsa regulii 1 din preambul | reia cu preambulul complet si cere doar diff-ul minim |
| Testele trec dar nu testeaza nimic | teste scrise dupa cod, ca sa treaca | cere intai testele, apoi implementarea, in doua mesaje |
| A schimbat o interfata din SPEC | i s-a parut ca e mai bine | reaminteste regula 2; daca chiar e mai bine, modifici intai SPEC-ul, apoi codul |
| Fisiere uriase, imposibil de revizuit | prompt care cerea prea multe fisiere | fiecare fisier ramane citibil si recenzabil pe cont propriu |
| Merge la el, nu merge la tine | dependinte sau versiuni diferite | cere comenzile exacte si versiunile presupuse, in fiecare raspuns |
| Respecta regulile la inceput, apoi le uita | contextul s-a umplut si `CLAUDE.md` a ramas departe in istoric | sesiune noua pentru fiecare prompt; nu duce zece prompturi in aceeasi conversatie |
| Comentarii care repeta codul | nu a citit `CLAUDE.md` §7 sau l-a citit superficial | cere-i sa parcurga lista de autoverificare §11 si sa raporteze punct cu punct |
| Cere permisiunea sa incalce o regula | regula chiar e prea stricta pentru cazul respectiv, sau nu a inteles-o | daca are dreptate, modifici `CLAUDE.md` explicit si notezi in devlog; niciodata "de data asta e ok" |
