#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

#include <test.h>
/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */


static struct cv *cv_N, *cv_E, *cv_S, *cv_W; // cars waiting on a direction
static struct lock *lk_dir; // protects the current green light direction
static volatile Direction dir; // current green light direction
bool volatile stop = false; 
static volatile int intCount = 0; 
static volatile int w_N = 0, w_E = 0, w_S = 0, w_W = 0; 
/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */


void
intersection_sync_init(void)
{
  cv_N = cv_create("North"); 
  cv_E = cv_create("East"); 
  cv_S = cv_create ("South"); 
  cv_W = cv_create ("West"); 
	
  if (cv_N == NULL || cv_E == NULL || cv_S == NULL || cv_W == NULL) {
	  panic("could not create intersection condition variables");
  }
  
  lk_dir = lock_create ("Direction"); 
  if (lk_dir == NULL) {
	  panic ("could not create direction and counter locks"); 
  }
 }

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
	KASSERT(cv_N != NULL && cv_E != NULL && cv_S != NULL && cv_W != NULL); 
	KASSERT (lk_dir != NULL); 
	cv_destroy(cv_N); 
	cv_destroy(cv_E);
	cv_destroy(cv_S);
	cv_destroy(cv_W);
	lock_destroy(lk_dir); 
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */



static void wchan_wakeCars () {
	KASSERT (lock_do_i_hold(lk_dir)); 
	if (dir == north) {
		cv_broadcast(cv_N, lk_dir);
	} else if (dir == east) {
		cv_broadcast (cv_E, lk_dir);
	} else if (dir == south) {
		cv_broadcast(cv_S, lk_dir);
	} else {
		cv_broadcast(cv_W, lk_dir); 
	}
}

static void switchLight () {
	// keep switching light until a direction with one car waiting is reached (unless there are no cars waiting)
	KASSERT (lock_do_i_hold(lk_dir)); 
	
	for (int i = 0; i < 3; ++i) {
		if (dir == west) {
			dir = north; 
		} else {
			dir++; 
		}
	//	kprintf ("direction check %d, N %d E %d, S %d, W %d\n", dir, w_N, w_E, w_S, w_W); 
		if (dir == north && w_N > 0) {
			break; 
		} else if (dir == east && w_E > 0) {
			break; 
		} else if (dir == south && w_S > 0) {
			break; 
		} else if (dir == west && w_W > 0) {
			break;
		}
	}
	//kprintf ("direction reset to %d\n", dir); 
}

void
intersection_before_entry(Direction origin, Direction destination) 
{
	lock_acquire(lk_dir); 

	if (w_N == 0 && w_E == 0 && w_S == 0 && w_W == 0 && intCount == 0) { // first car
		dir = origin; 
	}
	while (origin != dir) {
	//	kprintf ("waiting: %d to %d with dir %d\n", origin, destination, dir);
	//	kprintf ("wait status: N %d E %d S %d W %d\n", w_N, w_E, w_S, w_W); 
		if (origin == north) {
			w_N++; 
			// while loop
	//		kprintf ("++++++++++++++++++north wait incrementing %d\n", w_N); 
			cv_wait (cv_N, lk_dir);
			w_N--;
	//	       kprintf ("---------------------north wait changed to %d\n", w_N); 
		} else if (origin == east) {
			w_E++; 
			cv_wait (cv_E, lk_dir);
			w_E--; 
		} else if (origin == south) {
			w_S++; 
			cv_wait (cv_S, lk_dir);
			w_S--; 
		} else { // origin == west
			w_W++; 
			cv_wait (cv_W, lk_dir);	
			w_W--;  
			
		}
	//kprintf ("end wait: %d to %d with dir %d\n", origin, destination, dir);
		
	}
	// car is ready to go through intersection, increment counters
	intCount++; 
	//kprintf ("car going from %d to %d with Count %d\n", origin, destination, intCount); 
	lock_release(lk_dir); 
	
	(void)destination;  
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
	lock_acquire(lk_dir); 
	--intCount; 
	if (intCount == 0) {
		switchLight(); 
		wchan_wakeCars(); 
	}
  
	lock_release(lk_dir); 
	
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
}
