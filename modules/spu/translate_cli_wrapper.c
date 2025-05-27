/*****************************************************************************
 * translate_cli_wrapper.c: Command-line translation wrapper for VLC
 *****************************************************************************
 * This provides translation using command-line tools like translate-shell
 * Install translate-shell: sudo apt-get install translate-shell
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Translate text using translate-shell command line tool
 * @param text Text to translate
 * @param source_lang Source language code (e.g., "en")
 * @param target_lang Target language code (e.g., "fr")
 * @param obj VLC object for logging (can be NULL)
 * @return Translated text (must be freed by caller) or NULL on error
 */
char *translate_cli(const char *text, const char *source_lang, 
                    const char *target_lang, vlc_object_t *obj)
{
    if (!text || !target_lang) {
        return NULL;
    }
    
    // Check if the languages are the same
    if (source_lang && strcmp(source_lang, target_lang) == 0) {
        return strdup(text);
    }
    
    // Escape single quotes in the text
    char *escaped_text = malloc(strlen(text) * 2 + 1);
    if (!escaped_text) {
        return NULL;
    }
    
    const char *p = text;
    char *q = escaped_text;
    while (*p) {
        if (*p == '\'') {
            *q++ = '\'';
            *q++ = '\\';
            *q++ = '\'';
            *q++ = '\'';
        } else {
            *q++ = *p;
        }
        p++;
    }
    *q = '\0';
    
    // Build command
    char command[4096];
    if (source_lang) {
        snprintf(command, sizeof(command), 
                 "trans -brief -no-ansi %s:%s '%s' 2>/dev/null", 
                 source_lang, target_lang, escaped_text);
    } else {
        snprintf(command, sizeof(command), 
                 "trans -brief -no-ansi :%s '%s' 2>/dev/null", 
                 target_lang, escaped_text);
    }
    
    free(escaped_text);
    
    if (obj) {
        msg_Dbg(obj, "Running translation command: %s", command);
    }
    
    // Execute command
    FILE *fp = popen(command, "r");
    if (!fp) {
        if (obj) {
            msg_Warn(obj, "Failed to run translate command");
        }
        return NULL;
    }
    
    // Read result
    char result[2048] = {0};
    size_t total_len = 0;
    char buffer[256];
    
    while (fgets(buffer, sizeof(buffer), fp) && total_len < sizeof(result) - 1) {
        size_t len = strlen(buffer);
        if (total_len + len < sizeof(result) - 1) {
            strcat(result, buffer);
            total_len += len;
        }
    }
    
    int status = pclose(fp);
    
    // Remove trailing newline
    size_t len = strlen(result);
    while (len > 0 && (result[len-1] == '\n' || result[len-1] == '\r')) {
        result[--len] = '\0';
    }
    
    if (status != 0 || len == 0) {
        if (obj) {
            msg_Warn(obj, "Translation command failed or returned empty result");
        }
        return NULL;
    }
    
    if (obj) {
        msg_Dbg(obj, "Translated '%s' to %s: '%s'", text, target_lang, result);
    }
    
    return strdup(result);
}

/**
 * Check if translate-shell is available
 * @return true if available
 */
bool translate_cli_is_available(void)
{
    FILE *fp = popen("which trans 2>/dev/null", "r");
    if (!fp) {
        return false;
    }
    
    char buffer[256];
    bool available = (fgets(buffer, sizeof(buffer), fp) != NULL);
    pclose(fp);
    
    return available;
}