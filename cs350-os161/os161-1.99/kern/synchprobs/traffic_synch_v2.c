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
static volatile int carsEntered= 0; // number of cars currently in the intersection
static volatile int carsArrived = 0;

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
	        w_N = 0; 	
	} else if (dir == east) {
		cv_broadcast (cv_E, lk_dir);
	        w_E = 0; 	
	} else if (dir == south) {
		cv_broadcast(cv_S, lk_dir);
	        w_S = 0; 	
	} else {
		cv_broadcast(cv_W, lk_dir); 
		w_W = 0;  
	}
//	kprintf ("waking up cars from %d\n", dir); 
}

static int countWaiting () {
	KASSERT (lock_do_i_hold(lk_dir)); 
	if (dir == north) return w_N; 
	else if (dir == east) return w_E; 
	else if (dir == south) return w_S; 
	else return w_W; 
}

// lk_dir is held after leaving switchLight

static void switchLight () {
	// keep switching light until a direction with one car waiting is reached (unless there are no cars waiting)
	KASSERT (lock_do_i_hold(lk_dir)); 
	
	int switchCount = 0; 
	do {
		++switchCount; 
	if (dir == north) {
		dir = east;
	} else if (dir == east) {
		dir =  south; 
	} else if (dir == south) {
		dir = west; 
	} else {
		dir = north; 
	}
	} while (countWaiting() == 0 && switchCount <= 3); 
	// reset counter
//	kprintf ("direction reset to %d\n", dir); 
}

/*
static void switchLight () {
	KASSERT (lock_do_i_hold(lk_dir)); 
	
	if (dir == north) {
		dir = east;
	} else if (dir == east) {
		dir =  south; 
	} else if (dir == south) {
		dir = west; 
	} else {
		dir = north; 
	}
	kprintf ("direction reset to %d\n", dir); 
}
*/
void
intersection_before_entry(Direction origin, Direction destination) 
{
	lock_acquire(lk_dir); 
	if (origin == dir && !stop) { // car origin is on green light
	//	kprintf ("green light pass from %d to %d\n", origin, destination); 
		if (carsEntered >= 10) { // last car going through
			// stop all cars from going through
			stop = true; 
			kprintf ("stop cars from coming in\n"); 
		}		
	} else if (!stop && carsEntered == carsArrived) { // switch light here, base case
		dir = origin;
		carsEntered = 0; 
		carsArrived = 0; 
		wchan_wakeCars(); 
		kprintf ("intersection cleared, dir is now %d\n", dir); 
	} else { // car must wait
		kprintf ("waiting: %d to %d with dir %d\n", origin, destination, dir);
		if (origin == north) {
			w_N++; 
			cv_wait (cv_N, lk_dir);
		} else if (origin == east) {
			w_E++; 
			cv_wait (cv_E, lk_dir);
		} else if (origin == south) {
			w_S++; 
			cv_wait (cv_S, lk_dir);
		} else { // origin == west
			w_W++; 
			cv_wait (cv_W, lk_dir); 
		}
	kprintf ("end wait: %d to %d with dir %d\n", origin, destination, dir);
		
	}
	// car is ready to go through intersection, increment counters
	carsEntered++; 
	//kprintf ("car going from %d to %d with enteredCount %d\n", origin, destination, carsEntered); 
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
	++carsArrived;  
	//kprintf ("car arrived from %d to %d now arrival is %d departure is %d\n", origin, destination, carsArrived, carsEntered); 
	if (carsArrived == carsEntered) { // < base case for first car
	//	kprintf ("all cars have arrived, change dir now\n"); 	
		// all cars have cleared the intersection
		stop = false; 
		
		switchLight(); 
		wchan_wakeCars(); 
		
		carsEntered = 0; 
		carsArrived = 0; 
	} 
	lock_release(lk_dir); 
	
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
}
