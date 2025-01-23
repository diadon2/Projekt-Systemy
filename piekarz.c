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
#define CZAS_WYPIEKANIA 30

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

struct komunikacja {
   int index;
   int ilosc;
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
struct komunikacja* przestrzen = NULL;
int raport[ILOSC_PRODUKTOW];
int pamiec;
int sem_pam;
int sem_kier;
int id_komunikat;
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
      exit(EXIT_FAILURE);
   }
   new->pieczywo = malloc(strlen(produkty[index]) + 1);
   if (!new->pieczywo){
      printf("Blad alokacji pamieci dla pieczywa");
      free(new);
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
   free(temp);
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
      usun_linie(podajniki[i].p, i);
      pthread_mutex_destroy(&podajniki[i].mutex);
   }
   if (shmdt(przestrzen) == -1) {
      perror("Blad podczas odlaczania pamieci dzielonej");
   }
}

void *obsluga_klientow(void* arg){ //watek obsluga_klientow zarzadza odbieraniem produktow z podajnikow
   printf("Watek obsluga dziala\n");
   sem_v(sem_pam, 0); //obsluga dostepu do podajnikow dziala
   int podajnik_id = -1;
   char* produkt = NULL;
   while (running) {
      sem_p(sem_pam, 1); //czekamy na sygnal do dzialania lub konca programu
      if (running == 0) break;
      memset(przestrzen->pieczywo, 0, sizeof(przestrzen->pieczywo)); //robimy miejsce na pamieci dzielonej
      podajnik_id = przestrzen->index; //zbieramy produkt ktory chcemy odebrac
      if (podajnik_id > -1 && podajnik_id < (ILOSC_PRODUKTOW + 1)) {
         produkt = odbierz_produkt(podajnik_id);
      }
      else {
         produkt = NULL;
      }
      if (produkt != NULL) { //jezeli znalezlismy produkt to wrzucamy go na pamiec dzielona
         strncpy(przestrzen->pieczywo, produkt, sizeof(przestrzen->pieczywo) - 1);
         przestrzen->pieczywo[sizeof(przestrzen->pieczywo) - 1] = '\0';
      }
      sem_v(sem_pam, 2);
   }
}

void inwentaryzacja(int sig){
   printf("Sygnal %d - inwentaryzacja po zamnkeciu\n", sig);
   inwent = 1;
}

void otwieranie_zamykanie(int sig){
   if (otwarcie == 1){ //zamykanie
      if (inwent == 1){
         for (int i=0; i<ILOSC_PRODUKTOW; i++) printf("Ilosc wyprodukowanych %s: %d\n", produkty[i], raport[i]);
      }
      otwarcie = 0;
      sem_p(sem_kier, 1);
      for (int i=0; i<ILOSC_PRODUKTOW; i++){
         usun_linie(podajniki[i].p, i);
      }
   } else { //otwieranie
      for (int i=0; i<ILOSC_PRODUKTOW; i++) {
         raport[i] = 0;
         podajniki[i].p = NULL;
      }
      otwarcie = 1;
   }
}
void exit_handler(int sig){
   printf("Sygnal %d - koniec programu\n", sig);
   printf("exit - running\n");
   running = 0;
   printf("exit - sem_pam, 1\n");
   sem_v(sem_pam, 1);
   printf("exit - pthread join\n");
   pthread_join(id_obsluga, NULL);
   printf("exit - cleanup\n");
   cleanup();
   sem_v(sem_kier, 1);
   exit(EXIT_SUCCESS);
}


int main(int argc, char** argv){
   signal(SIGINT, exit_handler);
   signal(SIGTERM, exit_handler);
   signal(SIGUSR1, inwentaryzacja);
   signal(SIGUSR2, otwieranie_zamykanie);

   srand(time(NULL));

   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      inicjalizacja_podajnika(i);
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
   key_t key_semkier = ftok(".", 'R');
   utworz_semafor(&sem_kier, key_semkier, 5);
   key_t key_mkom = ftok(".", 'M');
   id_komunikat = msgget(key_mkom, IPC_CREAT | 0666);
   if (id_komunikat == -1) {
      perror("Blad przy tworzeniu kolejki komunikatow");
      exit(EXIT_FAILURE);
   }
//koniec inicjalizacji
   sem_p(sem_kier, 1);
   struct komunikat msg;
   msg.mtype = 1;
   msg.status = getpid(); //wiadomosc dla kierownika
   if (msgsnd(id_komunikat, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
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
         wypiekanie();
         sleep(CZAS_WYPIEKANIA);
         printf("\n");
      }
   }
}
