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

#define GODZINA_OTWARCIA 2
#define GODZINA_ZAMKNIECIA 22
#define ILOSC_PRODUKTOW 12
int czas = 1200; // czas w minutach
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

struct komunikacja {
   int index;
   int ilosc;
   char pieczywo[24];
}; //struktura do komunikacji podajnikow z innymi procesami

int raport[ILOSC_PRODUKTOW];
int id_komunikat;
int sem_kier;
int sem_pam;
int pamiec;
struct komunikacja* przestrzen = NULL;
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
   otwarcie = 0;
   for (int i=0; i<ilosc_klientow; i++) kill(pid_klientow[i], SIGUSR2);
   kill(pid_piekarz, SIGUSR2);
   kill(pid_kasjer, SIGUSR2);
   kill(pid_klient, SIGUSR2);
   if (inwent) {
      int i = 0;
      while (i < ILOSC_PRODUKTOW) {
         sem_p(sem_pam, 0);
         przestrzen->index = i;
         sem_v(sem_pam, 1);
         sem_p(sem_pam, 2);
         if (przestrzen->pieczywo[0] != '\0'){
            raport[i]++;
         } else {
            i++;
         }
         sem_v(sem_pam, 0);
      }
      printf("Inwentaryzacja:\n");
      for (i=0; i<ILOSC_PRODUKTOW; i++){
         printf("Ilosc %s w podajnikach: %d\n", produkty[i], raport[i]);
      }
   }
   sem_v(sem_kier, 1);
}

void otworz_piekarnie() {
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
   if (msgctl(id_komunikat, 0, IPC_RMID) == -1) {
      perror("Blad podczas usuwania kolejki komunikatow");
   } else {
      printf("Kolejka komunikatow usunieta\n");
   }
   if (semctl(sem_pam, 0, IPC_RMID) == -1) {
      perror("Blad podczas usuwania semafora");
   } else {
      printf("Semafor usuniety\n");
   }
   if (shmdt(przestrzen) == -1) {
      perror("Blad podczas odlaczania pamieci");
   }
   else {
      printf("Pamiec dzielona zostala odlaczona od klienta %d\n", getpid());
   }
   if (shmctl(pamiec, IPC_RMID, NULL) == -1) {
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
   struct komunikat wiad;
   wiad.mtype = 1;
   wiad.status = 0;
   if (msgsnd(id_komunikat, &wiad, sizeof(wiad) - sizeof(long), 0) == -1){
      perror("Blad przy wysylaniu komunikatu");
      exit(EXIT_FAILURE);
   }
   pthread_join(sym_czas, NULL);
   pthread_join(zbieranie_pid, NULL);
   cleanup();
   exit(EXIT_SUCCESS);
}

void* zbieranie_pid_klientow(void* arg){
   ilosc_klientow = 0;
   pid_klientow = NULL;
   struct komunikat wiad;
   int last;
   int* realloc_ptr;
   while (run) {
      sem_v(sem_kier, 0);
      if (msgrcv(id_komunikat, &wiad, sizeof(wiad) - sizeof(long), 1, 0) == -1) {
         perror("Blad przy odbieraniu komunikatu");
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
   }
   return NULL;
}

int godzina(){
   return czas/60;
}

int main() {
   signal(SIGINT, exit_handler);
   signal(SIGTERM, exit_handler);

   key_t key_semkier = ftok(".", 'R');
   utworz_semafor(&sem_kier, key_semkier, 5);
   key_t key_mkom = ftok(".", 'M');
   id_komunikat = msgget(key_mkom, IPC_CREAT | 0666);
   if (id_komunikat == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      raport[i] = 0;
   }
   key_t key_pam = ftok(".", 'T');
   if ((pamiec = shmget(key_pam, sizeof(struct komunikacja), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", pamiec);
   }
   przestrzen = (struct komunikacja*)shmat(pamiec, NULL, 0);
   if (przestrzen == (struct komunikacja*)(-1)){
      perror("Blad podczas przydzielania adresu dla pamieci");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Przestrzen adresowa zostala przyznana dla %d\n", pamiec);
   }
   key_t key_sem = ftok(".", 'P');
   utworz_semafor(&sem_pam, key_sem, 4);

//koniec inicjalizacji
   struct komunikat wiad;
   sem_v(sem_kier, 1); //zbiera pid piekarza
   if (msgrcv(id_komunikat, &wiad, sizeof(wiad) - sizeof(long), 1, 0) == -1) {
      perror("Blad przy odbieraniu komunikatu");
      exit(EXIT_FAILURE);
   }
   printf("Kierownik: Odebrano komunikat od piekarza: %d\n", wiad.status);
   pid_piekarz = wiad.status;
   sem_v(sem_kier, 2); //zbiera pid kasjera
   if (msgrcv(id_komunikat, &wiad, sizeof(wiad) - sizeof(long), 1, 0) == -1) {
      perror("Blad przy odbieraniu komunikatu");
      exit(EXIT_FAILURE);
   }
   printf("Kierownik: Odebrano komunikat od kasjera: %d\n", wiad.status);
   pid_kasjer = wiad.status;
   sem_v(sem_kier, 3); //zbiera pid klienta (proces macierzysty)
   if (msgrcv(id_komunikat, &wiad, sizeof(wiad) - sizeof(long), 1, 0) == -1) {
      perror("Blad przy odbieraniu komunikatu");
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

