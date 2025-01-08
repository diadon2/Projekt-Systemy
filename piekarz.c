#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#define ILOSC_PRODUKTOW 12

struct produkt {
   char* pieczywo;
   struct produkt* n;
};

const char* produkty[ILOSC_PRODUKTOW] = {
      "Chleb pszenny", "Chleb zytni", "Chleb razowy", "Chleb wieloziarnisty",
      "Bulka kajzerka", "Bulka grahamka", "Bulka zwykla", "Bagietka",
      "Obwarzanek", "Precel", "Bajgiel", "Croissant"
};

int max_podajnik[ILOSC_PRODUKTOW] = {0};
int ilosc_podajnik[ILOSC_PRODUKTOW] = {0};
struct produkt** podajniki = NULL;

int los(int min, int max){
   if (min > max) {
      errno = EINVAL;
      perror("Bledne wartosci funkcji los - min i max odwrocone");
      return (rand() % (min - max + 1)) + max;
   }
   return (rand() % (max - min + 1)) + min;
}

char* odbierz_produkt(int index){
   if (podajniki[index] == NULL) {
      printf("Brak produktu do odebrania\n");
      return NULL;
   }
   struct produkt* temp = podajniki[index];
   podajniki[index] = podajniki[index]->n;
   char* odbierany = malloc(strlen(temp->pieczywo) + 1);
   if (!odbierany) {
      perror("Blad alokacji pamieci dla odbierany\n");
      temp->n = podajniki[index];
      podajniki[index] = temp;
      return NULL;
   } else (strcpy(odbierany, temp->pieczywo));
   free(temp->pieczywo);
   free(temp);
   ilosc_podajnik[index]--;
   return odbierany;
}

void dodaj_produkt(int index){
   if (ilosc_podajnik[index] >= max_podajnik[index]) {
      printf("Brak miejsca na podajniku %d, produkt nie zostanie dodany.\n", index);
      return;
   }
   struct produkt* new = malloc(sizeof(struct produkt));
   if (!new) {
      perror("Blad alokacji pamieci dla struktury produkt\n");
      return;
   }
   new->pieczywo = malloc(strlen(produkty[index]) + 1);
   if (!new->pieczywo){
      printf("Blad alokacji pamieci dla pieczywa\n");
      free(new);
      return;
   }
   strcpy(new->pieczywo, produkty[index]);
   new->n = NULL;
   if (podajniki[index] == NULL){
      podajniki[index] = new;
   } else {
      struct produkt* temp = podajniki[index];
      while(temp->n != NULL) temp = temp->n;
      temp->n = new;
   }
   ilosc_podajnik[index]++;
   printf("Dodano produkt na podajnik %d\n", index);
}

void usun_linie(struct produkt** head){
   if (*head == NULL) return;
   struct produkt* temp = *head;
   while (*head != NULL){
      temp = (*head)->n;
      free((*head)->pieczywo);
      free(*head);
      *head=temp;
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
      ile = los(1, 4);
      for (int j=0; j<ile; j++){
         dodaj_produkt(pool[i]);
      }
   }
}

void cleanup(){
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      usun_linie(&podajniki[i]);
   }
   free(podajniki);
   podajniki = NULL;
}



int main(int argc, char** argv){
   podajniki = malloc(sizeof(struct produkt*) * ILOSC_PRODUKTOW);
   if (!podajniki) {
      perror("Blad alokacji pamieci dla podajniki\n");
      return 1;
   }
   srand(time(0));
   for (int i=0; i<ILOSC_PRODUKTOW; i++){
      max_podajnik[i]=los(6,14);
      podajniki[i] = NULL;
   }
   for (int i=0; i<10; i++) {
      printf("Wypiekamy\n");
      wypiekanie();
      sleep(1);
      printf("\n\n");
   }
   cleanup();
}
