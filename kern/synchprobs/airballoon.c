/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16

static volatile int ropes_left = NROPES;

// ===== Data structures for rope mappings =====
// true --- connected
// false --- disconnected
volatile bool ropes[NROPES];

// status of each hook/stake
// -1 --- disconnected
// other --- this # of rope is connected
volatile int hooks[NROPES];
volatile int stakes[NROPES];

// ===== Synchronization primitives =====
struct semaphore *sem;
struct lock *lk;
struct cv *cv_flowerkiller, *cv_dandelion, *cv_marigold, *cv_balloon;

volatile int flowerkiller_count = N_LORD_FLOWERKILLER;

volatile bool done_dandelion = false;
volatile bool done_marigold = false;
volatile bool done_balloon = false;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static
void
dandelion(void *p, unsigned long arg)
{
  (void)p;
  (void)arg;

  kprintf("Dandelion thread starting\n");

  /* Implement this function */
  size_t index = random() % NROPES;
  P(sem);
  while (ropes_left > 0) {
    while (ropes[hooks[index]] == false || hooks[index] == -1) {
      index = random() % NROPES;
    }

    ropes[hooks[index]] = false;
    ropes_left--;
    kprintf("Dandelion severed rope %d\n", hooks[index]);
    hooks[index] = -1;

    V(sem);

    thread_yield();

    P(sem);
  }

  kprintf("Dandelion thread done\n");
  V(sem);

  lock_acquire(lk);
    done_dandelion = true;
    // cv_signal(cv_dandelion, lk);
  lock_release(lk);

  return;
}

static
void
marigold(void *p, unsigned long arg)
{
  (void)p;
  (void)arg;

  kprintf("Marigold thread starting\n");

  /* Implement this function */
  size_t index = random() % NROPES;
  P(sem);
  while (ropes_left > 0) {
    while (ropes[stakes[index]] == false || stakes[index] == -1) {
      index = random() % NROPES;
    }

    ropes[stakes[index]] = false;
    ropes_left--;
    kprintf("Marigold severed rope %d\n", stakes[index]);
    stakes[index] = -1;

    V(sem);

    thread_yield();

    P(sem);
  }

  kprintf("Marigold thread done\n");
  V(sem);

  lock_acquire(lk);
    done_marigold = true;
    // cv_signal(cv_marigold, lk);
  lock_release(lk);

  return;
}

// static
// void
// flowerkiller(void *p, unsigned long arg)
// {
//   (void)p;
//   (void)arg;

//   kprintf("Lord FlowerKiller thread starting\n");

//   /* Implement this function */
//   int index = random() % NROPES;
//   P(sem);
//   while (ropes_left > 0) {
//     int stake1 = index;
//     while (ropes[stakes[index]] == false || stakes[index] == -1) {
//       stake1 = random() % NROPES;
//     }

//     int stake2 = index;
//     while (ropes[stakes[index]] == false || stakes[index] == -1 || stake1 == stake2) {
//       stake2 = random() % NROPES;
//     }

//     int rope1 = stakes[stake1];
//     int rope2 = stakes[stake2];
//     stakes[stake1] = rope2;
//     stakes[stake2] = rope1;
//     kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope1, stake1, stake2);
//     kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope2, stake2, stake1);

//     V(sem);
//     thread_yield();
//     P(sem);
//   }

//   kprintf("Lord FlowerKiller thread done\n");
//   V(sem);

//   lock_acquire(lk);
//     flowerkiller_count--;
//     if (flowerkiller_count == 0) {
//       cv_signal(cv_flowerkiller, lk);
//     }
//   lock_release(lk);
//   return;
// }

static
void
balloon(void *p, unsigned long arg)
{
  (void)p;
  (void)arg;

  kprintf("Balloon thread starting\n");

  /* Implement this function */
  P(sem);
  while (ropes_left > 0) {
    V(sem);
      thread_yield();
    P(sem);
  }

  kprintf("Balloon freed and Prince Dandelion escapes!\n");
  V(sem);

  lock_acquire(lk);
    done_balloon = true;
    // cv_signal(cv_balloon, lk);
  lock_release(lk);

  kprintf("Balloon thread done\n");
  return;
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{

  int err = 0;

  (void)nargs;
  (void)args;
  (void)ropes_left;

  // ===== setup =====
  // mappings
  for (size_t i = 0; i < NROPES; i++) {
    ropes[i] = true;
    hooks[i] = i;
    stakes[i] = i;
  }

  // locks
  sem = sem_create("sem", 1);
  if (sem == NULL) panic("sem_create failed: %s\n", strerror(err));

  lk = lock_create("lk");
  if (lk == NULL) panic("lock_create failed: %s\n", strerror(err));

  cv_flowerkiller = cv_create("cv_flowerkiller");
  if (cv_flowerkiller == NULL) panic("cv_create failed: %s\n", strerror(err));
  cv_dandelion = cv_create("cv_dandelion");
  if (cv_dandelion == NULL) panic("cv_create failed: %s\n", strerror(err));
  cv_marigold = cv_create("cv_marigold");
  if (cv_marigold == NULL) panic("cv_create failed: %s\n", strerror(err));
  cv_balloon = cv_create("cv_balloon");
  if (cv_balloon == NULL) panic("cv_create failed: %s\n", strerror(err));


  // ===== forking =====
  err = thread_fork("Marigold Thread", NULL, marigold, NULL, 0);
  if(err) goto panic;

  err = thread_fork("Dandelion Thread", NULL, dandelion, NULL, 0);
  if(err) goto panic;

  // for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
  //   err = thread_fork("Lord FlowerKiller Thread", NULL, flowerkiller, NULL, 0);
  //   if(err) goto panic;
  // }

  err = thread_fork("Air Balloon", NULL, balloon, NULL, 0);
  if(err) goto panic;

  // ===== done? =====
  lock_acquire(lk);
    while (!done_dandelion || !done_marigold || !done_balloon) {
      lock_release(lk);
      thread_yield();
      lock_acquire(lk);
    }
    
  //   // cv_wait(cv_flowerkiller, lk);
  //   kprintf("%d", ropes_left);
  //   cv_wait(cv_dandelion, lk);
  //   cv_wait(cv_marigold, lk);
  //   cv_wait(cv_balloon, lk);
  lock_release(lk);

  goto done;

  panic:
    panic("airballoon: thread_fork failed: %s)\n", strerror(err));

  done:
    // ===== free =====
    lock_destroy(lk);
    sem_destroy(sem);
    kprintf("Main thread done\n");

  return 0;
}