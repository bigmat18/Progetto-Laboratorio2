#define _GNU_SOURCE 
#include <stdio.h> 
#include <stdlib.h>
#include <stdbool.h> 
#include <assert.h> 
#include <string.h> 
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define HOST "127.0.0.1"
#define PORT 58449
#define Max_sequence_length 2048

#define check(val, str, result)                                                          \
    if (val) {                                                                           \
        fprintf(stderr, "== %s == Linea: %d, File: %s\n", str, __LINE__, __FILE__);      \
        result;                                                                          \
    }

int main(int argv, char** argc){
  assert(argv == 2);

  FILE *file = fopen(argc[1], "r");
  check(file != NULL, "Errore apertura file", exit(1));

  int fd_skt = 0, tmp;
  struct sockaddr_in serv_addr;

  check((fd_skt = socket(AF_INET, SOCK_STREAM, 0)) < 0, "Errore creazione socket", exit(1));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  serv_addr.sin_addr.s_addr = inet_addr(HOST);

  check(connect(fd_skt, &serv_addr, sizeof(serv_addr)) < 0, "Errore connessione", exit(1));

  int e, n;
  char *buffer = NULL;
  char type = 'a';
  check(write(fd_skt, &type, sizeof(type)) != sizeof(type),"Errore write 1", exit(1));

  while(true) {
    e = getline(&buffer, &n, file);
    if (e < 0)
      break;

    fprintf(stderr, "%zd - %s", e, buffer);

    tmp = htonl((int)(strlen(buffer)));
    check(write(fd_skt, &tmp, sizeof(tmp)) != sizeof(tmp), "Errore write 2", exit(1));

    for (unsigned int i = 0; i < strlen(buffer); ++i)
      check(write(fd_skt, &buffer[i], 1) != sizeof(char), "Errore write 3", exit(1));
  }

  check(close(fd_skt) < 0, "Errore chiusura socket", exit(1));  
  fclose(file);

  return 0;
}