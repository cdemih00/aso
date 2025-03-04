# include <stdio.h>
# include <unistd.h>
# include <stdlib.h>
# include <errno.h>
int main(){
    char cmd[25];
    sprintf(cmd,"cat /proc/%d/maps",getpid());
    system(cmd);
    printf("--\n") ;
    printf("Variable / funcion Direccion                 Tamano\n") ;
    printf("              main %p\n", main);
    printf("--\n") ;

    char *var;
    var = (char *)malloc(14000 * sizeof(char));

    sprintf(cmd,"cat /proc/%d/maps",getpid());
    system(cmd);
    printf("--\n") ;
    printf("Variable / funcion Direccion                 Tamano\n") ;
    printf("              main %p\n", main);
    printf("               var %p %12ld\n", &var, sizeof (var));
    printf("--\n") ;

    free(var);

    sprintf(cmd,"cat /proc/%d/maps",getpid());
    system(cmd);
    printf("--\n") ;
    printf("Variable / funcion Direccion                 Tamano\n") ;
    printf("              main %p\n", main);
    printf("               var %p %12ld\n", &var, sizeof (var));
    printf("--\n") ;

    return 0;

}
