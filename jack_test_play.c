#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <jack/jack.h>
#include "sndfile.h"
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <semaphore.h>
#include <getopt.h>
#define MAX_PLAYBACK_TIME 180 /* In seconds */

typedef struct _frame_params {
	 unsigned int channels;
	 unsigned int rate; 
     int format;
}frame_params_t;

typedef enum _playback_mode {
	PLAYBACK = 0,
    RECORD
}playback_mode;

//Utility structure
struct _framework_struct;

typedef struct _framework_struct {
	   jack_client_t *client_handle; //Jack Client Handle
	   frame_params_t fParams; //Params of File Read
	   char *client_name; // Name of client
	   playback_mode modeType; // Mode Type PLAYBACK / RECORD
	   jack_options_t options;  // Options of Jack
	   jack_status_t  status; // Status when Jack is open
	   char server_name[32]; // Name of Jack Server
       JackProcessCallback process_callback; //Process Call Back
	   JackShutdownCallback	shutdown_callback; //Shutdown Call back
	   jack_port_t *port[2]; // Port for Handle
	   char port_name[2][32]; //Port Name
	   char *filename; // Name of the file to playback
       SNDFILE* sndfile; // Handle returned by SNDFILE library
	   float *sampleBuf; // Buffer for Playback
	   sf_count_t (*rw_function)(SNDFILE *_sndfile, float *ptr, sf_count_t frames) ; //Read write function
	   unsigned int loop_frames; // Total Number of Frames to read in wav file
	   unsigned int offsetInBytes;  // Total Bytes Playbacked till now 
}framework_struct;

int play_process_cbk (jack_nframes_t nframes, void *arg);
 
void play_shutdown_cbk (void *arg);


framework_struct gJackStruct[] = {
	//{client_handle,fParams, client_name , modeType , options, status ,     server_name process_callback   shutdown_callback  port , port_name ,
	//filename, sndfile , sampleBuf, rw_function, loop_frames, offsetInBytes }, 
	{  NULL,  {0,0,0},"play_1_c",PLAYBACK,JackPortIsOutput,0,{0}, play_process_cbk,  play_shutdown_cbk, {NULL,NULL},{{"play_1_p1"},{"play_1_p2"}}, 
	NULL     ,   NULL,      NULL, NULL,     0,              0},	
}; 


int tTime = -1;
unsigned int  frames_processed=0;

void play_shutdown_cbk (void *arg){
}

void parse_sfinfo(framework_struct *vJackStruct, SF_INFO *sfinfo){
	if(vJackStruct->modeType == PLAYBACK){
		fprintf(stderr,"channels[%d] , samplerate[%d] , format[%d] \n",
			sfinfo->channels,sfinfo->samplerate,(sfinfo->format)&0xFF);
		if(sfinfo->channels != 2) {
			fprintf(stderr,"Only stereo channels are supported \n");
			exit(0);
		}
		vJackStruct->fParams.channels = sfinfo->channels;
		vJackStruct->fParams.rate = sfinfo->samplerate;
	
		switch (sfinfo->format & 0xFF) {
		case SF_FORMAT_FLOAT:
			vJackStruct->fParams.format = 0;
			vJackStruct->rw_function = sf_readf_float;
		break;
	    case SF_FORMAT_PCM_32:
	    case SF_FORMAT_PCM_16:
		case SF_FORMAT_PCM_S8:
		default:
			fprintf(stderr,"unsupported format \n");	
			exit(1);
		}
	}
}


static inline void deinterleave(float *src, float *dst, int n, int m)
{
	int i;
    for(i = 0; i < n; i++){
		dst[i] = src[m*i];
    }
}


int play_process_cbk (jack_nframes_t nframes, void *arg) {
	int i;
	jack_default_audio_sample_t  *out;
	
	framework_struct* vJackStruct = (framework_struct *) arg;
/*Last period will not be played */
	if(vJackStruct->loop_frames <= (frames_processed+nframes)) {
		free(vJackStruct->sampleBuf);
        exit(1); 
	}

	for(i=0;i<vJackStruct->fParams.channels;i++) {
		out = jack_port_get_buffer(vJackStruct->port[i], nframes);

		if(out)
			deinterleave(vJackStruct->sampleBuf+vJackStruct->offsetInBytes + i, out ,nframes, vJackStruct->fParams.channels);
	}

	vJackStruct->offsetInBytes +=vJackStruct->fParams.channels*nframes;
	frames_processed += nframes;
	//`fprintf(stderr,"vJackStruct->offsetInByte [%d] frame_processed [%d] nframes [%d] \n",vJackStruct->offsetInBytes,frames_processed,nframes);

	return 0;
}

void usage () {
	fprintf(stderr,"usage:./jack_test_play.out -f <filename> [-t <duration>] \n");
}
int main(int argc, char *argv[]) {
	SF_INFO sfinfo;
	int err,i,j;
	const char **ports;
	sf_count_t tFileFrames;
	int fileDuration;
	struct option long_option[] =
    {
        {"help", 0, NULL, 'h'},
        {"play_file", 1, NULL, 'f'},
        {"time", 1, NULL, 't'},
        {NULL, 0, NULL, 0},
    };
	if(argc < 2) {
		usage();
		exit(0);
	}

	while (1) {
        int c;
        if ((c = getopt_long(argc, argv, "f:ht:", long_option, NULL)) < 0)
            break;
        switch (c) {
        case 'f':
            gJackStruct[0].filename = strdup(optarg); //filename playback
            break;
		case 't':
			tTime = atoi(optarg);
		break;
		case 'h':
			usage();
		break;
		default:
			fprintf(stderr,"Invalid Option \n");
			exit(1);
		}
	}	


//Playback 
	for(i=0;i<1;i+=1){
			
			if(gJackStruct[i].modeType==PLAYBACK) {   //Read the file
				gJackStruct[i].sndfile = sf_open(gJackStruct[i].filename,SFM_READ,&sfinfo);
				if (!gJackStruct[i].sndfile) {
					fprintf(stderr, "Failed to open file '%s'\n",gJackStruct[i].filename);
					return 0;
				}
				parse_sfinfo(&(gJackStruct[i]),&sfinfo);
			}
			
			/* Calculation for time realted functionality */
			tFileFrames = sf_seek(gJackStruct[i].sndfile,-1,SEEK_END);
			fileDuration = tFileFrames /gJackStruct[i].fParams.rate;

			tFileFrames = sf_seek(gJackStruct[i].sndfile,0,SEEK_SET);
			
			/* Allowed max limit is 3 minutes which 180 seconds */
			
			if(tTime > MAX_PLAYBACK_TIME) {
				tTime = MAX_PLAYBACK_TIME;
			}

			if(tTime == -1) {
				fprintf(stderr, "Play entire duration of file \n");
				tTime = fileDuration;
				if(fileDuration > MAX_PLAYBACK_TIME) 
					tTime = MAX_PLAYBACK_TIME;
			}
/* If file duration is less than tTime */
			if(tTime > fileDuration)
				tTime = fileDuration;

			gJackStruct[i].client_handle = jack_client_open(gJackStruct[i].client_name,JackNullOption, &(gJackStruct[i].status),NULL);
			if(gJackStruct[i].client_handle == NULL){	
				fprintf(stderr," jack_client_open returned NULL for [%d]\n",i);
				return 0;
			}
			
			if (gJackStruct[i].status & JackServerStarted) 
				fprintf (stderr, "JACK server started for [%d] \n",i);
				
			if (gJackStruct[i].status & JackNameNotUnique) {
				gJackStruct[i].client_name = jack_get_client_name(gJackStruct[i].client_handle);
				fprintf (stderr, "unique name `%s' assigned\n", gJackStruct[i].client_name);
			}
			
			jack_set_process_callback (gJackStruct[i].client_handle,gJackStruct[i].process_callback,(&gJackStruct[i])) ;

			jack_on_shutdown (gJackStruct[i].client_handle, gJackStruct[i].shutdown_callback,&(gJackStruct[i]));
			
			for(j=0;j<gJackStruct->fParams.channels;j++) {
				if(gJackStruct[i].modeType==PLAYBACK) {
					gJackStruct[i].port[j] = jack_port_register (gJackStruct[i].client_handle, gJackStruct[i].port_name[j],
											JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
					if(gJackStruct[i].port[j]  == NULL) {
						fprintf(stderr, "no more JACK ports available for instance %d channel %d\n",i,j);
						return 0;
					}
				}
			}
			
			if(gJackStruct[i].modeType==PLAYBACK) {
				ports = jack_get_ports (gJackStruct[i].client_handle, NULL, NULL,JackPortIsPhysical|JackPortIsInput);
				if (ports == NULL) {
					fprintf(stderr, "no physical capture ports\n");
					return 0;
				}
			}
/* For 44.1 khz stream buffer allocated is for 300 sec duration*/
/* Buffer size for 44100*2*4*180 (samplefreq*channel*sizeof(float)*tTime) is 60MB 	 */
			//malloc file till tTime 

			if(gJackStruct[i].modeType == PLAYBACK) {
				gJackStruct[i].sampleBuf = (float *)malloc((tTime)*gJackStruct[i].fParams.rate*gJackStruct[i].fParams.channels*sizeof(float));
		    //Copy entire playback file 
				gJackStruct[i].rw_function(gJackStruct[i].sndfile,gJackStruct[i].sampleBuf,tTime*gJackStruct[i].fParams.rate);
			//loop frames
                gJackStruct[i].loop_frames = tTime*gJackStruct[i].fParams.rate;
				fprintf(stderr,"tTime [%d] loop_frames [%d] \n",tTime,gJackStruct[i].loop_frames);
			}

			if (jack_activate (gJackStruct[i].client_handle)) {
				fprintf (stderr, "cannot activate client");
				exit (1);
			}
			
			if(gJackStruct[i].modeType == PLAYBACK){
				for(j=0;j<gJackStruct->fParams.channels;j++){
				fprintf(stderr,"jack port name %s\n",jack_port_name (gJackStruct[i].port[j]));
				if (jack_connect (gJackStruct[i].client_handle,jack_port_name (gJackStruct[i].port[j]),ports[j])!=0) {
						fprintf (stderr, "cannot connect input ports %d channel %d\n",i,j);
					}
				}
			}
	
			free (ports);
		}
	sleep (-1);
}
