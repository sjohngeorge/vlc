/*****************************************************************************
 * livetranslate.c : live translation video plugin for vlc
 *****************************************************************************
 * Copyright (C) 2025 VLC authors and VideoLAN
 *
 * Authors: Live Translation Plugin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_block.h>
#include <vlc_fs.h>
#include <vlc_strings.h>
#include <vlc_subpicture.h>
#include <vlc_aout.h>

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  CreateFilter ( filter_t * );
static void DestroyFilter( filter_t * );
static subpicture_t *Filter( filter_t *, vlc_tick_t );

static int LiveTranslateCallback( vlc_object_t *p_this, char const *psz_var,
                            vlc_value_t oldval, vlc_value_t newval,
                            void *p_data );

static const int pi_color_values[] = {
               0xf0000000, 0x00000000, 0x00808080, 0x00C0C0C0,
               0x00FFFFFF, 0x00800000, 0x00FF0000, 0x00FF00FF, 0x00FFFF00,
               0x00808000, 0x00008000, 0x00008080, 0x0000FF00, 0x00800080,
               0x00000080, 0x000000FF, 0x0000FFFF};
static const char *const ppsz_color_descriptions[] = {
               "Default", "Black", "Gray",
               "Silver", "White", "Maroon", "Red",
               "Fuchsia", "Yellow", "Olive", "Green",
               "Teal", "Lime", "Purple", "Navy", "Blue",
               "Aqua" };

/*****************************************************************************
 * filter_sys_t: live translate filter descriptor
 *****************************************************************************/
typedef struct
{
    vlc_mutex_t lock;

    int i_xoff, i_yoff; /* positioning offsets */
    int i_pos; /* positioning: absolute, or relative location */

    vlc_tick_t i_timeout;
    vlc_tick_t i_refresh;

    char *source_lang; /**< source language code */
    char *target_lang; /**< target language code */
    char *whisper_model; /**< whisper model size */
    char *current_text; /**< current translation text */
    bool enabled; /**< whether translation is enabled */

    text_style_t *p_style; /* font control */

    vlc_tick_t last_time;
    
    /* Audio processing */
    pthread_t audio_thread;
    pthread_mutex_t audio_mutex;
    pthread_cond_t audio_cond;
    bool audio_thread_running;
    
    /* Python process for translation */
    pid_t python_pid;
    int pipe_fd[2]; /* pipe for communication with Python process */
    
} filter_sys_t;

#define ENABLED_TEXT N_("Enable live translation")
#define ENABLED_LONGTEXT N_("Enable or disable live translation overlay.")

#define SOURCE_LANG_TEXT N_("Source language")
#define SOURCE_LANG_LONGTEXT N_("Source language code (e.g., 'ja' for Japanese, 'en' for English)")

#define TARGET_LANG_TEXT N_("Target language") 
#define TARGET_LANG_LONGTEXT N_("Target language code (e.g., 'en' for English, 'es' for Spanish)")

#define WHISPER_MODEL_TEXT N_("Whisper model")
#define WHISPER_MODEL_LONGTEXT N_("Whisper model size (tiny, base, small, medium, large)")

#define TIMEOUT_TEXT N_("Timeout")
#define TIMEOUT_LONGTEXT N_("Number of milliseconds the translation must remain " \
                            "displayed. Default value is " \
                            "5000 (5 seconds).")
#define REFRESH_TEXT N_("Refresh period in ms")
#define REFRESH_LONGTEXT N_("Number of milliseconds between translation updates. " \
                            "Default is 1000ms.")

#define SIZE_TEXT N_("Font size, pixels")
#define SIZE_LONGTEXT N_("Font size, in pixels. Default is 0 (use default " \
    "font size)." )

#define COLOR_TEXT N_("Color")
#define COLOR_LONGTEXT N_("Color of the text that will be rendered on "\
    "the video. This must be an hexadecimal (like HTML colors). The first two "\
    "chars are for red, then green, then blue. #000000 = black, #FF0000 = red,"\
    " #00FF00 = green, #FFFF00 = yellow (red + green), #FFFFFF = white" )

#define CFG_PREFIX "livetranslate-"

#define LIVETRANSLATE_HELP N_("Display live translation overlay on video")

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin ()
    set_shortname( N_("Live Translate" ))
    set_description( N_("Live translation overlay") )
    set_help(LIVETRANSLATE_HELP)
    set_callback_sub_source( CreateFilter, 0 )
    set_subcategory( SUBCAT_VIDEO_SUBPIC )
    
    add_bool( CFG_PREFIX "enabled", false, ENABLED_TEXT, ENABLED_LONGTEXT )
    add_string( CFG_PREFIX "source-lang", "ja", SOURCE_LANG_TEXT, SOURCE_LANG_LONGTEXT )
    add_string( CFG_PREFIX "target-lang", "en", TARGET_LANG_TEXT, TARGET_LANG_LONGTEXT )
    add_string( CFG_PREFIX "whisper-model", "base", WHISPER_MODEL_TEXT, WHISPER_MODEL_LONGTEXT )

    set_section( N_("Position"), NULL )
    add_integer( CFG_PREFIX "x", 0, POSX_TEXT, POSX_LONGTEXT )
    add_integer( CFG_PREFIX "y", 0, POSY_TEXT, POSY_LONGTEXT )
    add_integer( CFG_PREFIX "position", 8, POS_TEXT, POS_LONGTEXT ) /* bottom by default */
        change_integer_list( pi_pos_values, ppsz_pos_descriptions )

    set_section( N_("Font"), NULL )
    add_integer_with_range( CFG_PREFIX "opacity", 255, 0, 255,
        OPACITY_TEXT, OPACITY_LONGTEXT )
    add_rgb(CFG_PREFIX "color", 0xFFFFFF, COLOR_TEXT, COLOR_LONGTEXT)
        change_integer_list( pi_color_values, ppsz_color_descriptions )
    add_integer( CFG_PREFIX "size", 0, SIZE_TEXT, SIZE_LONGTEXT )
        change_integer_range( 0, 4096)

    set_section( N_("Misc"), NULL )
    add_integer( CFG_PREFIX "timeout", 5000, TIMEOUT_TEXT, TIMEOUT_LONGTEXT )
    add_integer( CFG_PREFIX "refresh", 1000, REFRESH_TEXT, REFRESH_LONGTEXT )

    add_shortcut( "livetranslate" )
vlc_module_end ()

static const char *const ppsz_filter_options[] = {
    "enabled", "source-lang", "target-lang", "whisper-model", 
    "x", "y", "position", "color", "size", "timeout", "refresh", "opacity",
    NULL
};

static const struct vlc_filter_operations filter_ops = {
    .source_sub = Filter, .close = DestroyFilter,
};

/*****************************************************************************
 * Audio processing thread
 *****************************************************************************/
static void* AudioProcessingThread(void* arg)
{
    filter_t *p_filter = (filter_t*)arg;
    filter_sys_t *p_sys = p_filter->p_sys;
    
    /* This thread would capture audio and send it to Python process */
    /* For now, this is a placeholder - actual implementation would need */
    /* to hook into VLC's audio pipeline */
    
    while (p_sys->audio_thread_running) {
        pthread_mutex_lock(&p_sys->audio_mutex);
        pthread_cond_wait(&p_sys->audio_cond, &p_sys->audio_mutex);
        pthread_mutex_unlock(&p_sys->audio_mutex);
        
        if (!p_sys->audio_thread_running)
            break;
            
        /* Process audio here */
        usleep(100000); /* 100ms */
    }
    
    return NULL;
}

/*****************************************************************************
 * Start Python translation process
 *****************************************************************************/
static int StartPythonProcess(filter_t *p_filter)
{
    filter_sys_t *p_sys = p_filter->p_sys;
    
    if (pipe(p_sys->pipe_fd) == -1) {
        msg_Err(p_filter, "Failed to create pipe: %s", vlc_strerror_c(errno));
        return VLC_EGENERIC;
    }
    
    p_sys->python_pid = fork();
    if (p_sys->python_pid == -1) {
        msg_Err(p_filter, "Failed to fork Python process: %s", vlc_strerror_c(errno));
        close(p_sys->pipe_fd[0]);
        close(p_sys->pipe_fd[1]);
        return VLC_EGENERIC;
    }
    
    if (p_sys->python_pid == 0) {
        /* Child process - run Python script */
        close(p_sys->pipe_fd[0]); /* Close read end */
        
        /* Redirect stdout to pipe */
        dup2(p_sys->pipe_fd[1], STDOUT_FILENO);
        close(p_sys->pipe_fd[1]);
        
        /* Execute Python script with parameters */
        char script_path[256];
        snprintf(script_path, sizeof(script_path), "./vlc_live_translate.py");
        
        char source_arg[64], target_arg[64], model_arg[64];
        snprintf(source_arg, sizeof(source_arg), "--source-lang=%s", 
                 p_sys->source_lang ? p_sys->source_lang : "ja");
        snprintf(target_arg, sizeof(target_arg), "--target-lang=%s", 
                 p_sys->target_lang ? p_sys->target_lang : "en");
        snprintf(model_arg, sizeof(model_arg), "--whisper-model=%s", 
                 p_sys->whisper_model ? p_sys->whisper_model : "base");
        
        char *args[] = {
            "python3",
            script_path,
            source_arg,
            target_arg,
            model_arg,
            NULL
        };
        
        execvp("python3", args);
        _exit(1); /* If execvp fails */
    }
    
    /* Parent process */
    close(p_sys->pipe_fd[1]); /* Close write end */
    
    msg_Dbg(p_filter, "Started Python translation process with PID %d", p_sys->python_pid);
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Stop Python translation process
 *****************************************************************************/
static void StopPythonProcess(filter_t *p_filter)
{
    filter_sys_t *p_sys = p_filter->p_sys;
    
    if (p_sys->python_pid > 0) {
        kill(p_sys->python_pid, SIGTERM);
        waitpid(p_sys->python_pid, NULL, 0);
        p_sys->python_pid = 0;
    }
    
    if (p_sys->pipe_fd[0] >= 0) {
        close(p_sys->pipe_fd[0]);
        p_sys->pipe_fd[0] = -1;
    }
}

/*****************************************************************************
 * Read translation from Python process
 *****************************************************************************/
static char* ReadTranslation(filter_t *p_filter)
{
    filter_sys_t *p_sys = p_filter->p_sys;
    static char buffer[1024];
    ssize_t bytes_read;
    
    if (p_sys->pipe_fd[0] < 0)
        return NULL;
        
    /* Non-blocking read */
    fd_set readfds;
    struct timeval timeout = {0, 0}; /* No timeout - immediate return */
    
    FD_ZERO(&readfds);
    FD_SET(p_sys->pipe_fd[0], &readfds);
    
    int ready = select(p_sys->pipe_fd[0] + 1, &readfds, NULL, NULL, &timeout);
    if (ready <= 0)
        return NULL;
        
    bytes_read = read(p_sys->pipe_fd[0], buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        /* Remove newline if present */
        char *newline = strchr(buffer, '\n');
        if (newline)
            *newline = '\0';
        return strdup(buffer);
    }
    
    return NULL;
}

/*****************************************************************************
 * CreateFilter: allocates live translate video filter
 *****************************************************************************/
static int CreateFilter( filter_t *p_filter )
{
    filter_sys_t *p_sys;

    /* Allocate structure */
    p_sys = p_filter->p_sys = malloc( sizeof( filter_sys_t ) );
    if( p_sys == NULL )
        return VLC_ENOMEM;

    p_sys->p_style = text_style_Create( STYLE_NO_DEFAULTS );
    if(unlikely(!p_sys->p_style))
    {
        free(p_sys);
        return VLC_ENOMEM;
    }
    vlc_mutex_init( &p_sys->lock );

    config_ChainParse( p_filter, CFG_PREFIX, ppsz_filter_options,
                       p_filter->p_cfg );

#define CREATE_VAR( stor, type, var ) \
    p_sys->stor = var_CreateGet##type##Command( p_filter, var ); \
    var_AddCallback( p_filter, var, LiveTranslateCallback, p_sys );

    CREATE_VAR( enabled, Bool, "livetranslate-enabled" );
    CREATE_VAR( source_lang, String, "livetranslate-source-lang" );
    CREATE_VAR( target_lang, String, "livetranslate-target-lang" );
    CREATE_VAR( whisper_model, String, "livetranslate-whisper-model" );
    CREATE_VAR( i_xoff, Integer, "livetranslate-x" );
    CREATE_VAR( i_yoff, Integer, "livetranslate-y" );
    p_sys->i_timeout = VLC_TICK_FROM_MS(var_CreateGetIntegerCommand( p_filter,
                                                              "livetranslate-timeout" ));
    var_AddCallback( p_filter, "livetranslate-timeout", LiveTranslateCallback, p_sys );
    p_sys->i_refresh = VLC_TICK_FROM_MS(var_CreateGetIntegerCommand( p_filter,
                                                              "livetranslate-refresh" ));
    var_AddCallback( p_filter, "livetranslate-refresh", LiveTranslateCallback, p_sys );
    CREATE_VAR( i_pos, Integer, "livetranslate-position" );
    p_sys->p_style->i_font_alpha = var_CreateGetIntegerCommand( p_filter,
                                                            "livetranslate-opacity" );
    var_AddCallback( p_filter, "livetranslate-opacity", LiveTranslateCallback, p_sys );
    p_sys->p_style->i_features |= STYLE_HAS_FONT_ALPHA;
    p_sys->p_style->i_font_color = var_CreateGetIntegerCommand( p_filter, "livetranslate-color" );
    var_AddCallback( p_filter, "livetranslate-color", LiveTranslateCallback, p_sys );
    p_sys->p_style->i_features |= STYLE_HAS_FONT_COLOR;
    p_sys->p_style->i_font_size = var_CreateGetIntegerCommand( p_filter, "livetranslate-size" );
    var_AddCallback( p_filter, "livetranslate-size", LiveTranslateCallback, p_sys );

    /* Initialize */
    p_sys->current_text = NULL;
    p_sys->last_time = 0;
    p_sys->python_pid = 0;
    p_sys->pipe_fd[0] = p_sys->pipe_fd[1] = -1;
    
    /* Initialize audio processing */
    p_sys->audio_thread_running = false;
    pthread_mutex_init(&p_sys->audio_mutex, NULL);
    pthread_cond_init(&p_sys->audio_cond, NULL);

    p_filter->ops = &filter_ops;

    /* Start Python process if enabled */
    if (p_sys->enabled) {
        if (StartPythonProcess(p_filter) != VLC_SUCCESS) {
            msg_Warn(p_filter, "Failed to start Python translation process");
        }
    }

    msg_Dbg(p_filter, "Live translate filter created (enabled: %s, %s->%s)", 
            p_sys->enabled ? "yes" : "no", 
            p_sys->source_lang ? p_sys->source_lang : "unknown",
            p_sys->target_lang ? p_sys->target_lang : "unknown");

    return VLC_SUCCESS;
}

/*****************************************************************************
 * DestroyFilter: destroy live translate video filter
 *****************************************************************************/
static void DestroyFilter( filter_t *p_filter )
{
    filter_sys_t *p_sys = p_filter->p_sys;

    /* Stop audio thread */
    if (p_sys->audio_thread_running) {
        p_sys->audio_thread_running = false;
        pthread_cond_signal(&p_sys->audio_cond);
        pthread_join(p_sys->audio_thread, NULL);
    }
    
    /* Stop Python process */
    StopPythonProcess(p_filter);

    /* Delete the variables */
#define DEL_VAR(var) \
    var_DelCallback( p_filter, var, LiveTranslateCallback, p_sys ); \
    var_Destroy( p_filter, var );
    DEL_VAR( "livetranslate-enabled" );
    DEL_VAR( "livetranslate-source-lang" );
    DEL_VAR( "livetranslate-target-lang" );
    DEL_VAR( "livetranslate-whisper-model" );
    DEL_VAR( "livetranslate-x" );
    DEL_VAR( "livetranslate-y" );
    DEL_VAR( "livetranslate-timeout" );
    DEL_VAR( "livetranslate-refresh" );
    DEL_VAR( "livetranslate-position" );
    DEL_VAR( "livetranslate-opacity" );
    DEL_VAR( "livetranslate-color" );
    DEL_VAR( "livetranslate-size" );

    text_style_Delete( p_sys->p_style );
    free( p_sys->source_lang );
    free( p_sys->target_lang );
    free( p_sys->whisper_model );
    free( p_sys->current_text );
    
    pthread_mutex_destroy(&p_sys->audio_mutex);
    pthread_cond_destroy(&p_sys->audio_cond);
    
    free( p_sys );
}

/****************************************************************************
 * Filter: the whole thing
 ****************************************************************************
 * This function outputs subpictures at regular time intervals.
 ****************************************************************************/
static subpicture_t *Filter( filter_t *p_filter, vlc_tick_t date )
{
    filter_sys_t *p_sys = p_filter->p_sys;
    subpicture_t *p_spu = NULL;

    vlc_mutex_lock( &p_sys->lock );
    
    if (!p_sys->enabled)
        goto out;
        
    if( p_sys->last_time + p_sys->i_refresh > date )
        goto out;

    /* Try to read new translation from Python process */
    char *new_translation = ReadTranslation(p_filter);
    if (new_translation != NULL) {
        free(p_sys->current_text);
        p_sys->current_text = new_translation;
        msg_Dbg(p_filter, "New translation: %s", p_sys->current_text);
    }

    /* Only create subpicture if we have text to display */
    if (p_sys->current_text == NULL || strlen(p_sys->current_text) == 0)
        goto out;

    p_spu = filter_NewSubpicture( p_filter );
    if( !p_spu )
        goto out;

    subpicture_region_t *p_region = subpicture_region_NewText();
    if( !p_region )
    {
        subpicture_Delete( p_spu );
        p_spu = NULL;
        goto out;
    }
    vlc_spu_regions_push( &p_spu->regions, p_region );
    p_region->fmt.i_sar_den = p_region->fmt.i_sar_num = 1;

    p_sys->last_time = date;

    p_region->p_text = text_segment_New( p_sys->current_text );
    p_spu->i_start = date;
    p_spu->i_stop  = p_sys->i_timeout == 0 ? VLC_TICK_INVALID : date + p_sys->i_timeout;
    p_spu->b_ephemer = true;

    /*  where to locate the string: */
    if( p_sys->i_pos < 0 )
    {   /*  set to an absolute xy */
        p_region->i_align = SUBPICTURE_ALIGN_LEFT | SUBPICTURE_ALIGN_TOP;
        p_region->b_absolute = true; p_region->b_in_window = false;
    }
    else
    {   /* set to one of the 9 relative locations */
        p_region->i_align = p_sys->i_pos;
        p_region->b_absolute = false; p_region->b_in_window = false;
    }

    p_region->i_x = p_sys->i_xoff;
    p_region->i_y = p_sys->i_yoff;

    p_region->p_text->style = text_style_Duplicate( p_sys->p_style );

out:
    vlc_mutex_unlock( &p_sys->lock );
    return p_spu;
}

/**********************************************************************
 * Callback to update params on the fly
 **********************************************************************/
static int LiveTranslateCallback( vlc_object_t *p_this, char const *psz_var,
                            vlc_value_t oldval, vlc_value_t newval,
                            void *p_data )
{
    filter_sys_t *p_sys = p_data;
    filter_t *p_filter = (filter_t*)p_this;

    VLC_UNUSED(oldval);

    vlc_mutex_lock( &p_sys->lock );
    
    if( !strcmp( psz_var, "livetranslate-enabled" ) )
    {
        p_sys->enabled = newval.b_bool;
        if (p_sys->enabled) {
            if (p_sys->python_pid == 0) {
                StartPythonProcess(p_filter);
            }
        } else {
            StopPythonProcess(p_filter);
        }
    }
    else if ( !strcmp( psz_var, "livetranslate-source-lang" ) )
    {
        free( p_sys->source_lang );
        p_sys->source_lang = strdup( newval.psz_string );
    }
    else if ( !strcmp( psz_var, "livetranslate-target-lang" ) )
    {
        free( p_sys->target_lang );
        p_sys->target_lang = strdup( newval.psz_string );
    }
    else if ( !strcmp( psz_var, "livetranslate-whisper-model" ) )
    {
        free( p_sys->whisper_model );
        p_sys->whisper_model = strdup( newval.psz_string );
    }
    else if ( !strcmp( psz_var, "livetranslate-x" ) )
    {
        p_sys->i_xoff = newval.i_int;
    }
    else if ( !strcmp( psz_var, "livetranslate-y" ) )
    {
        p_sys->i_yoff = newval.i_int;
    }
    else if ( !strcmp( psz_var, "livetranslate-color" ) )
    {
        p_sys->p_style->i_font_color = newval.i_int;
    }
    else if ( !strcmp( psz_var, "livetranslate-opacity" ) )
    {
        p_sys->p_style->i_font_alpha = newval.i_int;
    }
    else if ( !strcmp( psz_var, "livetranslate-size" ) )
    {
        p_sys->p_style->i_font_size = newval.i_int;
    }
    else if ( !strcmp( psz_var, "livetranslate-timeout" ) )
    {
        p_sys->i_timeout = VLC_TICK_FROM_MS(newval.i_int);
    }
    else if ( !strcmp( psz_var, "livetranslate-refresh" ) )
    {
        p_sys->i_refresh = VLC_TICK_FROM_MS(newval.i_int);
    }
    else if ( !strcmp( psz_var, "livetranslate-position" ) )
    {
        p_sys->i_pos = newval.i_int;
    }

    /* Clear current text to force refresh */
    free( p_sys->current_text );
    p_sys->current_text = NULL;

    vlc_mutex_unlock( &p_sys->lock );
    return VLC_SUCCESS;
}
