#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define ILOSC_PRODUKTOW 12

struct zakupy {
   int id;
   int ilosc;
};

int los(int min, int max){
   if (min > max) {
      errno = EINVAL;
      perror("Bledne wartosci funkcji los - min i max odwrocone");
      return (rand() % (min - max + 1)) + max;
   }
   return (rand() % (max - min + 1)) + min;
}

struct zakupy* lista_zakupow(){
   int ilosc = los(2,5);
   int ile;
   struct* zakupy = malloc(sizeof(zakupy) * ilosc);
   int pool[ILOSC_PRODUKTOW];
   for (int i=0; i<ILOSC_PRODUKTOW; i++) pool[i]=i;
   for (int i = ILOSC_PRODUKTOW - 1; i > 0; i--) {
        int j = los(0, i);
        int temp = pool[i];
        pool[i] = pool[j];
        pool[j] = temp;
    }
   for (int i=0; i<ilosc; i++){
      zakupy->id = pool[i];
      zakupy->ilosc = los(1, 3);
   }
}

int main(int argc, char** argv){
}
