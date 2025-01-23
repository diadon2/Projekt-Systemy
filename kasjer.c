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

int sem_kier;
int id_komunikat;
int sem_k;
int kolejka;
struct kasa* kasowanie;
int raport[3][ILOSC_PRODUKTOW];
int ceny[ILOSC_PRODUKTOW];
pthread_t kasjerzy[3];
int ilosc_klientow = 0;
int komid;
int otwarte_kasy = 0;
int aktywne_kasy[3] = {0, 0, 0};
int czekajacy[3] = {0, 0, 0};
int otwarcie = 0;
int inwent = 0;
int aktywnosc[3] = {0, 0, 0};
int running = 1;
int koniec = 0;

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
   aktywnosc[id_kasa] = 1;
   n = n * 4;
   char* produkt = NULL;
   int produkt_index = 0;
   sem_v(sem_k, n + 2); //przekazuje sygnal - kasa jest gotowa do dzialania
   printf("Kasa %d dziala\n", id_kasa);
   while (1) {
      sem_p(sem_k, n + 3); //czeka na sygnal od klienta
      if ((n != 0 || koniec == 1) && (czekajacy[id_kasa] == 0) && (aktywne_kasy[id_kasa] == 0)) break; //koniec pracy kasy
      produkt = malloc(strlen(kasowanie->produkt) + 1);
      strcpy(produkt, kasowanie->produkt);
      produkt_index = sprawdz_produkt(produkt); //sprawdza produkt, aby go skasowac
      czekajacy[id_kasa] = kasowanie->cena; //przez kasowanie->cena klienci przekazuja ilosc czekajacych klientow
      kasowanie->cena = ceny[produkt_index]; //wysyla cene produktu
      raport[id_kasa][produkt_index]++;
      sem_v(sem_k, n + 4); //powrot do klienta
      free(produkt);
      produkt = NULL;
      if ((n != 0 || koniec == 1) && (czekajacy[id_kasa] == 0) && (aktywne_kasy[id_kasa] == 0)) break; //koniec pracy kasy
   }
   sem_p(sem_k, n + 2); //sygnal - kasa nie jest gotowa do dzialania
   printf("Kasa %d zamknieta\n", id_kasa);
   aktywnosc[id_kasa] = 2;
   return NULL;
}

void cleanup(){
   if (shmdt(kasowanie) == -1) {
      perror("Blad podczas odlaczania pamieci dzielonej");
   }
}

void inwentaryzacja(int sig){
   printf("Sygnal %d - inwentaryzacja po zamnkeciu\n", sig);
   inwent = 1;
}

void otwieranie_zamykanie(int sig){
   if (otwarcie == 1) { //zamykanie
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
      //czekamy az klienci ustawieni w kolejce do kasy odejda, na koniec zamykamy ostatnia kase
      //koniec = 1;
      //for (int i=0; i<3; i++){
      //   if (aktywnosc[i] == 1) {
      //      while (czekajacy[i] > 0) ;
      //      sem_v(sem_k, (i *4) + 3);
      //      pthread_join(kasjerzy[i], NULL);
      //      aktywnosc[i] = 0;
      //   }
      //}
      //for (int i=0; i<3; i++){
      //   if (aktywnosc[i] == 2) {
      //      pthread_join(kasjerzy[i], NULL);
      //      aktywnosc[i] = 0;
      //   }
      //}
   } else { //otwierania
      //int n_val[3] = {0, 1, 2};
      //if (pthread_create(&kasjerzy[0], NULL, Kasjer, &n_val[0]) != 0){ //pierwszy, zawsze dzialajacy kasjer
      //   perror("Blad podczas tworzenia watku kasjera");
      //   exit(EXIT_FAILURE);
      //}
      for (int i=0; i<3; i++) {
         for (int j=0; j<ILOSC_PRODUKTOW; j++) {
            raport[i][j] = 0;
         }
      }
      aktywne_kasy[0] = 1;
      aktywne_kasy[1] = 0;
      aktywne_kasy[2] = 0;
      ilosc_klientow = 0;
      otwarte_kasy = 1;
      koniec = 1;
      otwarcie = 1;
      return;
   }
}

void exit_handler(int sig){
   printf("Sygnal %d - koniec programu\n", sig);
   running = 0;
   koniec = 1;
   czekajacy[0] = 0;
   czekajacy[1] = 0;
   czekajacy[2] = 0;
   aktywne_kasy[0] = 0;
   aktywne_kasy[1] = 0;
   aktywne_kasy[2] = 0;
   for (int i=0; i<3; i++){
      if (aktywnosc[i]) {
         sem_v(sem_k, (i * 4) + 3);
         pthread_join(kasjerzy[i], NULL);
         aktywnosc[i] = 0;
      }
   }
   sem_v(sem_kier, 2);
   cleanup();
   exit(EXIT_SUCCESS);
}

int main(){
   signal(SIGINT, exit_handler);
   signal(SIGTERM, exit_handler);
   signal(SIGUSR2, otwieranie_zamykanie);
   signal(SIGUSR1, inwentaryzacja);

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
   key_t key_semkier = ftok(".", 'R');
   utworz_semafor(&sem_kier, key_semkier, 5);
   key_t key_mkom = ftok(".", 'M');
   id_komunikat = msgget(key_mkom, IPC_CREAT | 0666);
   if (id_komunikat == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
   sem_p(sem_kier, 2);
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = getpid(); //wiadomosc dla kierownika
   if (msgsnd(id_komunikat, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
      perror("Blad przy wysylaniu komunikatu");
      exit(EXIT_FAILURE);
   }
   printf("Program kasjer wyslal komunikat: %d\n", msg.status);

//koniec inicjalizacji
   int n_val[3] = {0, 1, 2};
   sem_v(sem_k, 0); //otwiera semafor dostepu do pamieci dzielonej kasjer-klient uzywanej w programie klienta
   while (1) { //zbiera informacje o nowych klientach oraz wychodzacych klientach i na ich podstawie otwiera/zamyka kasy
      if (msgrcv(komid, &msg, sizeof(msg) - sizeof(long), 1, 0) == -1) {
         if (errno == EINTR) continue;
         perror("Blad przy odbieraniu komunikatu");
         exit(EXIT_FAILURE);
      }
      printf("Kasjer: Odebrano komunikat od klienta: %d\n", msg.status);
      msg.mtype = 2;
      if (msg.status == 1) { //1 = nowy klient
         ilosc_klientow++;
         if (otwarte_kasy == 1 && ilosc_klientow > MAX_KLIENTOW/3) {
            msg.status = 1;
            aktywne_kasy[1] = 1;
            if (pthread_create(&kasjerzy[1], NULL, Kasjer, &n_val[1]) != 0){ //otwiera kase 1, jezeli spelnia warunki
               perror("Blad podczas tworzenia watku kasjera");
               exit(EXIT_FAILURE);
            }
            otwarte_kasy = 2;
         } else if (otwarte_kasy == 2 && ilosc_klientow > MAX_KLIENTOW * 2 / 3) {
            msg.status = 2;
            aktywne_kasy[2] = 0;
            if (pthread_create(&kasjerzy[2], NULL, Kasjer, &n_val[2]) != 0){ //otwiera kase 2, jezeli spelnia warunki
               perror("Blad podczas tworzenia watku kasjera");
               exit(EXIT_FAILURE);
            }
            otwarte_kasy = 3;
         } else msg.status = 0;
      } else if (msg.status == 0) { //2 = klient wychodzi
         ilosc_klientow--;
         if (otwarte_kasy == 2 && ilosc_klientow < MAX_KLIENTOW/3) { //kasa 1 po obsluzeniu klientow w kolejce sie zamknie
            msg.status = 3;
            aktywne_kasy[1] = 0;
            otwarte_kasy = 1;
            if (czekajacy[1] == 0) sem_v(sem_k, 4 + 3);
         } else if (otwarte_kasy == 3 && ilosc_klientow < MAX_KLIENTOW * 2 / 3) { //kasa 2 po obsluzeniu klientow w kolejce sie zamknie
            msg.status = 4;
            aktywne_kasy[2] = 0;
            otwarte_kasy = 2;
            if (czekajacy[2] == 0) sem_v(sem_k, 8 + 3);
         } else msg.status = 0;
      } else {
         perror("Otrzymano nieznany komunikat");
      }
      printf("Ilosc klientow: %d, czekajacy przy kasach: %d %d %d\n", ilosc_klientow, czekajacy[0], czekajacy[1], czekajacy[2]);
      if (msgsnd(komid, &msg, sizeof(msg) - sizeof(long), 0) == -1) { //wysyla odpowiedz do klienta
         perror("Blad przy wysylaniu odpowiedzi");
      }
      printf("Kasjer wyslal odpowiedz: %d\n", msg.status);

      for (int i=0; i<3; i++){ //zamykanie watku kas, jezeli juz sa gotowe do zamkniecia
         if (aktywnosc[i] == 2) {
            pthread_join(kasjerzy[i], NULL);
            aktywnosc[i] = 0;
         }
      }
   }
}
