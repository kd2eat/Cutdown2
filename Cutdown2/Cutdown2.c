/*
 * Cutdown2.c
 *
 * Created: 03/15/2014 
 *  Author: mqh1
 */ 


#include <stdlib.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#define CUT_DOWN_MINUTES	1

#define CUT_DOWN_HALF_SEC	(CUT_DOWN_MINUTES * 10 * 2)

/* State machine states */
#define STATE_OFF	0
#define STATE_START_RUNNING 1
#define STATE_RUNNING	2


#define BUTTON_UNPRESSED	0
#define BUTTON_PRESSED_PENDING	1
#define BUTTON_RELEASE_PENDING	2
#define BUTTON_PRESSED	3

#define DEBOUNCE_THRESHOLD	254		// Number of consecutive samples before we believe a button press is debounced.

/* I/O Ports */
#define START_DDR	DDRA			// Start button is input
#define START_PORT	PINA
#define START_PIN	PINA5

#define LED_DDR	DDRA				// Output
#define LED_PORT	PORTA
#define LED_PIN	PA7

#define BUZZER_DDR	DDRB			// Output
#define BUZZER_PORT	PORTB
#define BUZZER_PIN	PB5

#define FET_DDR	DDRA				// Output
#define FET_PORT	PORTA
#define FET_PIN	PA1



volatile int Global_Timer_Popped = 1;		// This will be set every time the interrupt timer pops.  Set it now to hit the first iteration of the main loop.

volatile int Global_Start_Button_State = BUTTON_UNPRESSED;
volatile int Global_Start_Button_Confidence = 0;

volatile int Global_Current_State = STATE_OFF;
volatile uint32_t Global_Half_Secs_Since_Boot = 0;




void
Check_A_Button(uint8_t Button_Port, uint8_t Button_Pin, volatile int *Button_State, volatile int *Button_Confidence)
{
	
	if ((Button_Port & (1<<Button_Pin)) == 0) {				// Button pulled down (pressed) right now

		switch (*Button_State) {
			case BUTTON_UNPRESSED:
				*Button_Confidence = 0;
				*Button_State = BUTTON_PRESSED_PENDING;
				break;
			case BUTTON_PRESSED_PENDING:
				if (*Button_Confidence < DEBOUNCE_THRESHOLD) {		// Don't exceed debounce threshold, lest we wrap around
					*Button_Confidence += 1;
				} 
				break;
			case BUTTON_RELEASE_PENDING:
				*Button_Confidence = 0;		// We're waiting for the release, but we got another press.  We're bouncing.
				break;
			case BUTTON_PRESSED:
				// Do nothing.  Hopefully, we don't see another press before this is acted upon.
				break;
		}
	} else {		// Start button is NOT pressed
		switch (*Button_State) {
			case BUTTON_UNPRESSED:
				// Nothing to do here.  
				break;
			case BUTTON_PRESSED_PENDING:
				if (*Button_Confidence == DEBOUNCE_THRESHOLD) {			// We're debounced, and they just released.
					*Button_State = BUTTON_RELEASE_PENDING;				// Wait on button release.
					*Button_Confidence = 0;								// Start debounce on button release process.
				} else {
					*Button_Confidence = 0;								// We're bouncing on button press.  Zero confidence.
				}
				break;
			case BUTTON_RELEASE_PENDING:
				if (*Button_Confidence < DEBOUNCE_THRESHOLD) {		// Don't exceed debounce threshold, lest we wrap around
					*Button_Confidence += 1;
				} else {
					*Button_State = BUTTON_PRESSED;					// The Release has debounced.  It's time to act on the button press.
					*Button_Confidence = 0;
				}	
				break;
			case BUTTON_PRESSED:
				// Do nothing.  Hopefully, we don't see another press before this is acted upon.
			break;
		}
	}
}
	
void
Check_Buttons(void)
{
	Check_A_Button(START_PORT, START_PIN, &Global_Start_Button_State, &Global_Start_Button_Confidence);
}

void
Setup(void)
{
		START_DDR &= ~(1<<START_PIN);	// Initialize start button pin as an input pin, raised high.
		START_PORT |= (1<<START_PIN);
		
		BUZZER_DDR |= (1<<BUZZER_PIN);		// Set buzzer off
		BUZZER_PORT &= ~(1<<BUZZER_PIN);
		
		FET_DDR |= (1<<FET_PIN);		// Set FET pin for output.
		FET_PORT &= ~(1<<FET_PIN);		// Pull low to start.
		
		LED_DDR |= (1<<LED_PIN);		// Set LED pin for output.
		LED_PORT |= (1<<LED_PORT);		// Pull high to start.

		/* Set up timer.  We want to pop every half second. */
		TCCR0A |= (1 << TCW0 );					// 16 bit counter mode
		TCCR0B |= (1 << CS01) | (1 << CS00);	// Prescaler 64.
		/* With 1 MHZ clock and pre-scaler 64 we have 7812 interrupts per half second (0-7811).  7811 = 0x1e83.  */
		OCR0B = 0x1e;							// Do high order byte first
		OCR0A = 0x83;							// Do low order byte second.
		TIMSK |= 1 << OCIE0A;					// Enable interrupts on Match for Timer A.
		sei();									// Enable interrupts.
		
}

void
Manage_Timers()
{
	// Act only once per half-second timer pop
	if (Global_Timer_Popped != 0) {		// Interrupt occurred since last time called.
		Global_Timer_Popped = 0;
			
		// The half-second timer has popped.  Manage other timers based on the current state.
		switch (Global_Current_State) {
			case STATE_OFF:
				break;
			case STATE_RUNNING:
				Global_Half_Secs_Since_Boot++;
				BUZZER_PORT ^= (1<<BUZZER_PIN);
				LED_PORT ^= (1<<LED_PIN);
				if (Global_Half_Secs_Since_Boot > CUT_DOWN_HALF_SEC) {
					FET_PORT |= (1<<FET_PIN);
				}
				break;

		}			
	}
}


int 
main(void)
{
	
	Setup();	// Do one time initializations.
	
	while (1)  {
		Check_Buttons();			// Manage button presses and update global variables.
		Manage_Timers();

		// Process based on the current state.
		switch (Global_Current_State) {
			case STATE_OFF:
				if (Global_Start_Button_State == BUTTON_PRESSED) {
					Global_Start_Button_State = BUTTON_UNPRESSED;		// Reset the button state to unpressed.  We've processed the press.
					Global_Current_State = STATE_START_RUNNING
					;						// Go to READY state.
				}
				break;

			case STATE_START_RUNNING:
				Global_Current_State = STATE_RUNNING;						// Go to Start_Running state.
				break;
			case STATE_RUNNING:

				break;

			
		}
	}
}


ISR(TIMER0_COMPA_vect)
{
	// Reset counter to 0.  No Waveform generation mode with a 16 bit counter.
	TCNT0H = 0x00;
	TCNT0L = 0x00;
	Global_Timer_Popped = 1;


}