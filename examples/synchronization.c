/* 
 * Let's implement a semaphore.
 * Attempt #1
 */

typedef struct _semaphore
{
  volatile int count;  /* oops! This must be declared volatile! */
} semaphore_t;

semaphore_t sem;

void P(semaphore *sem)
{
 retry: // busy-waiting!
  if(sem->count > 0) // needs to be atomic!
    sem->count--;
  else
    goto retry;
}

/*
 * Another thread might get in between lines 16 and 17, so we
 * will have a race condition.
 * Attempt #2
 */

typedef struct _semaphore
{
  volatile count;
  spinlock *lock;
}semaphore_t;


void P(semaphore_t *sem)
{
 retry:
  spinlock_get(sem->lock);
  if(sem->count > 0)
    {
      sem->count--;
      spinlock_release(sem->lock);
    }
  else
    goto retry;
}

/* What's wrong with that? 
 * If we don't succeed and go to retry, 
 * we deadlock, because we never released the lock. 
 * Attempt #3
 */

void P(semaphore_t *sem)
{
 retry:
  spinlock_get(sem->lock);
  if(sem->count > 0)
    {
      sem->count--;
      spinlock_release(sem->lock);
    }
  else
    {
      spinlock_release(sem->lock);
      goto retry;
    }
}

/* Ok, this seems to be ok. How do you implement
 * V()
 */

void V(semaphore_t *sem)
{
  sem->count++;
}

/* Does this work? We cannot guarantee it. 
 * Depends on how the compiler translates this
 * statement into hardware instructions. In most
 * cases this will be more than one instruction:
 * load 'count' into a register
 * add 1 to the register content
 * write register content back into 'count'
 * 
 * So we have a problem if another thread gets in-between. 
 */

void V(semaphore_t *sem)
{
  spinlock_get(sem->lock);
  sem->count++;
  spinlock_release(sem->lock);
}

/* Anyone see any problems with the above implementation?
 * You busy-wait. How do we get around it? Let's put the
 * thread to sleep!
 */

void P(semaphore_t *sem)
{
 retry:
  spinlock_get(sem->lock);
  if(sem->count > 0)
    {
      sem->count--;
      spinlock_release(sem->lock);
    }
  else
    {
      spinlock_release(sem->lock);
      thread_sleep();
      goto retry;
    }
}

/* Problem? 
 * How does the thread wake up? 
 * Solutions: have it wake up after a fixed time period.
 * Will this work? It'll be correct, but difficult to tune. 
 *
 * Have it sleep until a condition is met, that is, 
 * until someone V()s the semaphore. 
 * How might one implement it? 
 * Let's pretend that an OS provides us an abstraction:
 * a named sleep queue. So we put a thread to sleep into
 * that queue. When the semaphore_td is V()'d, the thread
 * that V()'d it wakes up the threads from that queue. 
 * Let's call that queue a wait channel (wchan).
 */

typedef struct _semaphore
{
  count;
  spinlock *lock;
  wait_channel *wchan;
}semaphore_t;

void P(semaphore_t *sem)
{
 retry:
  spinlock_get(sem->lock);
  if(sem->count > 0)
    {
      sem->count--;
      spinlock_release(sem->lock);
    }
  else
    {
      spinlock_release(sem->lock);
      thread_sleep(sem->wchan);
      goto retry;
    }
}

void V(semaphore_t *sem)
{
  spinlock_get(sem->lock);
  sem->count++;
  thread_wake(sem->wchan);
  spinlock_release(sem->lock);
}

/* 
 * Anyone sees a problem with that? 
 * If Thread1 tries to wakeup Thread2, when Thread2 is
 * between lines 156 and 157, the thread will miss the
 * wakeup signal. Then it'll go to sleep and sleep forever!
 *
 * How do we solve the problem? 
 *
 * Understand how OS161 solves it by studying its implementation of
 * semaphores. (It's a homework question too!).
 */
