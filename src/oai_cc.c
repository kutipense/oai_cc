#define _GNU_SOURCE
#include "oai_cc.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct oai_cc_chat oai_cc_chat;
typedef struct oai_cc_str oai_cc_str;
typedef struct oai_cc_options oai_cc_options;
typedef struct oai_cc_response oai_cc_response;

struct oai_cc_str {
  char* str;
  size_t len;
};

struct oai_cc_response {
  oai_cc_chat* chat;
  oai_cc_options* options;

  char* buffer;
  size_t length;

  void (*on_data)(char*, size_t);

  size_t _content_start;
  size_t _content_end;
};

#define OAI_CC_STR(_str) ((oai_cc_str){.str = _str, .len = sizeof(_str) - 1})
#define OAI_CC_COPY_STR(dest, dst_offset, src)             \
  memcpy(((dest) + (dst_offset)), (src)->str, (src)->len); \
  (dst_offset) += (src)->len;

// clang-format off
// static const oai_cc_str empty_string = OAI_CC_STR("");

static const oai_cc_str _messages_s   = OAI_CC_STR("{\"messages\":");
static const oai_cc_str _opt_template = OAI_CC_STR(",\"model\":\"%s\",\"stream\":\"%s\"}");

static const oai_cc_str _bracket_s = OAI_CC_STR("[\n");
static const oai_cc_str _bracket_e = OAI_CC_STR("\n]");


static const oai_cc_str _user_s      = OAI_CC_STR("{\"role\":\"user\",\"content\":[");
static const oai_cc_str _assistant_s = OAI_CC_STR("{\"role\":\"assistant\",\"content\":[");
static const oai_cc_str _tool_s      = OAI_CC_STR("{\"role\":\"tool\",\"content\":[");
static const oai_cc_str _role_e      = OAI_CC_STR("]}");

static const oai_cc_str _text_s  = OAI_CC_STR("{\"type\":\"text\",\"text\":\"");
static const oai_cc_str _text_e  = OAI_CC_STR("\"}");
static const oai_cc_str _image_s = OAI_CC_STR("{\"type\":\"image_url\",\"image_url\":{\"url\":\"");
static const oai_cc_str _image_e = OAI_CC_STR("\"}}");
static const oai_cc_str _audio_s = OAI_CC_STR("{\"type\":\"input_audio\",\"input_audio\":{\"format\":\"wav\",\"data\":\"");
static const oai_cc_str _audio_e = OAI_CC_STR("\"}}");
static const oai_cc_str _file_s  = OAI_CC_STR("{\"type\":\"file\",\"file\":{\"file_data\":\"");
static const oai_cc_str _file_e  = OAI_CC_STR("\"}}");

static const oai_cc_str _auth_template = OAI_CC_STR("Authorization: Bearer %s");
static const oai_cc_str _content_key = OAI_CC_STR("\"content\"");
static const oai_cc_str _data_key = OAI_CC_STR("data:");
static const oai_cc_str _newl_key = OAI_CC_STR("\n\n");
// clang-format on

// private

#define OAI_CC_CHAT_IS_EMPTY(chat) ((chat)->_l == _messages_s.len)

static int _oai_cc_chat_write_header(oai_cc_chat* chat) {
  if ((chat->_p = chat->_a.realloc(NULL, _messages_s.len + 1)) == NULL) return -1;
  chat->_l = _messages_s.len;
  memcpy(chat->_p, _messages_s.str, _messages_s.len + 1);
  return 0;
}

// init & destroy

int oai_cc_chat_init(oai_cc_chat* chat) {
  if (!chat->_a.realloc) chat->_a.realloc = realloc;
  if (!chat->_a.free) chat->_a.free = free;

  if (_oai_cc_chat_write_header(chat) != 0) return -1;
  chat->_b = 0;

  return 0;
}

void oai_cc_chat_destroy(oai_cc_chat* chat) {
  chat->_a.free(chat->_p);
  chat->_l         = 0;
  chat->_b         = 0;
  chat->_p         = NULL;
  chat->_a.realloc = NULL;
  chat->_a.free    = NULL;
}

// entries

int oai_cc_chat_add_entry(oai_cc_chat* chat, char* content, size_t content_length, enum oai_cc_role role,
                          enum oai_cc_content_type type) {
  const oai_cc_str* role_s    = &_user_s;
  const oai_cc_str* content_s = &_text_s;
  const oai_cc_str* content_e = &_text_e;

  // clang-format off
  switch (role) {
    case OAI_CC_USER     : role_s = &_user_s; break;
    case OAI_CC_ASSISTANT: role_s = &_assistant_s; break;
    case OAI_CC_TOOL     : role_s = &_tool_s; break;
  }

  switch (type) {
    case OAI_CC_TEXT : content_s = &_text_s; content_e = &_text_e; break;
    case OAI_CC_FILE : content_s = &_file_s; content_e = &_file_e; break;
    case OAI_CC_IMAGE: content_s = &_image_s; content_e = &_image_e; break;
    case OAI_CC_AUDIO: content_s = &_audio_s; content_e = &_audio_e; break;
  }
  // clang-format on

  size_t new_content_len = content_length * 6 + 1;  // with null
  size_t total_len       = chat->_l + role_s->len + content_s->len + new_content_len + content_e->len + _role_e.len;

  if (!OAI_CC_CHAT_IS_EMPTY(chat)) {
    total_len += 2;  // comma + new line
  } else {
    total_len += _bracket_s.len;  // or open brackets
  }

  char* ptr = chat->_a.realloc(chat->_p, total_len);
  if (!ptr) return -1;
  chat->_p = ptr;

  size_t offset = chat->_l;

  if (!OAI_CC_CHAT_IS_EMPTY(chat)) {
    chat->_p[offset++] = ',';
    chat->_p[offset++] = '\n';
  } else {
    OAI_CC_COPY_STR(chat->_p, offset, &_bracket_s);
  }

  OAI_CC_COPY_STR(chat->_p, offset, role_s);
  OAI_CC_COPY_STR(chat->_p, offset, content_s);

  char* p = chat->_p + offset;

  for (size_t i = 0; i < content_length; i++) {
    // clang-format off
    char c = content[i];
    switch (c) {
      case '\\': *p++ = '\\'; *p++ = '\\'; break;
      case '\"': *p++ = '\\'; *p++ = '\"'; break;
      case '\n': *p++ = '\\'; *p++ = 'n';  break;
      case '\t': *p++ = '\\'; *p++ = 't';  break;
      case '\r': *p++ = '\\'; *p++ = 'r';  break;
      case '\b': *p++ = '\\'; *p++ = 'b';  break;
      case '\f': *p++ = '\\'; *p++ = 'f';  break;
      default  :
        if (c < 0x20) p += sprintf(p, "\\u%04x", c);
        else *p++ = c;
    }
    // clang-format on
  }

  offset = p - chat->_p;

  OAI_CC_COPY_STR(chat->_p, offset, content_e);
  OAI_CC_COPY_STR(chat->_p, offset, &_role_e);

  chat->_p[offset++] = '\0';

  chat->_p = chat->_a.realloc(chat->_p, offset);
  chat->_l = offset - 1;
  chat->_b = 0;

  return 0;
}

size_t _oai_cc_chat_build(oai_cc_chat* chat, oai_cc_options* options, char** weak_ptr) {
  static char opt_buff[4096] = {0};

  size_t opt_buff_len =
      snprintf(opt_buff, sizeof(opt_buff), _opt_template.str, options->model_name, options->stream ? "true" : "false");

  size_t total_len = opt_buff_len + chat->_l + _bracket_e.len;

  char* ptr = chat->_a.realloc(chat->_p, total_len + 1);  // null byte
  if (!ptr) {
    *weak_ptr = NULL;
    return 0;
  }
  chat->_p = ptr;

  memcpy(chat->_p + chat->_l, _bracket_e.str, _bracket_e.len);
  memcpy(chat->_p + chat->_l + _bracket_e.len, opt_buff, opt_buff_len + 1);

  // return the whole string but don't update the internal string
  // I don't want to undo this operation because I might need to calculate the opt length
  *weak_ptr = chat->_p;

  chat->_b = 1;
  return total_len;
}

int oai_cc_chat_save(oai_cc_chat* chat, char* filename) {
  FILE* f = fopen(filename, "w");

  char* start = chat->_p + _messages_s.len;
  char *end, *new;
  size_t len = chat->_l - (start - chat->_p);

  size_t bytes_wr = 0;

  // bytes_wr += fwrite(_bracket_s.str, 1, _bracket_s.len, f); // no need

  do {
    new = memchr(start, '\n', len);
    if (new != NULL) {
      end = new;
      bytes_wr += fwrite(start, 1, end - start, f);
      bytes_wr += fwrite("\n", 1, 1, f);

      start = new + 1;
      len   = chat->_l - (start - chat->_p);
    }
  } while (new);

  bytes_wr += fwrite(start, 1, len, f);
  bytes_wr += fwrite(_bracket_e.str, 1, _bracket_e.len, f);

  fclose(f);

  return bytes_wr > 0 ? 0 : -1;
}

int oai_cc_chat_load(oai_cc_chat* chat, char* filename) {
  FILE* f = fopen(filename, "r");

  long file_size = 0;
  if (fseek(f, 0L, SEEK_END) != 0 || (file_size = ftell(f)) == -1) {
    fclose(f);
    return -1;
  }
  fseek(f, 0, SEEK_SET);

  size_t total_size = file_size + _messages_s.len - _bracket_e.len;  // don't include the ]
  char* ptr         = chat->_a.realloc(chat->_p, total_size + 1);
  if (!ptr) {
    fclose(f);
    return -1;
  }
  chat->_l = total_size;
  chat->_p = ptr;

  size_t offset = 0;
  OAI_CC_COPY_STR(chat->_p, offset, &_messages_s);

  size_t read_target = chat->_l - offset;
  int bytes_read     = fread(chat->_p + offset, 1, read_target, f);
  fclose(f);

  return bytes_read == read_target ? 0 : -1;
}

size_t _oai_cc_write_cb_streaming(const void* ptr, size_t size, size_t nmem, void* userdata) {
  size_t real_payload_size = nmem * size;  // size is always 1 tho
  oai_cc_response* mem     = (oai_cc_response*)userdata;

  void* _ptr = mem->chat->_a.realloc(mem->buffer, mem->length + real_payload_size + 1);  // + null
  if (_ptr == NULL) return 0;
  mem->buffer = _ptr;

  memcpy(mem->buffer + mem->length, ptr, real_payload_size);
  mem->length += real_payload_size;
  mem->buffer[mem->length] = '\0';

  char* _buff_end  = mem->buffer + mem->length;
  char *data_match = NULL, *newline_match = NULL;

  char* mem_it = mem->_content_start ? mem->buffer + mem->_content_start : mem->buffer;
  while (mem_it < _buff_end) {
    // find "data:"
    data_match = memmem(mem_it, _buff_end - mem_it, _data_key.str, _data_key.len);
    if (!data_match) break;

    // find "\n\n" after "data:"
    newline_match = memmem(data_match, _buff_end - data_match, _newl_key.str, _newl_key.len);
    if (!newline_match) break;

    char* _start = data_match + _data_key.len;
    char* _end   = newline_match;
    size_t len   = _end - _start;
    mem_it       = newline_match + _newl_key.len;

    char *content_start = NULL, *content_end = NULL;

    // content start
    char* it = memmem(_start, len, _content_key.str, _content_key.len);
    if (!it || (it += _content_key.len) >= _end) continue;  // can't parse

    it = memchr(it, '"', _end - it);
    if (!it || ++it >= _end) continue;  // can't parse

    content_start = it;

    // content end
    while ((it = memchr(it, '"', _end - it)) != NULL) {
      if (*(it - 1) != '\\') {
        content_end = it;
        break;
      };
      it++;
    }

    if (content_start && content_end && content_end > content_start) {  // good case
      size_t len         = content_end - content_start;
      content_start[len] = '\0';
      mem->on_data(content_start, len);
    }
  }

  if (!data_match || mem_it >= _buff_end) {  // clear memory
    mem->length         = 0;
    mem->buffer[0]      = '\0';
    mem->_content_start = 0;
  } else if (!newline_match) {
    mem->_content_start = (size_t)(data_match - mem->buffer);
  }

  return real_payload_size;
}

size_t _oai_cc_write_cb(const void* ptr, size_t size, size_t nmem, void* userdata) {
  oai_cc_response* mem     = (oai_cc_response*)userdata;
  size_t real_payload_size = nmem * size;  // size is always 1 tho

  // already returned the data, drop the rest
  if (mem->_content_start && mem->_content_end) return real_payload_size;

  void* _ptr = mem->chat->_a.realloc(mem->buffer, mem->length + real_payload_size + 1);  // + null
  if (_ptr == NULL) return 0;

  mem->buffer = _ptr;
  memcpy(mem->buffer + mem->length, ptr, real_payload_size);
  mem->length += real_payload_size;
  mem->buffer[mem->length] = '\0';

  char* _end = mem->buffer + mem->length;

  // find the content start
  if (!mem->_content_start) {
    char* it = memmem(mem->buffer, mem->length, _content_key.str, _content_key.len);
    if (!it || (it += _content_key.len) >= _end) return real_payload_size;

    it = memchr(it, '"', _end - it);
    if (!it || ++it >= _end) return real_payload_size;

    mem->_content_start = (size_t)(it - mem->buffer);
  }

  // find the content end
  // not sure about the chunk size, you may not find the closing quotes in one go (?)
  if (!mem->_content_end) {
    char* it = mem->buffer + mem->_content_start;
    while ((it = memchr(it, '"', _end - it)) != NULL) {
      if (*(it - 1) != '\\') {
        mem->_content_end = (size_t)(it - mem->buffer);
        break;
      };
      it++;
    }

    if (!mem->_content_end) return real_payload_size;
  }

  if (mem->_content_start && mem->_content_end) {
    char* start = mem->buffer + mem->_content_start;
    size_t len  = mem->_content_end - mem->_content_start;
    start[len]  = '\0';
    mem->on_data(start, len);
  }

  return real_payload_size;
}

void oai_cc_chat_call(oai_cc_chat* chat, oai_cc_options* options, void (*on_data)(char*, size_t)) {
  static char auth_header[256];

  char* json_body;
  if (!chat->_b) {
    _oai_cc_chat_build(chat, options, &json_body);
  } else {
    json_body = chat->_p;
  }

  CURL* curl = curl_easy_init();
  if (!curl) return;

  struct curl_slist* headers = NULL;
  snprintf(auth_header, sizeof(auth_header), _auth_template.str, options->api_key);

  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, auth_header);

  oai_cc_response response_memory = {
      .chat           = chat,
      .options        = options,
      .buffer         = NULL,
      .length         = 0,
      .on_data        = on_data,
      ._content_start = 0,
      ._content_end   = 0,
  };

  curl_easy_setopt(curl, CURLOPT_URL, options->completions_url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, options->stream ? _oai_cc_write_cb_streaming : _oai_cc_write_cb);

  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_memory);

  CURLcode res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    // maybe log
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  chat->_a.free(response_memory.buffer);
}