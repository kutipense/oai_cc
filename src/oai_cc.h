#ifndef __OPENAI_CHAT_COMPLETIONS__
#define __OPENAI_CHAT_COMPLETIONS__

#include <stddef.h>

struct oai_cc_alloc {
  void* (*realloc)(void*, size_t);
  void (*free)(void*);
};

struct oai_cc_options {
  char* completions_url;
  char* api_key;
  char* model_name;
  int stream;
};

#define oai_cc_create_default_options()                                                     \
  ((struct oai_cc_options){.completions_url = "https://api.openai.com/v1/chat/completions", \
                           .api_key         = "empty",                                      \
                           .model_name      = "gpt-5",                                      \
                           .stream          = 0})

enum oai_cc_role { OAI_CC_USER, OAI_CC_ASSISTANT, OAI_CC_TOOL };
enum oai_cc_content_type { OAI_CC_TEXT, OAI_CC_IMAGE, OAI_CC_AUDIO, OAI_CC_FILE };

struct oai_cc_chat {
  struct oai_cc_alloc _a;
  char* _p;
  size_t _l;
  int _b;
};

int oai_cc_chat_init(struct oai_cc_chat* chat);
void oai_cc_chat_destroy(struct oai_cc_chat* chat);

int oai_cc_chat_add_entry(struct oai_cc_chat* chat, char* content, size_t content_length, enum oai_cc_role role,
                          enum oai_cc_content_type type);

int oai_cc_chat_save(struct oai_cc_chat* chat, char* filename);
int oai_cc_chat_load(struct oai_cc_chat* chat, char* filename);

void oai_cc_chat_call(struct oai_cc_chat* chat, struct oai_cc_options* options, void (*on_data)(char*, size_t));

#endif