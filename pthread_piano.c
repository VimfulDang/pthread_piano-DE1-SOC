#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <pthread.h>
#include <signal.h>

#include "../address_map_arm.h"


#define KEY_RELEASED 0
#define KEY_PRESSED 1

#define SAMPLE_RATE 8000
#define TWO_PI	6.28318531
#define TOTAL_TONES 13

//Declare the tones frequency and corresponding input_event value for q2w3er5t6y7ui
static double note[TOTAL_NOTES] = {261.626, 277.183, 293.665, 311.127, 329.628, 
					349.228, 369.994, 391.995, 415.305, 440.000, 466.164,
					493.883, 523.251};

static double keyboard_note[TOTAL_NOTES] = {16, 2, 17, 3, 18, 19, 5, 20, 6, 21, 
											7, 22, 23};

//arbitrary fade factor, higher frequency has faster decay
static double tone_fade_factor[TOTAL_NOTES] = {.7, .68, .66, .64, .62, .60, .58, .56, .54, .52
									.50, .58, .56};

int tone_volume[TOTAL_NOTES];
static volatile int * audio_base_ptr;

void write_to_audio_port(double);

void * audio_thread(void) {
	int i;
	double sample;
	double nth_sample = 0;
	while (1) {
		//check if this thread is cancelled.
		//if it was, thread will halt at this point
		pthread_testcancel();
		sample = 0;
		for (i = 0; i < TOTAL_NOTES; i++, nth_sample++) {
			if (tone_volume[i] > 0) {
				sample += tone_volume[i] * sin(nth_sample * note[i] * TWO_PI / SAMPLE_RATE);
				pthread_mutex_lock(&mutex_tone_volume);
				tone_volume[i] *= tone_fade_factor[i];
				pthread_mutex_unlock(&mutex_tone_volume);
				write_to_audio_port((int) sample);
			}
		}
	}
}

volatile sig_atomic_t stop;
void catchSIGINT (int signum) {
	stop = 1;
}

int main(int argc, char *argv[]) {
	struct input_event ev;
	stop = 0;
	pthread_id tid;
	void * LW_virtual;
	int fd_keyboard, fd_mmap, event_size = sizeof(struct input_event);
	int ind;

	//Initialize tone_volumes
	for(ind = 0; ind < TOTAL_NOTES; ind++) {
		tone_volumes[ind] = 0;
	}

	//Establish memory mapping
	if ((fd_mmap = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
		printf("ERROR: could not open \"/dev/mem\"...\n");
		return (-1);
	}

	LW_virtual = mmap(NULL, LW_BRIDGE_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd_mmap, 
		LW_BRIDGE_BASE);

	if (LW_virtual == NULL) {
		printf("ERROR: could not mmap()...\n");
		close(fd);
		return (-1);
	}

	audio_base_ptr = (int *) (LW_virtual + AUDIO_BASE);

	//Clear Write FIFO & set back to zero
	printf("%#x, %#x\n", *(audio_base_ptr), *(audio_base_ptr + 1));	
	*(audio_base_ptr) |= 0xC;
	*(audio_base_ptr) &= 0;
	printf("%#x, %#x\n", *(audio_base_ptr), *(audio_base_ptr + 1));

	//Get the keyboard device
	if (argv[1] == NULL) {
		printf("Specify the path to the keyboard device ex."
			"/dev/input/by-id/HP-KEYBOARD\n");
		return -1;
	}

	// Open the keyboard device
	if ((fd_keyboard = open (argv[1], O_RDONLY | O_NONBLOCK)) == -1) {
		printf("Could not open %s\n", argv[1]);
		return -1;
	}

	if ((err = pthread_create(&tid, NULL, &audio_thread, NULL)) != 0) {
		printf("thread_create failed: [%s]\n", strerror(error));
	}


	while (!stop) {
		if (read (fd_keyboard, &ev, event_size) < event_size) {
			//No event
			continue;
		}

		if (ev.type == EV_KEY && ev.value == KEY_PRESSED) {
			//Set tone_volume
			pthread_mutex_lock(&mutex_tone_volume);
			tone_volume[tone_index] = 1;
			pthread_mutex_unlock(&mutex_tone_volume);
			tone_fade_factor[tone_index] = //
			printf("Pressed key: )x%04x\n", (int) ev.code);
		}	 
		else if (ev.type == EV_KEY && ev.value == KEY_RELEASED) {
			//Set tone_volume to zero
			pthread_mutex_lock(&mutex_tone_volume);
			tone_volume[tone_index] = 0;
			pthread_mutex_unlock(&mutex_tone_volume);
			printf("Released key: 0x%04x\n"), (int) ev.code);
		}
	}

	pthread_cancel(tid);
	pthread_join(tid, NULL);

	printf("Unmapping\n");
	if (munmap(LW_virtual, LW_BRIDGE_SPAN) != 0) {
		printf("ERROR: munmap() failed...\n");
		return(-1);
	}
	close(fd_keyboard);
	close(fd_mmap);
	return 0;
}

void write_to_audio_port(int sample) {
	int write = 0;
	while ((write == 0) && !stop) {
		if ((*(audio_base_ptr + 1) & 0xFF00000) && (*(audio_base_ptr + 1) & 0x00FF000)) {
			*(audio_base_ptr + 2) = sample;
			*(audio_base_ptr + 3) = sample;
			write = 1;
		}
	}
}
