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

struct kasa {
   char produkt[24];
   int cena;
};

struct komunikat {
    long mtype;
    int status;
};

int sem_k;
int kolejka;
struct kasa* kasowanie;
int raport[3][ILOSC_PRODUKTOW];
int ceny[ILOSC_PRODUKTOW];
pthread_t kasjerzy[3];
int ilosc_klientow = 0;
int komid;
int otwarte_kasy = 1;
int aktywne_kasy[3] = {1, 0, 0};
int czekajacy[3] = {1, 0, 0};

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

void *Kasjer(void* arg){
   int n = *(int*)arg;
   n = n * 4;
   char* produkt = NULL;
   int produkt_index = 0;
   sem_v(sem_k, n + 2);
   printf("Kasa %d dziala\n", n/4);
   while (1) {
      sem_p(sem_k, n + 3);
      if ((n != 0) && (!(czekajacy[n])) && (!(aktywne_kasy[n]))) break;
      produkt = malloc(strlen(kasowanie->produkt) + 1);
      strcpy(produkt, kasowanie->produkt);
      produkt_index = sprawdz_produkt(produkt);
      czekajacy[n] = kasowanie->cena;
      kasowanie->cena = ceny[produkt_index];
      raport[n/4][produkt_index]++;
      sem_v(sem_k, n + 4);
      free(produkt);
      produkt = NULL;
      if ((n != 0) && (!(czekajacy[n])) && (!(aktywne_kasy[n]))) break;
   }
   sem_p(sem_k, n);
   printf("Kasa %d zamknieta\n", n/4);
   return NULL;
}

int main(){
   srand(time(NULL));
   key_t key_semk = ftok(".", 'K');
   utworz_semafor(&sem_k, key_semk, 13);
   key_t key_kol = ftok(".", 'L');
   if ((kolejka = shmget(key_kol, sizeof(struct kasa), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", kolejka);
   }
   kasowanie = (struct kasa*)shmat(kolejka, NULL, 0);
   if (kasowanie == (struct kasa*)(-1)){
      perror("Blad podczas przydzielania adresu dla pamieci");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Przestrzen adresowa zostala przyznana dla %d\n", kolejka);
   }
   key_t key_kom = ftok(".", 'Q');
   komid = msgget(key_kom, IPC_CREAT | 0666);
   if (komid == -1) {
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
   int n_val[3] = {0, 1, 2};
   if (pthread_create(&kasjerzy[0], NULL, Kasjer, &n_val[0]) != 0){
      perror("Blad podczas tworzenia watku kasjera");
      exit(EXIT_FAILURE);
   }
   struct komunikat msg;
   sem_v(sem_k, 0);
   while (1) {
      if (msgrcv(komid, &msg, sizeof(msg) - sizeof(long), 1, 0) == -1) {
         perror("Blad przy odbieraniu komunikatu");
         break;
      }
      printf("Kasjer: Odebrano komunikat od klienta: %d\n", msg.status);
      msg.mtype = 2;
      if (msg.status == 1) {
         ilosc_klientow++;
         if (otwarte_kasy == 1 && ilosc_klientow > MAX_KLIENTOW/3) {
            msg.status = 1;
            aktywne_kasy[1] = 1;
            if (pthread_create(&kasjerzy[1], NULL, Kasjer, &n_val[1]) != 0){
               perror("Blad podczas tworzenia watku kasjera");
               exit(EXIT_FAILURE);
            }
            otwarte_kasy = 2;
         } else if (otwarte_kasy == 2 && ilosc_klientow > MAX_KLIENTOW * 2 / 3) {
            msg.status = 2;
            aktywne_kasy[2] = 0;
            if (pthread_create(&kasjerzy[2], NULL, Kasjer, &n_val[2]) != 0){
               perror("Blad podczas tworzenia watku kasjera");
               exit(EXIT_FAILURE);
            }
            otwarte_kasy = 3;
         } else msg.status = 0;
      } else if (msg.status == 0) {
         ilosc_klientow--;
         if (otwarte_kasy == 2 && ilosc_klientow < MAX_KLIENTOW/3) {
            msg.status = 3;
            aktywne_kasy[1] = 0;
            otwarte_kasy = 1;
            if (czekajacy[1] == 0) sem_v(sem_k, 4 + 3);
         } else if (otwarte_kasy == 3 && ilosc_klientow < MAX_KLIENTOW * 2 / 3) {
            msg.status = 4;
            aktywne_kasy[2] = 0;
            otwarte_kasy = 2;
            if (czekajacy[2] == 0) sem_v(sem_k, 8 + 3);
         } else msg.status = 0;
      } else {
         perror("Otrzymano nieznany komunikat");
      }
   if (msgsnd(komid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
      perror("Blad przy wysylaniu odpowiedzi");
   }
   printf("Kasjer wyslal odpowiedz: %d\n", msg.status);
   }
}
