#include "jsmn.h"

static jsmntok_t* jsmn_alloc_token(jsmn_parser* parser, jsmntok_t* tokens, size_t num_tokens) {
    if (parser->toknext >= num_tokens) {
        return NULL;
    }
    jsmntok_t* tok = &tokens[parser->toknext++];
    tok->start = -1;
    tok->end = -1;
    tok->size = 0;
#ifdef JSMN_PARENT_LINKS
    tok->parent = -1;
#endif
    tok->type = JSMN_UNDEFINED;
    return tok;
}

static void jsmn_fill_token(jsmntok_t* token, jsmntype_t type, int start, int end) {
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser* parser, const char* js, size_t len, jsmntok_t* tokens, size_t num_tokens) {
    int start = (int)parser->pos;

    for (; parser->pos < len; parser->pos++) {
        switch (js[parser->pos]) {
            case ':':
            case '\t':
            case '\r':
            case '\n':
            case ' ':
            case ',':
            case ']':
            case '}':
                goto found;
            default:
                if ((unsigned char)js[parser->pos] < 32 || (unsigned char)js[parser->pos] >= 127) {
                    parser->pos = (unsigned int)start;
                    return JSMN_ERROR_INVAL;
                }
        }
    }

found:
    if (tokens == NULL) {
        parser->pos--;
        return 0;
    }

    jsmntok_t* token = jsmn_alloc_token(parser, tokens, num_tokens);
    if (token == NULL) {
        parser->pos = (unsigned int)start;
        return JSMN_ERROR_NOMEM;
    }
    jsmn_fill_token(token, JSMN_PRIMITIVE, start, (int)parser->pos);
#ifdef JSMN_PARENT_LINKS
    token->parent = parser->toksuper;
#endif
    parser->pos--;
    return 0;
}

static int jsmn_parse_string(jsmn_parser* parser, const char* js, size_t len, jsmntok_t* tokens, size_t num_tokens) {
    int start = (int)parser->pos;

    parser->pos++;

    for (; parser->pos < len; parser->pos++) {
        char c = js[parser->pos];

        if (c == '"') {
            if (tokens == NULL) {
                return 0;
            }
            jsmntok_t* token = jsmn_alloc_token(parser, tokens, num_tokens);
            if (token == NULL) {
                parser->pos = (unsigned int)start;
                return JSMN_ERROR_NOMEM;
            }
            jsmn_fill_token(token, JSMN_STRING, start + 1, (int)parser->pos);
#ifdef JSMN_PARENT_LINKS
            token->parent = parser->toksuper;
#endif
            return 0;
        }

        if (c == '\\' && parser->pos + 1 < len) {
            parser->pos++;
            switch (js[parser->pos]) {
                case '"':
                case '/':
                case '\\':
                case 'b':
                case 'f':
                case 'r':
                case 'n':
                case 't':
                    break;
                case 'u':
                    for (int i = 0; i < 4; i++) {
                        parser->pos++;
                        if (parser->pos >= len) {
                            parser->pos = (unsigned int)start;
                            return JSMN_ERROR_PART;
                        }
                        char uc = js[parser->pos];
                        if (!((uc >= '0' && uc <= '9') || (uc >= 'A' && uc <= 'F') || (uc >= 'a' && uc <= 'f'))) {
                            parser->pos = (unsigned int)start;
                            return JSMN_ERROR_INVAL;
                        }
                    }
                    break;
                default:
                    parser->pos = (unsigned int)start;
                    return JSMN_ERROR_INVAL;
            }
        }
    }

    parser->pos = (unsigned int)start;
    return JSMN_ERROR_PART;
}

void jsmn_init(jsmn_parser* parser) {
    parser->pos = 0;
    parser->toknext = 0;
    parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser* parser, const char* js, size_t len, jsmntok_t* tokens, unsigned int num_tokens) {
    int r;
    int i;
    jsmntok_t* token;

    for (; parser->pos < len; parser->pos++) {
        char c = js[parser->pos];
        switch (c) {
            case '{':
            case '[':
                if (tokens == NULL) break;
                token = jsmn_alloc_token(parser, tokens, num_tokens);
                if (token == NULL) return JSMN_ERROR_NOMEM;
                if (parser->toksuper != -1) {
                    tokens[parser->toksuper].size++;
#ifdef JSMN_PARENT_LINKS
                    token->parent = parser->toksuper;
#endif
                }
                token->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
                token->start = (int)parser->pos;
                parser->toksuper = (int)(parser->toknext - 1);
                break;

            case '}':
            case ']':
                if (tokens == NULL) break;
                for (i = (int)parser->toknext - 1; i >= 0; i--) {
                    token = &tokens[i];
                    if (token->start != -1 && token->end == -1) {
                        if ((token->type == JSMN_OBJECT && c == ']') || (token->type == JSMN_ARRAY && c == '}')) {
                            return JSMN_ERROR_INVAL;
                        }
                        token->end = (int)parser->pos + 1;
#ifdef JSMN_PARENT_LINKS
                        parser->toksuper = token->parent;
#else
                        parser->toksuper = -1;
                        for (int j = i - 1; j >= 0; j--) {
                            if (tokens[j].start != -1 && tokens[j].end == -1) {
                                parser->toksuper = j;
                                break;
                            }
                        }
#endif
                        break;
                    }
                }
                if (i == -1) return JSMN_ERROR_INVAL;
                break;

            case '"':
                r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
                if (r < 0) return r;
                if (parser->toksuper != -1 && tokens != NULL) {
                    tokens[parser->toksuper].size++;
                }
                break;

            case '\t':
            case '\r':
            case '\n':
            case ' ':
            case ':':
            case ',':
                break;

            default:
                r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
                if (r < 0) return r;
                if (tokens != NULL && parser->toksuper != -1) {
                    tokens[parser->toksuper].size++;
                }
                break;
        }
    }

    if (tokens != NULL) {
        for (i = (int)parser->toknext - 1; i >= 0; i--) {
            if (tokens[i].start != -1 && tokens[i].end == -1) {
                return JSMN_ERROR_PART;
            }
        }
    }

    return (int)parser->toknext;
}
