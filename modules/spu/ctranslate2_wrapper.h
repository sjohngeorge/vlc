/*****************************************************************************
 * ctranslate2_wrapper.h: CTranslate2 translation wrapper header
 *****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *****************************************************************************/

#ifndef CTRANSLATE2_WRAPPER_H
#define CTRANSLATE2_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* Forward declaration */
typedef struct vlc_object_t vlc_object_t;

/* Opaque translation context */
typedef struct translation_ctx translation_ctx_t;

/**
 * Initialize CTranslate2 with a model
 * @param model_path Path to the CTranslate2 model directory
 * @param obj VLC object for logging (can be NULL)
 * @return Translation context or NULL on error
 */
translation_ctx_t *ctranslate2_init(const char *model_path, vlc_object_t *obj);

/**
 * Translate text using CTranslate2
 * @param ctx Translation context from ctranslate2_init
 * @param text Text to translate
 * @param source_lang Source language code (e.g., "en")
 * @param target_lang Target language code (e.g., "fr")
 * @param obj VLC object for logging (can be NULL)
 * @return Translated text (must be freed by caller) or NULL on error
 */
char *ctranslate2_translate(translation_ctx_t *ctx, const char *text,
                            const char *source_lang, const char *target_lang,
                            vlc_object_t *obj);

/**
 * Clean up CTranslate2 context
 * @param ctx Translation context to clean up
 */
void ctranslate2_cleanup(translation_ctx_t *ctx);

/**
 * Check if CTranslate2 support is compiled in
 * @return true if CTranslate2 is available
 */
bool ctranslate2_is_available(void);

#ifdef __cplusplus
}
#endif

#endif /* CTRANSLATE2_WRAPPER_H */