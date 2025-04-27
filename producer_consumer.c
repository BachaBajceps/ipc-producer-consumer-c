#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

#define ROZMIAR_SHM 1032

// Struktura komunikatu w kolejce
typedef struct {
    long typ;
    char dane[ROZMIAR_SHM];
} komunikat;

// Zmienne globalne
int id_shm;
int id_kolejki;
int id_kolejki_sygnalow;
int id_semafora_1;
int id_semafora_2;
int czy_dziala = 1;
int czy_z_pliku = 0;
int czy_zakonczono = 0;
pid_t pid_1, pid_2, pid_3;
struct sembuf operacja_p = {0, -1, 0};  // Operacja "czekaj": sem_num=0, sem_op=-1 (opusc), sem_flg=0
struct sembuf operacja_v = {0, 1, 0};   // Operacja "sygnalizuj": sem_num=0, sem_op=1 (podnies), sem_flg=0

// Deklaracje funkcji
void posprzataj();
pid_t utworz_proces(int numer_procesu);
void proces_1();
void proces_2();
void proces_3();
void handler_1(int numer_sygnalu, siginfo_t *informacje, void *kontekst);
void handler_2(int numer_sygnalu, siginfo_t *informacje, void *kontekst);
void handler_3(int numer_sygnalu, siginfo_t *informacje, void *kontekst);
void wyslij_komunikat_do_procesu(pid_t pid_procesu_1, pid_t pid_procesu_2, int sygnal);
void odczyt_sygnal(const siginfo_t *info);
int czy_kon_wpro(const char *bufor, ssize_t odczytane);
void stw_pam_wspol(int id_pam_wspol);
void stw_kolejke(int id_kolejki_param, const char *nazwa_kolejki);
void stw_semafor(int id_semafora, const char *nazwa_semafora);
void init_semafor(int rezultat, const char *nazwa_semafora);
void op_semafor(int semid, struct sembuf *sops, size_t nsops);
void wyslij_komunikat(int msqid, const void *msgp, size_t msgsz, int msgflg);
void odbierz_komunikat(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
void odl_pam_wspol(int rezultat);

// Funkcja do sprzatania
void posprzataj() {
    shmctl(id_shm, IPC_RMID, NULL);
    msgctl(id_kolejki, IPC_RMID, NULL);
    msgctl(id_kolejki_sygnalow, IPC_RMID, NULL);
    semctl(id_semafora_1, 0, IPC_RMID);
    semctl(id_semafora_2, 0, IPC_RMID);
}

// Funkcja do tworzenia procesu
pid_t utworz_proces(int numer_procesu) {
    pid_t pid = fork();
    if (pid == 0) {
        switch (numer_procesu) {
            case 1:
                proces_1();
                printf("Wychodzi 1 ");
                break;
            case 2:
                proces_2();
                printf("Wychodzi 2 ");
                break;
            case 3:
                proces_3();
                printf("Wychodzi 3 ");
                break;
        }
        exit(EXIT_SUCCESS);
    } else if (pid < 0) {
        perror("Blad podczas tworzenia procesu");
        exit(EXIT_FAILURE);
    }
    return pid;
}


void odczytaj_pid_procesow(int proces_num) {
    komunikat msg;
    int otrzymany_pid;

    memset(&msg, 0, sizeof(komunikat));
    switch (proces_num) {
        case 1:
            pid_1 = getpid();
            // typ 4 oznacza pid_2
            odbierz_komunikat(id_kolejki, &msg, sizeof(msg.dane), 4, 0);
            memcpy(&otrzymany_pid, msg.dane, sizeof(int));
            pid_2 = otrzymany_pid;
            //printf("Proces 1 otrzymal pid_2: %d\n", pid_2);

            // typ 5 oznacza pid_3
            odbierz_komunikat(id_kolejki, &msg, sizeof(msg.dane), 5, 0);
            memcpy(&otrzymany_pid, msg.dane, sizeof(int));
            pid_3 = otrzymany_pid;
            //printf("Proces 1 otrzymal pid_3: %d\n", pid_3);
            break;

        case 2:
            // typ 5 oznacza pid_3
            pid_2 = getpid();
            odbierz_komunikat(id_kolejki, &msg, sizeof(msg.dane), 5, 0);
            memcpy(&otrzymany_pid, msg.dane, sizeof(int));
            pid_3 = otrzymany_pid;
            //printf("Proces 2 otrzymal pid_3: %d\n", pid_3);
            break;

        case 3:
            // typ 4 oznacza pid_2
            msg.typ = 4;
            memcpy(msg.dane, &pid_2, sizeof(int));
            wyslij_komunikat(id_kolejki, &msg, sizeof(msg.dane), 0);
            //printf("Proces 3 wyslal pid_2: %d\n", pid_2);

            // typ 5 oznacza pid_3
            msg.typ = 5;
            pid_3 = getpid();
            memcpy(msg.dane, &pid_3, sizeof(int));
            wyslij_komunikat(id_kolejki, &msg, sizeof(msg.dane), 0);
            wyslij_komunikat(id_kolejki, &msg, sizeof(msg.dane), 0);
            //printf("Proces 3 wyslal pid_3: %d\n", pid_3);
            break;
    }
}

// Proces 1
void proces_1() {
    struct sigaction akcja_sygnalu;
    akcja_sygnalu.sa_sigaction = handler_1;
    akcja_sygnalu.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&akcja_sygnalu.sa_mask);
    sigaction(SIGUSR2, &akcja_sygnalu, NULL);
    sigaction(SIGINT, &akcja_sygnalu, NULL);
    sigaction(SIGCONT, &akcja_sygnalu, NULL);
    sigaction(SIGUSR1, &akcja_sygnalu, NULL);

    odczytaj_pid_procesow(1);
    char bufor[ROZMIAR_SHM];
    ssize_t odczytane;

    // Uzyskanie dostepu do pamieci wspoldzielonej
    char *shm_ptr = shmat(id_shm, NULL, 0);
    if (shm_ptr == (char*) - 1) {
        perror("Blad podczas dolaczania pamieci wspoldzielonej");
        exit(EXIT_FAILURE);
    }

    while (!czy_zakonczono) {
         if (!czy_dziala) {
            pause();
        }
        // Oczekiwanie na semafor 1
        op_semafor(id_semafora_1, &operacja_p, 1);

        // Czytanie danych ze standardowego wejscia linijka po linijce
        if (!czy_z_pliku) {
            const char* komunikat_wprowadzania = "Wprowadz dane (wpisz 'koniec' aby zakonczyc):";
            write(STDOUT_FILENO, komunikat_wprowadzania, strlen(komunikat_wprowadzania));
        }
        odczytane = read(STDIN_FILENO, bufor + sizeof(size_t), ROZMIAR_SHM - sizeof(size_t));
        if (odczytane == -1) {
            perror("Blad podczas odczytu ze standardowego wejscia");
            exit(EXIT_FAILURE);
        } else if (odczytane == 0) {
            // Osiagnieto koniec pliku
            czy_zakonczono = 1;
        }

        // Sprawdzenie, czy uzytkownik chce zakonczyc wprowadzanie danych
        czy_zakonczono = czy_kon_wpro(bufor,odczytane);

        // Przekazywanie danych do procesu 2 przez pamiec wspoldzielona
        if (czy_zakonczono == 1) {
            strncpy(bufor + sizeof(size_t), "KONIEC_PROGRAMU", 16);
            odczytane = 16;
        }
        size_t dlugosc = (size_t)odczytane;
        memcpy(bufor, &dlugosc, sizeof(size_t));
        memcpy(shm_ptr, bufor, dlugosc + sizeof(size_t));

        shm_ptr[dlugosc + sizeof(size_t)] = '\0';
        //printf("Odczytano %zd bajtow: %.*s\n", odczytane, (int)odczytane, bufor + sizeof(size_t));
        // Zwalnianie semafora 2
        op_semafor(id_semafora_2, &operacja_v, 1);
    }
    // Odlaczenie pamieci wspoldzielonej
    odl_pam_wspol(shmdt(shm_ptr));
}

// Proces 2
void proces_2() {
    struct sigaction akcja_sygnalu;
    akcja_sygnalu.sa_sigaction = handler_2;
    akcja_sygnalu.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&akcja_sygnalu.sa_mask);
    sigaction(SIGUSR2, &akcja_sygnalu, NULL);
    sigaction(SIGINT, &akcja_sygnalu, NULL);
    sigaction(SIGCONT, &akcja_sygnalu, NULL);
    sigaction(SIGUSR1, &akcja_sygnalu, NULL);

    char *shm_ptr = shmat(id_shm, NULL, 0);
    if (shm_ptr == (char *)-1) {
        perror("Blad podczas dolaczania pamieci wspoldzielonej");
        exit(EXIT_FAILURE);
    }

    odczytaj_pid_procesow(2);
    komunikat msg;
    memset(&msg, 0, sizeof(komunikat));
    long unsigned licznik_bajtow = 0;
    char tresc_komunikatu[100];

    while (!czy_zakonczono) {
        if (!czy_dziala) {
            pause();
        }
        // Oczekiwanie na semafor 2
        op_semafor(id_semafora_2, &operacja_p, 1);

        // Odczytanie dlugosci danych z pamieci wspoldzielonej
        size_t odczytane;
        memcpy(&odczytane, shm_ptr, sizeof(size_t));
        licznik_bajtow += odczytane;

        // Sprawdzenie, czy uzytkownik zakonczyl wprowadzanie danych
        if (strcmp(shm_ptr + sizeof(size_t), "KONIEC_PROGRAMU") == 0) {
            czy_zakonczono = 1;
            licznik_bajtow-=16;
            // Wysylanie wiadomosci o typie 2 do procesu 3
            msg.typ = 2;

            snprintf(tresc_komunikatu, sizeof(tresc_komunikatu), "Ostatecznie: %lu\n", licznik_bajtow);
            size_t odczytane_znaki = strlen(tresc_komunikatu);
            memcpy(msg.dane, &odczytane_znaki, sizeof(size_t));
            memcpy(msg.dane + sizeof(size_t), tresc_komunikatu, odczytane_znaki);
            wyslij_komunikat(id_kolejki, &msg, sizeof(size_t) + odczytane_znaki, 0);
            break;
        }

        // Obliczanie liczby odczytanych bajtow i wyswietlanie na standardowym strumieniu diagnostycznym
        fprintf(stderr, "Proces 2: Odczytano %zu bajtow\n", odczytane);

        // Przekazywanie danych do procesu 3 przez kolejke komunikatow
        msg.typ = 1;
        memcpy(msg.dane, &odczytane, sizeof(size_t));
        memcpy(msg.dane + sizeof(size_t), shm_ptr + sizeof(size_t), odczytane);
        wyslij_komunikat(id_kolejki, &msg, sizeof(size_t) + odczytane, 0);

        // Wyczysc pamiec wspoldzielona
        memset(shm_ptr, 0, ROZMIAR_SHM);

        // Czekanie na potwierdzenie odczytu danych przez proces 3
        odbierz_komunikat(id_kolejki, &msg, sizeof(msg.dane), 3, 0);

        // Zwalnianie semafora 1
        op_semafor(id_semafora_1, &operacja_v, 1);
    }
    // Odlaczenie pamieci wspoldzielonej
    odl_pam_wspol(shmdt(shm_ptr));
}

// Proces 3
void proces_3() {
    struct sigaction akcja_sygnalu;
    akcja_sygnalu.sa_sigaction = handler_3;
    akcja_sygnalu.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&akcja_sygnalu.sa_mask);
    sigaction(SIGUSR2, &akcja_sygnalu, NULL);
    sigaction(SIGINT, &akcja_sygnalu, NULL);
    sigaction(SIGCONT, &akcja_sygnalu, NULL);
    sigaction(SIGUSR1, &akcja_sygnalu, NULL);

    odczytaj_pid_procesow(3);
    komunikat msg;
    memset(&msg, 0, sizeof(komunikat));

    while (!czy_zakonczono) {
         if (!czy_dziala) {
            pause();
        }
        // Pobieranie danych z kolejki komunikatow
        odbierz_komunikat(id_kolejki, &msg, sizeof(komunikat) - sizeof(long), -2, 0);

        size_t odczytane;
        memcpy(&odczytane, msg.dane, sizeof(size_t));

        // Umieszczanie danych w standardowym wyjsciu za pomoca write
        ssize_t zapisane = write(STDOUT_FILENO, msg.dane + sizeof(size_t), odczytane);
        if (zapisane == -1) {
            perror("Blad podczas zapisu na standardowe wyjscie");
            exit(EXIT_FAILURE);
        }
        if (msg.typ == 2) {
            // Wyswietlenie danych za pomoca write
            czy_zakonczono = 1;
            if (czy_z_pliku)
                posprzataj();
            break;
        }

        // Wysylanie potwierdzenia odczytu danych do procesu 2
        msg.typ = 3;
        wyslij_komunikat(id_kolejki, &msg, sizeof(msg.dane), 0);
    }
    usleep(100000);
    posprzataj();
}

// Handler dla procesu 1
void handler_1(int numer_sygnalu, siginfo_t *informacje, void *kontekst) {
    switch (numer_sygnalu) {
        case SIGUSR2:
            // Wyslij komunikat do procesow 2 i 3 o zakonczeniu dzialania
            wyslij_komunikat_do_procesu(pid_2, pid_3, SIGUSR2);
            
            // Zakoncz dzialanie
            czy_zakonczono = 1;
            break;
        case SIGINT:
            // Wyslij komunikat do procesow 2 i 3 o przerwaniu dzialania
            wyslij_komunikat_do_procesu(pid_2, pid_3, SIGINT);

            // Przerwij dzialanie
            czy_dziala = 0;
            break;
        case SIGCONT:
            // Wyslij komunikat do procesow 2 i 3 o kontynuowaniu dzialania
            wyslij_komunikat_do_procesu(pid_2, pid_3, SIGCONT);
            
            // Kontynuuj dzialanie
            czy_dziala = 1;
            break;
        case SIGUSR1:
            odczyt_sygnal(informacje);
            break;
    }
}

// Handler dla procesu 2
void handler_2(int numer_sygnalu, siginfo_t *informacje, void *kontekst) {
    switch (numer_sygnalu) {
        case SIGUSR2:
            // Zakoncz dzialanie
            czy_zakonczono = 1;

            // Wyslij komunikat do procesow 1 i 3 o zakonczeniu dzialania
            wyslij_komunikat_do_procesu(pid_1, pid_3, SIGUSR2);
            break;
        case SIGINT:
            // Przerwij dzialanie
            czy_dziala = 0;

            // Wyslij komunikat do procesow 1 i 3 o przerwaniu dzialania
            wyslij_komunikat_do_procesu(pid_1, pid_3, SIGINT);
            break;
        case SIGCONT:
            // Kontynuuj dzialanie
            czy_dziala = 1;

            // Wyslij komunikat do procesow 1 i 3 o kontynuowaniu dzialania
            wyslij_komunikat_do_procesu(pid_1, pid_3, SIGCONT);
            break;
        case SIGUSR1:
            odczyt_sygnal(informacje);
            break;
    }
}

// Handler dla procesu 3
void handler_3(int numer_sygnalu, siginfo_t *informacje, void *kontekst) {
    switch (numer_sygnalu) {
        case SIGUSR2:
            // Zakoncz dzialanie
            czy_zakonczono = 1;

            // Wyslij komunikat do procesow 1 i 2 o zakonczeniu dzialania
            wyslij_komunikat_do_procesu(pid_1, pid_2, SIGUSR2);
            break;
        case SIGINT:
            // Przerwij dzialanie
            czy_dziala = 0;

            // Wyslij komunikat do procesow 1 i 2 o przerwaniu dzialania
            wyslij_komunikat_do_procesu(pid_1, pid_2, SIGINT);
            break;
        case SIGCONT:
            // Kontynuuj dzialanie
            czy_dziala = 1;

            // Wyslij komunikat do procesow 1 i 2 o kontynuowaniu dzialania
            wyslij_komunikat_do_procesu(pid_1, pid_2, SIGCONT);
            break;
        case SIGUSR1:
            odczyt_sygnal(informacje);
            break;
    }
}

// Funkcja do wysylania komunikatu do dwoch procesow
void wyslij_komunikat_do_procesu(pid_t pid_procesu_1, pid_t pid_procesu_2, int sygnal) {
    komunikat msg;

    // Wyslanie komunikatu do procesu 1
    msg.typ = pid_procesu_1;
    memcpy(msg.dane, &sygnal, sizeof(int));
    wyslij_komunikat(id_kolejki_sygnalow, &msg, sizeof(int), 0);
    kill(pid_procesu_1, SIGUSR1);

    // Wyslanie komunikatu do procesu 2
    msg.typ = pid_procesu_2;
    memcpy(msg.dane, &sygnal, sizeof(int));
    wyslij_komunikat(id_kolejki_sygnalow, &msg, sizeof(int), 0);
    kill(pid_procesu_2, SIGUSR1);
}

void odczyt_sygnal(const siginfo_t *info) {
    // Odczytaj tylko jesli sygnal nastapil od p1, p2 lub p3
    if (info->si_pid == pid_1 || info->si_pid == pid_2 || info->si_pid == pid_3) {
        // Odczytaj z kolejki
        komunikat msg;
        odbierz_komunikat(id_kolejki_sygnalow, &msg, sizeof(int), getpid(), 0);

        int odebrany_sygnal;
        memcpy(&odebrany_sygnal, msg.dane, sizeof(int));

        switch (odebrany_sygnal) {
            case SIGUSR2:
                // Zakoncz dzialanie
                czy_zakonczono = 1;
                break;
            case SIGINT:
                // Przerwij dzialanie
                czy_dziala = 0;
                break;
            case SIGCONT:
                // Kontynuuj dzialanie
                czy_dziala = 1;
                break;
        }
    }
}

int czy_kon_wpro(const char *bufor, ssize_t odczytane) {
    if (odczytane >= 7 && !czy_z_pliku) {
        if ((memcmp(bufor + sizeof(size_t), "koniec\n", 7) == 0) ||
            (memcmp(bufor + sizeof(size_t), "\nkoniec", 7) == 0) ||
            (memcmp(bufor + sizeof(size_t), "\nkoniec\n", 8) == 0)) {
            return 1;
        }
    }
    return czy_zakonczono;
}

// Funkcje do sprawdzania bledow
void stw_pam_wspol(int id_pam_wspol) {
    if (id_pam_wspol == -1) {
        fprintf(stderr, "PID:(%d) ", getpid());
        perror("Blad podczas tworzenia pamieci wspoldzielonej");
        exit(EXIT_FAILURE);
    }
}

void op_semafor(int semid, struct sembuf *sops, size_t nsops) {
    while (semop(semid, sops, nsops) == -1) {
        if (errno == EINTR) {
            // Powtorz operacje, jesli zostala przerwana
            continue;
        }
        fprintf(stderr, "PID:(%d) ", getpid());
        perror("Blad podczas operacji na semaforze");
        exit(EXIT_FAILURE);
    }
}

void stw_kolejke(int id_kolejki_param, const char *nazwa_kolejki) {
    if (id_kolejki_param == -1) {
        fprintf(stderr, "PID:(%d) ", getpid());
        perror(nazwa_kolejki);
        exit(EXIT_FAILURE);
    }
}

void stw_semafor(int id_semafora, const char *nazwa_semafora) {
    if (id_semafora == -1) {
        fprintf(stderr, "PID:(%d) ", getpid());
        perror(nazwa_semafora);
        exit(EXIT_FAILURE);
    }
}

void init_semafor(int rezultat, const char *nazwa_semafora) {
    if (rezultat == -1) {
        fprintf(stderr, "PID:(%d) ", getpid());
        perror(nazwa_semafora);
        exit(EXIT_FAILURE);
    }
}

void wyslij_komunikat(int msqid, const void *msgp, size_t msgsz, int msgflg) {
    while (msgsnd(msqid, msgp, msgsz, msgflg) == -1) {
        if (errno == EINTR) {
            // Powtorz operacje, jesli zostala przerwana
            continue;
        }
        fprintf(stderr, "PID:(%d) ", getpid());
        perror("Blad podczas wysylania komunikatu");
        exit(EXIT_FAILURE);
    }
}

void odbierz_komunikat(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg) {
    while (msgrcv(msqid, msgp, msgsz, msgtyp, msgflg) == -1) {
        if (errno == EINTR) {
            // Powtorz operacje, jesli zostala przerwana
            continue;
        }
        fprintf(stderr, "PID:(%d) ", getpid());
        perror("Blad podczas odbierania komunikatu");
        exit(EXIT_FAILURE);
    }
}

void odl_pam_wspol(int rezultat) {
    if (rezultat == -1) {
        fprintf(stderr, "PID:(%d) ", getpid());
        perror("Blad podczas odlaczania pamieci wspoldzielonej");
        exit(EXIT_FAILURE);
    }
}

int main() {
    // Tworzenie pamieci wspoldzielonej i kolejki komunikatow
    id_shm = shmget(IPC_PRIVATE, ROZMIAR_SHM, IPC_CREAT | IPC_EXCL | 0666);
    stw_pam_wspol(id_shm);

    id_kolejki = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    stw_kolejke(id_kolejki, "Blad podczas tworzenia kolejki komunikatow");

    id_kolejki_sygnalow = msgget(IPC_PRIVATE,  IPC_CREAT | IPC_EXCL | 0666);
    stw_kolejke(id_kolejki_sygnalow, "Blad podczas tworzenia kolejki komunikatow dla sygnalow");

    // Tworzenie semaforow
    id_semafora_1 = semget(IPC_PRIVATE, 1, IPC_CREAT | IPC_EXCL | 0666);
    stw_semafor(id_semafora_1, "Blad podczas tworzenia semafora 1");

    id_semafora_2 = semget(IPC_PRIVATE, 1, IPC_CREAT | IPC_EXCL | 0666);
    stw_semafor(id_semafora_2, "Blad podczas tworzenia semafora 2");

    // Inicjalizacja semaforow
    init_semafor(semctl(id_semafora_1, 0, SETVAL, 1), "Blad podczas inicjalizacji semafora 1");
    init_semafor(semctl(id_semafora_2, 0, SETVAL, 0), "Blad podczas inicjalizacji semafora 2");

    // Tworzenie maski sygnalow
    sigset_t maska_sygnalow;
    sigemptyset(&maska_sygnalow);
    sigaddset(&maska_sygnalow, SIGHUP);
    sigaddset(&maska_sygnalow, SIGTERM);
    sigaddset(&maska_sygnalow, SIGTSTP);
    sigaddset(&maska_sygnalow, SIGTTIN);
    sigaddset(&maska_sygnalow, SIGTTOU);
    sigaddset(&maska_sygnalow, SIGQUIT);

    // Maskowanie sygnalow
    if (sigprocmask(SIG_BLOCK, &maska_sygnalow, NULL) == -1) {
        perror("Blad podczas maskowania sygnalow");
        exit(EXIT_FAILURE);
    }

    // Sprawdzenie, czy program otrzymuje dane z pliku przez przekierowanie standardowego wejscia
    czy_z_pliku = !isatty(STDIN_FILENO);

    // Tworzenie procesow potomnych
    pid_1 = utworz_proces(1);
    pid_2 = utworz_proces(2);
    pid_3 = utworz_proces(3);
    printf("pid_1: %d \n ",pid_1);
    printf("pid_2: %d \n ",pid_2);
    printf("pid_3: %d \n ",pid_3);

    //Tryb interaktywny
    if (!czy_z_pliku)
    {
        //Musi czekac poniewaz jesli nie to terminal zabeira kontrle nad stdin
        wait(NULL);
        wait(NULL);
        wait(NULL);
        posprzataj();
    }
    exit(EXIT_SUCCESS);
}