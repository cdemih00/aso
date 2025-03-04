
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    char cmd[50];
    int variable =5;
    sprintf(cmd, "cat /proc/%d/maps", getpid());
    printf("Mapa de memoria antes de crear el hilo:\n");
    system(cmd);
    printf("--\n");
    printf("Variable / funcion Direccion                  Tamaño\n");
    printf("              main %p\n", main);
    printf("          variable %p  %12ld\n", &variable, sizeof(variable));
    int main ( void ) {
        int pid = fork () ;
        if(pid==0){
            printf (" Soy el hijo y mi pid es: %5d\n",getpid());
            char cmd[50];
            int variable =5;
            sprintf(cmd, "cat /proc/%d/maps", getpid());
            printf("Mapa de memoria antes de crear el hilo:\n");
            system(cmd);
            printf("--\n");
            printf("Variable / funcion Direccion                  Tamaño\n");
            printf("              main %p\n", main);
            printf("          variable %p  %12ld\n", &variable, sizeof(variable));
        }
        else{
        printf (" Soy el padre , mi pid es: %5d y el pid de mi hijo es: %5d\n", getpid () , pid ) ;
        }
}
