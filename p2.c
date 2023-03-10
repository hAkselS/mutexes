/* p2.c: Edoardo Biagioni, 2021
 * this program demonstrates that incrementing a variable is not an
 * atomic operation -- if the increment happens in multiple threads,
 * the final total will not be as expected.  This is true when the
 * number of threads and loops is large enough, whereas for small
 * numbers of loops and threads (or in case of a single thread),
 * the computation gives the expected result. 
 *
 * to understand why, consider that to increment a variable in memory,
 * the CPU must read the value from memory into a register, increment
 * the value, then store the new value back into memory.  If two threads
 * are repeatedly doing this at the same time, sometimes they will get
 * the same value v from memory, and both will store back the value v+1,
 * instead of one of them storing v+1 and the other one v+2.  So one of
 * the increments is lost.
 *
 * this program takes up to two optional command-line arguments: the
 * number of threads to start, and the number of loops for each thread.
 * If no command-line arguments are provided, this program starts 2
 * threads, each doing 10 million loops.
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#define LOOPS	10 * 1000 * 1000
#define THREADS	2
//#define DEBUG

//first run indicator, used in thread()
static int firstRun = 1; //1 for first run, 0 else

//create the mutex 
static pthread_mutex_t countLock;   //ensures no race conditions on the counter
static pthread_mutex_t decreLock;   //ensures no race conditions on decrement & end 
static pthread_mutex_t mainLock;    //locks the main thread while other threads are running
static pthread_mutex_t threadLock;  //locks the the threads while the main is creating other threads

//create the condition 
static int threadComplete  = 0;   
static pthread_cond_t waitCond;     //prevents spinning in main 

struct state_struct {
  int start;                        // 0 until ready to start
  volatile long counter;            // volatile: always read the value from memory
  volatile long threads;
  long num_loops;                   // set in main, not modified in the threads
};

/* after busy-waiting for the start variable to be set, increment the
 * state counter variable the given number of times.  Once that
 * is finished, print that we are finished, taking the thread number
 * from the number of threads that haven't finished yet. */
static void * thread(void * arg)
{
  pthread_mutex_lock(&mainLock);            //aquire main lock to prevent main spinning 
  
  
  if(firstRun){                             //wait for thread lock only on the first run, then destroy it 
    pthread_mutex_lock(&threadLock);        //used to avoid spinning in threads
    #ifdef DEBUG
        printf("{thread:} thread lock aquired\n");
    #endif 
    firstRun = 0; 
    pthread_mutex_destroy(&threadLock);                         //want to destroy the lock here
  }
  struct state_struct * state = (struct state_struct *) arg;    //create a state_struct called 'state'. set equal to arg (2nd part is a guess)
  
  while (state->start == 0) {                                   //loop until all are ready to start 
    printf("{thread:} SPINNING\n");
  }
          
    pthread_cond_wait(&waitCond,&mainLock);                     //first thread to execute will start wait condition 
    for (int i = 0; i < state->num_loops; i++) {                    
        pthread_mutex_lock(&countLock);                         //lock only the section where the counter is being updated 
        state->counter++;
        pthread_mutex_unlock(&countLock);
  }

  pthread_mutex_unlock(&mainLock);                                   

  //ensure that the state.threads is decremented accurately
  pthread_mutex_lock(&decreLock); 
  printf ("thread %ld finishing\n", state->threads);
  state->threads--; 
  
  if( (state->threads) == 0 ){                                  //send a signal on last thread to allow the main thread to continue
    pthread_cond_signal(&waitCond);
  }
  pthread_mutex_unlock(&decreLock);
  return NULL;
}

/* print to a string the elapsed time and the CPU time relative
 * to the start (elapsed time) and startc (CPU time) variables
 *
 * The string returned is in a static array, so if multiple threads
 * call this function at the same, the results may be inaccurate.
 */

static char * all_times (struct timeval start, clock_t startc)
{
// all computations done in microseconds
#define US_PER_S	(1000 * 1000)
  struct timeval finish;
  gettimeofday (&finish, NULL);
  long delta = (finish.tv_sec - start.tv_sec) * US_PER_S +
               finish.tv_usec - start.tv_usec;
  long delta_cpu = (clock() - startc) * US_PER_S / CLOCKS_PER_SEC;
  static char result [1000];
  snprintf (result, sizeof (result), "%ld.%06lds, cpu time %ld.%06ld",
            delta / US_PER_S, delta % US_PER_S,
            delta_cpu / US_PER_S, delta_cpu % US_PER_S);
  return result;
}

int main (int argc, char ** argv)
{
  //initialize the mutex 
  if (pthread_mutex_init(&countLock, NULL) != 0){
    printf( "countLock mutex initialization failed\n" );
    return -1; 
  }
  if (pthread_mutex_init(&decreLock, NULL) != 0){
    printf( "decreLock mutex initialization failed\n" );
    return -1; 
  }
  if (pthread_mutex_init(&mainLock, NULL) != 0){
    printf( "mainLock mutex initialization failed\n" );
    return -1; 
  }
  if (pthread_mutex_init(&threadLock, NULL) != 0){
    printf( "threadLock mutex initialization failed\n" );
    return -1; 
  }

  //create threads 
  long num_threads = (argc <= 1) ? THREADS : atoi (argv[1]);            //am I true ? if yes : if no //is no arguement #of threads = 2, else it equals user input
  struct state_struct state =
    { .start = 0, .counter = 0, .threads = 0,
      .num_loops = (argc <= 2) ? LOOPS : atoi (argv[2]) };              //10 * 1000 * 1000 unless another argument is specified 
  
  //aquire thread lock 
  pthread_mutex_lock(&threadLock);                   
  while (state.threads < num_threads) {
    pthread_t t;
    pthread_create (&t, NULL, thread, (void *)&state);                  //address = t, start routine = threat() <above>, state is the arguement 
    state.threads++;                                                    //incease the number of threads in state 
    #ifdef DEBUG
        printf("{thread creator}: there are %ld threads\n",state.threads);  
    #endif
  }
  
  //get time of loop 
  struct timeval start;
  gettimeofday (&start, NULL);
  clock_t startc = clock();
  state.start = 1;                                                      //start all the threads
  
  //release thread lock 
  #ifdef DEBUG
    printf("unlocking threadLock\n");
  #endif 
  pthread_mutex_unlock(&threadLock);


  sleep(1);                                                             //sleep so the threads can get the lock first 
  pthread_mutex_lock(&mainLock);                                        //must aquire this lock before moving forward

  
  
  while (state.threads > 0){                                            /* loop until all the threads are done */
    printf("{main:} SPINNING\n");
    sleep(1);
  }

  printf ("%ld total count, expected %ld, time %ss\n",
          state.counter, state.num_loops * num_threads,
          all_times (start, startc));

  pthread_mutex_destroy(&mainLock);                                      //destroy this lock 

  #ifdef DEBUG
    printf("end of program\n");
  #endif 
  return 0; 
}