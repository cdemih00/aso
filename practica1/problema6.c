
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
        int pid = fork () ;
        if(pid==0){
            printf ("Soy el hijo y mi pid es:%d\n",getpid());
            int variable_hijo = 10;
            char cmd[50];
            int variable =5;
            sprintf(cmd, "cat /proc/%d/maps", getpid());
            printf("Mapa de memoria en el hijo:\n");
            system(cmd);
            printf("--\n");
            printf("Variable / funcion Direccion                  Tamaño\n");
            printf("              main %p\n", main);
            printf("          variable %p  %12ld\n", &variable_hijo, sizeof(variable_hijo));
        }
        else{
            sleep(3);
            printf (" Soy el padre , mi pid es:%d y el pid de mi hijo es:%d\n", getpid () , pid ) ;
            int variable_padre = 15;
            char cmd[50];
            int variable =5;
            sprintf(cmd, "cat /proc/%d/maps", getpid());
            printf("Mapa de memoria en el hijo:\n");
            system(cmd);
            printf("--\n");
            printf("Variable / funcion Direccion                  Tamaño\n");
            printf("              main %p\n", main);
            printf("    variable_padre %p  %12ld\n", &variable_padre, sizeof(variable_padre));
            sleep(3);
        }
}
