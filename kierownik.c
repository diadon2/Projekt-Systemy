#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include "semafory.h"

int GODZINA_OTWARCIA;
int GODZINA_ZAMKNIECIA;

#define ILOSC_PRODUKTOW 12
int czas = 360; // czas w minutach
//piekarnia dziala w godzinach dziennych - OTWARCIE < ZAMKNIECIE

const char* produkty[ILOSC_PRODUKTOW] = {
      "Chleb pszenny", "Chleb zytni", "Chleb razowy", "Chleb wieloziarnisty",
      "Bulka kajzerka", "Bulka grahamka", "Bulka zwykla", "Bagietka",
      "Obwarzanek", "Precel", "Bajgiel", "Croissant"
};

struct komunikat {
    long mtype;
    int status;
};

struct st_podajnik {
   int index;
   char pieczywo[24];
}; //struktura do komunikacji podajnikow z innymi procesami

int raport[ILOSC_PRODUKTOW];
int kom_kier;
int sem_kier;
int sem_podajnik;
int pam_podajnik;
struct st_podajnik* przestrzen_podajnik = NULL;
pthread_t sym_czas;
pthread_t zbieranie_pid;
int otwarcie = 0;
int run = 1;
int pid_klient;
int pid_kasjer;
int pid_piekarz;
int* pid_klientow;
int ilosc_klientow;
int inwent = 0;

void ewakuacja() {
   printf("Ewakuacja ogloszona: klienci opuszczaja sklep\n");
   for (int i=0; i<ilosc_klientow; i++) kill(pid_klientow[i], SIGUSR1);
}

void inwentaryzacja() {
   printf("Inwentaryzacja ogloszona: klienci kontynuuja zakupy, pracownicy po zamknieciu robia raport\n");
   kill(pid_piekarz, SIGUSR1);
   kill(pid_kasjer, SIGUSR1);
   inwent = 1;
}

void zamknij_piekarnie() {
   printf("Zamykanie\n");
   otwarcie = 0;
   for (int i=0; i<ilosc_klientow; i++) kill(pid_klientow[i], SIGUSR2);
   kill(pid_piekarz, SIGUSR2);
   kill(pid_kasjer, SIGUSR2);
   kill(pid_klient, SIGUSR2);
   if (inwent) { //jezeli zostala ogloszona inwentaryzacja - rozpoczyna komunikacje z podajnikami
      int i = 0;
      while (i < ILOSC_PRODUKTOW) {
         sem_p(sem_podajnik, 0);
         przestrzen_podajnik->index = i;
         sem_v(sem_podajnik, 1);
         sem_p(sem_podajnik, 2);
         if (przestrzen_podajnik->pieczywo[0] != '\0'){
            raport[i]++;
         } else {
            i++;
         }
         sem_v(sem_podajnik, 0);
      }
      printf("Inwentaryzacja:\n");
      for (i=0; i<ILOSC_PRODUKTOW; i++){
         printf("Ilosc %s w podajnikach: %d\n", produkty[i], raport[i]);
      }
   }
   sem_v(sem_kier, 1);
}

void otworz_piekarnie() {
   printf("Otwieranie\n");
   otwarcie = 1;
   kill(pid_piekarz, SIGUSR2);
   kill(pid_kasjer, SIGUSR2);
   kill(pid_klient, SIGUSR2);
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      raport[i] = 0;
   }
}

void cleanup() {
   if (semctl(sem_kier, 0, IPC_RMID) == -1) {
      perror("Blad podczas usuwania semafora");
   } else {
      printf("Semafor usuniety\n");
   }
   if (msgctl(kom_kier, 0, IPC_RMID) == -1) {
      perror("Blad podczas usuwania kolejki komunikatow");
   } else {
      printf("Kolejka komunikatow usunieta\n");
   }
   if (semctl(sem_podajnik, 0, IPC_RMID) == -1) {
      perror("Blad podczas usuwania semafora");
   } else {
      printf("Semafor usuniety\n");
   }
   if (shmdt(przestrzen_podajnik) == -1) {
      perror("Blad podczas odlaczania pamieci");
   }
   else {
      printf("Pamiec dzielona zostala odlaczona\n");
   }
   if (shmctl(pam_podajnik, IPC_RMID, NULL) == -1) {
      perror("Blad podczas usuwania pamieci dzielonej");
   } else {
      printf("Pamiec dzielona zostala usunieta\n");
   }
}

void exit_handler(int sig) {
   printf("Sygnal %d - koniec programu\n", sig);
   kill(pid_piekarz, sig);
   sem_p(sem_kier, 1);
   kill(pid_kasjer, sig);
   sem_p(sem_kier, 2);
   for (int i=0; i<ilosc_klientow; i++) kill(pid_klientow[i], sig);
   kill(pid_klient, sig);
   sem_p(sem_kier, 3);
   run = 0;
   pthread_cancel(sym_czas);
   pthread_cancel(zbieranie_pid);
   pthread_join(sym_czas, NULL);
   pthread_join(zbieranie_pid, NULL);
   cleanup();
   exit(EXIT_SUCCESS);
}

void* zbieranie_pid_klientow(void* arg){ //tworzy tablice dynamiczna z PID klientow - dostaje je za pomoca komunikatu
   ilosc_klientow = 0;
   pid_klientow = NULL;
   struct komunikat wiad;
   int last;
   int* realloc_ptr;
   while (run) { //wartosc dodatnia - pid do dodania, wartosc ujemna - pid do usuniecia
      sem_v(sem_kier, 0);
      while (msgrcv(kom_kier, &wiad, sizeof(wiad) - sizeof(long), 1, 0) == -1) {
         if (errno == EINTR) continue;
         perror("Error receiving message");
         exit(EXIT_FAILURE);
      }
      if (run == 0) break;
      printf("Kierownik: Odebrano komunikat od klienta: %d\n", wiad.status);
      if (wiad.status > 0) { //dodajemy nowy element do tablicy
         ilosc_klientow++;
         realloc_ptr = realloc(pid_klientow, sizeof(int) * ilosc_klientow);
         if (realloc_ptr == NULL) {
            perror("Blad przy alokacji pamieci");
            exit(EXIT_FAILURE);
         }
         pid_klientow = realloc_ptr;
         pid_klientow[ilosc_klientow - 1] = wiad.status;
      } else if (wiad.status < 0) { //usuwamy element z tablicy
         ilosc_klientow--;
         last = pid_klientow[ilosc_klientow];
         if (ilosc_klientow > 0) {
            realloc_ptr = realloc(pid_klientow, sizeof(int) * ilosc_klientow);
            if (realloc_ptr == NULL) {
               perror("Blad przy alokacji pamieci");
               exit(EXIT_FAILURE);
            }
            pid_klientow = realloc_ptr;
            if (last != (-wiad.status)) {
               for (int i=0; i<ilosc_klientow; i++) {
                  if (pid_klientow[i] == (-wiad.status)) {
                     pid_klientow[i] = last;
                     i = ilosc_klientow;
                  }
               }
            }
         } else {
            free(pid_klientow);
            pid_klientow = NULL;
         }
      }
   }
   if (pid_klientow) free(pid_klientow);
   return NULL;
}

void* symulacja_czasu (void* arg){
   int czas_otwarcia = GODZINA_OTWARCIA * 60;
   int czas_zamkniecia = GODZINA_ZAMKNIECIA * 60;
   while (run) {
      sleep(1);
      czas++;
      if (czas < czas_otwarcia || czas > czas_zamkniecia) {
         if (otwarcie) {
            zamknij_piekarnie();
         }
      } else if (!otwarcie) {
         otworz_piekarnie();
      }
      if (czas == 1440) czas = 0; //polnoc
      if (!run) break;
   }
   return NULL;
}

int godzina(){
   return czas/60;
}

int main() {
   struct sigaction sa;
   sa.sa_flags = 0;
   sa.sa_handler = exit_handler;
   sigaction(SIGTERM, &sa, NULL);
   sigaction(SIGINT, &sa, NULL);

   printf("Czas symulacji zaczyna sie o 6:00\n");
   printf("Podaj godzine otwarcia piekarni (0 <= GODZINA_OTWARCIA < 23, default: 8 w przypadku blednego inputu\n");
   scanf("%d", &GODZINA_OTWARCIA);
   if (0 > GODZINA_OTWARCIA && 23 < GODZINA_OTWARCIA) {
      printf("Bledna wartosc GODZINA_OTWARCIA - ustawianie na default\n");
      GODZINA_OTWARCIA = 8;
   }
   printf("Podaj godzine zamkniecia piekarni (GODZINA_OTWARCIA < GODZINA_ZAMKNIECIA < 24, default: 23 w przypadku blednego inputu\n");
   scanf("%d", &GODZINA_ZAMKNIECIA);
   if (GODZINA_OTWARCIA >= GODZINA_ZAMKNIECIA && 24 < GODZINA_ZAMKNIECIA) {
      printf("Bledna wartosc GODZINA_ZAMKNIECIA - ustawianie na default\n");
      GODZINA_ZAMKNIECIA = 23;
   }

   key_t key_sem_kier = ftok(".", 'R');
   utworz_semafor(&sem_kier, key_sem_kier, 5);
   key_t key_kom_kier = ftok(".", 'M');
   kom_kier = msgget(key_kom_kier, IPC_CREAT | 0666);
   if (kom_kier == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      raport[i] = 0;
   }
   key_t key_pam_podajnik = ftok(".", 'T');
   if ((pam_podajnik = shmget(key_pam_podajnik, sizeof(struct st_podajnik), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", pam_podajnik);
   }
   przestrzen_podajnik = (struct st_podajnik*)shmat(pam_podajnik, NULL, 0);
   if (przestrzen_podajnik == (struct st_podajnik*)(-1)){
      perror("Blad podczas przydzielania adresu dla pamieci");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Przestrzen adresowa zostala przyznana dla %d\n", pam_podajnik);
   }
   key_t key_sem_podajnik = ftok(".", 'P');
   utworz_semafor(&sem_podajnik, key_sem_podajnik, 4);

//koniec inicjalizacji
   struct komunikat wiad;
   sem_v(sem_kier, 1); //zbiera pid piekarza
   while (msgrcv(kom_kier, &wiad, sizeof(wiad) - sizeof(long), 1, 0) == -1) {
      if (errno == EINTR) continue;
      perror("Error receiving message");
      exit(EXIT_FAILURE);
   }
   printf("Kierownik: Odebrano komunikat od piekarza: %d\n", wiad.status);
   pid_piekarz = wiad.status;
   sem_v(sem_kier, 2); //zbiera pid kasjera
   while (msgrcv(kom_kier, &wiad, sizeof(wiad) - sizeof(long), 1, 0) == -1) {
      if (errno == EINTR) continue;
      perror("Error receiving message");
      exit(EXIT_FAILURE);
   }
   printf("Kierownik: Odebrano komunikat od kasjera: %d\n", wiad.status);
   pid_kasjer = wiad.status;
   sem_v(sem_kier, 3); //zbiera pid klienta (proces macierzysty)
   while (msgrcv(kom_kier, &wiad, sizeof(wiad) - sizeof(long), 1, 0) == -1) {
      if (errno == EINTR) continue;
      perror("Error receiving message");
      exit(EXIT_FAILURE);
   }
   printf("Kierownik: Odebrano komunikat od klienta: %d\n", wiad.status);
   pid_klient = wiad.status;

   if (pthread_create(&sym_czas, NULL, symulacja_czasu, NULL) != 0){
      perror("Blad podczas tworzenia watku symulacja czasu");
      exit(EXIT_FAILURE);
   }
   if (pthread_create(&zbieranie_pid, NULL, zbieranie_pid_klientow, NULL) != 0){
      perror("Blad podczas tworzenia watku zbieranie pid klientow");
      exit(EXIT_FAILURE);
   }
   int wybor;
   while (1) {
      printf("\nZegarek: %02d:%02d\t\tGodzina otwarcia: %d, Godzina zamkniecia: %d\n", godzina(), czas % 60, GODZINA_OTWARCIA, GODZINA_ZAMKNIECIA);
      printf("Sygnaly:\n");
      printf("1. Ewakuacja\n");
      printf("2. Inwentaryzację\n");
      printf("3. Aktualizuj zegarek\n");
      printf("4. Koniec\n");
      scanf("%d", &wybor);
      switch (wybor) {
         case 1:
            ewakuacja();
            break;
         case 2:
            inwentaryzacja();
            break;
         case 3:
            break;
         case 4:
            exit_handler(SIGINT);
         default:
            printf("Nieznana opcja\n");
      }
   }
}

