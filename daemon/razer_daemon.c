#include "razer_daemon.h"

// end_daemon variable written to by function 'got_sigterm_signal' and used by 'daemon_run'
volatile sig_atomic_t end_daemon = 0;

// Used to catch SIGTERM
void got_sigterm_signal(int sigal_number)
{
	end_daemon = 1;
}


void daemon_kill(struct razer_daemon *daemon,char *error_message)
{
	daemon->running = 0;
	printf("Exiting daemon.\nError: %s\n",error_message);
	exit(1);
}

struct razer_daemon *daemon_open(void)
{
 	//signal(SIGINT,stop);
 	//signal(SIGKILL,stop);
        //signal(SIGTERM,stop);
	struct razer_daemon *daemon = (struct razer_daemon*)malloc(sizeof(struct razer_daemon));
 	daemon->chroma = NULL;
 	daemon->running = 1;
 	daemon->is_paused = 0;
 	daemon->fps = 12;
 	daemon->libs_uid = 1;
 	daemon->libs = list_Create(0,0);
 	daemon->effects_uid = 1;
 	daemon->effects = list_Create(0,0);
 	daemon->fx_render_nodes_uid = 1;
 	daemon->fx_render_nodes = list_Create(0,0);//list of all render_nodes available
 	daemon->is_render_nodes_dirty = 0;
 	daemon->render_nodes = list_Create(0,0);

 	if(!(daemon->chroma=razer_open()))
 	{
 		free(daemon->chroma);
		list_Close(daemon->libs);
		list_Close(daemon->fx_render_nodes);
		list_Close(daemon->render_nodes);
		list_Close(daemon->effects);
 		free(daemon);
		return(NULL);
	}
	#ifdef USE_DBUS
	 	daemon->dbus = NULL;
		#ifdef USE_DEBUGGING
			printf("dbus: opened\n");
		#endif
	 	if(!daemon_dbus_open(daemon))
	 	{
	 		free(daemon->chroma);
			list_Close(daemon->libs);
			list_Close(daemon->fx_render_nodes);
			list_Close(daemon->render_nodes);
			list_Close(daemon->effects);
	 		free(daemon);
			return(NULL);
		}
	 	if(!daemon_dbus_announce(daemon))
	 	{
 			free(daemon->chroma);
			list_Close(daemon->libs);
			list_Close(daemon->fx_render_nodes);
			list_Close(daemon->render_nodes);
			list_Close(daemon->effects);
	 		free(daemon);
			return(NULL);
		}
	#endif
	razer_set_input_handler(daemon->chroma,daemon_input_event_handler);
	daemon->chroma->tag = daemon;
	daemon->frame_buffer = razer_create_rgb_frame();
	daemon->frame_buffer_linked_uid = 0;
	daemon->return_render_node = NULL; //TODO remember what i wanted to achieve with this variable ... :-)

	razer_set_custom_mode(daemon->chroma);
	razer_clear_all(daemon->chroma->keys);
	razer_update_keys(daemon->chroma,daemon->chroma->keys);

	//TODO Move to configuration options (dbus race condition present)

	#ifdef USE_DEBUGGING
		struct daemon_lib *lib = daemon_load_fx_lib(daemon,"daemon/fx/pez2001_collection_debug.so");
	#else
		//void *lib = daemon_load_fx_lib(daemon,"daemon/fx/pez2001_collection.so");
		struct daemon_lib *lib = daemon_load_fx_lib(daemon,"/usr/share/razer_bcd/fx/pez2001_collection.so");
	#endif
	if(lib)
		daemon_register_lib(daemon,lib);

	#ifdef USE_DEBUGGING
		struct daemon_lib *blib = daemon_load_fx_lib(daemon,"daemon/fx/pez2001_light_blast_debug.so");
	#else
		struct daemon_lib *blib = daemon_load_fx_lib(daemon,"/usr/share/razer_bcd/fx/pez2001_light_blast.so");
	#endif
	if(lib)
		daemon_register_lib(daemon,blib);

	//daemon->render_node = daemon_create_render_node(daemon,daemon_get_effect(daemon,2),-1,-1,0,"First Render Node","Default Render Node");
	daemon->render_node = daemon_create_render_node(daemon,daemon_get_effect(daemon,11),-1,-1,0,"First Render Node","Default Render Node");
	daemon_register_render_node(daemon,daemon->render_node);
	daemon_compute_render_nodes(daemon);
	daemon_connect_frame_buffer(daemon,daemon->render_node);

	/*daemon->render_node = daemon_create_render_node(daemon,daemon_get_effect(daemon,3),-1,-1,0,"Second Test Render Node","Additional Testing Render Node");
	daemon_register_render_node(daemon,daemon->render_node);
	daemon_compute_render_nodes(daemon);
	*/
	
	// Catch SIGTERM
	struct sigaction sigterm_action;
	memset(&sigterm_action, 0, sizeof(struct sigaction));
	sigterm_action.sa_handler = got_sigterm_signal;
	sigaction(SIGTERM, &sigterm_action, NULL);
	
 	return(daemon);
}

void daemon_close(struct razer_daemon *daemon)
{
	#ifdef USE_DBUS
		daemon_dbus_close(daemon);
	#endif
	list_Close(daemon->libs);
	list_Close(daemon->fx_render_nodes);
	list_Close(daemon->render_nodes);
	list_Close(daemon->effects);
 	razer_close(daemon->chroma);
 	free(daemon);
}

int daemon_update_render_nodes(struct razer_daemon *daemon)
{
	if(daemon->is_render_nodes_dirty)
		daemon_compute_render_nodes(daemon);
		//printf("daemon render_nodes to update:%d\n",daemon->render_nodes_num);
	int ret = 0;
	struct razer_fx_render_node *rn = NULL;
	for(int i = list_GetLen(daemon->render_nodes)-1;i>=0;i--)
	{
		rn = list_Get(daemon->render_nodes,i);
		ret = daemon_update_render_node(daemon,rn);
		if(!ret && rn->id != daemon->frame_buffer_linked_uid)
		{
			//TODO rewrite
			if(rn->next)
			{
				//exchange this render_node with the next one
				list_Set(daemon->render_nodes,i,rn->next);
				if(rn->move_frame_buffer_linkage_to_next)
				{
					if(rn->next->output_frame_linked_uid == -1)
						razer_free_rgb_frame(rn->next->output_frame);
					if(rn->next->input_frame_linked_uid == -1)
						razer_free_rgb_frame(rn->next->input_frame);
					if(rn->next->second_input_frame_linked_uid == -1)
						razer_free_rgb_frame(rn->next->second_input_frame);
					rn->next->input_frame = rn->input_frame;
					rn->next->second_input_frame = rn->second_input_frame;
					rn->next->input_frame_linked_uid = rn->input_frame_linked_uid;
					rn->next->second_input_frame_linked_uid = rn->second_input_frame_linked_uid;
					//if(rn->output_frame_linked_uid!= -1)
				}
				rn->next->output_frame = rn->output_frame;
				rn->next->output_frame_linked_uid = rn->output_frame_linked_uid;


				rn->next->prev = rn;
				rn->next->start_ticks = 0;
				rn->start_ticks = 0;
				rn->running = 0;

			}
		}
		//else
		//	if(!ret)
		//		printf("skipping root render node:%d at %d\n",rn->id,i);
	}
	//printf("ret:%d\n",ret);
	//if(rn && rn->next)
	//	printf("rn->next->id:%d\n",rn->next->id);
	if(rn->next && !ret)
	{
		//printf("switching root render node from %d to %d\n",rn->id,rn->next->id);
		rn->start_ticks = 0;
		rn->running = 0;
		daemon_connect_frame_buffer(daemon,rn->next);
		//root render_node effect returned 0
		//start next render_node in chain or default

	}
		//razer_clear_frame(daemon->render_node->input_frame);
		//daemon_update_render_node(daemon->render_node);
	razer_update_frame(daemon->chroma,daemon->frame_buffer);
	return(1);
}

int daemon_input_event_render_nodes(struct razer_daemon *daemon,struct razer_chroma_event *event)
{
	if(daemon->is_render_nodes_dirty)
		daemon_compute_render_nodes(daemon);
	int ret = 0;
	struct razer_fx_render_node *rn = NULL;
	for(int i = list_GetLen(daemon->render_nodes)-1;i>=0;i--)
	{
		rn = list_Get(daemon->render_nodes,i);
		ret = daemon_input_event_render_node(daemon,rn,event);
		if(!ret && rn->id != daemon->frame_buffer_linked_uid)
		{
			if(rn->next)
			{
				//exchange this render_node with the next one
				list_Set(daemon->render_nodes,i,rn->next);
				if(rn->move_frame_buffer_linkage_to_next)
				{
					if(rn->next->output_frame_linked_uid == -1)
						razer_free_rgb_frame(rn->next->output_frame);
					if(rn->next->input_frame_linked_uid == -1)
						razer_free_rgb_frame(rn->next->input_frame);
					if(rn->next->second_input_frame_linked_uid == -1)
						razer_free_rgb_frame(rn->next->second_input_frame);
					rn->next->input_frame = rn->input_frame;
					rn->next->second_input_frame = rn->second_input_frame;
					rn->next->output_frame = rn->output_frame;
					rn->next->input_frame_linked_uid = rn->input_frame_linked_uid;
					rn->next->second_input_frame_linked_uid = rn->second_input_frame_linked_uid;
					//if(rn->output_frame_linked_uid!= -1)
					rn->next->output_frame_linked_uid = rn->output_frame_linked_uid;
				}
				rn->next->prev = rn;
				rn->next->start_ticks = 0;
				rn->start_ticks = 0;
				rn->running = 0;

			}
		}
	}
	if(rn->next && !ret)
	{
		//printf("switching root render node from %d to %d\n",rn->id,rn->next->id);
		rn->start_ticks = 0;
		rn->running = 0;
		daemon_connect_frame_buffer(daemon,rn->next);
		//root render_node effect returned 0
		//start next render_node in chain or default

	}
	return(1);
}



int daemon_input_event_handler(struct razer_chroma *chroma,struct razer_chroma_event *event)
{
	#ifdef USE_VERBOSE_DEBUGGING
		if(event->type == RAZER_CHROMA_EVENT_TYPE_KEYBOARD)
			printf("daemon input event handler called (keyboard): %d,%d\n",event->values->keyboard.keycode,event->values.pressed);
		if(event->type == RAZER_CHROMA_EVENT_TYPE_MOUSE)
			printf("daemon input event handler called (mouse): %d,%d,%d\n",event->values->mouse.rel_x,event->values.mouse.rel_y,event->values.mouse.buttons_mask);
	#endif
	daemon_input_event_render_nodes((struct razer_daemon*)chroma->tag,event);
	return(1);
}


int daemon_run(struct razer_daemon *daemon)
{
    while(daemon->running)
	{
		unsigned long ticks = razer_get_ticks();
		if(!daemon->is_paused)
			daemon_update_render_nodes(daemon);
		#ifdef USE_DBUS
			daemon_dbus_handle_messages(daemon);
		#endif
		razer_update(daemon->chroma);
		razer_frame_limiter(daemon->chroma,daemon->fps);
		unsigned long end_ticks = razer_get_ticks();
		#ifdef USE_DEBUGGING
			//printf("\rframe time:%ums,actual fps:%f (Wanted:%d)",end_ticks-ticks,1000.0f/(float)(end_ticks-ticks),daemon->render_node->effect->fps);
			printf("                                                                             \rft:%ums,fps:%f(%d)",end_ticks-ticks,1000.0f/(float)(end_ticks-ticks),daemon->fps);
		#endif
		if(end_daemon)
		{
			daemon->running = 0;
			printf("Caught SIGTERM. Exiting daemon.\n");
		}
	}
	return(1);
}

void daemon_compute_append_queue(struct razer_daemon *daemon,list *queue)
{
	while(!list_IsEmpty(queue))
	{
		struct razer_fx_render_node *render_node = (struct razer_fx_render_node*)list_Dequeue(queue);
		list_Push(daemon->render_nodes,render_node);
		if(render_node->input_frame_linked_uid!=-1 && render_node->input_frame_linked_uid != 0)
			list_Queue(queue,daemon_get_render_node(daemon,render_node->input_frame_linked_uid));
		if(render_node->second_input_frame_linked_uid!=-1 && render_node->second_input_frame_linked_uid != 0)
			list_Queue(queue,daemon_get_render_node(daemon,render_node->second_input_frame_linked_uid));
	}
}

void daemon_compute_render_nodes(struct razer_daemon *daemon)
{
	//struct razer_queue *queue = daemon_create_queue();
	list *queue = list_Create(0,0);
	list_Clear(daemon->render_nodes);
	struct razer_fx_render_node *rn = daemon_get_render_node(daemon,daemon->frame_buffer_linked_uid);
	if(rn)
	{
		list_Queue(queue,rn);
		daemon_compute_append_queue(daemon,queue);
	}
	//daemon_free_queue(&queue);
	list_Close(queue);
	daemon->is_render_nodes_dirty = 0;
}



/*

void sdl_update()
{
		SDL_Event event;
	    while(SDL_PollEvent(&event))
    	{
		    if(event.type == SDL_KEYUP)
    		{
		    	if(event.key.keysym.sym == SDLK_ESCAPE)
			    	done=1;
	      	}
		    if(event.type == SDL_MOUSEBUTTONUP)
    		{
		    	//if(event.key.keysym.sym == SDLK_ESCAPE)
				int w,h;
				SDL_GetWindowSize(window,&w,&h);
				int kw=w/22;
				int kh=(h-32)/6;
		    	if(event.button.y<h-32)
		    	{
		    		int kx = (event.button.x)/kw;
		    		int ky = event.button.y/kh;
		    		//printf("button pressed in:%d,%d\n",kx,ky);
					struct razer_rgb cr = {.r=128,.g=0,.b=0};
					razer_set_key(keys,kx,ky,&cr);
		    	}
			    //	done=1;

	      	}
		    if(event.type == SDL_QUIT)
    		{
		    	done=1;
	      	}

 		}
		update_sdl(keys,renderer,window,tex);
}

*/

/*
void create_sdl_window()
{
   	SDL_Init(SDL_INIT_VIDEO);
	SDL_Window *sdl_window;
	SDL_Renderer *sdl_renderer;
	SDL_CreateWindowAndRenderer(22*32, 6*32, SDL_WINDOW_RESIZABLE, &sdl_window, &sdl_renderer);
	SDL_SetWindowTitle(sdl_window,"Razer Chroma Setup/Debug");
	SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
	SDL_RenderClear(sdl_renderer);
	SDL_RenderPresent(sdl_renderer);
	SDL_Texture *sdl_texture = SDL_CreateTexture(sdl_renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,22,6);
	load_icons(sdl_renderer,"icons",sdl_icons);
}


void close_sdl_window()
{
  	SDL_DestroyWindow(sdl_window);
    SDL_Quit();
}
*/

//list of last keystrokes
//time since hit /hitstamps

const char *dc_helpmsg = "razer_bcd\n\
\n\
Arguments:\n\
  -f, --foreground	Don't daemonize. Run in foreground\n\
  -p, --pid-file    File to write PID to\n\
\
  -h, --help        Display this help and exit\n\
  -v, --verbose     Turn on verbose output\n\
\n\
\n\
      Report bugs to <pez2001@voyagerproject.de>.\n";



struct daemon_options parse_args(int argc,char *argv[]) {

	struct daemon_options options;
	options.daemonize = 1;
	options.verbose = 0;
	options.pid_file = NULL;
	options.keyboard_input_file = NULL;
	options.mouse_input_file = NULL;

	struct option long_options[] =
	{
		// No arguments
		{"verbose", no_argument,        0, 'v'},
		{"foreground", no_argument,     0, 'f'},
		{"help", no_argument, 0, 'h'},
		// Have arguments
		{"pid-file", required_argument, 0, 'p'},
		{"keyboard-input-file", required_argument, 0, 'k'},
		{"mouse-input-file", required_argument, 0, 'm'},
		{0, 0, 0, 0}
	};

	int option_index = 0;
	char c;
	while((c=getopt_long(argc, argv, "vfhp:k:m:", long_options, &option_index)) != -1)
	{
		switch(c)
		{
			case 'p':
				if(options.pid_file == NULL)
				{
					options.pid_file = (char*)malloc(strlen(optarg) * sizeof(char));
					strcpy(options.pid_file, optarg);
				} else {
					printf("PID file has already been specified! Ignoring.\n",optopt);
				}
				break;

			case 'k':
				if(options.keyboard_input_file == NULL)
				{
					options.keyboard_input_file = (char*)malloc(strlen(optarg) * sizeof(char));
					strcpy(options.keyboard_input_file, optarg); //TODO free memory on shutdown
				} else {
					printf("keyboard input event file has already been specified! Ignoring.\n",optopt);
				}
				break;

			case 'm':
				if(options.mouse_input_file == NULL)
				{
					options.mouse_input_file = (char*)malloc(strlen(optarg) * sizeof(char));
					strcpy(options.mouse_input_file, optarg); //TODO free memory on shutdown
				} else {
					printf("mouse input event file has already been specified! Ignoring.\n",optopt);
				}
				break;

			case 'v':
				options.verbose = 1;
				break;

			case 'f':
				options.daemonize = 0;
				break;

			case 'h':
				printf(dc_helpmsg);
				exit(0);

			case '?':
				printf(dc_helpmsg); // getopt_long will print error
				exit(1);

			default:
				printf(dc_helpmsg);
				exit(1);
		}

	}

	return options;
}

void write_pid_file(char* pid_file, pid_t pid_number)
{
	FILE* fp;
	fp = fopen(pid_file, "w+");
	fprintf(fp, "%d\n", pid_number);
	fclose(fp);
}

int daemonize(char* pid_file)
{
	pid_t pid = 0;
	pid_t sid = 0;
	pid = fork();
	if(pid<0)
	{
		printf("razer_bcd: fork failed\n");
		exit(1);
	}
	if(pid)
	{
		#ifdef USE_DEBUGGING
			printf("killing razer_bcd parent process\n");
		#endif
		exit(0);
	}
	umask(0);
	sid = setsid();
	if(sid < 0)
	{
		printf("razer_bcd: setsid failed\n");
		exit(1);
	}
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	// Write PID to file
	if(pid_file != NULL)
	{
		write_pid_file(pid_file, sid);
	}

	return(1);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

int main(int argc,char *argv[])
{
	struct daemon_options options = parse_args(argc, argv);

	if(options.daemonize)
	{
		printf("Starting razer blackwidow chroma daemon as a daemon\n");
		daemonize(options.pid_file);
	} else {
		printf("Starting razer blackwidow chroma daemon in the foreground\n");
		if(options.pid_file != NULL)
		{
			write_pid_file(options.pid_file, getpid());
		}
	}

	struct razer_daemon *daemon=NULL;
	if(!(daemon=daemon_open()))
	{
		printf("razer_bcd: error initializing daemon\n");
		return(1);
	}
	if(options.mouse_input_file)
		daemon->chroma->sys_mouse_event_path = options.mouse_input_file;
	if(options.keyboard_input_file)
		daemon->chroma->sys_keyboard_event_path = options.keyboard_input_file;

	daemon_run(daemon);
	daemon_close(daemon);

	// Remove the PID file if we exit normally
	if(options.pid_file != NULL)
	{
		remove(options.pid_file);
		free(options.pid_file);
	}
}

#pragma GCC diagnostic pop

