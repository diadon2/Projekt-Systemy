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

int sem_pam;
int pamiec;

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

void klient_cleanup(struct komunikacja* przestrzen){
   if (shmdt(przestrzen) == -1) {
      perror("Blad podczas odlaczania pamieci");
   }
   else {
      printf("Pamiec dzielona zostala odlaczona dla klienta %d\n", getpid());
   }
}

void cleanup(){
   struct shmid_ds shm_info;
   if (shmctl(pamiec, IPC_STAT, &shm_info) == -1) {
      perror("Blad podczas uzyskiwaniu informacji o pamieci dzielonej");
      return;
   }
   if (shm_info.shm_nattch == 0){
      if (shmctl(pamiec, IPC_RMID, NULL) == -1) {
         perror("Blad podczas usuwania pamiec dzielona");
      } else {
         printf("Pamiec dzielona zostala usunieta\n");
      }
   } else {
      printf("Pamiec dzielona jest nadal w uzytku\n");
   }
   usun_semafor(sem_pam);
}

void exit_handler(int sig){
   if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT){
      cleanup();
      exit(EXIT_SUCCESS);
   }
}


int main(int argc, char** argv){
   //signal(SIGINT, exit_handler);
   //signal(SIGTERM, exit_handler);
   //signal(SIGQUIT, exit_handler);
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
   pid_t pid;
   printf("Semaphore before: sem_pam[0]=%d\n", semctl(sem_pam, 0, GETVAL));
   while (1) {
      printf("Semaphore before: sem_pam[0]=%d\n", semctl(sem_pam, 0, GETVAL));
      sleep(3);
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
               free(lista->elementy);
               free(lista);
               exit(EXIT_FAILURE);
            }
            int koszyk_wsk = 0;
            int czas = los(3, 10);
            int ilosc;
            sleep(3);
            for (int i=0; i<lista->dlugosc; i++){
               for (ilosc = lista->elementy[i].ilosc; ilosc>0; ilosc--){
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
                        exit(EXIT_FAILURE);
                     }
                     strcpy(koszyk[koszyk_wsk], przestrzen->pieczywo);
                     printf("Klient %d odebral %s\n", getpid(), koszyk[koszyk_wsk]);
                     koszyk_wsk++;
                  }
                  else {
                     for (int j=0; j<lista->elementy[i].ilosc; j++) {
                        koszyk[koszyk_wsk] = NULL;
                        koszyk_wsk++;
                     }
                     ilosc = 0;
                     printf("Klient %d nie zastal produktu o indeksie %d\n", getpid(), lista->elementy[i].index);
                  }
                  sem_v(sem_pam, 0);
               }
               czas = los(1, 5);
               sleep(1);
            }
            klient_cleanup(przestrzen);
            free(lista->elementy);
            free(lista);
            for (int i=0; i<ilosc_elementow; i++){
               if (koszyk[i]) free(koszyk[i]);
            }
            free(koszyk);
         default:
            break;
      }
   }
}

