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
