#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>

#define FILEPRINT(...) do { \
    sem_wait(printMutex); \
    (*Astage)++; \
    fprintf(file, __VA_ARGS__); \
    fflush(file); \
    sem_post(printMutex); \
} while(0)

// Global input
int L;
int Z;
int K;
int TL;
int TB;
FILE *file;

//Binary semaphores
sem_t *allAboard;
sem_t *printMutex;
sem_t *waitForNext;

//Auxiliary variables
int *currentStop;
int *howMuchDone;
int *skiersOnStop;
int *Astage;

//Pseudo-random generator from 0-max (including 0 and max)
int randInt(int max) {
    if (max < 0) {
        fprintf(stderr, "max must nonegative");
        exit(1);
    }
    return (rand() % (max + 1));
}

void skibus_process(sem_t **stops_s) {
    bool noSkip; //Dummy auxiliary variable to not repeat leaving message.

    usleep(randInt(TB));
    FILEPRINT("%d: BUS: started\n", *Astage);
    do {
        sem_wait(waitForNext); //Let the skiers who didn't get in because of the limit, wait.
        for (*currentStop = 1; (*currentStop) <= Z; (*currentStop)++) {
            noSkip = false;
            usleep(randInt(TB));
            FILEPRINT("%d: BUS: arrived to %d\n", *Astage, *currentStop);
            
            //Don't release currentStop when empty or full skibus.
            //skiersOnStop[0] <=> number of skiers in skibus.
            if ((skiersOnStop[*currentStop] != 0) && (skiersOnStop[0] != K)) {
                sem_post(stops_s[*currentStop]);    //Release currentStop.
                sem_wait(allAboard);                //Wait for all to board.
                sem_wait(stops_s[*currentStop]);    //Take back currentStop
                FILEPRINT("%d: BUS: leaving %d\n", *Astage, *currentStop);
                noSkip = true;
            }
            if (!noSkip) {
                FILEPRINT("%d: BUS: leaving %d\n", *Astage, *currentStop);
            }
        }

        usleep(randInt(TB));
        FILEPRINT("%d: BUS: arrived to final\n", *Astage);
        noSkip = false; //Unrelated to the previous noSkip.
        if (skiersOnStop[0]) {      
            sem_post(stops_s[0]);
            sem_wait(allAboard);
            noSkip = true;
        }
        FILEPRINT("%d: BUS: leaving final\n", *Astage);
        if (noSkip) {
            sem_wait(stops_s[0]);
        }
        sem_post(waitForNext);
    } while (*howMuchDone != L);

    FILEPRINT("%d: BUS: finish\n", *Astage);
    
}

void lyzar_process(int idL, sem_t **stops_s) {
    int idZ = randInt(Z-1)+1; // Random stop in range 1-Z (including 1 and Z).

    FILEPRINT("%d: L %d: started\n", *Astage, idL);
    usleep(randInt(TL));
    FILEPRINT("%d: L %d: arrived to %d\n", *Astage, idL, idZ);
    skiersOnStop[idZ]++;
    while (1) {
        sem_wait(stops_s[idZ]);

        if (skiersOnStop[0] == K) { //If no more space in skibus.
            sem_post(stops_s[idZ]); //Release currentStop.
            sem_wait(waitForNext);  //Lock yourself until next round.
            sem_post(waitForNext);
            continue;
        }
        
        FILEPRINT("%d: L %d: boarding\n", *Astage, idL);
        skiersOnStop[idZ]--;
        skiersOnStop[0]++; //Number of people in skibus.
        (*howMuchDone)++;  //How much skiers were in skibus.
        
        if ((skiersOnStop[0] == K) || (skiersOnStop[idZ] == 0)) {
            sem_post(allAboard); //Last or no more space in skibus so unlock.
        }
        sem_post(stops_s[idZ]);  //Unlock currentStop.
        break;
        
    }

    //stops_s[0] <=> final "stop"
    sem_wait(stops_s[0]); 
    FILEPRINT("%d: L %d: going to ski\n", *Astage, idL);
    skiersOnStop[0]--;
    if (skiersOnStop[0] == 0){  //Everyone is out, unlock 
        sem_post(allAboard);
    }    
    sem_post(stops_s[0]);
}

void DESTROY() {
    munmap(currentStop, sizeof(int));
    munmap(skiersOnStop, (Z+1)*sizeof(int));
    munmap(howMuchDone, sizeof(int));
    munmap(Astage, sizeof(int));

    // Closure and removal of named semaphores.
    char sem_name[20];
    for (int i = 0; i < Z+1; i++) {
        sprintf(sem_name, "/stop%d", i);
        sem_unlink(sem_name);
    }
    fclose(file);
}

void main_process(sem_t** stops_s) {
    pid_t skibus_pid;
    pid_t lyzari_pid[L];

    // Initializing semaphores and shared memory.
    //----------------------------------------------------------------------------------------------------
    // Semaphores
    allAboard = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sem_init(allAboard, 1, 0) == -1) {
        fprintf(stderr, "Semaphore allAboard failed to initialize.\n");
        exit(1);
    }

    printMutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sem_init(printMutex, 1, 1) == -1) {
        fprintf(stderr, "Semaphore printMutex failed to initialize.\n");
        sem_destroy(allAboard);
        exit(1);
    } 
    
    waitForNext = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sem_init(waitForNext, 1, 1) == -1) {
        sem_destroy(allAboard);
        sem_destroy(printMutex);
        fprintf(stderr, "Semaphore waitForNext failed to mmap.\n");
        exit(1);
    }

    // Shared memory
    currentStop = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    skiersOnStop = mmap(NULL, (Z+1)*sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    howMuchDone = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    Astage = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (currentStop == MAP_FAILED || skiersOnStop == MAP_FAILED || howMuchDone == MAP_FAILED || Astage == MAP_FAILED) {
        DESTROY();
        fprintf(stderr, "Creating shared memory failed.\n");
        exit(1);
    }

    // Initialization of named semaphores for stops.
    for (int i = 0; i < Z+1; i++) {
        char sem_name[20];
        sprintf(sem_name, "/stop%d", i);
        stops_s[i] = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 0);
        if (stops_s[i] == SEM_FAILED) {
            fprintf(stderr, "Creating named semaphore failed.\n");
            DESTROY();
            exit(1);
        }
    }
    //----------------------------------------------------------------------------------------------------



    // Creating skibus process.
    skibus_pid = fork();
    if (skibus_pid > 0) {
        // SKIBUS PROCESS
        srand(time(NULL) ^ getpid());
        skibus_process(stops_s);
        exit(0);
    } else if (skibus_pid < 0) {
        fprintf(stderr, "Error when creating skibus\n");
        exit(1);
    }

    // Creating skier processes.
    for (int i = 0; i < L; i++) {
        lyzari_pid[i] = fork();
        if (lyzari_pid[i] > 0) {
            // SKIER PROCESS
            srand(time(NULL) ^ getpid());
            lyzar_process(i+1, stops_s);
            exit(0);
        } else if (lyzari_pid[i] < 0) {
            fprintf(stderr, "Error when creating skier %d.\n", i + 1);
            exit(1);
        }
    }

    // Waiting for the completion of all skiers.
    for (int i = 0; i < L; i++) {
        waitpid(lyzari_pid[i], NULL, 0);
    }

    // Waiting for the completion of skibus.
    waitpid(skibus_pid, NULL, 0);

    // Releasing semaphores and shared memory.
    sem_destroy(allAboard);
    sem_destroy(printMutex);
    sem_destroy(waitForNext);
    DESTROY();
}


bool isNumber(char number[]) {
    int i = 0;

    // Checking for negative numbers.
    if (number[0] == '-')
        i = 1;
    for (; number[i] != 0; i++) {
        // if (number[i] > '9' || number[i] < '0')
        if (!isdigit(number[i]))
            return false;
    }
    return true;
}

int main(int argc, char *argv[]) {

    if (argc != 6) {
        fprintf(stderr, "Too few arguments.\n");
        return 1;
    }

    file = fopen("proj2.out", "w");
    if (file == NULL) {
        fprintf(stderr, "File canot be opened or created.\n");
        return 1;
    }

    if (!(isNumber(argv[1]) && isNumber(argv[2]) && isNumber(argv[3]) && isNumber(argv[4]) && isNumber(argv[5]))) {
        fprintf(stderr, "Wrong input, input must be an integer.\n");
        return 1;
    }

    L = atoi(argv[1]);
    Z = atoi(argv[2]);
    K = atoi(argv[3]);
    TL = atoi(argv[4]);
    TB = atoi(argv[5]);

    if (L>=20000 || Z>10 || K<10 || K>100 || TL>10000 || TB>1000) {
        fprintf(stderr, "Wrong input, input is out of range.\n");
        return 1;
    }
    
    sem_t *stops_s[Z+1];
    
    main_process(stops_s);
    return 0;
}