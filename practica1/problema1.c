# include <stdio.h>
# include <unistd.h>
# include <stdlib.h>
# include <errno.h>
const int constante = 33;
int a;
char b;
int c=0;
char d='A';
int e[10];
char f[10];
int g[10]= {0,1,2,3,4,5,6,7,8,9};
char h[10] = {'a','b','c','d','e','f','g','h','i','j'};
int main(){
char cmd[25];
int i;
static char j;
int k = 33;
static char l='B';
int m[10];
int n[10] = {0 ,1 ,2 ,3 ,4 ,5 ,6 ,7 ,8 ,9};
sprintf(cmd,"cat /proc/%d/maps", getpid());
system(cmd);
printf ("--\n") ;
printf ("Variable / funcion Direccion Tamano\n") ;
printf ("              main %p\n", main ) ;
printf ("             errno %p %12ld\n", &errno, sizeof(errno));
printf ("         constante %p %12ld\n", &constante ,sizeof(constante));
printf ("                 a %p %12ld\n", &a, sizeof (a));
printf ("                 b %p %12ld\n", &b, sizeof (b));
printf ("                 c %p %12ld\n", &c, sizeof (c));
printf ("                 d %p %12ld\n", &d, sizeof (d));
printf ("                 e %p %12ld\n", &e, sizeof (e));
printf ("                 f %p %12ld\n", &f, sizeof (f));
printf ("                 g %p %12ld\n", &g, sizeof (g));
printf ("                 h %p %12ld\n", &h, sizeof (h));
printf ("                 i %p %12ld\n", &i, sizeof (i));
printf ("                 j %p %12ld\n", &j, sizeof (j));
printf ("                 k %p %12ld\n", &k, sizeof (k));
printf ("                 l %p %12ld\n", &l, sizeof (l));
printf ("                 m %p %12ld\n", &m, sizeof (m));
printf ("                 n %p %12ld\n", &n, sizeof (n));
return 0;
}
