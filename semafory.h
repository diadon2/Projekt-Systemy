#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>

static void sem_p(int semafor, int n) {
   struct sembuf bufor_sem = {n, -1, 0};
   if (semop(semafor, &bufor_sem, 1) == -1) {
      if (errno == EINTR) {
         sem_p(semafor, n);
      } else {
         perror("Blad podczas zamykania semafora");
         exit(EXIT_FAILURE);
      }
   }
   //printf("Semafor %d, %d zostal zamkniety\n", semafor, n);
}

static void sem_v(int semafor, int n) {
   struct sembuf bufor_sem = {n, 1, 0};
   if (semop(semafor, &bufor_sem, 1) == -1) {
      perror("Blad podczas otwierania semafora");
      exit(EXIT_FAILURE);
   }
   //printf("Semafor %d, %d zostal otwarty\n", semafor, n);
}

static void usun_semafor(int semafor) {
   struct semid_ds sem_info;
   if (semctl(semafor, 0, IPC_STAT, &sem_info) == -1) {
      perror("Blad podczas uzyskiwania informacji o semaforze");
      return;
   }
   if (sem_info.sem_otime == 0) {
      if (semctl(semafor, 0, IPC_RMID) == -1) {
         perror("Blad podczas usuwania semafora");
      } else {
         printf("Semafor zostal usuniety\n");
      }
   } else {
      printf("Semafor jest nadal w uzytku\n");
   }
}

static void utworz_semafor(int* semafor, key_t key, int n) {
   *semafor = semget(key, n, 0666|IPC_CREAT);
   if (*semafor == -1) {
      perror("Blad podczas tworzenia semafora");
      exit(EXIT_FAILURE);
   }
   else {
      printf("Semafor %d zostal utworzony\n", *semafor);
   }
}
