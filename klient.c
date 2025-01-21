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
#include "semafory.h"

#define ILOSC_PRODUKTOW 12

struct zakupy {
   int index;
   int ilosc;
};

struct lista_zakupow {
   struct zakupy* elementy;
   int dlugosc;
};

struct komunikacja {
   int index;
   int ilosc;
   char pieczywo[24];
};

struct paragon {
   int ilosc;
   char* produkt;
};

struct kasa {
   int ilosc;
   char produkt[26];
};

int sem_pam;
int pamiec;
int sem_k;
int kolejka;
int dlugosc_kolejki[3] = {0, 99, 99};

int los(int min, int max){
   if (min > max) {
      errno = EINVAL;
      perror("Bledne wartosci funkcji los - min i max odwrocone");
      return (rand() % (min - max + 1)) + max;
   }
   return (rand() % (max - min + 1)) + min;
}

struct lista_zakupow* stworz_liste_zakupow(){
   int ilosc = los(2,5);
   int ile;
   struct lista_zakupow* lista_zakupow = malloc(sizeof(struct lista_zakupow));
   if (!lista_zakupow){
      perror("Blad podczas alokacji pamieci dla listy zakupow");
      exit(EXIT_FAILURE);
   }
   struct zakupy* zakupy = malloc(sizeof(struct zakupy) * ilosc);
   if (!zakupy){
      perror("Blad podczas alokacji pamieci dla zakupy");
      free(lista_zakupow);
      exit(EXIT_FAILURE);
   }
   lista_zakupow->elementy = zakupy;
   lista_zakupow->dlugosc = ilosc;
   int pool[ILOSC_PRODUKTOW];
   for (int i=0; i<ILOSC_PRODUKTOW; i++) pool[i]=i;
   for (int i = ILOSC_PRODUKTOW - 1; i > 0; i--) {
        int j = los(0, i);
        int temp = pool[i];
        pool[i] = pool[j];
        pool[j] = temp;
    }
   for (int i=0; i<ilosc; i++){
      zakupy[i].index = pool[i];
      zakupy[i].ilosc = los(1, 3);
   }
   return lista_zakupow;
}

int wybierz_kase(int* miejsce){
   int min = 0;
   for (int i = 1; i < 3; i++) {
      if (dlugosc_kolejki[i] < dlugosc_kolejki[min]) {
         min = i;
      }
   }
   *miejsce = dlugosc_kolejki[min];
   dlugosc_kolejki[min]++;
   return min;
}

void klient_cleanup(struct komunikacja* przestrzen, struct lista_zakupow* lista, char** koszyk, int ilosc_elementow){
   if (shmdt(przestrzen) == -1) {
      perror("Blad podczas odlaczania pamieci");
   }
   else {
      printf("Pamiec dzielona zostala odlaczona od klienta %d\n", getpid());
   }
   if (lista) {
      if (lista->elementy) {
         free(lista->elementy);
      }
      free(lista);
   }
   if (koszyk) {
      for (int i = 0; i < ilosc_elementow; i++) {
         if (koszyk[i]) {
            free(koszyk[i]);
         }
      }
      free(koszyk);
   }
}

void cleanup(){
   struct shmid_ds shm_info;
   if (shmctl(pamiec, IPC_STAT, &shm_info) != -1 && shm_info.shm_nattch == 0) {
      if (shmctl(pamiec, IPC_RMID, NULL) == -1) {
         perror("Blad podczas usuwania pamieci dzielonej");
      } else {
         printf("Pamiec dzielona zostala usunieta.\n");
      }
   } else {
      perror("Blad podczas uzyskiwania informacji o pamieci dzielonej");
   }
   struct semid_ds sem_info;
   if (semctl(sem_pam, 0, IPC_STAT, &sem_info) == 0) {
      if (sem_info.sem_otime == 0) {
         if (semctl(sem_pam, 0, IPC_RMID) == -1) {
            perror("Blad podczas usuwania semafora");
         } else {
            printf("Semafor usuniety.\n");
         }
      }
   } else {
      perror("Blad podczas uzyskiwania informacji o semaforze");
   }
}

void exit_handler(int sig){
   printf("Sygnal %d - koniec programu\n", sig);
   while (wait(NULL) > 0) {
       if (errno == ECHILD) break;
   }
   cleanup();
   exit(EXIT_SUCCESS);
}


int main(int argc, char** argv){
   signal(SIGINT, exit_handler);
   signal(SIGTERM, exit_handler);
   signal(SIGQUIT, exit_handler);
   srand(time(NULL));
   key_t key_sem = ftok(".", 'P');
   utworz_semafor(&sem_pam, key_sem, 4);
   key_t key_pam = ftok(".", 'T');
   if ((pamiec = shmget(key_pam, 32, 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", pamiec);
   }
   key_t key_semk = ftok(".", 'K');
   utworz_semafor(&sem_k, key_semk, 6);
   key_t key_kol = ftok(".", 'L');
   if ((kolejka = shmget(key_kol, 32, 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", pamiec);
   }
   pid_t pid;
   int czekanie = 5;;
   while (1) {
      sleep(czekanie);
      czekanie = los(5, 100);
      pid = fork();
      switch (pid){
         case -1:
            perror("Blad podczas tworzenia procesu za pomoca fork");
            exit(EXIT_FAILURE);
         case 0:
            //Proces potomny
            printf("Klient %d rozpoczyna dzialanie\n", getpid());
            struct komunikacja* przestrzen = (struct komunikacja*)shmat(pamiec, NULL, 0);
            if (przestrzen == (struct komunikacja*)(-1)){
               perror("Blad podczas przydzielania adresu dla pamieci");
               exit(EXIT_FAILURE);
            }
            else {
               printf("Przestrzen adresowa zostala przyznana dla %d\n", pamiec);
            }
            struct kasa* kasjer = (struct kasa*)shmat(kolejka, NULL, 0);
            if (kasjer == (struct kasa*)(-1)){
               perror("Blad podczas przydzielania adresu dla kolejki");
               exit(EXIT_FAILURE);
            }
            else {
               printf("Przestrzen adresowa zostala przyznana dla %d\n", kolejka);
            }
            struct lista_zakupow* lista = stworz_liste_zakupow();
            int ilosc_elementow = 0;
            printf("Lista zakupow dla klienta %d :\n", getpid());
            for (int i=0; i<lista->dlugosc; i++){
               printf("%d sztuk produktu %d\n", lista->elementy[i].ilosc, lista->elementy[i].index);
               ilosc_elementow = ilosc_elementow + lista->elementy[i].ilosc;
            }
            char** koszyk = malloc(sizeof(char*) * ilosc_elementow);
            if (!koszyk){
               perror("Blad podczas alokacji pamieci dla koszyk");
               klient_cleanup(przestrzen, lista, NULL, 0);
               exit(EXIT_FAILURE);
            }
            int koszyk_wsk = 0;
            int czas = los(3, 10);
            sleep(czas);
            for (int i=0; i<lista->dlugosc; i++){
               while (lista->elementy[i].ilosc > 0) {
                  sem_p(sem_pam, 0);
                  przestrzen->index = lista->elementy[i].index;
                  przestrzen->ilosc = lista->elementy[i].ilosc;
                  sem_v(sem_pam, 1);
                  sem_p(sem_pam, 2);
                  if (przestrzen->pieczywo[0] != '\0'){
                     koszyk[koszyk_wsk] = malloc(strlen(przestrzen->pieczywo) + 1);
                     strcpy(koszyk[koszyk_wsk], przestrzen->pieczywo);
                     if (!koszyk[koszyk_wsk]){
                        perror("Blad alokacji pamieci dla pieczywa klienta");
                        klient_cleanup(przestrzen, lista, koszyk, ilosc_elementow);
                        exit(EXIT_FAILURE);
                     }
                     strcpy(koszyk[koszyk_wsk], przestrzen->pieczywo);
                     printf("Klient %d odebral %s\n", getpid(), koszyk[koszyk_wsk]);
                     koszyk_wsk++;
                     lista->elementy[i].ilosc--;
                  }
                  else {
                     for (int j=0; j<lista->elementy[i].ilosc; j++) {
                        koszyk[koszyk_wsk++] = NULL;
                     }
                     lista->elementy[i].ilosc = 0;
                     printf("Klient %d nie zastal produktu o indeksie %d\n", getpid(), lista->elementy[i].index);
                  }
                  sem_v(sem_pam, 0);
                  sleep(1);
               }
               czas = los(1, 5);
               sleep(czas);
            }
            czas = los(5, 10);
            sleep(czas);
            int miejsce;
            int kasa = wybierz_kase(&miejsce);
            int n = kasa * 5;
            printf("Klient %d staje w kolejce przy kasie %d na miejscu %d\n", getpid(), kasa, miejsce);
            for (int i=0; i<miejsce; i++){ //czekamy az nasza kolej
               sem_p(sem_k, n + 1); //czekamy na ruch kolejki
               sem_v(sem_k, n + 1); //pozwalamy innym sie ruszyc
               sem_p(sem_k, n + 2); //czekamy az wszyscy sie rusza
               sem_v(sem_k, n + 2); //pozwalamy innym sie ruszyc
            }
            sleep(3);
            sem_p(sem_k, n + 2);
            for (int i=0; i<ilosc_elementow; i++){
               if (koszyk[i] != NULL){
                  sem_p(sem_k, 0); //zajmujemy pamiec dzielona
                  //ustawia produkt na pamieci dzielonej
                  sem_v(sem_k, n + 3);
                  sem_p(sem_k, n + 4);
                  //jezeli ostatni element - zabiera paragon
                  sem_v(sem_k, 0);
               }
            }
            sem_v(sem_k, n + 1); //zwalniamy swoje miejsce
            dlugosc_kolejki[kasa]--;
            sleep(1);
            sem_p(sem_k, n + 1);
            sem_v(n + 2);

            klient_cleanup(przestrzen, lista, koszyk, ilosc_elementow);
            exit(EXIT_SUCCESS);
         default:
            break;
      }
   }
}

