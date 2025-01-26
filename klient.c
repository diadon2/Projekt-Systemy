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

struct st_podajnik {
   int index;
   char pieczywo[24];
}; //struktura komunikacji pomiedzi klientem, a podajnikami

struct paragon {
   int ilosc;
   int cena;
   char* produkt;
}; //element paragonu - calosc robiona jako tablica

struct st_kasa {
   char produkt[24];
   int cena;
}; //struktura komunikacji pomiedzy klientem, a kasami

struct komunikat {
    long mtype;
    int status;
}; //komunikat uzywany do komunikacji pomiedzy programami klient i kasjer

int sem_podajnik;
int sem_kier;
int sem_kasa;
int kom_kier;
int kom_kasa;
int pam_podajnik;
int pam_kasa;
int pam_shared;
int running = 1;
pthread_t id_czyszczenie;

int otwarcie = 0;
int przy_kasie = 0;
int ilosc_paragon = 0;
struct paragon* paragon = NULL;
int ilosc_elementow = 0;
int paragon_wsk = 0;

struct st_podajnik* przestrzen_podajnik = NULL;
struct lista_zakupow* lista = NULL;
char** koszyk = NULL;
struct st_kasa* przestrzen_kasa = NULL;

struct st_shared {
   int dlugosc_kolejki[3];
   int czekajacy[3];
   int liczba_klientow;
} *info; //pamiec dzielona zawierajaca zmienne wspoldzielone przez klientow

int los(int min, int max){
   if (min > max) {
      errno = EINVAL;
      perror("Bledne wartosci funkcji los - min i max odwrocone");
      return (rand() % (min - max + 1)) + max;
   }
   return (rand() % (max - min + 1)) + min;
}

int wybierz_kase(int* miejsce){
   sem_p(sem_podajnik, 3);
   int min = 0;
   for (int i = 1; i < 3; i++) {
      if (info->dlugosc_kolejki[i] < info->dlugosc_kolejki[min]) {
         min = i;
      }
   }
   *miejsce = info->dlugosc_kolejki[min];
   info->dlugosc_kolejki[min]++;
   sem_v(sem_podajnik, 3);
   return min;
}

void klient_cleanup(){
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = -getpid(); //wiadomosc dla kierownika
   if (msgsnd(kom_kier, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
      perror("Blad przy wysylaniu komunikatu");
   }
   if (shmdt(przestrzen_podajnik) == -1) {
      perror("Blad podczas odlaczania pamieci");
   }
   else {
      printf("Pamiec dzielona zostala odlaczona od klienta %d\n", getpid());
   }
   if (shmdt(przestrzen_kasa) == -1) {
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

void wiadomosc_kasjer(int n){
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = n;
   if (msgsnd(kom_kasa, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
      perror("Blad przy wysylaniu komunikatu");
      exit(EXIT_FAILURE);
   }
}

void klient_wchodzi(){
   info->liczba_klientow++;
   if (info->liczba_klientow == 2*MAX_KLIENTOW/3){
      info->dlugosc_kolejki[2] = info->czekajacy[2];
      if(!info->czekajacy[2]) wiadomosc_kasjer(3);
   } else if (info->liczba_klientow == MAX_KLIENTOW/3) {
      info->dlugosc_kolejki[1] = info->czekajacy[1];
      if(!info->czekajacy[1]) wiadomosc_kasjer(2);
   }
}

void klient_wychodzi(){
   int ilosc = info->liczba_klientow--;
   if (ilosc == 2*MAX_KLIENTOW/3){
      info->dlugosc_kolejki[2] = 99;
      if(!info->czekajacy[2]) wiadomosc_kasjer(-3);
   } else if (ilosc == MAX_KLIENTOW/3) {
      info->dlugosc_kolejki[1] = 99;
      if(!info->czekajacy[1]) wiadomosc_kasjer(-2);
   }
}

struct lista_zakupow* stworz_liste_zakupow(){
   int ilosc = los(2,5);
   struct lista_zakupow* lista_zakupow = malloc(sizeof(struct lista_zakupow));
   if (!lista_zakupow){
      perror("Blad podczas alokacji pamieci dla listy zakupow");
      klient_cleanup();
      exit(EXIT_FAILURE);
   }
   struct zakupy* zakupy = malloc(sizeof(struct zakupy) * ilosc);
   if (!zakupy){
      perror("Blad podczas alokacji pamieci dla zakupy");
      free(lista_zakupow);
      klient_cleanup();
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

void stworz_koszyk() {
   koszyk = malloc(sizeof(char*) * ilosc_elementow);
   if (!koszyk){
      perror("Blad podczas alokacji pamieci dla koszyk");
      klient_cleanup();
      exit(EXIT_FAILURE);
   }
   int koszyk_wsk = 0;
   for (int i=0; i<lista->dlugosc; i++){
      while (lista->elementy[i].ilosc > 0) {
         sem_p(sem_podajnik, 0); //czekamy na sygnal od obslugi dostepu do podajnikow
         przestrzen_podajnik->index = lista->elementy[i].index; //wysylamy informacje o produkcie, ktory chcemy odebrac
         sem_v(sem_podajnik, 1);
         sem_p(sem_podajnik, 2); //wysylamy sygnal do obslugi podajnikow i odbieramy sygnal, kiedy produkt jest gotowy
         if (przestrzen_podajnik->pieczywo[0] != '\0'){ //jezeli produktu byl na podajniku, dodajemy do koszyka
            koszyk[koszyk_wsk] = malloc(strlen(przestrzen_podajnik->pieczywo) + 1);
            strcpy(koszyk[koszyk_wsk], przestrzen_podajnik->pieczywo);
            if (!koszyk[koszyk_wsk]){
               perror("Blad alokacji pamieci dla pieczywa klienta");
               klient_cleanup();
               exit(EXIT_FAILURE);
            }
            strcpy(koszyk[koszyk_wsk], przestrzen_podajnik->pieczywo);
            printf("Klient %d odebral %s\n", getpid(), koszyk[koszyk_wsk]);
            koszyk_wsk++;
            lista->elementy[i].ilosc--;
         } else { //jezeli nie byl to ignorujemy ten produkt i przechodzimy do kolejnego
            for (int j=0; j<lista->elementy[i].ilosc; j++) {
               koszyk[koszyk_wsk++] = NULL;
            }
            lista->elementy[i].ilosc = 0;
            printf("Klient %d nie zastal produktu o indeksie %d\n", getpid(), lista->elementy[i].index);
         }
         sem_v(sem_podajnik, 0);
         sleep(1);
      }
      sleep(los(1, 5)); //przechodzimy do kolejnego produktu
   }
}

void odbierz_paragon(int n, int kasa){
   struct paragon* realloc_wsk;
   int pierwszy_element = 1;
   for (int i=0; i<ilosc_elementow; i++){
      if (koszyk[i] != NULL){
         if (pierwszy_element || strcmp(koszyk[i], paragon[paragon_wsk].produkt) != 0){ //dodajemy produkty do paragonu
            if (pierwszy_element) {
               pierwszy_element = 0;
               paragon = malloc(sizeof(struct paragon));
               if (!paragon){
                  perror("Blad alokacji pamieci dla paragonu");
                  klient_cleanup();
                  exit(EXIT_FAILURE);
               }
               ilosc_paragon = 1;
               paragon[paragon_wsk].ilosc = 0;
               paragon[paragon_wsk].cena = 0;
               paragon[paragon_wsk].produkt = malloc(strlen(koszyk[i] + 1));
               if (!paragon[paragon_wsk].produkt){
                  perror("Blad alokacji pamieci dla produktu paragonu");
                  klient_cleanup();
                  exit(EXIT_FAILURE);
               }
               strcpy(paragon[paragon_wsk].produkt, koszyk[i]);
            } else {
               paragon_wsk++;
               ilosc_paragon++;
               realloc_wsk = realloc(paragon, sizeof(struct paragon) * (paragon_wsk + 1));
               if (!realloc_wsk){
                  perror("Blad alokacji pamieci dla paragonu");
                  klient_cleanup();
                  exit(EXIT_FAILURE);
               }
               paragon = realloc_wsk;
               paragon[paragon_wsk].ilosc = 0;
               paragon[paragon_wsk].cena = 0;
               paragon[paragon_wsk].produkt = malloc(strlen(koszyk[i] + 1));
               if (!paragon[paragon_wsk].produkt){
                  perror("Blad alokacji pamieci dla produktu paragonu");
                  klient_cleanup();
                  exit(EXIT_FAILURE);
               }
               strcpy(paragon[paragon_wsk].produkt, koszyk[i]);
            }
         }
         sem_p(sem_kasa, n + 3); //czekamy na dostep do kasjera
         sem_p(sem_kasa, 0); //czekamy na dostep do pamieci dzielonej kasy
         memset(przestrzen_kasa->produkt, 0, sizeof(przestrzen_kasa->produkt));
         strncpy(przestrzen_kasa->produkt, koszyk[i], sizeof(przestrzen_kasa->produkt) - 1); //wstawiamy produkt na kase
         przestrzen_kasa->produkt[sizeof(przestrzen_kasa->produkt) - 1] = '\0';
         sem_v(sem_kasa, n + 4); //wysylamy sygnal do kasjera
         sem_p(sem_kasa, n + 5); //odbieramy sygnal od kasjera
         paragon[paragon_wsk].ilosc++;
         paragon[paragon_wsk].cena = paragon[paragon_wsk].cena + przestrzen_kasa->cena; //zwiekszamy cene i ilosc produktu do paragonu
         sem_v(sem_kasa, 0);
         sem_v(sem_kasa, n + 3);
         sleep(1); //pakujemy element i podajemy kolejny
      }
   }
}

void cleanup(){
   if (shmdt(info) == -1) {
      perror("Blad podczas odlaczania pamieci");
   } else {
      printf("Pamiec dzielona zostala odlaczona\n");
   }
   if (shmctl(pam_kasa, IPC_RMID, NULL) == -1) {
      perror("Blad podczas usuwania pamieci dzielonej");
   } else {
      printf("Pamiec dzielona zostala usunieta\n");
   }
   if (shmctl(pam_shared, IPC_RMID, NULL) == -1) {
      perror("Blad podczas usuwania pamieci dzielonej");
   } else {
      printf("Pamiec dzielona zostala usunieta\n");
   }
   if (semctl(sem_kasa, 0, IPC_RMID) == -1) {
      perror("Blad podczas usuwania semafora");
   } else {
      printf("Semafor usuniety\n");
   }
   if (msgctl(kom_kasa, 0, IPC_RMID) == -1) {
      perror("Blad podczas usuwania kolejki komunikatow");
   } else {
      printf("Kolejka komunikatow usunieta\n");
   }
}

void* czyszczenie(void* arg){ //watek do czyszczenia procesow zombie
   while(running) {
      if (wait(NULL) == -1) {
         if (errno == ECHILD) {
            sleep(1);
         }
      } else {
         perror("Blad podczas wait");
         sleep(1);
      }
   }
   return NULL;
}

void exit_handler(int sig){
   printf("Sygnal %d - koniec programu\n", sig);
   running = 0;
   pthread_join(id_czyszczenie, NULL);
   cleanup();
   sem_v(sem_kier, 3);
   exit(EXIT_SUCCESS);
}

void exit_handler_klient(int sig){
   printf("Proces potomny %d otrzymał sygnał %d - koniec programu\n", getpid(), sig);
   klient_cleanup();
   exit(EXIT_SUCCESS);
}

void otwarcie_zamykanie(int sig){
   sem_p(sem_podajnik, 3);
   if (otwarcie) { //zamykanie
      otwarcie = 0;
      info->dlugosc_kolejki[0] = 99;
      if (!info->czekajacy[0]) wiadomosc_kasjer(-1);
   } else { //otwieranie
      otwarcie = 1;
      info->dlugosc_kolejki[0] = 0;
      info->dlugosc_kolejki[1] = 99;
      info->dlugosc_kolejki[2] = 99;
      for (int i=0; i<3; i++) info->czekajacy[i] = 0;
   }
   sem_v(sem_podajnik, 3);
}

void ewakuacja(int sig){
   printf("Proces potomny %d otrzymał sygnał %d - ewakuacja, klient wyrzuca wszystko i wychodzi\n", getpid(), sig);
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = 0; //wysylamy sygnal - klient wychodzi
   sem_p(sem_podajnik, 3);
   for (int i=0; i<3; i++){
      info->czekajacy[i] = 0;
      if (i != 0) info->dlugosc_kolejki[i] = 99;
      else info->dlugosc_kolejki[i] = 0;
   }
   sem_v(sem_podajnik, 3);
   klient_cleanup();
   exit(EXIT_SUCCESS);
}

void zamykanie_klient(int sig){
   if (przy_kasie) {
      printf("Klient %d stoi przy kasie i musi jeszcze zostac obsluzony przed zamnkieciem\n", getpid());
      return;
   } else {
      printf("Klient %d nie zdazyl stanac w kolejce do kasy przed zamknieciem i wychodzi\n", getpid());
      sem_p(sem_podajnik, 3);
      klient_wychodzi();
      sem_v(sem_podajnik, 3);
      klient_cleanup();
      exit(EXIT_SUCCESS);
   }
}

int main(int argc, char** argv){
   struct sigaction sa;
   sa.sa_flags = 0;
   sa.sa_handler = exit_handler;
   sigaction(SIGTERM, &sa, NULL);
   sigaction(SIGINT, &sa, NULL);
   sa.sa_handler = otwarcie_zamykanie;
   sigaction(SIGUSR2, &sa, NULL);

   srand(time(NULL));
   key_t key_sem_podajnik = ftok(".", 'P');
   utworz_semafor(&sem_podajnik, key_sem_podajnik, 4);
   key_t key_pam_podajnik = ftok(".", 'T');
   if ((pam_podajnik = shmget(key_pam_podajnik, sizeof(struct st_podajnik), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", pam_podajnik);
   }
   key_t key_sem_kasa = ftok(".", 'K');
   utworz_semafor(&sem_kasa, key_sem_kasa, 15);
   key_t key_pam_kasa = ftok(".", 'L');
   if ((pam_kasa = shmget(key_pam_kasa, sizeof(struct st_kasa), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", pam_kasa);
   }
   key_t key_kom_kasa = ftok(".", 'Q');
   kom_kasa = msgget(key_kom_kasa, IPC_CREAT | 0666);
   if (kom_kasa == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
   pid_t pid;
   int czekanie = 5;
   key_t key_s = ftok(".", 'S');
   if ((pam_shared = shmget(key_s, sizeof(struct st_shared), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", pam_shared);
   }
   key_t key_sem_kier = ftok(".", 'R');
   utworz_semafor(&sem_kier, key_sem_kier, 5);
   key_t key_kom_kier = ftok(".", 'M');
   kom_kier = msgget(key_kom_kier, IPC_CREAT | 0666);
   if (kom_kier == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
   info = (struct st_shared*)shmat(pam_shared, NULL, 0);
   if (info == (struct st_shared*) -1) {
      perror("Blad podczas przydzielania adresu dla pamieci wspoldzielonej");
      exit(EXIT_FAILURE);
   }

   info->dlugosc_kolejki[0] = 99;
   info->dlugosc_kolejki[1] = 99;
   info->dlugosc_kolejki[2] = 99;
   info->czekajacy[0] = 0;
   info->czekajacy[1] = 0;
   info->czekajacy[2] = 0;
   info->liczba_klientow = 0;
   sem_v(sem_podajnik, 3); //semafor uzywany do uzyskiwania dostep do zmiennych wspoldzielonych przez klientow

   if (pthread_create(&id_czyszczenie, NULL, czyszczenie, NULL) != 0){
      perror("Blad podczas tworzenia watku czyszczenie");
      exit(EXIT_FAILURE);
   }

   sem_p(sem_kier, 3);
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = getpid(); //wiadomosc dla kierownika
   if (msgsnd(kom_kier, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
      perror("Blad przy wysylaniu komunikatu");
      exit(EXIT_FAILURE);
   }
   printf("Program klient wyslal komunikat: %d\n", msg.status);

   while (running) {
   while (otwarcie) { //generowanie nowych klientow
      czekanie = los(1, 30); //klienci przychodza w losowych momentach czasu
      printf("Klient: Odebrano komunikat od klienta: %d\n", msg.status);
      pid = fork();
      switch (pid){
         case -1:
            perror("Blad podczas tworzenia procesu za pomoca fork");
            exit(EXIT_FAILURE);
         case 0:
            //Proces potomny - klient przychodzi do piekarni
            sem_p(sem_kier, 0);
            msg.mtype = 1;
            msg.status = getpid(); //wiadomosc dla kierownika
            if (msgsnd(kom_kier, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
               perror("Blad przy wysylaniu komunikatu");
               exit(EXIT_FAILURE);
            }
            signal(SIGINT, exit_handler_klient);
            signal(SIGTERM, exit_handler_klient);
            signal(SIGUSR1, ewakuacja);
            signal(SIGUSR2, zamykanie_klient);
            struct sigaction sa;
            sa.sa_flags = 0;
            sa.sa_handler = exit_handler_klient;
            sigaction(SIGTERM, &sa, NULL);
            sigaction(SIGINT, &sa, NULL);
            sa.sa_handler = zamykanie_klient;
            sigaction(SIGUSR2, &sa, NULL);
            sa.sa_handler = ewakuacja;
            sigaction(SIGUSR1, &sa, NULL);
            while (running) { //jezeli piekarnia osiagnela limit klientow, czeka przed piekarnia
               sem_p(sem_podajnik, 3);
               if (info->liczba_klientow < MAX_KLIENTOW) {
                  klient_wchodzi();
                  sem_v(sem_podajnik, 3);
                  break;
               }
               sem_v(sem_podajnik, 3);
               sleep(1);
            }
            printf("Klient %d rozpoczyna dzialanie\n", getpid());

            //inicjalizacja klienta
            przestrzen_podajnik = (struct st_podajnik*)shmat(pam_podajnik, NULL, 0);
            if (przestrzen_podajnik == (struct st_podajnik*)(-1)){
               perror("Blad podczas przydzielania adresu dla pamieci");
               klient_cleanup();
               exit(EXIT_FAILURE);
            }
            else {
               printf("Przestrzen adresowa zostala przyznana dla %d\n", pam_podajnik);
            }
            przestrzen_kasa = (struct st_kasa*)shmat(pam_kasa, NULL, 0);
            if (przestrzen_kasa == (struct st_kasa*)(-1)){
               perror("Blad podczas przydzielania adresu dla kolejki");
               klient_cleanup();
               exit(EXIT_FAILURE);
            }
            else {
               printf("Przestrzen adresowa zostala przyznana dla %d\n", pam_kasa);
            }
            //generowanie listy zakupow
            lista = stworz_liste_zakupow();
            printf("Lista zakupow dla klienta %d :\n", getpid());
            for (int i=0; i<lista->dlugosc; i++){
               printf("%d sztuk produktu %d\n", lista->elementy[i].ilosc, lista->elementy[i].index);
               ilosc_elementow = ilosc_elementow + lista->elementy[i].ilosc;
            }

            //tworzenie koszyka - miejsce przechowywania odebranych produktow
            stworz_koszyk();

            sleep(los(5, 10));
            int miejsce;
            int kasa = wybierz_kase(&miejsce);
            sem_p(sem_podajnik, 3);
            info->czekajacy[kasa]++;
            sem_v(sem_podajnik, 3);
            printf("Klient %d staje w kolejce przy kasie %d na miejscu %d\n", getpid(), kasa, miejsce);
            przy_kasie = 1;
            for (int i=0; i<miejsce; i++){ //czekamy w kolejce
               sem_p(sem_kasa, (kasa * 5) + 1); //czeka na sygnal od wychodzacego klienta
               sem_p(sem_kasa, (kasa * 5) + 2); //blokuje sie az wychodzacy klient wypusci wszystkich
            }
            sleep(los(1, 3));
            odbierz_paragon(kasa * 5, kasa);
            sem_p(sem_podajnik, 3);
            for (int i=0; i<info->czekajacy[kasa]; i++){ //ruszamy kolejke
               sem_v(sem_kasa, (kasa * 5) + 1);
            }
            for (int i=0; i<info->czekajacy[kasa]; i++){ //ruszamy kolejke
               sem_v(sem_kasa, (kasa * 5) + 2);
            }
            info->czekajacy[kasa]--;
            if (info->dlugosc_kolejki[kasa] == 99) {
               if (info->czekajacy[kasa] == 0){
                  wiadomosc_kasjer(-(kasa + 1));//wysylamy wiadomosc do programu kasjer aby zamknac kase
               }
            } else info->dlugosc_kolejki[kasa]--;
            sem_v(sem_podajnik, 3);
            printf("Klient %d otrzymal paragon:\n", getpid());
            for(int i=0; i<ilosc_paragon; i++){
               printf("%d sztuk %s, koszt %d\n", paragon[i].ilosc, paragon[i].produkt, paragon[i].cena);
            }
            //klient wychodzi ze sklepu
            sleep(los(5, 10));
            sem_p(sem_podajnik, 3);
            klient_wychodzi();
            sem_v(sem_podajnik, 3);

            klient_cleanup();
            exit(EXIT_SUCCESS);
         default:
            sleep(czekanie);
            break;
      }
   }
   sleep(1);
   }
}
