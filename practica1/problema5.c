#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int global=10;
void *hilo1(void *arg);

int main() {
    char cmd[50];
    sprintf(cmd, "cat /proc/%d/maps", getpid());
    printf("Mapa de memoria antes de crear el hilo:\n");
    system(cmd);
    printf("--\n");
    printf("Variable / funcion Direccion                  Tamaño\n");
    printf("              main %p\n", main);
    printf("              hilo %p\n", hilo1);
    printf("            global %p  %12ld\n", &global, sizeof(global));

    pthread_t t1;
    pthread_create(&t1, NULL, hilo1, NULL);
    pthread_join(t1,NULL);
    sprintf(cmd, "cat /proc/%d/maps", getpid());
    printf("Mapa de memoria despues del hilo:\n");
    system(cmd);
    printf("--\n");
    printf("Variable / funcion Direccion                  Tamaño\n");
    printf("              main %p\n", main);
    printf("              hilo %p\n", hilo1);
    printf("            global %p  %12ld\n", &global, sizeof(global));
    return 0;
}
void *hilo1(void *arg) {
    int local=10;
    char cmd[50];
    sprintf(cmd, "cat /proc/%d/maps", getpid());
    printf("Mapa de memoria en el hilo:\n");
    system(cmd);
    printf("--\n");
    printf("Variable / funcion Direccion                  Tamaño\n");
    printf("              main %p\n", main);
    printf("              hilo %p\n", hilo1);
    printf("            global %p  %12ld\n", &global, sizeof(global));
    printf("             local %p  %12ld\n", &local, sizeof(local));
    printf("--\n");
    return 0;
}