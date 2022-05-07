
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <signal.h>
#include <strings.h>
#include "unistd.h"

int serve_socket(int port)
{
  int s;
	struct sockaddr_in sn;
  struct hostent *he;

  if (!(he = gethostbyname("localhost"))) {
    fprintf(stderr,"can't gethostname");
    exit(1);
  }

  bzero((char*)&sn, sizeof(sn));
  sn.sin_family = AF_INET;
  sn.sin_port = htons((short)port);
	sn.sin_addr.s_addr = htonl(INADDR_ANY);

  if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    return NULL;
  }
	
  if (bind(s, (struct sockaddr *)&sn, sizeof(sn)) == -1) {
    return -1;
  }

  return s;
}

int accept_connection(int s)
{
  int l;
  int x;
	struct sockaddr_in sn;

  if(listen(s, 1) == -1) {
    perror("listen()");
    exit(1);
  }

	bzero((char *)&sn, sizeof(sn));
  l = sizeof(sn);
  if((x = accept(s, (struct sockaddr *)NULL, NULL)) == -1) {
    perror("accept()");
    exit(1);
  }
  return x;
}

int request_connection(char *hn, int port)
{
  struct sockaddr_in sn;
  int s, ok, tries;
  struct hostent *he;

  if (!(he = gethostbyname(hn))) {
    //puts("can't gethostname");
    printf("can't get hostname\n");
    fflush(stdout);
    exit(1);
  } 
  ok = 0;
  tries = 0;
  while (!ok) {
		bzero((char *)&sn,sizeof(sn));
    sn.sin_family = AF_INET;
    sn.sin_port  = htons((short)port);
		sn.sin_addr = *(struct in_addr *)(he->h_addr_list[0]);

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("socket()");
      exit(1);
    }
    ok = (connect(s, (struct sockaddr*)&sn, sizeof(sn)) != -1);
    if (!ok) { tries++; if(tries > 500){ ok = 1; s=-1;}}
  }
  return s;
}