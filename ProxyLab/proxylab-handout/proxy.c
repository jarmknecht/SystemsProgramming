#include <stdio.h>
#include <assert.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define DEST_PORT_SIZE 100
#define SBUFSIZE 16 //size of buffer with conn descriptors
#define NTHREADS 4 //number of worker threads
#define CBUFSIZE 32 //size of log buffer

FILE *fp; //File that logging thread writes to
pthread_rwlock_t lock; //lock that will protect my cache for readers and writers

typedef struct {
   int *buf; //Buffer array
   int n; //Maximum number of slots
   int front; //buf[front+1%n] is first item
   int rear; //buf[rear%n] is last item
   sem_t mutex; //Protects accesses to buf
   sem_t slots; //Counts abailable slots
   sem_t items; //Counts abailable items
} sbuf_t;

typedef struct {
   char **logs; //Logging character string array
   int n;  //Maximum number of slots
   int front; //buf[front+1%n] is first item
   int rear; //buf[rear%n] is last item
   sem_t mutex; //Protects accesses to buf
   sem_t slots; //Counts abailable slots
   sem_t items; //Counts abailable items
} charlog_t;

typedef struct CachedItem CachedItem;
struct CachedItem {
   char url[MAXLINE]; //holds the cached URL
   void *item_p; //holds a pointer the cached item
   size_t size; //size of the item
   CachedItem *prev; //pointer to previous item
   CachedItem *next; //pointer to next item
};

typedef struct {
   size_t size; //size of the entire Cache
   CachedItem *first; //pointer to the first item most used
   CachedItem *last; //pointer to the last item and least used
} CacheList;

void interrupt_handler(int); //for when the user clicks Ctrl-c
void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

void charlog_init(charlog_t *sp, int n);
void charlog_deinit(charlog_t *sp);
void charlog_insert(charlog_t *sp, char *item);
char *charlog_remove(charlog_t *sp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
void http_proxy(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_request(char *http_header, char *hostname, char *path, int port, rio_t *rio_client);
void *thread(void *vargp);
void *loggingthread(void *vargp);

void cache_init(CacheList *list);
void cache_URL(char *URL, void *item, size_t size, CacheList *list);
void evict(CacheList *list);
CachedItem *find(char *URL, CacheList *list);
void move_to_front(char *URL, CacheList *list);
void print_URLs(CacheList *list);
void cache_destruct(CacheList *list);

/* Pthread reader writer lock wrapper functions likes */
int Pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr); //initiate lock
int Pthread_rwlock_wrlock(pthread_rwlock_t *rwlock); //makes a write lock
int Pthread_rwlock_rdlock(pthread_rwlock_t *rwlock); //makes a read lock
int Pthread_rwlock_unlock(pthread_rwlock_t *rwlock); //unlocks lock
int Pthread_rwlock_destroy(pthread_rwlock_t *rwlock); //destroys read-write lock

/* Create an empty, bounded, shared FIFO buffer with n slots */
void sbuf_init(sbuf_t *sp, int n) {
   sp->buf = Calloc(n, sizeof(int));
   sp->n = n;                    /* Buffer holds max of n items */
   sp->front = sp->rear = 0;     /* Empty buffer iff front == rear */
   Sem_init(&sp->mutex, 0, 1);   /* Binary semaphore for locking */
   Sem_init(&sp->slots, 0, n);   /* Initially, buf has n empty slots */
   Sem_init(&sp->items, 0, 0);   /* Initially, buf has 0 items */
}

/* Clean up buffer sp */
void sbuf_deinit(sbuf_t *sp) {
   Free(sp->buf);
}

/* Insert item onto the rear of shared buffer sp */
void sbuf_insert(sbuf_t *sp, int item) {
   P(&sp->slots);                         /* Wait for available slot */
   P(&sp->mutex);                         /* Lock the buffer */
   sp->rear = (sp->rear + 1) % sp->n;     /* Resets the rear so no overflow */
   sp->buf[sp->rear] = item;            /* Inserts the item */
   V(&sp->mutex);                         /* Unlock the buffer */
   V(&sp->items);                         /* Announce available item */
}

/* Remove and return the first item from buffer sp */
int sbuf_remove(sbuf_t *sp) {
   int item;
   P(&sp->items);                         /* Wait for available item */
   P(&sp->mutex);                         /* Lock the buffer */
   sp->front = (sp->front + 1) % sp->n;   /* Resets the front so no overflow */
   item = sp->buf[sp->front];             /* Remove the item */
   V(&sp->mutex);                         /* Unlock the buffer */
   V(&sp->slots);                         /* Announce available slot */
   return item;
}

/* Create an empty, bounded, shared FIFO buffer with n slots */
void charlog_init(charlog_t *sp, int n) {
   sp->logs = Calloc(n, sizeof(char **));
   sp->n = n;                    /* Buffer holds max of n items */
   sp->front = sp->rear = 0;     /* Empty buffer iff front == rear */
   Sem_init(&sp->mutex, 0, 1);   /* Binary semaphore for locking */
   Sem_init(&sp->slots, 0, n);   /* Initially, buf has n empty slots */
   Sem_init(&sp->items, 0, 0);   /* Initially, buf has 0 items */
}

/* Clean up buffer sp */
void charlog_deinit(charlog_t *sp) {
   Free(sp->logs);
}

/* Insert item onto the rear of shared buffer sp */
void charlog_insert(charlog_t *sp, char *item) {
   P(&sp->slots);                         /* Wait for available slot */
   P(&sp->mutex);                         /* Lock the buffer */
   sp->rear = (sp->rear + 1) % sp->n;     /* Resets the rear so no overflow */
   sp->logs[sp->rear] = item;             /* Inserts the item */
   V(&sp->mutex);                         /* Unlock the buffer */
   V(&sp->items);                         /* Announce available item */
}

/* Remove and return the first item from buffer sp */
char *charlog_remove(charlog_t *sp) {
   char *item;
   P(&sp->items);                         /* Wait for available item */
   P(&sp->mutex);                         /* Lock the buffer */
   sp->front = (sp->front + 1) % sp->n;   /* Resets the front so no overflow */
   item = sp->logs[sp->front];             /* Remove the item */
   V(&sp->mutex);                         /* Unlock the buffer */
   V(&sp->slots);                         /* Announce available slot */
   return item;
}

void cache_init(CacheList *list) {
   list->size = 0;
   list->first = NULL;
   list->last = NULL;
}

void cache_URL(char *URL, void *item, size_t size, CacheList *list) {
   if (size > MAX_OBJECT_SIZE) {
      return; //can't hold something this big in the cache
   }
   
   /* check to see if there is space in the cache if there isn't any
    start evicting till there is space for the new thing */
   while ((list->size + size) > MAX_CACHE_SIZE) {
      evict(list);
   }
   list->size += size; //add the new object size to the total size of the cache
   
   CachedItem *cached_item = (CachedItem*) Malloc(sizeof(struct CachedItem)); //make room for a new item
   strcpy(cached_item->url, URL); //copy URL into the cached_item's url
   cached_item->item_p = item; //store what itemp is
   cached_item->size = size; //store size of item
   
   
   /* If the list is empty store first and last item as item just added */
   if (list->first == NULL) {
      list->first = cached_item;
      list->last = cached_item;
   }
   
   else { //the list isn't empty so put at the front
      list->first->prev = cached_item;
      cached_item->next = list->first;
      cached_item->prev = NULL;
      list->first = cached_item;
   }
   
   return;
}

void evict(CacheList *list) { //evicts based off of a LRU policy
   assert(list->size > 0); //should be so we can get rid of stuff
   assert(list->first != NULL); //same check that the list isn't empty
   assert(list->last != NULL); //same check that the list isn't empty
   
   if (list->last->size == list->size) { //there is only one thing in the list taking up whole thing
      free(list->last->item_p);
      free(list->last);
      list->last = NULL;
      list->first = NULL;
      list->size = 0;
      return;
   }
   
   assert(list->last != list->first); //this checks to make sure that last and first aren't equal
   
   //move new list around
   list->last = list->last->prev;
   list->size -= list->last->next->size;
   free(list->last->next->item_p);
   free(list->last->next);
   list->last->next = NULL;
   return;
}

CachedItem *find(char *URL, CacheList *list) {
   
   if (list->size > 0) { //contains something
      if (strcmp(list->first->url, URL) == 0){
         return list->first;
      }
      //Check if the last item in list is it
      if (strcmp(list->last->url, URL) == 0){
         return list->last;
      }
      
      CachedItem *temp = list->first;
      while (temp->next != NULL){ //Iterate through list till item is found
         if (strcmp(temp->url, URL) != 0){
            temp = temp->next;
         }
         else{
            return temp;
         }
      }
   }
   return NULL; //list was empty
}

void move_to_front(char *URL, CacheList *list){
   CachedItem *item = find(URL, list);
   
   if (item == NULL) { //didn't find the item
      return;
   }
   
   if (item == list->first) { //item is already at the front
      return;
   }
   
   if (item == list->last) { //item is a the end move to front
      list->last = item->prev;
      list->first->prev = item;
      item->next = list->first;
      item->prev = NULL;
      list->last->next = NULL;
      list->first = item;
      return;
   }
   else { //item is in the middle move to front and fix pointers
      item->prev->next = item->next;
      item->next->prev = item->prev;
      item->prev = NULL;
      item->next = list->first;
      list->first->prev = item;
      list->first = item;
      return;
   }
   
}

charlog_t c_log; /* Shared buffer of chars for print statements */
sbuf_t sbuf; /* Shared buffer of connected descriptors */
CacheList *CACHE_LIST; //holds my cache

void print_URLs(CacheList *list){ //used to print the contents of the cache
   if (list->size > 0) {
      CachedItem *item = list->first;
      while (item->next != NULL){
         
         char message[MAXLINE];
         sprintf(message, "%s", item->url);
         charlog_insert(&c_log, message);
         //printf("%s/n", item->url);
         item = item->next;
      }
      char message[MAXLINE];
      sprintf(message, "%s", item->url);
      charlog_insert(&c_log, message);
      //printf("%s\n", item->url);
   }
}

void cache_destruct(CacheList *list){
   while (list->size != 0) { //keep evicting all of the items out to clean cache
      evict(list);
   }
}


int Pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) {
   int lock_num;
   if ((lock_num = pthread_rwlock_init(rwlock, attr)) != 0) {
      char *string = "prthread_rwlock_init failed";
      size_t len = strlen(string);
      char message[len];
      sprintf(message, "%s\n", string);
      charlog_insert(&c_log, message);
      //printf("pthread_rwlock_init failed");
   }
   return lock_num;
}

int Pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
   int lock_num;
   if ((lock_num = pthread_rwlock_wrlock(rwlock)) != 0) {
      char *string = "prthread_rwlock_wrlock failed";
      size_t len = strlen(string);
      char message[len];
      sprintf(message, "%s\n", string);
      charlog_insert(&c_log, message);
      //printf("pthread_rwlock_wrlock failed");
   }
   return lock_num;
}

int Pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
   int lock_num;
   if ((lock_num = pthread_rwlock_rdlock(rwlock)) != 0) {
      char *string = "prthread_rwlock_rdlock failed";
      size_t len = strlen(string);
      char message[len];
      sprintf(message, "%s\n", string);
      charlog_insert(&c_log, message);
      //printf("pthread_rwlock_rdlock failed");
   }
   return lock_num;
}

int Pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
   int lock_num;
   if ((lock_num = pthread_rwlock_unlock(rwlock)) != 0) {
      char *string = "prthread_rwlock_unlock failed";
      size_t len = strlen(string);
      char message[len];
      sprintf(message, "%s\n", string);
      charlog_insert(&c_log, message);
      //printf("pthread_rwlock_unlock failed");
   }
   return lock_num;
}

int Pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
   int lock_num;
   if ((lock_num = pthread_rwlock_destroy(rwlock)) != 0) {
      char *string = "prthread_rwlock_destroy failed";
      size_t len = strlen(string);
      char message[len];
      sprintf(message, "%s\n", string);
      charlog_insert(&c_log, message);
      //printf("pthread_rwlock_destroy failed");
   }
   return lock_num;
}

/*
 * http_proxy - handle proxy HTTP request/response transactions
 */
void http_proxy(int connfd) {
   int dst_serverfd; //holds the destination server socket
   char read_buf[MAXLINE]; //buffer read from for response
   char request_method[MAXLINE]; //The HTTP method will only be GET since that's all that needs to be done
   char uri[MAXLINE]; //website address we are going to if it had http:// its a proxy request
   char http_version[MAXLINE]; //version of HTTP being used
   char hostname[MAXLINE]; //holds the host name
   char path[MAXLINE]; //holds file path
   char http_header[MAXLINE]; //holds http header request
   rio_t rio_client;  //holds the input output of client
   rio_t rio_server; //holds the server input output
   int port; //port server is on
   size_t written = 0;
   
   char buf[MAXLINE]; //buffer that will hold request
   memset(&buf[0], 0, sizeof(buf));
   
   char *message = "Thread in http_proxy\n";
   charlog_insert(&c_log, message);
   
   // Read request line and headers
   Rio_readinitb(&rio_client, connfd);
   if (!Rio_readlineb(&rio_client, buf, MAXLINE)) { //read up to MAXLINE from read_buf and store in rio_client
      return;
   }
   //printf("%s", read_buf);
   sscanf(buf, "%s %s %s", request_method, uri, http_version);
   charlog_insert(&c_log, "User Request: ");
   charlog_insert(&c_log, buf);
   if (strcasecmp(request_method, "GET")) {
      //method isn't Get so don't do anything with it
      char *message = "ERROR: Proxy only implements the GET method\n";
      charlog_insert(&c_log, message);
      //printf("Proxy does not implement this %s only the GET method\n", request_method);
      return;
   }
   
   Pthread_rwlock_rdlock(&lock); //locks to read
   CachedItem *cached_item = find(buf, CACHE_LIST);
   Pthread_rwlock_unlock(&lock); //unlock after reading
   if (cached_item != NULL) { //we found the request we wanted
      
      Pthread_rwlock_wrlock(&lock); //writting when moving something to the front
      move_to_front(cached_item->url, CACHE_LIST); //moves it to the front
      Pthread_rwlock_unlock(&lock);
      
      size_t to_be_written = cached_item->size; //size of the cached request
      written = 0;
      char *item_buf = cached_item->item_p;
      while ((written = rio_writen(connfd, item_buf, to_be_written)) != to_be_written) {
         item_buf += written;
         to_be_written -= written;
      }
      char *message = "Found a cached item!! Item is: ";
      charlog_insert(&c_log, message);
      
      char message2[MAXLINE];
      sprintf(message2, "%s", cached_item->url);
      charlog_insert(&c_log, message2);
      
      return; //don't need to parse the uri cause it was cached
   }
   
   
   //Parse_uri: get hostname, see what the port is set to, and get path from URI
   memset(&path[0], 0, sizeof(path)); //Reset memory of path to 0
   memset(&hostname[0], 0, sizeof(hostname)); //Reset memory of hostname to 0
   
   parse_uri(uri, hostname, path, &port); //sets hostname, path and port from URI
   
   //Makes the request from the info from parsed URI so it can be sent to server
   build_http_request(http_header, hostname, path, port, &rio_client);
   
   //Connect to destination server with proxy server
   char conn_port[DEST_PORT_SIZE];
   sprintf(conn_port, "%d", port); //writes port number to conn_port string
   dst_serverfd = Open_clientfd(hostname, conn_port); //opens connection from proxy to dst server at hostname:port
   
   //Get and send info to the destination server
   Rio_readinitb(&rio_server, dst_serverfd);
   Rio_writen(dst_serverfd, http_header, sizeof(http_header));
   
   size_t size = 0; //gets the size of the object
   size_t total_bytes = 0; //keeps track of total bytes to be written to cache
   char object[MAX_OBJECT_SIZE]; //holds the response object to be cached
   
   while ((size = Rio_readlineb(&rio_server, read_buf, MAXLINE)) != 0) {
      total_bytes += size;
      //printf("Received %zu bytes...\n", size);
      Rio_writen(connfd, read_buf, size); //forwards response to client
      if (total_bytes < MAX_OBJECT_SIZE) {
         strcat(object, read_buf); //if object is smaller than the max bytes concatinate the read_buf into it
      }
   }
   
   
   if (total_bytes < MAX_OBJECT_SIZE) { //now copy it over to the cache
      char *to_be_cached = (char*) Malloc(total_bytes);
      strcpy(to_be_cached, object); //copy object into to_be_cached
      
      Pthread_rwlock_wrlock(&lock); //writting
      charlog_insert(&c_log, "Caching URL: ");
      char message2[MAXLINE];
      sprintf(message2, "%s", buf);
      charlog_insert(&c_log, message2);
      cache_URL(buf, to_be_cached, total_bytes, CACHE_LIST);
      Pthread_rwlock_unlock(&lock); //unlock
   }
   
}

/*
 * parse_uri - parse URI into hostname path and port
 */
void parse_uri(char *uri, char *hostname, char *path, int *port) {
   char *proxy_substr = strstr(uri, "//"); //looks where the // is in the URI and holds pointer to it
   char my_uri[MAXLINE]; //will hold the uri after the http:// the proxy request
   memset(&my_uri[0], 0, sizeof(my_uri)); //Reset memory of my_uri to 0
   char *uriline = my_uri; //holds everything after http://
   char portnum[MAXLINE]; //holds the port number
   int host_is_set = 0; //boolean that will change to one when the hostname is done
   
   char *message = "Thread in parse_uri\n";
   charlog_insert(&c_log, message);
   charlog_insert(&c_log, "Parsing URI: ");
   charlog_insert(&c_log, uri);
   charlog_insert(&c_log, "\n");
   
   *port = 80; //Default port is 80 if it isn't specified
   
   if (proxy_substr != NULL) { //it will be null if there isn't http:// at the beginning
      int i = 2; //go past the // to start after the http:// in proxy_substr
      int j = 0; //used to start storing the rest of proxy_substr into uriline
      for (; i < strlen(proxy_substr); i++) {
         uriline[j++] = proxy_substr[i];
      }
   }
   
   //Now see if there is a colon if it is there we need to change the port number
   char *port_substr = strstr(uriline, ":"); //store : forward in port_substr
   if (port_substr != NULL) { //means there was a : so there is a port number
      int m = 1; //go past : so we are just looking at the port number now
      int n = 0; //index into portnum array
      while (1) {
         if (port_substr[m] == '/') { //once / is hit all the numbers of the port have been seen
            break;
         }
         portnum[n++] = port_substr[m++]; //store port number in port_substr into portnum
      }
      *port = atoi(portnum); //convert portnum char into an int and store in port
      
      m = 0; //used to index into hostname
      n = 0; //used to index into uriline
      
      while (1) {
         if (uriline[n] == ':') { //end of hostname
            break;
         }
         hostname[m++] = uriline[n++]; //store the hostname in array
      }
      host_is_set = 1;
   }
   
   char *path_substr = strstr(uriline, "/"); //takes out hostname to get the path
   if (path_substr != NULL) {
      int a = 0; //used to index into path array
      int b = 0; //used to index into path_substr
      
      while (1) { //gets the path which goes to a null character
         if (path_substr[b] == '\0') {
            break;
         }
         path[a++] = path_substr[b++];
      }
      
      if (!host_is_set) { //if host wasn't set do it now
         int c = 0; //index for hostname array
         int d = 0; // index for uriline array
         
         while (1) {
            if (uriline[d] == '/') { //hostname has ended
               break;
            }
            hostname[c++] = uriline[d++];
         }
      }
   }
   
}

void build_http_request(char *http_header, char *hostname, char *path, int port, rio_t *rio_client) {
   char client_request[MAXLINE]; //holds the clients request into it
   char request_header[MAXLINE]; //holds request
   char host_header[MAXLINE]; //holds hostname header
   char other_headers[MAXLINE]; //holds any additional headers
   char *connection_header = "Connection: close\r\n"; //signifies the connection header
   char *proxy_header = "Proxy-Connection: close\r\n"; //signifies the proxy header
   char *host_header_format = "Host: %s\r\n"; //format of a host header
   char *request_header_format = "GET %s HTTP/1.0\r\n"; //format of the request header with HTTP version set to 1.0
   char *carriage_return = "\r\n"; //used to signal that headers are all done
   char *connection_phrase = "Connection"; //the phrase to look for in a header for when it is a normal connection
   char *user_agent_phrase = "User-Agent"; //the phrase to look for in a header for when it will be the user_agent header
   char *proxy_connection_phrase = "Proxy-Connection"; //the phrase to look for in a header for when it is a proxy connection
   char *host_phrase = "Host"; //the phrase to look for in a header for when it is a host header
   int connection_len = strlen(connection_phrase); //used for how many chars a connection_phrase is to use with strncasecmp
   int user_len = strlen(user_agent_phrase); //used for how many chars a user_agent_phrase is to use with strncasecmp
   int proxy_len = strlen(proxy_connection_phrase); //used for how many chars a proxy_connection_phrase is to use with strncasecmp
   int host_len = strlen(host_phrase); //used for how many chars a host_phrase is to use with strncasecmp
   
   sprintf(request_header, request_header_format, path); //format is written to request_header array with path included
   //printf("request_header: %s\n", request_header); //prints the request header
   
   char *message = "Thread starting in build_http_request\n";
   charlog_insert(&c_log, message);
   
   //reads at most MAXLINE chars and reads what is in rio_client and puts into client_request array
   while (Rio_readlineb(rio_client, client_request, MAXLINE) > 0) {
      if (!strcmp(client_request, carriage_return)) { //IF EOF is first thing in client_request break
         break;
      }
      
      if (!strncasecmp(client_request, host_phrase, host_len)) { //compares host_phrase to the first host_len chars in client_request
         strcpy(host_header, client_request); //copy what is in the client_request to host_header
         //printf("HOST_HEADER: %s\n", host_header); //print host header
         continue;
      }
      
      //Checks if there are any other headers and puts them into other_headers
      if (!strncasecmp(client_request, connection_phrase, connection_len) &&
          !strncasecmp(client_request, proxy_connection_phrase, proxy_len) &&
          !strncasecmp(client_request, user_agent_phrase, user_len)) {  //if there are connection, proxy and user agent headers store in other_headers
         strcat(other_headers, client_request); //concatenates chars in client_request to chars already in other_headers array
      }
   }
   
   if (strlen(host_header) == 0) { //if for some reason the host wasn't set, set it here
      sprintf(host_header, host_header_format, hostname); //format is written to host_header array with hostname included
   }
   
   //Build the HTTP header
   sprintf(http_header, "%s%s%s%s%s%s%s", request_header, host_header, connection_header,
           proxy_header, user_agent_hdr, other_headers,
           carriage_return); //all the strings %s from all of the headers are stored in the http_header
   charlog_insert(&c_log, "HTTP HEADER CREATED:\n");
   charlog_insert(&c_log, http_header);
   charlog_insert(&c_log, "\n");
   //printf("HTTP_HEADERS: %s\n", http_header); //print http_header
}


void *thread(void *vargp) {
   //int connfd = *((int *)vargp); //holds connfdp from main thread
   Pthread_detach(pthread_self()); //can't join or get the return value. it is separate from the main thread
   //Free(vargp); //frees storage used to hold connfd which is the pointer to connfdp
   while (1) {
      int connfd = sbuf_remove(&sbuf);
      char *string = "Starting new thread request with connection fd: ";
      size_t len = strlen(string);
      char message[len];
      sprintf(message, "%s %i\n", string, connfd);
      charlog_insert(&c_log, message);
      //charlog_insert(&c_log, "Starting up a thread request");
      http_proxy(connfd); //fires up a HTTP proxy server request
      Close(connfd); //always need to close connfd when done otherwise resources are depleted
   }
   //return NULL;
}

void *loggingthread(void *vargp) {
   fp = fopen("log.txt", "w");
   if (fp == NULL) {
      printf("Error! Couldn't write to file\n");
      exit(1);
   }
   Pthread_detach(pthread_self());
   while (1) {
      char *message = charlog_remove(&c_log);
      fprintf(fp, "%s", message);
      fflush(fp);
   }
}

void interrupt_handler(int num){
   cache_destruct(CACHE_LIST);
   Free(CACHE_LIST);
   CACHE_LIST = NULL;
   if (fp != NULL) {
      fclose(fp);
   }
   sbuf_deinit(&sbuf);
   charlog_deinit(&c_log);
   exit(0);
}

int main(int argc, char** argv) {
   int listenfd, connfd;  //Holds the listen socket and the connection socket
   socklen_t clientlen;  //Holds how big the client's socket is
   struct sockaddr_storage clientaddr;  //holds the client's address
   pthread_t tid;  //holds the thread id
   
   listenfd = Open_listenfd(argv[1]); //opens a listenfd with csapp wrapper
   CACHE_LIST = (CacheList*) Malloc(sizeof(CacheList)); //creates cache to use
   cache_init(CACHE_LIST); //inits list
   signal(SIGINT, interrupt_handler); //calls this when ctrl-c is types
   
   sbuf_init(&sbuf, SBUFSIZE);
   charlog_init(&c_log, CBUFSIZE);
   Pthread_create(&tid, NULL, loggingthread, NULL); //makes the logging thread
   for (int i = 0; i < NTHREADS; i++) { //Creates worker threads
      Pthread_create(&tid, NULL, thread, NULL);
   }
   while (1) {
      clientlen = sizeof(struct sockaddr_storage);
      connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
      sbuf_insert(&sbuf, connfd); //Insert connfd into buffer
      
      /* For parts 1 and 2 since the sbuf is allocated all of the connfds made will be on the heap
       so we don't need these anymore */
      //connfdp = Malloc(sizeof(int)); //don't want connfdp to be a local variable want on the heap
      //*connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen); //accepts a connection
      //Pthread_create(&tid, NULL, thread, connfdp); //makes a new thread using function thread stores thread id in tid
   }
   return 0;
}
