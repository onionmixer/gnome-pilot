#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_XOPEN_SOURCE
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 
#endif
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pi-source.h>
#include <pi-socket.h>
#include <pi-dlp.h>

void to64(register char *s, register long v, register int n);

/* From local_passwd.c (C) Regents of Univ. of California blah blah */
static unsigned char itoa64[] =         /* 0 ... 63 => ascii - 64 */
        "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

void to64(register char *s, register long v, register int n)
{
    while (--n >= 0) {
        *s++ = itoa64[v&0x3f];
        v >>= 6;
    }
}


int main(int argc,char *argv[]) {
  int sd,ret;
  struct PilotUser U;
  char salt[3];

  if (argc < 2) {
    fprintf(stderr,"usage:%s\n",argv[0]);
    exit(2);
  }
  if (!(sd = pi_socket(PI_AF_PILOT, PI_SOCK_STREAM, PI_PF_PADP))) {
    perror("pi_socket");
    exit(1);
  }

  ret = pi_bind(sd, argv[1]);

  if(ret < 0) {
    perror("pi_bind");
    exit(1);
  }

  ret = pi_listen(sd, 1);
  if(ret < 0) {
    perror("pi_listen");
    exit(1);
  }
   sd = pi_accept(sd, NULL, NULL);
  if(sd < 0) {
    perror("pi_accept");
    exit(1);
  }
  
  /* Tell user (via Pilot) that we are starting things up */
  dlp_OpenConduit(sd);

  dlp_ReadUserInfo(sd, &U);
  /*
  printf("password length = %d\n",U.passwordLength);
  printf("password        = %s\n",U.password);
  */
  (void)srand((int)time((time_t *)NULL));
  to64(&salt[0],rand(),2);
  /*printf("Encrypted password = \"%s\"\n",crypt(U.password,salt));*/
  printf("%s", (char*)crypt(U.password,salt));

  pi_close(sd);
  exit(0);
 
}
