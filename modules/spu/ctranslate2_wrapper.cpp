/*****************************************************************************
 * ctranslate2_wrapper.cpp: CTranslate2 translation wrapper for VLC
 *****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
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
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string>
#include <vector>
#include <memory>
#include <map>

#include <vlc_common.h>

extern "C" {
#include "ctranslate2_wrapper.h"
}

// Try to include CTranslate2 if available
#ifdef __has_include
  #if __has_include(<ctranslate2/translator.h>)
    #define HAVE_CTRANSLATE2 1
    #include <ctranslate2/translator.h>
    #include <ctranslate2/models/model.h>
  #endif
#endif

#ifdef HAVE_CTRANSLATE2

struct ctranslate2_ctx {
    std::unique_ptr<ctranslate2::Translator> translator;
    std::string model_dir;
    std::map<std::string, std::string> lang_map;
};

static void init_language_map(ctranslate2_ctx_t *ctx) {
    // Map common language codes to model-specific codes
    // This handles variations like "fr" vs "fra", etc.
    ctx->lang_map["en"] = "eng";
    ctx->lang_map["fr"] = "fra";
    ctx->lang_map["es"] = "spa";
    ctx->lang_map["de"] = "deu";
    ctx->lang_map["it"] = "ita";
    ctx->lang_map["pt"] = "por";
    ctx->lang_map["ru"] = "rus";
    ctx->lang_map["zh"] = "zho";
    ctx->lang_map["ja"] = "jpn";
    ctx->lang_map["ko"] = "kor";
    ctx->lang_map["ar"] = "ara";
    ctx->lang_map["hi"] = "hin";
    ctx->lang_map["nl"] = "nld";
    ctx->lang_map["pl"] = "pol";
    ctx->lang_map["tr"] = "tur";
    ctx->lang_map["vi"] = "vie";
    ctx->lang_map["th"] = "tha";
    ctx->lang_map["he"] = "heb";
    ctx->lang_map["sv"] = "swe";
    ctx->lang_map["da"] = "dan";
    ctx->lang_map["no"] = "nor";
    ctx->lang_map["fi"] = "fin";
}

extern "C" {

translation_ctx_t *ctranslate2_init(const char *model_path, vlc_object_t *obj) {
    try {
        auto ctx = new ctranslate2_ctx();
        ctx->model_dir = model_path;
        
        // Initialize language mapping
        init_language_map(ctx);
        
        // Set compute type based on available hardware
        ctranslate2::ComputeType compute_type = ctranslate2::ComputeType::DEFAULT;
        
        // Create translator with default options
        ctranslate2::TranslatorOptions options;
        options.num_threads = 4;  // Use 4 threads for translation
        options.max_batch_size = 1;  // Process one sentence at a time for real-time
        
        ctx->translator = std::make_unique<ctranslate2::Translator>(
            model_path,
            ctranslate2::Device::CPU,
            compute_type,
            options
        );
        
        if (obj) {
            msg_Info(obj, "CTranslate2 initialized with model: %s", model_path);
        }
        
        return reinterpret_cast<translation_ctx_t*>(ctx);
    } catch (const std::exception& e) {
        if (obj) {
            msg_Err(obj, "Failed to initialize CTranslate2: %s", e.what());
        }
        return nullptr;
    }
}

char *ctranslate2_translate(translation_ctx_t *ctx, const char *text, 
                            const char *source_lang, const char *target_lang,
                            vlc_object_t *obj) {
    if (!ctx || !text || !target_lang) {
        return nullptr;
    }
    
    auto ct2_ctx = reinterpret_cast<ctranslate2_ctx_t*>(ctx);
    
    try {
        // Map language codes if needed
        std::string src = source_lang ? source_lang : "en";
        std::string tgt = target_lang;
        
        // Check if we need to map the language codes
        auto src_it = ct2_ctx->lang_map.find(src);
        if (src_it != ct2_ctx->lang_map.end()) {
            src = src_it->second;
        }
        
        auto tgt_it = ct2_ctx->lang_map.find(tgt);
        if (tgt_it != ct2_ctx->lang_map.end()) {
            tgt = tgt_it->second;
        }
        
        // Prepare input
        std::vector<std::vector<std::string>> batch_tokens;
        std::vector<std::string> tokens;
        
        // Simple tokenization - split by spaces
        // For production, you'd want proper tokenization
        std::string input_text(text);
        size_t pos = 0;
        while ((pos = input_text.find(' ')) != std::string::npos) {
            tokens.push_back(input_text.substr(0, pos));
            input_text.erase(0, pos + 1);
        }
        if (!input_text.empty()) {
            tokens.push_back(input_text);
        }
        
        batch_tokens.push_back(tokens);
        
        // Set translation options
        ctranslate2::TranslationOptions trans_opts;
        trans_opts.max_decoding_length = 256;
        trans_opts.beam_size = 2;  // Smaller beam for faster translation
        
        // Add language tokens if the model expects them
        // Many models expect source and target language tokens
        if (!src.empty() && !tgt.empty()) {
            // Prepend language tokens in the format expected by the model
            // This varies by model - adjust as needed
            batch_tokens[0].insert(batch_tokens[0].begin(), ">>" + tgt + "<<");
            batch_tokens[0].insert(batch_tokens[0].begin(), ">>" + src + "<<");
        }
        
        // Translate
        auto results = ct2_ctx->translator->translate_batch(batch_tokens, trans_opts);
        
        if (!results.empty() && !results[0].output().empty()) {
            // Join tokens back into a string
            std::string translated;
            for (const auto& token : results[0].output()) {
                if (!translated.empty()) {
                    translated += " ";
                }
                translated += token;
            }
            
            if (obj) {
                msg_Dbg(obj, "Translated '%s' from %s to %s: '%s'", 
                        text, source_lang, target_lang, translated.c_str());
            }
            
            return strdup(translated.c_str());
        }
    } catch (const std::exception& e) {
        if (obj) {
            msg_Err(obj, "Translation failed: %s", e.what());
        }
    }
    
    return nullptr;
}

void ctranslate2_cleanup(translation_ctx_t *ctx) {
    if (ctx) {
        auto ct2_ctx = reinterpret_cast<ctranslate2_ctx_t*>(ctx);
        delete ct2_ctx;
    }
}

bool ctranslate2_is_available(void) {
    return true;
}

} // extern "C"

#else // !HAVE_CTRANSLATE2

extern "C" {

translation_ctx_t *ctranslate2_init(const char *model_path, vlc_object_t *obj) {
    if (obj) {
        msg_Warn(obj, "CTranslate2 support not compiled in");
    }
    return nullptr;
}

char *ctranslate2_translate(translation_ctx_t *ctx, const char *text,
                            const char *source_lang, const char *target_lang,
                            vlc_object_t *obj) {
    if (obj) {
        msg_Warn(obj, "CTranslate2 support not compiled in");
    }
    return nullptr;
}

void ctranslate2_cleanup(translation_ctx_t *ctx) {
    (void)ctx;
}

bool ctranslate2_is_available(void) {
    return false;
}

} // extern "C"

#endif // HAVE_CTRANSLATE2