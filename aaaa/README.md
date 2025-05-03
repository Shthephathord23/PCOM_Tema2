# PROTOCOALE DE COMUNICATIE
# Tema 2 - Aplicatie client-server TCP si UDP pentru gestionarea mesajelor

Dupa cum este mentionat si in cerinta, tema presupune implementarea unei platforme ce contine trei componente:
* **server-ul** (unic) va realiza legatura intre clientii din platforma, cu scopul publicarii si abonarii la mesaje. Serverul implementeaza si un mecanism "Store-and-Forward" ([text](https://en.wikipedia.org/wiki/Store_and_forward)) pentru clienti care dau unsubscribe temporar.
* **clientii TCP** vor avea urmatorul comportament: un client TCP se conecteaza la server, poate primi (in orice moment) de la tastatura(interactiunea cu utilizatorul uman) comenzi de tipul subscribe si unsubscribe si afiseaza pe ecran mesajele primite de la server.
* **clientii UDP** (ii asum implementati) publica, prin trimiterea catre server, mesaje in platforma propusa folosind un protocol predefinit.

Clientii UDP trimit mesaje la server in formatul mentionat in tema, iar in functie de cum sunt abonati clientii TCP, serverul va avea grija sa trimita eficient mesajele corespunzatoare fiecarei subscriptie a fiecarui client TCP.

Pentru a facilita eficienta, serverul face doar niste mici modificari asupra pachetelor de UDP primite (de ex adauga ip-ul si port-ul de la clientii UDP, lucru necesar pentru a face afisarea ca in cerinta, altfel as fi putut sa dau forward la pachet direct TCP, asta doar dupa ce fac pattern matching pe topic), apoi forwardeaza pachetele catre clientii TCP, acestia din urma fiind cei care decodifica payload-ul.

In enuntul temei nu se cere explicit implementarea mecanismului de Store-and-Forward. Eu am decis insa sa il implementez (motivul este ca am inteles gresit raspunsul la o intrebare FAQ, si am realizat spre finalizarea temei ca nu era necesara implementarea). Din testarile mele cu un client UDP custom, SF-ul parea sa mearga.

Pe parcusrul acestui README voi folosi <"clienti UDP" si "publisheri"> respectiv <"clienti TCP" si "subscriberi"> drept sinonime.

## Diferite feature-uri implementate conform constrangerilor din enuntul temei
* **Modelul de Publish/Subscribe** Clientii UDP publica mesaje fara sa le pese clientii TCP. Clientii TCP primesc mesaje doar la topic-urile la care sunt abonati.
* **Primirea de meaje UDP** Serverul asculta pe un singur port mesajele primite de la clientii UDP.
* **Comunicarea cu subscriberii** Subscriberii comunica cu server-ul via TCP.
* **Multiple Client Support** Folsind API-ul de `poll()` putem asculta si da handle la mai multe conexiuni cu mai multi clienti.
* **Filter in functie de topic** Subsciberii specifica doar topicurile de la care sunt interesati sa primeasca mesaje.
* **Store-and-Forward** Subscriberii, teoretic pot avea optiunea sa aiba mesajele stocate pe server pe perioada pe care ei sunt deconectati (acest lucru nu se intampla deoarece nu se cere acest lucru in cerinta, de aceea clientul trimite server-ului flag-ul de 0 pentru store and forward). Mesajele stocate sunt trimise deodata ce clientul se reconecteaza.
* **Matching pe wildcard-uri** Subscriberii pot folosi '+' (single level wildcard) si '*' (multi level wildcard) pentru a specifica topic-uri la care vor sa se aboneze.
* **Multiplexare I/O robusta** Server-ul foloseste `poll()` pentru a manageria I/O si o functie custom `send_all()` pentru a lua in calcul potentialele trimiteri partiale de la `send()` peste TCP. Aceasta functie a fost preluata (cu niste modificari) din laboratoarele de la materia ```Sisteme de Operare```.
* **Buffer circular** Buffer-ul circular este folosit pentru comunicarea intre server si clientii TCP. Deoarece TCP poate fragmenta mesajele (din diverse motive) pentru a transmite informatia, trebuie luat in calcul situatia in care se trimit mai multe mesaje (posibil 0) si un fragment din urmatorul mesaj. Pentru a rezolva aceasta problema, ideea initiala era sa tin un buffer suficient de mare si sa scriu in el ce primesc. Dupa ce am luat tot ce era de luat pe din `send()`, ma apuc sa scot prima comanda transmisa full din buffer apoi o execut si repet pana ce raman cu 0 comenzi sau o comanda incompleta. Daca buffer-ul ar fi un array clasic, stergerea din el ar fi ineficienta, motiv pentru care am ales sa folosesc un circular buffer pentru a nu fi nevoit sa sterg comenzile, doar le voi suprascrie pe masura ce primesc comenzi. Tin sa mentionez ca implementarea buffer-ului circular a fost luata si adaptata pentru C++ din tema 3 de la materia de Sisteme de Operare.
* **Handling de comenzi de la clienti** Clientii pot oricand sa dea exit si sa inchida conexiunea, sa dea subscribe (cu optiunea de Store-and-Forward sau nu) si sa dea unsubscribe. Aceste lucruri se fac de la stdin-ul clientului.
* **Managerierea datelor de la clientii UDP** Server-ul primeste payload-ul de la clientii UDP, il prelucreaza putin pentru a isi da seama care e topic-ul si care e content-ul (are nevoie doar de topic ca sa isi dea seama la care clienti trimite), adauga ip-ul si portul serverului UDP (aceste lucruri sunt necesare pentru ca subscriber-ul sa aiba informatiile necesare pentru logging-ul verificat de checker) intr-un buffer care este forwardat la clientii TCP abonati la acel topic.
* **Management-ul id-urilor subscriber-ilor** Fiecare subscriber are un ID unic. Cand un subscriber se deconecteaza si reconecteaza, el tot ramane abonat la aceleasi topic-uri, server-ul nu le uita.


## Structura
Sistemul consta in 3 parti mari:
1. **Server-ul (`server.cpp`, `server.h`)** Server-ul insusi
2. **Subscriberii (`subscriber.cpp`)**: Aplicatia de TCP
3. **Biblioteci comune (`common.cpp`, `common.h`, `circular_buffer.cpp`, `circular_buffer.h`)**: Cod folosit de ambele, utilitati folosite de server cat si de subscriberi.

### Server-ul
*   **Structuri de date:**
    * **`struct Subscriber`:** Retine id-ul, socket-ul pe care este conectat subscriber-ul, daca este conectat sau nu, un map intre topic-uri si daca s-a facut abonarea cu SF (altfel se putea tine un set de topic-uri daca nu se dorea implementarea Store-and-Forward) si un buffer circular cu toate comenzile.
    * **`struct UdpMessage`:** Contine structura mesajului UDP, asa cum este mentionata in cerinta, precum adresa de unde au fost trimise mesajele de UDP.
    * **SubscribersMap:** Un map intre ID si subscriberi
    * **PollFds:** Un vector de `struct pollfd`
    * **SocketToIdMap:** Map de la socket la ID-ul subscriber-ului.
*   **Initializare:**
        * Serverul primeste din linia de comanda portul pe care va fi deschis.
        * Se foloseste de functia `setup_server_sockets()` pentru a crea si a da bind la un TCP listening socket si un UDP socket pe portul specificat. Se da enable la `SO_REUSEADDR`.
        * Initializeaza un vector de `pollfd` pentru a il trimite mai departe in functia de `poll()` pentru a monitoriza socket-ul care da listen pentru potentiali noi clienti TCP, socket-ul de UDP si stdin.
    **Main Loop(`while(running)`):**
        * Asteapta evenimente de la `poll()`
        * Da handle
            * **STDIN:** Serverul accepta doar o comanda, anume ```exit``` care inchide server-ul (i.e. seteaza `running = false`).
            * **Noi conexiuni TCP:**
                * Le accepta folosind `accept()`
                * Da disable la algoritmul lui Nagle, conform cerintei.
                * Primeste ID-ul clientului
                * Verifica daca exista in SubscribersMap:
                    * In caz afirmativ verifica daca este conectat. Daca da ii da reject si inchide conexiunea noua (conform cerintei temei). Altfel se ocupa de reconexiune prin functia `handle_reconnection`.
                    * Daca nu se afla in SubscribersMap se adauga noul id prin functia `handle_new_client`.
            * **Mesaje UDP:**
                * Se primeste un mesaj de la `recv_from()`.
                * Se parseaza sumar mesajul primit intr-un buffer in structura `struct UdpMessage` prin functia `parse_udp_datagram`.
                * Se serializeaza informatia pentru a fi trimisa clientilor prin functia `serialize_forward_message`.
                * Mesajul este apoi distribuit la toti clientii care au fost abonati la topicul primit in mesaj prin functia `distribute_udp_message`.
            * **Activitatea clientilor:**
                * Se verifica `POLLERR`, `POLLHUP`, `POLLNVAL`, iar in caz afirmativ clientul este deconectat prin functia `handle_client_disconnection`.
                * Daca `POLLIN` este verificat cu succes, executam pe rand comenzile din buffer-ul circular prin functia `process_commands_from_buffer` care apeleaza `parse_and_execute_command`. Aceasta din urma asigura functionalitatea buna pentru comenzile de subscribe si unsubscribe.
            * **Deconectari clienti:**
                * Se inchide socket-ul.
                * Se gaseste subscriber-ul in functie de ID si setam `connected = false`
                * Se reseteaza buffer-ul de comenzi.
                * Se scoate socket-ul din `socket_to_id` map.
                * Se scoate pollfd-ul corespunzator clientului din vector.

### Algoritmul de topic_matches

Algoritmul implementat in functia `topic_m_atches` din `src/server.cpp` verifica daca un string `topic` da match cu un string `pattern`, conform conventiilor din tema.

*   **Topicurile:** String-uri ierarhice separate de `/`.
*   **Patteruri:** Pot contine doar segmente literale, wildcard-uri pe un singur nivel (`+`) si wildcard-uri multi leveled (`*`).
    *   `+`: Match la un singur segment pe acel nivel (e.g., `a/+/c` matches `a/b/c` but not `a/c` or `a/b/d/c`).
    *   `*`: Matches zero sau mai multe segmente la acel nivel si toate nivelurile de dedesubt.

## Implementare folosind programare dinamica

Acest algoritm de programare dinamica pentru matching de pattern-uri a fost facut la cercul de programare competitiva de anul trecut.

Ideea de baza este de a construi solutia din match-uri pentru prefixe de pattern/topic de lungime din ce in ce mai mare.

1.  **Segmentare:** String-urile de `topic` si `pattern` sunt mai intai delimitate in segmente dupa caracterul `/`. Fie `t_segs` segmentele din topic (lungime N) si `p_segs` segmentele din pattern (lungime M).

2.  **Matricea de DP:** Fie `dp` o matrice booleana astfel: `dp[i][j] = true` daca si numai daca primele `i` segmente din topic (`t_segs[0...i-1]`) dau match pe primele `j` segmente din pattern (`p_segs[0...j-1]`).

3.  **Cazurile de baza:**
    *   `dp[0][0] = true`: Un topic vid da match pe un pattern vid.
    *   `dp[i][0] = false` pentru `i > 0`: Un topic nevid nu poate da match pe un pattern vid.
    *   `dp[0][j]` for `j > 0`: Un topic vid poate da match doar pe un pattern prefix doar daca acel prefix este fix `*` (deoarece `*` poate da match pe zero segmente). Deci, `dp[0][j] = true` daca `p_segs[j-1] == '*'` si `dp[0][j-1]`.

4.  **Relatia de recurenta:** Calculam `dp[i][j]` pentru `i > 0` si `j > 0` in functie de tipul celui de-al `j`-lea segment din pattern (`p_segs[j-1]`) si de al `i`-lea segment din topic (`t_segs[i-1]`):

    *   **Daca `p_segs[j-1]` este `+`:**
        `+` trebuie sa dea match la segmentul din topic curent `t_segs[i-1]`. Match-ul depinde daca prefixele de dinaintea acestui segment au dat match.
        `dp[i][j] = dp[i-1][j-1]`

    *   **Daca `p_segs[j-1]` is `*`:**
        Wildcard-ul `*` ofera doua posibilitati:
        a.  Da match la zero segmente la nivelul curent. Match-ul depends atunci de pattern-ul pana la `j-1` a dat match la prefix-ul topicului (`t_segs[0...i-1]`). Asta corespunde lui `dp[i][j-1]`.
        b.  Da match la unul sau mai multe segmente, incluzand segmentul curent din topic `t_segs[i-1]`. Match-ul depinde daca pattern-ul pana la `j` a dat match la prefixul precedent din topic (`t_segs[0...i-2]`). Acest lucru corespunde lui `dp[i-1][j]`.
        Ambele cazuri rezulta intr-un match:
        `dp[i][j] = dp[i][j-1] || dp[i-1][j]`

    *   **Daca `p_segs[j-1]` este un segment literal:**
        Pentru a avea un match, segmentul din pattern trebuie sa coincida cu cel din topic (`p_segs[j-1] == t_segs[i-1]`), si prefixele de dinainte trebuie sa fi dat match.
        `dp[i][j] = (p_segs[j-1] == t_segs[i-1]) && dp[i-1][j-1]`

5.  **Final Result:** Raspunsul final este `dp[N][M]`.

## Optimizarea complexitatii spatiale (Rolling Array)

Un tablou 2D ar avea nevoie de O(N * M) spatiu. Se observa totusi ca atunci cand se calculeaza valorile pentru linia `i` sunt necesare doar valorile de pe linia `i-1` si linia curenta `i`.

Putem astfel aplica optimizarea **Rolling Array**:

*   Doar doua linii de marime `M+1` sunt retinute: `prev_dp` (reprezentand linia `i-1`) si `curr_dp` (reprezentand linia `i`).
*   Cand calculam `curr_dp[j]`, folosim valorile din `prev_dp` si eventual `curr_dp[j-1]`.
*   Dupa ce linia `i` este calculata, `prev_dp` este updatat sa devina `curr_dp` inainte de a trece la linia urmatoare (`i+1`).

Astfel se reduce complexitatea spatiala.

## Complexitate

*   **Complexitate temporala:** O(N * M), unde N este numarul de segmente din topic si M este numarul de segmente din pattern.
*   **Complexitate spatiala:** O(M), unde M este numarul de segmente din pattern.

### Subscriberii
*   **Structuri de date:**
*   **Initializare:**
        * Se parseaza argumentele din linia de comanda prin functia `parse_arguments`.
        * Se conecteaza pe portul de TCP al server-ului prin `setup_and_connect`. Se dezactiveaza algoritmul lui Nagle.
        * Trimite ID-ul serverului prin `send_client_id`.
        * Se initializeaza un vector de `struct pollfd` pentru a monitoriza atat `stdin` cat si raspunsurile primite de la server.
    **Main Loop:**
        * Asteapta un event de la `poll()`.
        * Foloseste un circular buffer pentru a stoca datele de la server.
        * **User Input:**
            * Se citeste o linie de la `stdin`.
            * Comenzile implementate sunt `subscribe`, `unsubscribe`, `exit`.
            * User-ul seteaza Store-and-Forward flag pe 0 in cazul comenzii de subscribe.
            * Se formateaza string-ul comenzii si se trimite catre server prin functia `send_all`.
            * In cazul comenzii de exit, se seteaza `running = false` si se incheie activitatea clientului.
        * **Mesaje de la server:**
            * Se verifica daca s-a primit `POLLERR`, `POLLHUP`, `POLLNVAL`, iar in caz afirmativ clientul se va deconecta.
            * Daca `POLLIN` este available se apleaza `receive_server_data`.
            * Daca `receive_server_data` returneaza ceva util, atunci se apeleaza `deserialize_and_process_message`.
            * `deserialize_and_process_message` citeste cate un mesaj din circular buffer si il transforma in human readable format.


### Biblioteci comune
* **`common.h`, `common.cpp`:**
    * Constante: `BUFFER_SIZE`, `TOPIC_SIZE`, etc.
    * `error()`: Apeleaza `perror` si da exit.
    * `send_all()`: Un wrapper peste `send()` din laboratoarele de SO.
* **`circular_buffer.h`, `circular_buffer.cpp`:**
