#define _GNU_SOURCE
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include "hash_table.h"
#include "thread.h"
#include "connection.h"

// Stringa usata per tokenizare
#define TOKENIZATOR ".,:; \n\r\t"

sigset_t mask;

// ------ Definizioni funzioni usati dai thread -------
void *tbody_prod(void *args);

void *tbody_cons_writer(void *args);

void *tbody_cons_reader(void *args);

void *tbody_signals_handler(void *args);
// ------ Definizioni funzioni usati dai thread -------


int main(int argc, char **argv){
  assert(argc == 3);
  int num_writers = atoi(argv[1]);
  int num_readers = atoi(argv[2]);

  // Inizzializzaioni informazioni per i segnali
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);

  // Creazioni hash_table
  hash_table_t *hash_table = hash_table_create();

  // Creazione thread che gestisce segnali
  thread_t *signals_handler = thread_create(hash_table, &tbody_signals_handler);

  // Apertura pipe e file utili
  int caposc = open("caposc", O_RDONLY);
  int capolet = open("capolet", O_RDONLY);
  FILE *file = fopen("lettori.log", "a");
  check(file == NULL, "Errore apertura file", exit(1));

  // Creazione dei buffer usati dagli scrittori e dai lettori
  buffer_t *buffer_writer = buffer_create();
  buffer_t *buffer_reader = buffer_create();

  // Inizializzazione dei dati dai inviare ai scrittori
  data_t *data_writer = (data_t*)malloc(sizeof(data_t));
  check(data_writer == NULL, "Errore allocazione data writer", exit(1));
  data_writer->buffer = buffer_writer;
  data_writer->pipe = caposc;
  data_writer->hash_table = hash_table;
  data_writer->num_sub_threads = num_writers;
  data_writer->file = NULL;

  // Inizializzazione dei dati dai inviare ai lettori
  data_t *data_reader = (data_t *)malloc(sizeof(data_t));
  check(data_reader == NULL, "Errore allocazione data reader", exit(1));
  data_reader->buffer = buffer_reader;
  data_reader->pipe = capolet;
  data_reader->hash_table = hash_table;
  data_reader->num_sub_threads = num_readers;
  data_reader->file = file;

  // Inizializzazione mutex per gestire la scrittura su file
  check(pthread_mutex_init(&data_reader->file_mutex, NULL) != 0, "Errore creazione mutex interrupt", exit(1));

  // Creazione del thread capo (produttore) e dei thread consumatori che si occupano della scrittura
  thread_t *prod_writer = thread_create(data_writer, &tbody_prod);
  thread_t *cons_writer[num_writers];

  for(int i=0; i<num_writers; i++)
    cons_writer[i] = thread_create(data_writer, &tbody_cons_writer);


  // Creazione del thread capo (produttore) e dei thread consumatori che si occupano della lettura
  thread_t *prod_reader = thread_create(data_reader, &tbody_prod);
  thread_t *cons_reader[num_readers];

  for (int i = 0; i < num_readers; i++)
    cons_reader[i] = thread_create(data_reader, &tbody_cons_reader);


  // Attesa di tutti i thread creati
  pthread_join(signals_handler->thread, NULL);
  pthread_join(prod_writer->thread, NULL);

  for(int i=0; i<num_writers; i++)
    pthread_join(cons_writer[i]->thread, NULL);

  pthread_join(prod_reader->thread, NULL);

  for (int i = 0; i < num_readers; i++)
    pthread_join(cons_reader[i]->thread, NULL);


  // Chiusura file e connessioni a pipe e deallocazioni delle strutture utilizzate
  // fprintf(stderr, "Terminazione in corso...\n");

  thread_destroy(signals_handler);
  thread_destroy(prod_reader);
  thread_destroy(prod_writer);

  for(int i=0; i<num_writers; i++)
    thread_destroy(cons_writer[i]);


  for (int i = 0; i < num_readers; i++)
    thread_destroy(cons_reader[i]);

  fclose(file);
  close(caposc);
  close(capolet);

  buffer_destroy(buffer_writer);
  buffer_destroy(buffer_reader);
  hash_table_destroy(hash_table);

  free(data_writer);
  free(data_reader);

  return 0;
}

void* tbody_prod(void *args){
  data_t *data = (data_t*)args;
  unsigned short int n;
  char *temp_buf = NULL;
  char *token, *rest = NULL;
  size_t e;

  do {
    // Lettura dalla pipe della lunghezza della stringa da leggere
    e = readN(data->pipe, &n, sizeof(n));
    if(e != sizeof(n)) break;

    // Allocazione buffere dove scrivere la seguenza di caratteri
    if(n <= 0) continue;
    temp_buf = (char*)malloc((n + 1) * sizeof(char));

    // Lettura dalla pipe della seguneza di caratteri
    e = readN(data->pipe, temp_buf, n);
    if (e != n) break;

    temp_buf[n] = '\0';

    // Inserimento nel buffer
    for (token = strtok_r(temp_buf, TOKENIZATOR, &rest);
         token != NULL;
         token = strtok_r(NULL, TOKENIZATOR, &rest))
    {
      buffer_insert(data->buffer, strdup(token));
    }

    // Deallocazione buffer temporaneo
    free(temp_buf);
    temp_buf = NULL;

  } while (true);

  if(temp_buf != NULL) free(temp_buf);

  // Gestionne della SIGTERM andando ad inserirre un valore NULL nel buffer cio indicheerà che sarà terminato il programma
  for (int i = 0; i < data->num_sub_threads; i++)
    buffer_insert(data->buffer, NULL);

  pthread_exit(NULL);
}

void* tbody_cons_writer(void* args){
  data_t *data = (data_t *)args;
  char *str;
  while((str = buffer_remove(data->buffer)) != NULL) {
    // Si prende l'elemento dal buffer e si inserisce nell'hash table
    hash_table_insert(data->hash_table, str);
  }
  pthread_exit(NULL);
}

void *tbody_cons_reader(void *args){
  data_t *data = (data_t *)args;
  char *key;
  while((key = buffer_remove(data->buffer)) != NULL) {
    // Si prende il numero di occorrenze della stringa
    int num = hash_table_count(data->hash_table, key);

    // Si stampa nel file gestendo anche la muta eslusione 
    check(pthread_mutex_lock(&data->file_mutex) != 0, "Errore lock file", pthread_exit(NULL));
    fprintf(data->file, "%s %d\n", key, num);
    check(pthread_mutex_unlock(&data->file_mutex) != 0, "Errore unlock file", pthread_exit(NULL));

    free(key);
  }

  fflush(data->file);
  pthread_exit(NULL);
}

void *tbody_signals_handler(void *args) {
  hash_table_t *data = (hash_table_t*)args;
  int s;

  while(true) {
    check(sigwait(&mask,&s) != 0, "Errore sigwait", pthread_exit(NULL));

    if (s == SIGINT) {
      // Gestione della SIGINT andando a stampare il numero di stringhe nell'hash map gestendo anche la muta eslusione
      check(pthread_mutex_lock(&data->mutex) != 0, "Errore mutex lock in signals handler", pthread_exit(NULL));
      fprintf(stderr, "Numero stringhe in hash map: %d\n", data->index_entrys);
      check(pthread_mutex_unlock(&data->mutex) != 0, "Errore mutex lock in signals handler", pthread_exit(NULL));
      
    } else if (s == SIGTERM) {

      // Stampa del numero di stringhe nell'hash map
      check(pthread_mutex_lock(&data->mutex) != 0, "Errore mutex lock in signals handler", pthread_exit(NULL));
      fprintf(stderr, "Numero stringhe in hash map: %d\n", data->index_entrys);
      check(pthread_mutex_unlock(&data->mutex) != 0, "Errore mutex lock in signals handler", pthread_exit(NULL));

      break;
    }

  }

  pthread_exit(NULL);
}