/**
 * 
 * https://people.cs.rutgers.edu/~pxk/417/notes/images/clocks-vector.png
 * 
 * Compilação: mpicc -o etapa3OF etapa3OF.c
 * Execução:   mpiexec -n 3 ./etapa3OF
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mpi.h>
#include <unistd.h>

#define BUFFER_SIZE 30
#define THREAD_NUM 3 // Thread de recepção, relógios vetoriais e envio
#define NUM_PROCESSES 3

typedef struct {
    int p[NUM_PROCESSES];
    int dest;
} Clock;

Clock recvQueue[BUFFER_SIZE];  // Fila para armazenar relógios recebidos
Clock sendQueue[BUFFER_SIZE];  // Fila para armazenar relógios para envio
int recvCount = 0, 
    sendCount = 0;

pthread_mutex_t recvMutex, sendMutex;
pthread_cond_t recvNotEmpty, recvNotFull, sendNotEmpty, sendNotFull;

int rank, size, allRecv = 0, allSend = 0;

void Event(Clock *clock){
    clock->p[rank]++;
    printf("Processo %d, Clock (após Event): (%d, %d, %d)\n", rank, clock->p[0], clock->p[1], clock->p[2]);

}

void Send(int dest, Clock *clock){
    clock->dest = dest;
    clock->p[rank]++;

    pthread_mutex_lock(&sendMutex); // sendMutex é usado para verificar e alterar sendCount
    if (sendCount == BUFFER_SIZE) {
        pthread_cond_wait(&sendNotFull, &sendMutex);
    }
    sendQueue[sendCount++] = *clock;
    pthread_mutex_unlock(&sendMutex);
    pthread_cond_signal(&sendNotEmpty);
}

void Receive(Clock *clock){
    clock->p[rank]++; // nem a thread de recepeção nem a de envio vão disputar pela atualização do valor de p[rank], então
   // não precisa estar dentro do mutex
    pthread_mutex_lock(&recvMutex);
        if (recvCount == 0) {
            pthread_cond_wait(&recvNotEmpty, &recvMutex);
        }
        Clock receivedClock = recvQueue[0];
        for (int i = 0; i < recvCount - 1; i++) {
            recvQueue[i] = recvQueue[i + 1];
        }
        recvCount--;
        pthread_mutex_unlock(&recvMutex);
        pthread_cond_signal(&recvNotFull);
   
   for (int i = 0; i < NUM_PROCESSES; i++) {
        if (receivedClock.p[i] > clock->p[i]) {
            clock->p[i] = receivedClock.p[i];
        }
    }
    printf("Processo %d, Clock (após Receive): (%d, %d, %d)\n", rank, clock->p[0], clock->p[1], clock->p[2]);
}

void *receivingThread(void *arg) {
        while (1) {
            if(rank == 0 && allRecv > 1)
                break;
            if(rank == 1 && allRecv > 1)
                break;
            if(rank == 2 && allRecv > 0) // teria que mudar a lógica desses if's caso mudasse o nº de recepções de cada processo
                break;                  // ou então poderíamos forçar o término do processo de dada thread ao fim da execução da thread de processamento
            Clock clock;
            MPI_Recv(&clock.p, NUM_PROCESSES, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    
            pthread_mutex_lock(&recvMutex);
            if (recvCount == BUFFER_SIZE) {
                pthread_cond_wait(&recvNotFull, &recvMutex);
            }
            recvQueue[recvCount++] = clock;
            allRecv++;
            pthread_mutex_unlock(&recvMutex);
            pthread_cond_signal(&recvNotEmpty);
        }
    return NULL;
}

void *processingThread(void *arg) {

    Clock clock;
    clock.p[0] = 0;
    clock.p[1] = 0;
    clock.p[2] = 0;
    
    if (rank == 0) {
        
        Event(&clock);
        
        Send(1, &clock);
        
        Receive(&clock);
        
        Send(2, &clock);
        
        Receive(&clock);
        
        Send(1, &clock);
        
        Event(&clock);

    } else if (rank == 1) {
        Send(0, &clock);
        
        Receive(&clock);
        
        Receive(&clock);
    } else if (rank == 2) {
        Event(&clock);
        
        Send(0, &clock);
        
        Receive(&clock);
    }

    return NULL;
}


void *sendingThread(void *arg) {
    while (1) {
        if(rank == 0 && allSend > 2)
            break;
        if(rank == 1 && allSend > 0)
            break;
        if(rank == 2 && allSend > 0) // teria que mudar a lógica desses if's caso mudasse o nº de envios de cada processo
            break;
        Clock clock;

        // Receber relógio da fila de envio
        pthread_mutex_lock(&sendMutex);
        if (sendCount == 0) {
            pthread_cond_wait(&sendNotEmpty, &sendMutex);
        }
        clock = sendQueue[0];
        for (int i = 0; i < sendCount - 1; i++) {
            sendQueue[i] = sendQueue[i + 1];
        }
        sendCount--;
        allSend++;
        pthread_mutex_unlock(&sendMutex);
        pthread_cond_signal(&sendNotFull);

        // Enviar relógio vetorial para outro processo
        MPI_Send(&clock.p, NUM_PROCESSES, MPI_INT, clock.dest, 0, MPI_COMM_WORLD);
        printf("Processo %d enviou Clock (%d, %d, %d) para processo %d\n", rank, clock.p[0], clock.p[1], clock.p[2], clock.dest);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size != NUM_PROCESSES) {
        printf("Este código precisa ser executado com %d processos.\n", NUM_PROCESSES);
        MPI_Finalize();
        return 0;
    }

    pthread_mutex_init(&recvMutex, NULL);
    pthread_mutex_init(&sendMutex, NULL);
    pthread_cond_init(&recvNotEmpty, NULL);
    pthread_cond_init(&recvNotFull, NULL);
    pthread_cond_init(&sendNotEmpty, NULL);
    pthread_cond_init(&sendNotFull, NULL);

    pthread_t threads[THREAD_NUM];

    pthread_create(&threads[0], NULL, receivingThread, NULL);
    pthread_create(&threads[1], NULL, processingThread, NULL);
    pthread_create(&threads[2], NULL, sendingThread, NULL);
    
    for (int i = 0; i < THREAD_NUM; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&recvMutex);
    pthread_mutex_destroy(&sendMutex);
    pthread_cond_destroy(&recvNotEmpty);
    pthread_cond_destroy(&recvNotFull);
    pthread_cond_destroy(&sendNotEmpty);
    pthread_cond_destroy(&sendNotFull);

    MPI_Finalize();
    return 0;
}
