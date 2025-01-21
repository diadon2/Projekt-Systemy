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
   int cena;
   char* produkt;
};

struct kasa {
   char produkt[24];
   int cena;
};

struct komunikat {
    long mtype;
    int status;
};

int sem_pam;
int pamiec;
int sem_k;
int kolejka;
int komid;
int shared;

struct info {
   int dlugosc_kolejki[3];
   int czekajacy[3];
   int liczba_klientow;
} *info;

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
   sem_p(sem_pam, 3);
   int min = 0;
   for (int i = 1; i < 3; i++) {
      if (info->dlugosc_kolejki[i] < info->dlugosc_kolejki[min]) {
         min = i;
      }
   }
   *miejsce = info->dlugosc_kolejki[min];
   info->dlugosc_kolejki[min]++;
   sem_v(sem_pam, 3);
   return min;
}

void klient_cleanup(struct komunikacja* przestrzen, struct lista_zakupow* lista, char** koszyk, int ilosc_elementow,
                    struct kasa* kasjer, struct paragon* paragon, int ilosc_paragon){
   if (shmdt(przestrzen) == -1) {
      perror("Blad podczas odlaczania pamieci");
   }
   else {
      printf("Pamiec dzielona zostala odlaczona od klienta %d\n", getpid());
   }
   if (shmdt(kasjer) == -1) {
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
   if (paragon){
      for (int i=0; i<ilosc_paragon; i++) {
         if (paragon[i].produkt) free(paragon[i].produkt);
      }
      free(paragon);
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
   if ((pamiec = shmget(key_pam, sizeof(struct komunikacja), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", pamiec);
   }
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
   key_t key_kom = ftok(".", 'Q');
   komid = msgget(key_kom, IPC_CREAT | 0666);
   if (komid == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
   pid_t pid;
   int czekanie = 5;
   struct komunikat msg;
   key_t key_s = ftok(".", 'S');
   if ((shared = shmget(key_s, sizeof(struct komunikacja), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", shared);
   }
   info = (struct info*) shmat(shared, NULL, 0);
   if (info == (struct info*) -1) {
      perror("Blad podczas przydzielania adresu dla pamieci wspoldzielonej");
      exit(EXIT_FAILURE);
   }

   info->dlugosc_kolejki[0] = 0;
   info->dlugosc_kolejki[1] = 99;
   info->dlugosc_kolejki[2] = 99;
   info->czekajacy[0] = 0;
   info->czekajacy[1] = 0;
   info->czekajacy[2] = 0;
   info->liczba_klientow = 0;
   sem_v(sem_pam, 3);
   while (1) {
      sleep(czekanie);
      czekanie = los(5, 15);
      msg.mtype = 1;
      msg.status = 1;
      if (msgsnd(komid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
         perror("Blad przy wysylaniu komunikatu");
         exit(EXIT_FAILURE);
      }
      printf("Program klient wyslal komunikat: %d\n", msg.status);
      if (msgrcv(komid, &msg, sizeof(msg) - sizeof(long), 2, 0) == -1) {
         perror("Blad przy odbieraniu odpowiedzi");
         exit(EXIT_FAILURE);
      }
      switch (msg.status) {
         case 1:
            sem_p(sem_pam, 3);
            info->dlugosc_kolejki[1] = (info->dlugosc_kolejki[1] == 99 ? 0 : info->dlugosc_kolejki[1]);
            sem_v(sem_pam, 3);
            break;
         case 2:
            sem_p(sem_pam, 3);
            info->dlugosc_kolejki[2] = (info->dlugosc_kolejki[2] == 99 ? 0 : info->dlugosc_kolejki[2]);
            sem_v(sem_pam, 3);
            break;
         case 3:
            sem_p(sem_pam, 3);
            info->dlugosc_kolejki[1] = 99;
            sem_v(sem_pam, 3);
            break;
         case 4:
            sem_p(sem_pam, 3);
            info->dlugosc_kolejki[2] = 99;
            sem_v(sem_pam, 3);
            break;
         default:
      }
      printf("Klient: Odebrano komunikat od klienta: %d\n", msg.status);
      pid = fork();
      switch (pid){
         case -1:
            perror("Blad podczas tworzenia procesu za pomoca fork");
            exit(EXIT_FAILURE);
         case 0:
            //Proces potomny
            while (1) {
               sem_p(sem_pam, 3);
               if (info->liczba_klientow < MAX_KLIENTOW) {
                  sem_v(sem_pam, 3);
                  break;
               }
               sem_v(sem_pam, 3);
            }
            sem_p(sem_pam, 3);
            info->liczba_klientow++;
            sem_v(sem_pam, 3);
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
               klient_cleanup(przestrzen, lista, NULL, 0, kasjer, NULL, 0);
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
                        klient_cleanup(przestrzen, lista, koszyk, ilosc_elementow, kasjer, NULL, 0);
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
            struct paragon* paragon = NULL;
            int paragon_wsk = 0;
            int pierwszy_element = 1;
            sem_p(sem_pam, 3);
            info->czekajacy[kasa]++;
            sem_v(sem_pam, 3);
            printf("Klient %d staje w kolejce przy kasie %d na miejscu %d\n", getpid(), kasa, miejsce);
            for (int i=0; i<miejsce; i++){
               sem_p(sem_k, n + 1);
               sem_v(sem_k, n + 1);
               sem_p(sem_k, n + 2);
               sem_v(sem_k, n + 2);
            }
            sleep(3);
            sem_p(sem_k, n + 2);
            for (int i=0; i<ilosc_elementow; i++){
               if (koszyk[i] != NULL){
                  if (pierwszy_element || strcmp(koszyk[i], paragon[paragon_wsk].produkt) != 0){
                     if (pierwszy_element) {
                        pierwszy_element = 0;
                        paragon = malloc(sizeof(struct paragon));
                        if (!paragon){
                           perror("Blad alokacji pamieci dla paragonu");
                           klient_cleanup(przestrzen, lista, koszyk, ilosc_elementow, kasjer, paragon, paragon_wsk + 1);
                           exit(EXIT_FAILURE);
                        }
                        paragon[paragon_wsk].ilosc = 0;
                        paragon[paragon_wsk].cena = 0;
                        paragon[paragon_wsk].produkt = malloc(strlen(koszyk[i] + 1));
                        if (!paragon[paragon_wsk].produkt){
                           perror("Blad alokacji pamieci dla produktu paragonu");
                           klient_cleanup(przestrzen, lista, koszyk, ilosc_elementow, kasjer, paragon, paragon_wsk + 1);
                           exit(EXIT_FAILURE);
                        }
                        strcpy(paragon[paragon_wsk].produkt, koszyk[i]);
                     } else {
                        paragon_wsk++;
                        paragon = realloc(paragon, sizeof(struct paragon) * (paragon_wsk + 1));
                        if (!paragon){
                           perror("Blad alokacji pamieci dla paragonu");
                           klient_cleanup(przestrzen, lista, koszyk, ilosc_elementow, kasjer, paragon, paragon_wsk + 1);
                           exit(EXIT_FAILURE);
                        }
                        paragon[paragon_wsk].ilosc = 0;
                        paragon[paragon_wsk].cena = 0;
                        paragon[paragon_wsk].produkt = malloc(strlen(koszyk[i] + 1));
                        if (!paragon[paragon_wsk].produkt){
                           perror("Blad alokacji pamieci dla produktu paragonu");
                           klient_cleanup(przestrzen, lista, koszyk, ilosc_elementow, kasjer, paragon, paragon_wsk + 1);
                           exit(EXIT_FAILURE);
                        }
                        strcpy(paragon[paragon_wsk].produkt, koszyk[i]);
                     }
                     sleep(1);
                  }
                  sem_p(sem_k, 0);
                  memset(kasjer->produkt, 0, sizeof(kasjer->produkt));
                  strncpy(kasjer->produkt, koszyk[i], sizeof(kasjer->produkt) - 1);
                  kasjer->produkt[sizeof(kasjer->produkt) - 1] = '\0';
                  if (i == (ilosc_elementow - 1)){
                     sem_p(sem_pam, 3);
                     info->czekajacy[kasa]--;
                     sem_v(sem_pam, 3);
                  }
                  sem_p(sem_pam, 3);
                  kasjer->cena = info->czekajacy[kasa];
                  sem_v(sem_pam, 3);
                  sem_v(sem_k, n + 3);
                  sem_p(sem_k, n + 4);
                  paragon[paragon_wsk].ilosc++;
                  paragon[paragon_wsk].cena = paragon[paragon_wsk].cena + kasjer->cena;
                  sem_v(sem_k, 0);
                  sleep(1);
               } else if (i == (ilosc_elementow - 1)){
                  sem_p(sem_k, 0);
                  memset(kasjer->produkt, 0, sizeof(kasjer->produkt));
                  kasjer->produkt[sizeof(kasjer->produkt) - 1] = '\0';
                  sem_p(sem_pam, 3);
                  info->czekajacy[kasa]--;
                  kasjer->cena = info->czekajacy[kasa];
                  sem_v(sem_pam, 3);
                  sem_v(sem_k, n + 3);
                  sem_p(sem_k, n + 4);
                  sem_v(sem_k, 0);
               }
            }
            czas = los(3, 10);
            sleep(czas);
            sem_v(sem_k, n + 1);
            sem_p(sem_pam, 3);
            info->dlugosc_kolejki[kasa]--;
            sem_v(sem_pam, 3);
            sem_p(sem_k, n + 1);
            sem_v(sem_k, n + 2);
            printf("Klient %d otrzymal paragon:\n", getpid());
            for(int i=0; i<(paragon_wsk + 1); i++){
               printf("%d sztuk %s, koszt %d\n", paragon[i].ilosc, paragon[i].produkt, paragon[i].cena);
            }
            msg.mtype = 1;
            msg.status = 1;
            if (msgsnd(komid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
               perror("Blad przy wysylaniu komunikatu");
               exit(EXIT_FAILURE);
            }
            printf("Program klient wyslal komunikat: %d\n", msg.status);
            if (msgrcv(komid, &msg, sizeof(msg) - sizeof(long), 2, 0) == -1) {
               perror("Blad przy odbieraniu odpowiedzi");
               exit(EXIT_FAILURE);
            }
            switch (msg.status) {
                case 1:
                   sem_p(sem_pam, 3);
                   info->dlugosc_kolejki[1] = (info->dlugosc_kolejki[1] == 99 ? 0 : info->dlugosc_kolejki[1]);
                   sem_v(sem_pam, 3);
                   break;
                case 2:
                   sem_p(sem_pam, 3);
                   info->dlugosc_kolejki[2] = (info->dlugosc_kolejki[2] == 99 ? 0 : info->dlugosc_kolejki[2]);
                   sem_v(sem_pam, 3);
                   break;
                case 3:
                   sem_p(sem_pam, 3);
                   info->dlugosc_kolejki[1] = 99;
                   sem_v(sem_pam, 3);
                   break;
                case 4:
                   sem_p(sem_pam, 3);
                   info->dlugosc_kolejki[2] = 99;
                   sem_v(sem_pam, 3);
                   break;
                default:
            }
            printf("Klient: Odebrano komunikat od klienta: %d\n", msg.status);
            sleep(3);
            sem_p(sem_pam, 3);
            info->liczba_klientow--;
            sem_v(sem_pam, 3);
            klient_cleanup(przestrzen, lista, koszyk, ilosc_elementow, kasjer, paragon, paragon_wsk + 1);
            exit(EXIT_SUCCESS);
         default:
            break;
      }
   }
}

