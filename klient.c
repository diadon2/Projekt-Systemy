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
}; //struktura komunikacji pomiedzi klientem, a podajnikami

struct paragon {
   int ilosc;
   int cena;
   char* produkt;
}; //element paragonu - calosc robiona jako tablica

struct kasa {
   char produkt[24];
   int cena;
}; //struktura komunikacji pomiedzy klientem, a kasami

struct komunikat {
    long mtype;
    int status;
}; //komunikat uzywany do komunikacji pomiedzy programami klient i kasjer

int sem_pam;
int sem_kier;
int id_komunikat;
int pamiec;
int sem_k;
int kolejka;
int komid;
int shared;
int running = 1;
pthread_t id_czyszczenie;
int otwarcie = 0;
int przy_kasie = 0;
int ilosc_paragon;

struct komunikacja* przestrzen = NULL;
struct lista_zakupow* lista = NULL;
char** koszyk = NULL;
struct kasa* kasjer = NULL;
struct paragon* paragon = NULL;
int ilosc_elementow = 0;
int paragon_wsk = 0;

struct info {
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

void klient_cleanup(){
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
   if (shmctl(kolejka, IPC_RMID, NULL) == -1) {
      perror("Blad podczas usuwania pamieci dzielonej");
   } else {
      printf("Pamiec dzielona zostala usunieta\n");
   }
   if (shmdt(info) == -1) {
      perror("Blad podczas odlaczania pamieci");
   }
   else {
      printf("Pamiec dzielona zostala odlaczona od klienta %d\n", getpid());
   }
   if (shmctl(shared, IPC_RMID, NULL) == -1) {
      perror("Blad podczas usuwania pamieci dzielonej");
   } else {
      printf("Pamiec dzielona zostala usunieta\n");
   }
   if (semctl(sem_k, 0, IPC_RMID) == -1) {
      perror("Blad podczas usuwania semafora");
   } else {
      printf("Semafor usuniety\n");
   }
   if (msgctl(komid, 0, IPC_RMID) == -1) {
      perror("Blad podczas usuwania kolejki komunikatow");
   } else {
      printf("Kolejka komunikatow usunieta\n");
   }
}

void* czyszczenie(void* arg){ //watek do czyszczenia procesow zombie
   while(running) {
      wait(NULL);
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
   if (otwarcie) { //zamykanie
      otwarcie = 0;
   } else { //otwieranie
      otwarcie = 1;
   }
}

void ewakuacja(int sig){
   printf("Proces potomny %d otrzymał sygnał %d - ewakuacja, klient wyrzuca wszystko i wychodzi\n", getpid(), sig);
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = 0; //wysylamy sygnal - klient wychodzi
   if (msgsnd(komid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
      perror("Blad przy wysylaniu komunikatu");
      klient_cleanup();
      exit(EXIT_FAILURE);
   }
   printf("Program klient wyslal komunikat: %d\n", msg.status);
   if (msgrcv(komid, &msg, sizeof(msg) - sizeof(long), 2, 0) == -1) {
      perror("Blad przy odbieraniu odpowiedzi");
      klient_cleanup();
      exit(EXIT_FAILURE);
   }
   switch (msg.status) { //odbieramy odpowiedz od kasjera
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
   sem_p(sem_pam, 3);
   info->liczba_klientow--;
   sem_v(sem_pam, 3);
   klient_cleanup();
   exit(EXIT_SUCCESS);
}

void zamykanie_klient(int sig){
   if (przy_kasie) {
      printf("Klient %d stoi przy kasie i musi jeszcze zostac obsluzony przed zamnkieciem\n", getpid());
      return;
   } else {
      printf("Klient %d nie zdazyl stanac w kolejce do kasy przed zamknieciem i wychodzi\n", getpid());
      struct komunikat msg;
      msg.mtype = 1;
      msg.status = 0;
      if (msgrcv(komid, &msg, sizeof(msg) - sizeof(long), 2, 0) == -1) {
         perror("Blad przy odbieraniu odpowiedzi");
         klient_cleanup();
         exit(EXIT_FAILURE);
      }
      switch (msg.status) { //odbieramy odpowiedz od kasjera
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
      sem_p(sem_pam, 3);
      info->liczba_klientow--;
      sem_v(sem_pam, 3);
      klient_cleanup();
      exit(EXIT_SUCCESS);
   }
}

int main(int argc, char** argv){
   signal(SIGINT, exit_handler);
   signal(SIGTERM, exit_handler);
   signal(SIGUSR2, otwarcie_zamykanie);

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
   key_t key_s = ftok(".", 'S');
   if ((shared = shmget(key_s, sizeof(struct komunikacja), 0666|IPC_CREAT)) == -1) {
      perror("Blad podczas tworzenia pamieci dzielonej");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Pamiec dzielona %d zostala utworzona\n", shared);
   }
   key_t key_semkier = ftok(".", 'R');
   utworz_semafor(&sem_kier, key_semkier, 5);
   key_t key_mkom = ftok(".", 'M');
   id_komunikat = msgget(key_mkom, IPC_CREAT | 0666);
   if (id_komunikat == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
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
   sem_v(sem_pam, 3); //semafor uzywany do uzyskiwania dostep do zmiennych wspoldzielonych przez klientow

   if (pthread_create(&id_czyszczenie, NULL, czyszczenie, NULL) != 0){
      perror("Blad podczas tworzenia watku czyszczenie");
      exit(EXIT_FAILURE);
   }

   sem_p(sem_kier, 3);
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = getpid(); //wiadomosc dla kierownika
   if (msgsnd(id_komunikat, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
      perror("Blad przy wysylaniu komunikatu");
      exit(EXIT_FAILURE);
   }
   printf("Program klient wyslal komunikat: %d\n", msg.status);

   while (1) {
   while (otwarcie) { //generowanie nowych klientow
      czekanie = los(1, 30); //klienci przychodza w losowych momentach czasu
      msg.mtype = 1;
      msg.status = 1; //wiadomosc 1 dla kasjera - nowy klient
      if (msgsnd(komid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
         perror("Blad przy wysylaniu komunikatu");
         exit(EXIT_FAILURE);
      }
      printf("Program klient wyslal komunikat: %d\n", msg.status);

      if (msgrcv(komid, &msg, sizeof(msg) - sizeof(long), 2, 0) == -1) { //odbieramy odpowiedz od kasjera, nastepnie zmieniamy dlugosc_kolejki by zablokowac zamkniete kasy
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
            //Proces potomny - klient przychodzi do piekarni
            sem_p(sem_kier, 0);
            msg.mtype = 1;
            msg.status = getpid(); //wiadomosc dla kierownika
            if (msgsnd(id_komunikat, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
               perror("Blad przy wysylaniu komunikatu");
               exit(EXIT_FAILURE);
            }
            signal(SIGINT, exit_handler_klient);
            signal(SIGTERM, exit_handler_klient);
            signal(SIGUSR1, ewakuacja);
            signal(SIGUSR2, zamykanie_klient);
            while (1) { //jezeli piekarnia osiagnela limit klientow, czeka przed piekarnia
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

            //inicjalizacja klienta
            przestrzen = (struct komunikacja*)shmat(pamiec, NULL, 0);
            if (przestrzen == (struct komunikacja*)(-1)){
               perror("Blad podczas przydzielania adresu dla pamieci");
               exit(EXIT_FAILURE);
            }
            else {
               printf("Przestrzen adresowa zostala przyznana dla %d\n", pamiec);
            }
            kasjer = (struct kasa*)shmat(kolejka, NULL, 0);
            if (kasjer == (struct kasa*)(-1)){
               perror("Blad podczas przydzielania adresu dla kolejki");
               exit(EXIT_FAILURE);
            }
            else {
               printf("Przestrzen adresowa zostala przyznana dla %d\n", kolejka);
            }
            //generowanie listy zakupow
            lista = stworz_liste_zakupow();
            printf("Lista zakupow dla klienta %d :\n", getpid());
            for (int i=0; i<lista->dlugosc; i++){
               printf("%d sztuk produktu %d\n", lista->elementy[i].ilosc, lista->elementy[i].index);
               ilosc_elementow = ilosc_elementow + lista->elementy[i].ilosc;
            }
            //tworzenie koszyka - miejsce przechowywania odebranych produktow
            koszyk = malloc(sizeof(char*) * ilosc_elementow);
            if (!koszyk){
               perror("Blad podczas alokacji pamieci dla koszyk");
               klient_cleanup();
               exit(EXIT_FAILURE);
            }
            int koszyk_wsk = 0;
            int czas = los(3, 10);
            sleep(czas);
            for (int i=0; i<lista->dlugosc; i++){
               while (lista->elementy[i].ilosc > 0) {
                  sem_p(sem_pam, 0); //czekamy na sygnal od obslugi dostepu do podajnikow
                  przestrzen->index = lista->elementy[i].index; //wysylamy informacje o produkcie, ktory chcemy odebrac
                  przestrzen->ilosc = lista->elementy[i].ilosc;
                  sem_v(sem_pam, 1);
                  sem_p(sem_pam, 2); //wysylamy sygnal do obslugi podajnikow i odbieramy sygnal, kiedy produkt jest gotowy
                  if (przestrzen->pieczywo[0] != '\0'){ //jezeli produktu byl na podajniku, dodajemy do koszyka
                     koszyk[koszyk_wsk] = malloc(strlen(przestrzen->pieczywo) + 1);
                     strcpy(koszyk[koszyk_wsk], przestrzen->pieczywo);
                     if (!koszyk[koszyk_wsk]){
                        perror("Blad alokacji pamieci dla pieczywa klienta");
                        klient_cleanup();
                        exit(EXIT_FAILURE);
                     }
                     strcpy(koszyk[koszyk_wsk], przestrzen->pieczywo);
                     printf("Klient %d odebral %s\n", getpid(), koszyk[koszyk_wsk]);
                     koszyk_wsk++;
                     lista->elementy[i].ilosc--;
                  }
                  else { //jezeli nie byl to ignorujemy ten produkt i przechodzimy do kolejnego
                     for (int j=0; j<lista->elementy[i].ilosc; j++) {
                        koszyk[koszyk_wsk++] = NULL;
                     }
                     lista->elementy[i].ilosc = 0;
                     printf("Klient %d nie zastal produktu o indeksie %d\n", getpid(), lista->elementy[i].index);
                  }
                  sem_v(sem_pam, 0);
                  sleep(1);
               }
               czas = los(1, 5); //przechodzimy do kolejnego produktu
               sleep(czas);
            }

            czas = los(5, 10); //idziemy do kasy
            sleep(czas);
            int miejsce;
            int kasa = wybierz_kase(&miejsce);
            int n = kasa * 4;
            int pierwszy_element = 1;
            sem_p(sem_pam, 3);
            info->czekajacy[kasa]++;
            sem_v(sem_pam, 3);
            printf("Klient %d staje w kolejce przy kasie %d na miejscu %d\n", getpid(), kasa, miejsce);
            przy_kasie = 1;
            for (int i=0; i<miejsce; i++){ //czekamy w kolejce
               sem_p(sem_k, n + 1);
               sem_v(sem_k, n + 1);
               sem_p(sem_k, n + 2);
               sem_v(sem_k, n + 2);
            }
            sleep(3);
            sem_p(sem_k, n + 2); //czekamy na sygnal, ze kasa dziala i jest wolna
            struct paragon* realloc_wsk;
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
                     sleep(1);
                  }
                  sem_p(sem_k, 0); //czekamy na dostep do pamieci dzielonej kasy
                  memset(kasjer->produkt, 0, sizeof(kasjer->produkt));
                  strncpy(kasjer->produkt, koszyk[i], sizeof(kasjer->produkt) - 1); //wstawiamy produkt na kase
                  kasjer->produkt[sizeof(kasjer->produkt) - 1] = '\0';
                  if (i == (ilosc_elementow - 1)){ //jezeli jest to ostatni produkt, zmniejszamy ilosc czekajacych w kasie, aby przekazac go kasjerowi
                     sem_p(sem_pam, 3);
                     info->czekajacy[kasa]--;
                     sem_v(sem_pam, 3);
                  }
                  sem_p(sem_pam, 3);
                  kasjer->cena = info->czekajacy[kasa]; //przekazujemy informacje o ilosci czekajacych klientow przy kasie
                  sem_v(sem_pam, 3);
                  sem_v(sem_k, n + 3); //wysylamy sygnal do kasjera
                  sem_p(sem_k, n + 4); //odbieramy sygnal od kasjera
                  paragon[paragon_wsk].ilosc++;
                  paragon[paragon_wsk].cena = paragon[paragon_wsk].cena + kasjer->cena; //zwiekszamy cene i ilosc produktu do paragonu
                  sem_v(sem_k, 0);
                  sleep(1); //pakujemy element i podajemy kolejny
               } else if (i == (ilosc_elementow - 1)){ //jezeli jest to ostatni element, a jest on pusty to wysylamy informacje o czekajacych klientach po zmniejszeniu wartosci
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
            czas = los(3, 10); //placimy i odchodzimy od kasy
            sleep(czas);
            sem_v(sem_k, n + 1); //pozwalamy kolejce sie ruszyc i zmniejszamy jej dlugosc
            sem_p(sem_pam, 3);
            info->dlugosc_kolejki[kasa]--;
            sem_v(sem_pam, 3);
            sem_p(sem_k, n + 1);
            sem_v(sem_k, n + 2);
            printf("Klient %d otrzymal paragon:\n", getpid());
            for(int i=0; i<ilosc_paragon; i++){
               printf("%d sztuk %s, koszt %d\n", paragon[i].ilosc, paragon[i].produkt, paragon[i].cena);
            }
            msg.mtype = 1;
            msg.status = 0; //wysylamy sygnal - klient wychodzi
            if (msgsnd(komid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
               perror("Blad przy wysylaniu komunikatu");
               klient_cleanup();
               exit(EXIT_FAILURE);
            }
            printf("Program klient wyslal komunikat: %d\n", msg.status);
            if (msgrcv(komid, &msg, sizeof(msg) - sizeof(long), 2, 0) == -1) {
               perror("Blad przy odbieraniu odpowiedzi");
               klient_cleanup();
               exit(EXIT_FAILURE);
            }
            switch (msg.status) { //odbieramy odpowiedz od kasjera
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
            klient_cleanup();
            exit(EXIT_SUCCESS);
         default:
            sleep(czekanie);
            break;
      }
   }
   }
}

