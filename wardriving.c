#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>

#define QUEUESIZE 2
#define LOOP 10

#define SSID_PATH "./out/ssids.txt"
#define dT_PATH "./out/timestamps.txt"
#define SCRIPT_PATH "./searchWifi.sh"
#define SEC_SSIDS_PATH "/tmp/sec_ssids"

void *scanner (void *args);
void *writer (void *args);

typedef struct {
  int buf[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_mutex_t *mut;
  pthread_cond_t *notFull, *notEmpty;
  
  float scantime;
  long long timestamp;
  FILE * dt;
  FILE * ssids;
} queue;

queue *queueInit (float scantime);
void queueDelete (queue *q);
void queueAdd (queue *q, int in);
void queueDel (queue *q, int *out);

void writeTimestamps(queue *q, long long dt);
void writeSSIDs(queue *q);
void getSSIDData(const char *s, char ** name, char ** mac_address, char ** security);
int existsInFile(char *s, queue *q);
long long current_timestamp();


int main (int argc, char** argv)
{

  if (argc != 2){
    printf("usage: %s <scantime>", argv[0]);
    exit(1);
  }

  queue *fifo;
  pthread_t pro, con;

  float scantime = atof(argv[1]) *1000000;
  fifo = queueInit (scantime);
  if (fifo ==  NULL) {
    fprintf (stderr, "main: Queue Init failed.\n");
    exit (1);
  }
  
  pthread_create (&pro, NULL, scanner, fifo);
  pthread_create (&con, NULL, writer, fifo);
  
  pthread_join (pro, NULL);
  pthread_join (con, NULL);
  
  queueDelete (fifo);

  return 0;
}

void *scanner (void *q)
{
  queue *fifo;
  fifo = (queue *) q;
  int i;
  double wait = fifo->scantime;

  while(1){
    pthread_mutex_lock (fifo->mut);
    
    while (fifo->full) {
      printf ("[SCANNER] queue FULL.\n");
      pthread_cond_wait (fifo->notFull, fifo->mut);
    }
    
    fifo->timestamp = current_timestamp(); // Add production timestamp 
    queueAdd (fifo, i++);

    pthread_mutex_unlock (fifo->mut);
    pthread_cond_signal (fifo->notEmpty);
    usleep (wait); // scan every x seconds
  }
  return (NULL);
}

// Pops items from queue
void *writer (void *q)
{
  queue *fifo;
  int i, d;

  fifo = (queue *)q; // create the queue
  double wait = fifo->scantime;

  while(1){
    pthread_mutex_lock (fifo->mut); 
  
    while (fifo->empty) {  //  Decrease semaphore & Block mutex 
      printf ("[WRITER] queue EMPTY.\n");
      pthread_cond_wait  (fifo->notEmpty, fifo->mut);
    }
    queueDel (fifo, &d);

    pthread_mutex_unlock (fifo->mut);
    pthread_cond_signal (fifo->notFull); // increment notfull semaphore
    //usleep(4*wait);
  }
  return (NULL);
}

queue *queueInit (float scantime)
{
  queue *q;

  q = (queue *)malloc (sizeof (queue));
  if (q == NULL) return (NULL);

  q->empty = 1;
  q->full = 0;
  q->head = 0;
  q->tail = 0;
  q->mut = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
  pthread_mutex_init (q->mut, NULL);
  q->notFull = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (q->notFull, NULL);
  q->notEmpty = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (q->notEmpty, NULL);
  
  q->timestamp = 0;
  q->scantime = scantime;

  return (q);
}

void queueDelete (queue *q)
{
  pthread_mutex_destroy (q->mut);
  free (q->mut);	
  pthread_cond_destroy (q->notFull);
  free (q->notFull);
  pthread_cond_destroy (q->notEmpty);
  free (q->notEmpty);
  free (q);
}

void queueAdd (queue *q, int in)
{
  q->buf[q->tail] = in;
  q->tail++;
  printf("[+] Wifi Scan #%d | Calling searchWifi.sh...\n", in); 
  system(SCRIPT_PATH);
  if (q->tail == QUEUESIZE)
    q->tail = 0; //
  if (q->tail == q->head)
    q->full = 1;
  q->empty = 0;

  return;
}

// Using *out pointer to modify the output value without returning
void queueDel (queue *q, int *out)
{
  struct timeval currentTime;
  *out = q->buf[q->head];
  
  // millis -> DDMMYY - HHMMSS format
  long long writingTime = current_timestamp();
  char text[100];
  time_t now = writingTime/1000;
  struct tm *t = localtime(&now); 
  strftime(text, sizeof(text)-1, "%d/%m/%Y %H:%M:%S", t); // 
  printf("[-] Wifi Scan #%d removed from queue at %s\n", *out, text); 

  long long dt = writingTime - q->timestamp;
  writeTimestamps(q, dt);
  writeSSIDs(q); // Opens the ssid file generated from the script
  q->head++;
  if (q->head == QUEUESIZE)
    q->head = 0; // 
  if (q->head == q->tail)
    q->empty = 1;
  q->full = 0;

  return;
}

// Writes time delay from production to consumption to file.
// Extract data from waiting times, visualize
void writeTimestamps(queue *q, long long dt){
  q->dt = fopen(dT_PATH , "a+"); 
  printf("\t--> Elapsed time: %lli\n", dt);
  fprintf(q->dt, "%lli\n", dt);
  fclose(q->dt);
}

/* writeSSIDS()
    Sample Script Output:
    "dlink"  C8:D3:A3:0E:49:8A mixed WPA/WPA2 PSK (TKIP, CCMP) 
    "COSMOTE-4DF32C"  C4:A3:66:4D:F3:2C mixed WPA/WPA2 PSK (TKIP, CCMP) 
    "hol - NetFasteR WLAN 3"  00:05:59:56:93:19 mixed WPA/WPA2 PSK (TKIP, CCMP) 
  Steps: 
    1) Check if SSID Exists in file
    2) If it exists -> update timestamp
    3) If not -> append to file
  
  Pseudocode: 
  for each line_sec_ssids in /tmp/sec_ssids:
      name = line_sec_ssids.split()[0]
      found = false
      for each line_ssids in ssids.txt:
        if name in line_ssids:
          found = true
          updateTimestamp
      if not found:
        appendToFile()
  */
void writeSSIDs(queue *q){
  
  printf("[+] Opening file...\n");
  q->ssids = fopen(SSID_PATH, "a+");
  FILE * data = fopen(SEC_SSIDS_PATH, "r");
  
  char * line_sec_ssids = NULL;
  size_t len = 0;
  ssize_t read;

  char * name;
  char * mac_address;
  char * security;
  
  int found = 0;
  
  // Read every line and append to file
  while ((read = getline(&line_sec_ssids, &len, data)) != -1 ){ // foreach line 
    
    getSSIDData(line_sec_ssids, &name, &mac_address, &security); 
    
    // If name exists in file, updates its timestamp, returns 0 if not found
    found = existsInFile(name, q);

    // Append SSID data to file 
    if (found == 0) {
      // millis -> DDMMYY - HHMMSS format
      char text[100];
      time_t now = q->timestamp/1000;
      struct tm *t = localtime(&now); 
      strftime(text, sizeof(text)-1, "%d/%m/%Y %H:%M:%S", t); // 

      printf("[!] New SSID: %s\n", name);

      fprintf(q->ssids, "SSID: %s | Last seen: %s\n", name, text); 
      fprintf(q->ssids, "\tMAC Address: %s\n",  mac_address);
      fprintf(q->ssids, "\tSecurity: %s\n\n",  security); 
    }else{
      printf("[!] SSID %s exists, timestamp updated!\n", name);
    }
  }
  fclose(q->ssids);
}

// Split string utility
size_t split(char *buffer, char *argv[], size_t argv_size)
{
    char *p, *start_of_word;
    int c;
    enum states { DULL, IN_WORD, IN_STRING } state = DULL;
    size_t argc = 0;

    for (p = buffer; argc < argv_size && *p != '\0'; p++) {
        c = (unsigned char) *p;
        switch (state) {
        case DULL:
            if (isspace(c)) {
                continue;
            }

            if (c == '"') {
                state = IN_STRING;
                start_of_word = p + 1; 
                continue;
            }
            state = IN_WORD;
            start_of_word = p;
            continue;

        case IN_STRING:
            if (c == '"') {
                *p = 0;
                argv[argc++] = start_of_word;
                state = DULL;
            }
            continue;

        case IN_WORD:
            if (isspace(c)) {
                *p = 0;
                argv[argc++] = start_of_word;
                state = DULL;
            }
            continue;
        }
    }

    if (state != DULL && argc < argv_size)
        argv[argc++] = start_of_word;

    return argc;
}

// Updates SSID variables from file
void getSSIDData(const char *s, char ** name, char** mac_address, char** security)
{
    char buf[1024];
    size_t i, argc;
    char *argv[20];
    strcpy(buf, s);
    argc = split(buf, argv, 20); // Splits line into tokens and strips quotation marks
    *name = argv[0]; // First token is the SSID
    *mac_address = argv[1]; // Second token is MAC address
    // The rest of the tokens are security settings
    *security = argv[2];
    for (i=3; i<argc; i++)
      sprintf(*security, "%s %s", *security, argv[i]); // combine all security options
}

/* existsInFile: Checks if SSID exists in file ~/ssids.txt . 
   If found:
    1) Prints debug output
    2) Updates its timestamp
    3) Returns 1
   If not found:
    returns 0
 */
int existsInFile(char *s, queue *q){
  int found = 0; 
  char * line = NULL;
  size_t len = 0;
  ssize_t read;
  FILE * f = fopen(SSID_PATH, "r+");
  
  while ((read = getline(&line, &len, f)) != -1 ){ // forEach line in file

    /* Update current line's timestamp. Since timestamps all have the 
     * same length we do not have file issues and can directly update 
     * the line. 
     */
    if (strstr(line,s)){ // If ssid exists in line
	  // millis -> DDMMYY - HHMMSS format
      fseek(f, -strlen(line), SEEK_CUR); // seek to start of line 
      
      char text[100];
      time_t now = q->timestamp/1000;
      struct tm *t = localtime(&now); 
      strftime(text, sizeof(text)-1, "%d/%m/%Y %H:%M:%S", t); 
      fprintf(f, "SSID: %s | Last seen: %s\n", s, text);
      found = 1;
      break;
    }
  }
  fclose(f);
  return found;
}

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    // printf("milliseconds: %lld\n", milliseconds);
    return milliseconds;
}

