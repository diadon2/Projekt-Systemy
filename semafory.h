#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>

static void sem_p(int semafor, int n) {
   int zmien_sem;
   struct sembuf bufor_sem;
   bufor_sem.sem_num = n;
   bufor_sem.sem_op = -1;
   bufor_sem.sem_flg = 0;
   zmien_sem=semop(semafor, &bufor_sem, 1);
   if (zmien_sem == -1) {
      if (errno == EINTR) {
         sem_p(semafor, n);
      }
      else {
         perror("Blad podczas zamykania semafora");
         exit(EXIT_FAILURE);
      }
   }
   else {
      //printf("Semafor %d, %d zostal zamkniety", semafor, n);
   }
}

static void sem_v(int semafor, int n) {
   int zmien_sem;
   struct sembuf bufor_sem;
   bufor_sem.sem_num = n;
   bufor_sem.sem_op = 1;
   bufor_sem.sem_flg = SEM_UNDO;
   zmien_sem=semop(semafor, &bufor_sem, 1);
   if (zmien_sem == -1) {
      perror("Blad podczas otwierania semafora");
      exit(EXIT_FAILURE);
   }
   else {
      //printf("Semafor %d, %d zostal otwarty\n", semafor, n);
   }
}

static void usun_semafor(int semafor) {
   int sem;
   sem = semctl(semafor, 0, IPC_RMID);
   if (sem == -1) {
      perror("Blad podczas usuwania semafora");
      exit(EXIT_FAILURE);
   }
   else {
      //printf("Semafor %d zostal usuniety\n", semafor);
   }
}

static void utworz_semafor(int* semafor, key_t key, int n) {
   *semafor = semget(key, n, 0666|IPC_CREAT);
   if (*semafor == -1) {
      perror("Blad podczas tworzenia semafora");
      exit(EXIT_FAILURE);
   }
   else {
      //printf("Semafor %d zostal utworzony\n", *semafor);
   }
   for (int i = 0; i < n; i++) {
      if (semctl(*semafor, i, SETVAL, 0) == -1) {
         perror("Blad podczas ustawiania wartosci semafora");
         exit(EXIT_FAILURE);
      }
   }
}
