## Simple and Incomplete C Implementation of the OpenAI Chat Completions API

A blazing-fast, memory-unsafe, probably buggy, and incomplete C client for the OpenAI Chat Completions API.
Obviously, you should use this for every important project.

This library lets you add chat entries (one modality at a time: text, b64 image, b64 audio, or b64 file), save them to a file, load them from a file, and make API calls using `libcurl` in both streaming and non-streaming modes.

If you run into any problems, create an issue, I'll try my best to help you.

I highly recommend using this with your custom arena allocator, see the example below.

---

### Build

```bash
make all
make install
# make clean
# make uninstall
```

---

### Example Code

```c
// test.c

#include <stddef.h>
#include <stdio.h>
#include "oai_cc.h"

struct str {
  char* str;
  size_t len;
};

#define _STR(_str) ((struct str){.str = _str, .len = sizeof(_str) - 1})

static const struct str content0 = _STR("how are you today?");
static const struct str content1 = _STR("I'm fine, what about you?");
static const struct str content2 = _STR("not bad :(");

void cb(char* str, size_t len) { 
    printf("%s", str); 
}

int main() {
    struct oai_cc_chat chat = {0};

    // you can use your custom allocator
    // chat._a.free = free;
    // chat._a.realloc = realloc;

    oai_cc_chat_init(&chat);

    // make sure to convert to base64 before using image, audio, or file content
    oai_cc_chat_add_entry(&chat, content0.str, content0.len, OAI_CC_USER, OAI_CC_TEXT); // or OAI_CC_IMAGE
    oai_cc_chat_add_entry(&chat, content1.str, content1.len, OAI_CC_ASSISTANT, OAI_CC_TEXT); // or OAI_CC_FILE
    oai_cc_chat_add_entry(&chat, content2.str, content2.len, OAI_CC_USER, OAI_CC_TEXT); // or OAI_CC_AUDIO

    oai_cc_chat_save(&chat, "test.json");

    oai_cc_chat_destroy(&chat);

    // Load chat from file
    oai_cc_chat_init(&chat);
    oai_cc_chat_load(&chat, "test.json");

    // Use default options
    struct oai_cc_options opt = oai_cc_create_default_options();

    // Or set custom options
    opt = (struct oai_cc_options){
        .api_key         = "...",
        .completions_url = "https://openrouter.ai/api/v1/chat/completions",
        .model_name      = "openai/gpt-oss-20b",
        .stream          = 0,
    };

    oai_cc_chat_call(&chat, &opt, cb);

    oai_cc_chat_destroy(&chat);

    return 0;
}
```

### Build Example

```bash
gcc -Wall -Ibuild/include test.c -Lbuild/lib -loai_cc `pkg-config --libs libcurl` -o test
```
