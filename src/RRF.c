/*
*	JAVIER  PRIETO   CEPEDA  -- 100307011
*	MARIN   LUCIAN   PRIALA  -- 100303625
*	SERGIO  JIMENEZ  RUIZ    -- 100303582
*/

#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"

/* --Incluimos biblioteca para el uso de las colas-- */
#include "queue.h"

TCB* scheduler();

void activator();

void timer_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N]; 

/* Current running thread */
//static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* START- NEW VARIABLES */

/* --COLA CON LOS PROCESOS LISTOS PARA EJECUTAR -- */
struct queue *lowReadyQueue;
struct queue *highReadyQueue;

/* --PROCESO ACTUAL EN EJECUCION-- */
TCB *actualProcess;

/* END- NEW VARIABLES */

/* Initialize the thread library */
void init_mythreadlib() 
{
  	int i;  
	t_state[0].state = INIT;
	t_state[0].priority = LOW_PRIORITY;
	t_state[0].ticks = QUANTUM_TICKS;
	if(getcontext(&t_state[0].run_env) == -1)
	{
		perror("getcontext in my_thread_create");
		exit(5);
	}	
	/*-- START-NEW CODE--*/

	//Ponemos variable current (proceso actual) para el primer proceso en ejecucion e inicializamos la cola de readys
	actualProcess = &t_state[0];
	lowReadyQueue  = queue_new();
	highReadyQueue = queue_new();



	/*-- END-NEW CODE--*/
	for(i=1; i<N; i++)
	{
		t_state[i].state = FREE;
	}
	init_interrupt();
}


int mythread_create (void (*fun_addr)(),int priority)
{
	int i;
  
	if (!init)
	{
		init_mythreadlib();
		init=1;
	}
	for (i=0; i<N; i++)
	{
		if (t_state[i].state == FREE)
		{
			break;
		}
	}
	if (i == N)
	{
		return(-1);
	}
	if(getcontext(&t_state[i].run_env) == -1)
	{
		perror("getcontext in my_thread_create");
		exit(-1);
	}
  	t_state[i].state = INIT;
  	t_state[i].priority = priority;
  	t_state[i].function = fun_addr;
	/*-- START-NEW CODE --*/

	/* DAMOS UN ID AL PROCESO E INICIALIZAMOS LA RODAJA*/
	t_state[i].tid   = i;
	t_state[i].ticks = QUANTUM_TICKS; 
	
	/*-- END-NEW CODE --*/
  	t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  	if(t_state[i].run_env.uc_stack.ss_sp == NULL)
	{
    		printf("thread failed to get stack space\n");
    		exit(-1);
  	}
  	t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  	t_state[i].run_env.uc_stack.ss_flags = 0;
  	makecontext(&t_state[i].run_env, fun_addr, 1);
	/*-- START-NEW CODE --*/

	/* PROTEGEMOS EL ACCESO A LA COLA DESHABILITANDO LAS INTERRUPCIONES E INSERTAMOS EL NUEVO HILO EN LA COLA DE LISTOS*/
	disable_interrupt();
	TCB *tcb = &t_state[i];
	/*CONSULTAMOS LA PRIORIDAD DEL PROCESO PARA ENCOLAR EN SU COLA*/
	//printf("INSERTO: %d con %d\n",tcb->tid,tcb->priority);
	if(tcb->priority == HIGH_PRIORITY)
	{
		if(actualProcess->priority == HIGH_PRIORITY)
		{
			enqueue(highReadyQueue,tcb);
		}else{
			/*EXPULSION -- LLEGA UNO DE ALTA PRIORIDAD .. Y HAY UNO DE BAJA*/
			//printf("swap\n");
			printf(" *** THREAD %d EJECTED : SET CONTEXT OF %d\n",actualProcess->tid,tcb->tid);
			activator(tcb);
		}		
	}else{
		enqueue(lowReadyQueue,tcb);
	}
	enable_interrupt();
	
	/*-- END-NEW CODE --*/
  
  	return i;
} /****** End my_thread_create() ******/


/* Free terminated thread and exits */
void mythread_exit() 
{
	int tid = mythread_gettid();	

  	printf(" *** THREAD %d FINISHED\n", tid);		
  	t_state[tid].state = FREE;
  	free(t_state[tid].run_env.uc_stack.ss_sp); 
  
  	/* PROXIMO PROCESO A EJECUTAR */
	disable_interrupt();		
	TCB *newProcess=scheduler();
	if(newProcess==actualProcess)
	{
		/*SEGUIMOS EJECUTANDO, NO SE REALIZA CAMBIO DE CONTEXTO*/
	}else{
		printf(" *** THREAD %d TERMINATED: SETCONTEXT OF %d\n",actualProcess->tid, newProcess->tid);
		activator(newProcess);
	}
	enable_interrupt();
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) 
{
  	int tid = mythread_gettid();	
  	t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) 
{
  	int tid = mythread_gettid();	
  	return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid()
{
  	if (!init)
	{ 
		init_mythreadlib();
		init=1;
	}
  	return actualProcess->tid;
}

/* Timer interrupt  */
void timer_interrupt(int sig)
{
	actualProcess->ticks--;
	//printf("interrupt: actual: %d -- ticks: %d \n",actualProcess->tid, actualProcess->ticks);
	/*MIRAMOS SI EL PROCESO ACTUAL ES DE PRIORIDAD BAJA..NO MIRAMOS SI HAY DE ALTA, PORQUE HABRIA EXPULSION EN EL CREATE*/	
	if(actualProcess->priority == LOW_PRIORITY)
	{
		if(actualProcess->ticks == 0)
		{
			/* PROXIMO PROCESO A EJECUTAR */
			disable_interrupt();
				
			TCB *newProcess = scheduler();
			//printf("interrupt: actual: %d -- ticks: %d -- %d vs %d \n",actualProcess->tid, actualProcess->ticks, actualProcess->tid, auxProcess->tid);
			if(newProcess==actualProcess)
			{
				/*SEGUIMOS EJECUTANDO, NO SE REALIZA CAMBIO DE CONTEXTO*/
			}else{
				printf(" *** SWAPCONTEXT FROM %d to %d\n", actualProcess->tid, newProcess->tid);
				activator(newProcess);		
			}

			enable_interrupt();
		} 	
	}else
	{
		/*HACEMOS FIFO EN LA COLA ALTA PRIORIDAD*/
	}
}



/* Scheduler: returns the next thread to be executed */
TCB* scheduler()
{ 	
	/*-- START-NEW CODE --*/
	TCB* newProcess = actualProcess;
	if(!queue_empty(highReadyQueue))
	{
		/*Obtenemos primer proceso de la cola de alta prioridad*/
		newProcess=dequeue(highReadyQueue);
		return newProcess;
	}else{
		if(!queue_empty(lowReadyQueue))
		{
			/*Obtenemos primer proceso de la cola de baja prioridad*/
			newProcess=dequeue(lowReadyQueue);
			return newProcess;
		}else
		{
			if(actualProcess->state==INIT)
			{
				return newProcess; // Si todas las colas vacias y el proceso no esta acabado, retornamos el actual
			}
		}	
	}
	printf(" FINISH\n");
	enable_interrupt();
	/*-- END-NEW CODE --*/

  	printf(" MYTHREAD_FREE: NO THREAD IN THE SYSTEM\nEXITING...\n");	
  	exit(1);
}
/* Activator */
void activator(TCB* next)
{
	//printf("prev: %d, next: %d\n",*next,*prev);
	if(actualProcess->state==FREE)
	{
		//printf("set\n");
		//printf("*** THREAD %d TERMINATED: SETCONTEXT OF %d\n",actualProcess->tid, next->tid);		
		actualProcess=next; 
		setcontext (&(next->run_env));
		printf(" MYTHREAD_FREE: AFTER SETCONTEXT, SHOULD NEVER GET HERE!!...\n");
	}else{
		//printf("swap\n");
		TCB* prev = actualProcess;
		prev->ticks=QUANTUM_TICKS;
		prev->state=INIT;
		actualProcess = next;
		/*ENCOLAMOS EN SU COLA CORRESPONDIENTE*/
		if(prev->priority==LOW_PRIORITY)
		{
			enqueue(lowReadyQueue,prev);
		}else
		{
			enqueue(highReadyQueue,prev);
		}
		//printf("*** SWAPCONTEXT FROM %d to %d\n", prev->tid, actualProcess->tid);
		swapcontext (&(prev->run_env), &(next->run_env));
	}
}


