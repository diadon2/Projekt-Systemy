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
#include <sys/msg.h>
#include <signal.h>
#include "semafory.h"

#define ILOSC_PRODUKTOW 12
int CZAS_WYPIEKANIA;

struct produkt {
   char* pieczywo;
   struct produkt* n;
};

struct podajnik {
   struct produkt* p;
   int ilosc;
   int pojemnosc;
   pthread_mutex_t mutex;
};

struct st_podajnik {
   int index;
   char pieczywo[24];
}; //struktura do komunikacji podajnikow z innymi procesami

struct komunikat {
    long mtype;
    int status;
};

const char* produkty[ILOSC_PRODUKTOW] = {
      "Chleb pszenny", "Chleb zytni", "Chleb razowy", "Chleb wieloziarnisty",
      "Bulka kajzerka", "Bulka grahamka", "Bulka zwykla", "Bagietka",
      "Obwarzanek", "Precel", "Bajgiel", "Croissant"
};

struct podajnik podajniki[ILOSC_PRODUKTOW];
struct st_podajnik* przestrzen_podajnik = NULL;
int raport[ILOSC_PRODUKTOW];
int pam_podajnik;
int sem_podajnik;
int sem_kier;
int kom_kier;
pthread_t id_obsluga;
int running = 1;
int otwarcie = 0;
int inwent = 0;

int los(int min, int max){
   if (min > max) {
      errno = EINVAL;
      perror("Bledne wartosci funkcji los - min i max odwrocone");
      return (rand() % (min - max + 1)) + max;
   }
   return (rand() % (max - min + 1)) + min;
}

void dodaj_produkt(int index) {
   pthread_mutex_lock(&podajniki[index].mutex);
   if (podajniki[index].pojemnosc <= podajniki[index].ilosc) {
      printf("Brak miejsca na podajniku %d, produkt nie zostanie dodany.\n", index);
      pthread_mutex_unlock(&podajniki[index].mutex);
      return;
   }
   struct produkt* new = malloc(sizeof(struct produkt));
   if (!new) {
      perror("Blad alokacji pamieci dla struktury produkt");
      pthread_mutex_unlock(&podajniki[index].mutex);
      exit(EXIT_FAILURE);
   }
   new->pieczywo = malloc(strlen(produkty[index]) + 1);
   if (!new->pieczywo){
      printf("Blad alokacji pamieci dla pieczywa");
      free(new);
      pthread_mutex_unlock(&podajniki[index].mutex);
      exit(EXIT_FAILURE);
   }
   strcpy(new->pieczywo, produkty[index]);
   new->n = NULL;
   if (podajniki[index].p == NULL){
      podajniki[index].p = new;
   } else {
      struct produkt* temp = podajniki[index].p;
      while(temp->n != NULL) temp = temp->n;
      temp->n = new;
   }
   podajniki[index].ilosc++;
   raport[index]++;
   printf("Dodano produkt na podajnik %d\n", index);
   pthread_mutex_unlock(&podajniki[index].mutex);
}

char* odbierz_produkt(int index){
   pthread_mutex_lock(&podajniki[index].mutex);
   if (podajniki[index].ilosc == 0){
      pthread_mutex_unlock(&podajniki[index].mutex);
      return NULL;
   }
   char* odebrany = podajniki[index].p->pieczywo;
   struct produkt* temp = podajniki[index].p;
   podajniki[index].p = podajniki[index].p->n;
   podajniki[index].ilosc--;
   if (temp) free(temp);
   pthread_mutex_unlock(&podajniki[index].mutex);
   return odebrany;
}

void inicjalizacja_podajnika(int index){
   podajniki[index].pojemnosc = los(9,17);
   podajniki[index].ilosc = 0;
   podajniki[index].p = NULL;
   pthread_mutex_init(&podajniki[index].mutex, NULL);
}

void usun_linie(struct produkt* head, int index){
   if (head == NULL) return;
   pthread_mutex_lock(&podajniki[index].mutex);
   struct produkt* temp = head;
   while (head != NULL){
      temp = head->n;
      if (head->pieczywo) free(head->pieczywo);
      if (head) free(head);
      head = temp;
   }
   podajniki[index].p = NULL;
   podajniki[index].ilosc = 0;
   pthread_mutex_unlock(&podajniki[index].mutex);
}

void wypiekanie(){
   int ilosc = los(1, ILOSC_PRODUKTOW);
   int ile = 0;
   int pool[ILOSC_PRODUKTOW];
   for (int i=0; i<ILOSC_PRODUKTOW; i++) pool[i]=i;
   for (int i = ILOSC_PRODUKTOW - 1; i > 0; i--) {
        int j = los(0, i);
        int temp = pool[i];
        pool[i] = pool[j];
        pool[j] = temp;
    }
   for (int i=0; i<ilosc; i++){
      ile = los(1, 3);
      for (int j=0; j<ile; j++){
         dodaj_produkt(pool[i]);
      }
   }
}

void cleanup(){
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      if (podajniki[i].p) usun_linie(podajniki[i].p, i);
      pthread_mutex_destroy(&podajniki[i].mutex);
   }
   if (shmdt(przestrzen_podajnik) == -1) {
      perror("Blad podczas odlaczania pamieci dzielonej");
   }
}

void *obsluga_klientow(void* arg){ //watek obsluga_klientow zarzadza odbieraniem produktow z podajnikow
   printf("Watek obsluga dziala\n");
   sem_v(sem_podajnik, 0); //obsluga dostepu do podajnikow dziala
   int podajnik_id = -1;
   char* produkt = NULL;
   while (running) {
      sem_p(sem_podajnik, 1); //czekamy na sygnal do dzialania lub konca programu
      if (running == 0) break;
      memset(przestrzen_podajnik->pieczywo, 0, sizeof(przestrzen_podajnik->pieczywo)); //robimy miejsce na pamieci dzielonej
      podajnik_id = przestrzen_podajnik->index; //zbieramy produkt ktory chcemy odebrac
      if (podajnik_id > -1 && podajnik_id < (ILOSC_PRODUKTOW + 1)) {
         produkt = odbierz_produkt(podajnik_id);
      }
      else {
         produkt = NULL;
      }
      if (produkt != NULL) { //jezeli znalezlismy produkt to wrzucamy go na pamiec dzielona
         strncpy(przestrzen_podajnik->pieczywo, produkt, sizeof(przestrzen_podajnik->pieczywo) - 1);
         przestrzen_podajnik->pieczywo[sizeof(przestrzen_podajnik->pieczywo) - 1] = '\0';
         free(produkt);
      }
      if (running == 0) break;
      sem_v(sem_podajnik, 2);
   }
}

void inwentaryzacja(int sig){
   printf("Sygnal %d - inwentaryzacja po zamnkeciu\n", sig);
   inwent = 1;
}

void otwieranie_zamykanie(int sig){
   if (otwarcie == 1){ //zamykanie
      printf("Sygnal %d - zamykanie\n", sig);
      if (inwent == 1){
         for (int i=0; i<ILOSC_PRODUKTOW; i++) printf("Ilosc wyprodukowanych %s: %d\n", produkty[i], raport[i]);
      }
      otwarcie = 0;
      sem_p(sem_kier, 1);
      for (int i=0; i<ILOSC_PRODUKTOW; i++){
         usun_linie(podajniki[i].p, i);
      }
   } else { //otwieranie
      printf("Sygnal %d - otwieranie\n", sig);
      for (int i=0; i<ILOSC_PRODUKTOW; i++) {
         raport[i] = 0;
      }
      otwarcie = 1;
   }
}
void exit_handler(int sig){
   printf("Sygnal %d - koniec programu\n", sig);
   running = 0;
   sem_v(sem_podajnik, 1);
   pthread_join(id_obsluga, NULL);
   cleanup();
   sem_v(sem_kier, 1);
   exit(EXIT_SUCCESS);
}


int main(int argc, char** argv){
   struct sigaction sa;
   sigset_t block_mask, orig_mask;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = 0;
   sa.sa_handler = exit_handler;
   sigaction(SIGTERM, &sa, NULL);
   sigaction(SIGINT, &sa, NULL);
   sa.sa_handler = inwentaryzacja;
   sigaction(SIGUSR1, &sa, NULL);
   sa.sa_handler = otwieranie_zamykanie;
   sigaction(SIGUSR2, &sa, NULL);
   sigemptyset(&block_mask);
   sigaddset(&block_mask, SIGUSR1);
   sigaddset(&block_mask, SIGUSR2);
   sigaddset(&block_mask, SIGINT);
   sigaddset(&block_mask, SIGTERM);

   srand(time(NULL));

   printf("Podaj czas wypiekania przez piekarza (>0, default: 30 w przypadku blednego inputu\n");
   scanf("%d", &CZAS_WYPIEKANIA);
   if (0 > CZAS_WYPIEKANIA) {
      printf("Bledna wartosc CZAS_WYPIEKANIA - ustawianie na default\n");
      CZAS_WYPIEKANIA = 30;
   }
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      inicjalizacja_podajnika(i);
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
   key_t key_sem_kier = ftok(".", 'R');
   utworz_semafor(&sem_kier, key_sem_kier, 5);
   key_t key_kom_kier = ftok(".", 'M');
   kom_kier = msgget(key_kom_kier, IPC_CREAT | 0222);
   if (kom_kier == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
//koniec inicjalizacji
   sem_p(sem_kier, 1);
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = getpid(); //wiadomosc dla kierownika
   if (msgsnd(kom_kier, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
      perror("Blad przy wysylaniu komunikatu");
      exit(EXIT_FAILURE);
   }
   printf("Program piekarz wyslal komunikat: %d\n", msg.status);

   if (pthread_create(&id_obsluga, NULL, obsluga_klientow, NULL) != 0){
      perror("Blad podczas tworzenia watku obslugi");
      exit(EXIT_FAILURE);
   }

   while (1) {
      while (otwarcie) { //co okreslony czas wypieka produkty i dodaje do podajnikow
         sigprocmask(SIG_BLOCK, &block_mask, &orig_mask);
         wypiekanie();
         sigprocmask(SIG_SETMASK, &orig_mask, NULL);
         sleep(CZAS_WYPIEKANIA);
         printf("\n");
      }
   sleep(1);
   }
}
