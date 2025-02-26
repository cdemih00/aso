#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

void funcion();

int main(){
    char cmd[25];
    sprintf(cmd,"cat /proc/%d/maps",getpid());
    system(cmd);
    printf ("--\n") ;
    printf ("              main %p\n", main);
    printf ("           funcion %p\n", funcion);
    printf ("--\n") ;
    funcion();
    printf ("--\n") ;
    sprintf(cmd,"cat /proc/%d/maps",getpid());
    system(cmd);
    printf ("--\n") ;
    printf ("              main %p\n", main);
    printf ("           funcion %p\n", funcion);
    return 0;


}
void funcion(){
    char cmd[25];
    char vector[14000];
    sprintf(cmd,"cat /proc/%d/maps",getpid());
    system(cmd);
    printf("--\n") ;
    printf ("Variable / funcion Direccion Tamano\n") ;
    printf("             vector %p %12ld \n",&vector,sizeof(vector));
    printf("               main %p\n", main);
    printf("            funcion %p\n", funcion);

}