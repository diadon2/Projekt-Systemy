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

#define ILOSC_PRODUKTOW 12
#define MAX_KLIENTOW 24

const char* produkty[ILOSC_PRODUKTOW] = {
      "Chleb pszenny", "Chleb zytni", "Chleb razowy", "Chleb wieloziarnisty",
      "Bulka kajzerka", "Bulka grahamka", "Bulka zwykla", "Bagietka",
      "Obwarzanek", "Precel", "Bajgiel", "Croissant"
};

struct st_kasa {
   char produkt[24];
   int cena;
}; //struktura do komunikacji kasy z innymi procesami

struct komunikat {
    long mtype;
    int status;
};

int sem_kier;
int kom_kier;
int sem_kasa;
int pam_kasa;
struct st_kasa* przestrzen_kasa;
int kom_kasa;
int raport[3][ILOSC_PRODUKTOW];
int ceny[ILOSC_PRODUKTOW];
pthread_t kasjerzy[3];
int aktywnosc[3] = {0, 0, 0};
int aktywne_kasy[3] = {0, 0, 0};
int otwarcie = 0;
int inwent = 0;
int running = 1;
int n_val[3] = {0, 1, 2};

int los(int min, int max){
   if (min > max) {
      errno = EINVAL;
      perror("Bledne wartosci funkcji los - min i max odwrocone");
      return (rand() % (min - max + 1)) + max;
   }
   return (rand() % (max - min + 1)) + min;
}

int sprawdz_produkt(char* produkt){
   for (int i = 0; i < 12; i++) {
        if (produkty[i] != NULL && strcmp(produkt, produkty[i]) == 0) {
            return i;
        }
    }
    return -1;
}

void *Kasjer(void* arg){ //watek kasy, przekazuje argument n - index kasy
   int n = *(int*)arg;
   int id_kasa = n;
   n = n * 5;
   char* produkt = NULL;
   int produkt_index = 0;
   aktywnosc[id_kasa] = 1;
   sem_v(sem_kasa, n + 3); //przekazuje sygnal - kasa jest gotowa do dzialania
   printf("Kasa %d dziala\n", id_kasa);
   while (running) {
      sem_p(sem_kasa, n + 4); //czeka na sygnal od klienta
      if (aktywne_kasy[id_kasa] == 0) break; //koniec pracy kasy
      produkt = malloc(strlen(przestrzen_kasa->produkt) + 1);
      if (!produkt){
         perror("Blad podczas alokacji pamieci");
         continue;
      }
      strcpy(produkt, przestrzen_kasa->produkt);
      produkt_index = sprawdz_produkt(produkt); //sprawdza produkt, aby go skasowac
      przestrzen_kasa->cena = ceny[produkt_index]; //wysyla cene produktu
      raport[id_kasa][produkt_index]++;
      sem_v(sem_kasa, n + 5); //powrot do klienta
      free(produkt);
      produkt = NULL;
      if (aktywne_kasy[id_kasa] == 0) break; //koniec pracy kasy
   }
   printf("Kasa %d zamknieta\n", id_kasa);
   aktywnosc[id_kasa] = 0;
   return NULL;
}

void cleanup(){
   if (shmdt(przestrzen_kasa) == -1) {
      perror("Blad podczas odlaczania pamieci dzielonej");
   }
}

void inwentaryzacja(int sig){
   printf("Sygnal %d - inwentaryzacja po zamnkeciu\n", sig);
   inwent = 1;
}

void otwieranie_zamykanie(int sig){
   if (otwarcie == 1) { //zamykanie
      printf("Sygnal %d - zamykanie\n", sig);
      if (inwent == 1) {
         printf("Inwentaryzacja:\n");
         for (int i=0; i<3; i++) {
         printf("Kasa %d:\n", i);
            for (int j=0; j<ILOSC_PRODUKTOW; j++) {
               printf("Ilosc sprzedanych %s: %d\n", produkty[j], raport[i][j]);
            }
         }
         inwent = 0;
      }
      otwarcie = 0;
      //czekamy az klienci ustawieni w kolejce do kasy odejda, na koniec zamykamy ostatnia kase
      return;
   } else { //otwieranie
      printf("Sygnal %d - otwieranie\n", sig);
      for (int i=0; i<3; i++) {
         for (int j=0; j<ILOSC_PRODUKTOW; j++) {
            raport[i][j] = 0;
         }
      }
      aktywne_kasy[0] = 1;
      aktywne_kasy[1] = 0;
      aktywne_kasy[2] = 0;
      otwarcie = 1;
      if (pthread_create(&kasjerzy[0], NULL, Kasjer, &n_val[0]) != 0){
         perror("Blad podczas tworzenia watku kasjera");
         exit(EXIT_FAILURE);
      }
      return;
   }
}

void exit_handler(int sig){
   printf("Sygnal %d - koniec programu\n", sig);
   running = 0;
   for (int i=0; i<3; i++){
      if (aktywnosc[i]) {
         aktywne_kasy[i] = 0;
         sem_v(sem_kasa, (i * 5) + 4);
         pthread_join(kasjerzy[i], NULL);
      }
   }
   cleanup();
   sem_v(sem_kier, 2);
   exit(EXIT_SUCCESS);
}

int main(){
   struct sigaction sa;
   sa.sa_flags = 0;
   sa.sa_handler = exit_handler;
   sigaction(SIGTERM, &sa, NULL);
   sigaction(SIGINT, &sa, NULL);
   sa.sa_handler = inwentaryzacja;
   sigaction(SIGUSR1, &sa, NULL);
   sa.sa_handler = otwieranie_zamykanie;
   sigaction(SIGUSR2, &sa, NULL);

   srand(time(NULL));
   key_t key_sem_kasa = ftok(".", 'K');
   utworz_semafor(&sem_kasa, key_sem_kasa, 15);
   key_t key_kol = ftok(".", 'L');
   if ((pam_kasa = shmget(key_kol, sizeof(struct st_kasa), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", pam_kasa);
   }
   przestrzen_kasa = (struct st_kasa*)shmat(pam_kasa, NULL, 0);
   if (przestrzen_kasa == (struct st_kasa*)(-1)){
      perror("Blad podczas przydzielania adresu dla pamieci");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Przestrzen adresowa zostala przyznana dla %d\n", pam_kasa);
   }
   key_t key_kom_kasa = ftok(".", 'Q');
   kom_kasa = msgget(key_kom_kasa, IPC_CREAT | 0666);
   if (kom_kasa == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      ceny[i] = los(1, 13);
   }
   for (int i=0; i<3; i++) {
      for (int j=0; j<ILOSC_PRODUKTOW; j++) {
         raport[i][j] = 0;
      }
   }
   key_t key_sem_kier = ftok(".", 'R');
   utworz_semafor(&sem_kier, key_sem_kier, 5);
   key_t key_kom_kier = ftok(".", 'M');
   kom_kier = msgget(key_kom_kier, IPC_CREAT | 0666);
   if (kom_kier == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
   sem_p(sem_kier, 2);
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = getpid(); //wiadomosc dla kierownika
   if (msgsnd(kom_kier, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
      perror("Blad przy wysylaniu komunikatu");
      exit(EXIT_FAILURE);
   }
   printf("Program kasjer wyslal komunikat: %d\n", msg.status);

//koniec inicjalizacji
   sem_v(sem_kasa, 0); //otwiera semafor dostepu do pamieci dzielonej kasjer-klient uzywanej w programie klienta
   while (running) { //zbiera informacje o nowych klientach oraz wychodzacych klientach i na ich podstawie otwiera/zamyka kasy
      while(msgrcv(kom_kasa, &msg, sizeof(msg) - sizeof(long), 1, 0) == -1) {
         if (errno == EINTR) continue;
         perror("Blad przy odbieraniu komunikatu");
         exit(EXIT_FAILURE);
      }
      printf("Kasjer: Odebrano komunikat od klienta: %d\n", msg.status);

      switch (msg.status){
         case 2: //otwieramy kase 2
            aktywne_kasy[1] = 1;
            if (pthread_create(&kasjerzy[1], NULL, Kasjer, &n_val[1]) != 0){
               perror("Blad podczas tworzenia watku kasjera");
               exit(EXIT_FAILURE);
            }
            break;
         case 3: //otwieramy kase 3
            aktywne_kasy[2] = 1;
            if (pthread_create(&kasjerzy[2], NULL, Kasjer, &n_val[2]) != 0){
               perror("Blad podczas tworzenia watku kasjera");
               exit(EXIT_FAILURE);
            }
            break;
         case -1: //zamykamy kase 1
            aktywne_kasy[0] = 0;
            sem_v(sem_kasa, 4);
            break;
         case -2: //zamykamy kase 2
            aktywne_kasy[1] = 0;
            sem_v(sem_kasa, 9);
            break;
         case -3: //zamykamy kase 3
            aktywne_kasy[2] = 0;
            sem_v(sem_kasa, 14);
            break;
         default:
      }

      for (int i=0; i<3; i++){ //zamykanie watku kas, jezeli juz sa gotowe do zamkniecia
         if (!(aktywnosc[i])) {
            pthread_join(kasjerzy[i], NULL);
            aktywnosc[i] = 0;
         }
      }
   }
}
