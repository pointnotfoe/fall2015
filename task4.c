/*
 * Placeholder PetaLinux user application.
 *
 * Replace this with your application code
 */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include "vga_ioctl.h"
#include <termios.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <errno.h>
#include "../../user-modules/timer_driver/timer_ioctl.h"

#define SCREEN_W 640
#define SCREEN_H 480

#define BUFFER_SIZE SCREEN_W*SCREEN_H*4

struct timer_ioctl_data data; // data structure for ioctl calls
int fd_time; // file descriptor for timer driver
time_t startime;
time_t endtime;
long totaltime;
struct vga_ioctl_data data1;//return to data
int fd;
int image_fd;
int serial_fd;
float systime;

struct pixel{
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
};

struct point{
	int x;
	int y;
};

struct rect{
	int x;
	int y;
	int w;
	int h;
};


struct image{
	int *mem_loc;
	int w;
	int h;
};

struct sub_image{
	int row;
	int col;
	int w;
	int h;
};

/* 
 * Function to read the value of the timer
 */
__u32 read_timer()
{
	data.offset = TIMER_REG;
	ioctl(fd_time, TIMER_READ_REG, &data);
	return data.data;
}

/*
 * SIGIO Signal Handler
 */
void sigio_handler(int signum) 
{
	printf("Received Signal, signum=%d (%s)\n", signum, strsignal(signum));
}

/*
 * SIGINT Signal Handler
 */
void sigint_handler(int signum)
{
	printf("Received Signal, signum=%d (%s)\n", signum, strsignal(signum));

	if (fd_time) {
		// Turn off timer and reset device
		data.offset = CONTROL_REG;
		data.data = 0x0;
		ioctl(fd_time, TIMER_WRITE_REG, &data);

		// Close device file
		close(fd_time);
	}

	exit(EXIT_SUCCESS);
}

void timer_start()
{
	// Rest the counter
	data.offset = LOAD_REG;
	data.data = 0x0;
	ioctl(fd_time, TIMER_WRITE_REG, &data);
	
	//sleep(1);

	// Set control bits to load value in load register into counter
	data.offset = CONTROL_REG;
	data.data = LOAD0;
	ioctl(fd_time, TIMER_WRITE_REG, &data);

	// Set control bits to enable timer, count up
	data.offset = CONTROL_REG;
	data.data = ENT0;
	ioctl(fd_time, TIMER_WRITE_REG, &data);

	//printf("timer initial  = %u\n", read_timer());

	
}

void timer_end()
{
	/// Clear control bits to disable timer
	data.offset = CONTROL_REG;
	data.data = 0x0;
	ioctl(fd_time, TIMER_WRITE_REG, &data);

	// Read value from timer
	//printf("timer value  = %u\n", read_timer());

}



float system_time(){
	struct sysinfo systime;
	float boot_time = systime.uptime;
	return boot_time;
}




void draw_sub_image(int *screen_buffer, struct image *i, struct point *p, struct sub_image *sub_i, struct pixel *color) {

	int imgXstart, imgXend, imgYstart, imgYend;

	/*Invalid screen buffer*/
	if(!screen_buffer)
		return;

	/*invalid image*/
	if (!i->mem_loc || sub_i->w <= 0 || sub_i->h <= 0)
		return;

	/*Starting offsets if image is partially offscreen*/
	imgXstart = (p->x < 0) ? -1*p->x : 0;
	imgYstart = (p->y < 0) ? -1*p->y : 0;

	imgXend = (p->x + sub_i->w) > SCREEN_W ? (SCREEN_W - p->x): sub_i->w;
	imgYend = (p->y + sub_i->h) > SCREEN_H ? (SCREEN_H - p->y): sub_i->h;

	/*Image is entirely offscreen*/
	if (imgXstart == imgXend || imgYstart == imgYend)
		return;

	/*Copy image*/
	int row, col;
	for (row = imgYstart; row < imgYend; row++) {
		for (col = imgXstart; col < imgXend; col++) {
			struct pixel src_p, dest_p;

			int *imgP;
			int *screenP;

			imgP    = i->mem_loc + col + sub_i->col + (i->w * (row + sub_i->row));
			screenP = screen_buffer + (p->x + col) + (p->y + row)*SCREEN_W;

			src_p.a = 0xFF & (*imgP >> 24);

			if(color) {
				src_p.r = color->r;
				src_p.g = color->g;
				src_p.b = color->b;
			}
			else {
				src_p.b = 0xFF & (*imgP >> 16);
				src_p.g = 0xFF & (*imgP >> 8);
				src_p.r = 0xFF & (*imgP >> 0);
			}

			dest_p.b = 0xFF & (*screenP >> 16);
			dest_p.g = 0xFF & (*screenP >> 8);
			dest_p.r = 0xFF & (*screenP >> 0);


			dest_p.b = (((int)src_p.b)*src_p.a + ((int)dest_p.b)*(255 - src_p.a))/256;
			dest_p.g = (((int)src_p.g)*src_p.a + ((int)dest_p.g)*(255 - src_p.a))/256;
			dest_p.r = (((int)src_p.r)*src_p.a + ((int)dest_p.r)*(255 - src_p.a))/256;

			*screenP = (dest_p.b << 16) | (dest_p.g << 8) | (dest_p.r << 0);
		}
	}


}

void draw_letter(char a, int *screen_buffer, struct image *i, struct point *p, struct pixel *color) {


    struct sub_image sub_i;

    sub_i.row = 23*((0x0F & (a >> 4))%16) + 1;
    sub_i.col = 12*((0x0F & (a >> 0))%16) + 1;
    sub_i.w = 11;
    sub_i.h = 22;


    draw_sub_image(screen_buffer, i, p, &sub_i, color);


}

void draw_string(char *s, int *screen_buffer, struct image *i, struct point *m, struct pixel *color) {
    int index = 0;
    while(s[index]) {
    	draw_letter(s[index], screen_buffer, i, m, color);
		m->x += 11;
		index++;
    }
}



int main(int argc, char *argv[])
{
    
    

    int * buffer;
    struct image img;
    struct point p;
    struct point m;
    int i, j;
    struct pixel color;
    struct rect r;
    int row, col;
    struct stat sb;
    struct termios tio;
    float update;
    int totaltime1; 
    float timer_value;
    float avg;

char Mbuffer[100];
char Mbuffer2[100];
char Mbuffer3[100];
char Mbuffer4[100];
char Mbuffer5[100];
	
/*------Open timer driver file*/
	if (!(fd_time = open("/dev/timer_driver", O_RDWR))) {
		perror("open");
		exit(EXIT_FAILURE);
	}


/*------Open VGA driver file*/   
	fd = open("/dev/vga_driver",O_RDWR);
	image_fd = open("/home/root/example2.raw",O_RDONLY);
		
	if(image_fd  == -1){
			printf("Failed to open image... :( \n");
	}
		
	/**/
       //--------------------Read from Kermit-------------------------------
       memset(&tio, 0, sizeof(tio));
       serial_fd=open("/dev/ttyPS0",O_RDWR);
       //-----------------------Opening Serial Port--------------------------
       if(serial_fd == -1){
               printf("Failed to open serial port... \n");
       }
       tcgetattr(serial_fd, &tio); //gets the parameters associated with the serial port and stores them to tio structure
       cfsetospeed(&tio, B115200); //sets the output baud rate stored in the termios structure tio to 115200
       cfsetispeed(&tio, B115200); //sets the input baud rate stored in the termios structure tio to 115200
       tio.c_lflag = tio.c_lflag & ~(ICANON);
	   tcsetattr(serial_fd, TCSANOW, &tio); //sets the parameters associated with the serial port from stucture tio ;TCSANOW implies changes shall occur immediately

	if(fd != -1){
		
		buffer = (int*)mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		printf("buffer addr: 0x%08x\n", buffer);

        fstat (image_fd, &sb);
        img.mem_loc = (int*)mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, image_fd, 0);
        if ( img.mem_loc == MAP_FAILED) {
                  perror ("mmap");
          } 
    	printf("image addr: 0x%08x\n", img.mem_loc );
    	

        for (row = 0; row < 480; row++) {
            for (col = 0; col < 640; col++) {
                    	*(buffer + col + row*640) = 0x00000000;
    	    }
    	}
    	printf("screen cleared\n" );
    	    	
    	  p.x = 20;
    	  p.y=20;
    	  
    	  m.x = 20;
    	  m.y = 200;
    	    	
  
	color.r = 128;
        color.g = 64;
        color.b = 128;
        color.a = 255;
        
    	img.w = 192;
        img.h = 368;
        int i = 0;



/*--------Begin reading characters*/
looping:       
	while(1) {
		startime = time(NULL);
		if(startime == (startime-1)){
		perror("Failed to get starting application time \n");
		}
            
	char c;
        char foo='\n' ;

/*-------Clear top screen when reach end*/
    	   if (p.x > 640-12*2) {//end of line
                p.x = 20;
                p.y += 22;
      reread:       if (p.y > 200-23*2) {//end of screen
                    p.y = 20;
                    for (row = 0; row < 200; row++) {
                        for (col = 0; col < 640; col++) {
                                	*(buffer + col + row*640) = 0x00000000;
                	    }
                	}
                }
           }
        	printf("pre read\n" );
        	fflush(stdout);

/*--------Use hardware timer to time read operation*/
                timer_start();	
		read(serial_fd, &c, 1); 
		timer_end();
        	
		printf("post read\n" );

/*-------Check if new line*/
			if (c==foo) {//new line
           			p.x = 20;
				p.y += 22;
				goto reread; //check if bottom of screen is reached
					}
		draw_letter(c, buffer, &img, &p, &color);
	 	i ++; //increment
	
	color.r = rand() % 255;
            color.g = rand() % 255;
           	color.b = (rand()%255 + 128) % 255;
                p.x+=11;
		

		
/*-------Clear bottom screen when reach end*/
		 if (m.y > 440-23*2) {//end of screen
                    m.y = 200;
                    for (row = 200; row < 480; row++) {
                        for (col = 0; col < 640; col++) {
                                	*(buffer + col + row*640) = 0x00000000;
                	    }
                	}
               }
	
	 goto getchar;//Print out metrics	
    	
    	printf("buffer synced\n" );

    	

    close(fd);
    close(image_fd);
}
/*---Printing out metrics*/
getchar:
/*----Print number of characters read*/
sprintf(Mbuffer, "Number of characters read: %d ", i);
	draw_string(Mbuffer, buffer, &img, &m, &color);
	m.y+=22;
	m.x=20;
/*---Print number of seconds to read*/
timer_value = (float)read_timer();
sprintf(Mbuffer2,"Number of seconds to read character: %f ", timer_value/100000000);
	draw_string(Mbuffer2, buffer,  &img, &m, &color);
	m.y+=22;
	m.x=20;
/*----Print average cycle time*/
update += timer_value;
avg = update/(i+1);
sprintf(Mbuffer3,"Average cycle time: %f seconds", avg/100000000);
   	draw_string(Mbuffer3, buffer,  &img, &m, &color);
   	m.y+=22;
	m.x=20;	
/*----Print system time*/
systime = system_time();
sprintf(Mbuffer4,"Time since system start: %u seconds", systime);
        draw_string(Mbuffer4, buffer,  &img, &m, &color);
        m.y+=22;
        m.x=20;
/*----Print console time*/
endtime = time(NULL);
	if(endtime == (endtime-1)){
		perror("Failed to get ending application time \n");
	}
	totaltime = (long)endtime - (long)startime;
	totaltime1 += totaltime;
sprintf(Mbuffer5,"Console Time: %d seconds", totaltime1);
        draw_string(Mbuffer5, buffer,  &img, &m, &color);
        m.y+=22;
        m.x=20;
goto looping;

	}else{
		printf("Failed to open driver... :( \n");
	}
	return 0;
}


