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
};

const char* produkty[ILOSC_PRODUKTOW] = {
      "Chleb pszenny", "Chleb zytni", "Chleb razowy", "Chleb wieloziarnisty",
      "Bulka kajzerka", "Bulka grahamka", "Bulka zwykla", "Bagietka",
      "Obwarzanek", "Precel", "Bajgiel", "Croissant"
};

struct podajnik podajniki[ILOSC_PRODUKTOW];
struct komunikacja* przestrzen = NULL;
int pamiec;
int sem_pam;
pthread_t id_obsluga;

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

void usun_linie(struct produkt* head){
   if (head == NULL) return;
   struct produkt* temp = head;
   while (head != NULL){
      temp = head->n;
      free(head->pieczywo);
      free(head);
      head = temp;
   }
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
      usun_linie(podajniki[i].p);
      pthread_mutex_destroy(&podajniki[i].mutex);
   }
   if (shmdt(przestrzen) == -1) {
      perror("Blad podczas odlaczania pamieci");
   }
   else {
      printf("Pamiec dzielona zostala odlaczona\n");
   }
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

void *obsluga_klientow(void* arg){
   printf("Watek obsluga dziala\n");
   sem_v(sem_pam, 0);
   int podajnik_id = -1;
   char* produkt = NULL;
   while (1) {
      printf("czeka\n");
      sem_p(sem_pam, 1);
      memset(przestrzen->pieczywo, 0, sizeof(przestrzen->pieczywo));
      podajnik_id = przestrzen->index;
      if (podajnik_id != -1) {
         produkt = odbierz_produkt(podajnik_id);
      }
      else {
         produkt = NULL;
      }
      if (produkt != NULL) {
         strncpy(przestrzen->pieczywo, produkt, sizeof(przestrzen->pieczywo) - 1);
         przestrzen->pieczywo[sizeof(przestrzen->pieczywo) - 1] = '\0';
      }
      sem_v(sem_pam, 2);
   }
}

void exit_handler(int sig){
   if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT){
      pthread_cancel(id_obsluga);
      pthread_join(id_obsluga, NULL);
      cleanup();
      exit(EXIT_SUCCESS);
   }
}


int main(int argc, char** argv){
   signal(SIGINT, exit_handler);
   signal(SIGTERM, exit_handler);
   signal(SIGQUIT, exit_handler);
   srand(time(NULL));
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      inicjalizacja_podajnika(i);
   }
   key_t key_pam = ftok(".", 'T');
   if ((pamiec = shmget(key_pam, 32, 0666|IPC_CREAT)) == -1) {
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

   int czas;
   if (pthread_create(&id_obsluga, NULL, obsluga_klientow, NULL) != 0){
      perror("Blad podczas tworzenia watku obslugi");
      exit(EXIT_FAILURE);
   }
   while (1) {
      printf("Wypiekanie\n");
      czas = los(10, 50);
      wypiekanie();
      printf("\n");
      sleep(5);
   }
}
