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
   char* pieczywo;
};

const char* produkty[ILOSC_PRODUKTOW] = {
      "Chleb pszenny", "Chleb zytni", "Chleb razowy", "Chleb wieloziarnisty",
      "Bulka kajzerka", "Bulka grahamka", "Bulka zwykla", "Bagietka",
      "Obwarzanek", "Precel", "Bajgiel", "Croissant"
};

struct podajnik podajniki[ILOSC_PRODUKTOW];
struct komunikacja* przestrzen;
int pamiec;
int sem_pam;

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
   }
   int odlaczenie1, odlaczenie2;
   odlaczenie1 = shmctl(pamiec, IPC_RMID, 0);
   odlaczenie2 = shmdt(przestrzen);
   if (odlaczenie1 == -1 || odlaczenie2 == -1){
      perror("Blad podczas odlaczania pamieci");
      exit(EXIT_FAILURE);
   }
   usun_semafor(sem_pam);
}

void *obsluga_klientow(void* arg){
   while (1) {
      int podajnik_id = -1;
      sem_p(sem_pam, 2); //czekamy na sygnal od klienta, ze chce odebrac produkt
      sem_p(sem_pam, 1); //czekamy az pamiec dzielona jest dostepna
   }
}


int main(int argc, char** argv){
   srand(time(0));
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      inicjalizacja_podajnika(i);
   }
   key_t key_pam = ftok(".", 'T');
   if (pamiec = shmget(key_pam, 32, 0222|IPC_CREAT) == -1) {
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
   utworz_semafor(&sem_pam, key_sem, 2);

   int czas;
   for (int i=0; i<10; i++) {
      printf("Wypiekanie\n");
      czas = los(10, 50);
      wypiekanie();
      printf("\n");
      sleep(czas);
   }

   cleanup();
}
