# include <stdio.h>
# include <unistd.h>
# include <stdlib.h>
# include <errno.h>
#include <math.h>

int main(){

    double numero=0.0;
    double resultado=cos(numero);
    char cmd[25];
    sprintf(cmd,"cat /proc/%d/maps",getpid());
    system(cmd);
    system(cmd);
    printf ("--\n") ;
    printf ("Variable / funcion Direccion Tamano\n") ;
    printf ("              main %p\n", main);
    printf ("               cos %p\n", cos);
    printf ("         resultado %p %12ld\n", &var, sizeof (var));
    printf ("--\n") ;
}