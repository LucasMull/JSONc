#include "../JSON.h"
#include "global_share.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/* create and branch json item to current's and return it's address */
static json_item_st*
json_item_property_create(json_item_st *item)
{
  ++item->num_branch;
  item->branch = realloc(item->branch, (item->num_branch)*(sizeof *item));
  assert(item->branch);

  item->branch[item->num_branch-1] = calloc(1, sizeof *item);
  assert(item->branch[item->num_branch-1]);

  item->branch[item->num_branch-1]->parent = item;

  return item->branch[item->num_branch-1];
}

/* Destroy current item and all of its nested object/arrays */
void
json_item_destroy(json_item_st *item)
{
  for (size_t i=0; i < item->num_branch; ++i){
    json_item_destroy(item->branch[i]);
  }
  free(item->branch);

  if (JSON_STRING == json_item_get_type(item)){
    free(item->string);
  }
  free(item);
}

/* clean up json item and global allocated keys */
void json_item_cleanup(json_item_st *item)
{
  json_item_st *root = json_item_get_root(item);
  json_item_destroy(root);

  if (0 < g_keycache.cache_size){
    do {
      --g_keycache.cache_size;
      free(*g_keycache.list_keyaddr[g_keycache.cache_size]);
      free(g_keycache.list_keyaddr[g_keycache.cache_size]);
    } while (0 != g_keycache.cache_size);

    free(g_keycache.list_keyaddr);
  }
}

/* stores new exclusive key to global keylist variable
  and return its storage address */
static json_string_kt*
json_cache_key(const json_string_kt kCache_entry)
{
  ++g_keycache.cache_size;

  g_keycache.list_keyaddr = realloc(g_keycache.list_keyaddr, (g_keycache.cache_size)*(sizeof *g_keycache.list_keyaddr));
  assert(g_keycache.list_keyaddr);

  int i = g_keycache.cache_size-1;
  while ((i > 0) && STRLT(kCache_entry,*g_keycache.list_keyaddr[i-1])){
    g_keycache.list_keyaddr[i] = g_keycache.list_keyaddr[i-1];
    --i;
  }
  json_string_kt *p_new_key = malloc(sizeof *p_new_key);
  assert(p_new_key);

  *p_new_key = strndup(kCache_entry,strlen(kCache_entry));
  assert(*p_new_key);

  g_keycache.list_keyaddr[i] = p_new_key;

  return g_keycache.list_keyaddr[i];
}

/* try to find key entry at the global keylist,
  return if found, else create, store it and return
  its address */
static json_string_kt*
json_get_key(const json_string_kt kCache_entry)
{
  int found_index = json_search_key(kCache_entry);
  if (-1 != found_index) return g_keycache.list_keyaddr[found_index];

  // key not found, create it and save in cache
  return json_cache_key(kCache_entry);
}

/* get numerical key for array type
    json, formatted to string */
static json_string_kt* //fix: get asprintf to work
json_set_array_key(json_item_st *item)
{
  char cache_entry[MAX_DIGITS];
  snprintf(cache_entry,MAX_DIGITS-1,"%ld",item->num_branch);

  return json_get_key(cache_entry);
}

/* fetch string type json and return
  allocated string */
static json_string_kt
json_string_set(char **p_buffer)
{
  char *start = *p_buffer;
  assert('\"' == *start); //makes sure a string is given

  char *end = ++start;
  while (('\0' != *end) && ('\"' != *end)){
    if ('\\' == *end++){ //skips escaped characters
      ++end;
    }
  }
  assert('\"' == *end); //makes sure end of string exists

  *p_buffer = end+1; //skips double quotes buffer position

  json_string_kt set_str = strndup(start, end-start);
  assert(NULL != set_str);

  return set_str;
}

/* fetch string type json and parse into static string */
static void
json_string_set_static(char **p_buffer, json_string_kt set_str, const int kStr_length)
{
  char *start = *p_buffer;
  assert('\"' == *start); //makes sure a string is given

  char *end = ++start;
  while (('\0' != *end) && ('\"' != *end)){
    if ('\\' == *end++){ //skips escaped characters
      ++end;
    }
  }
  assert('\"' == *end); //makes sure end of string exists

  *p_buffer = end+1; //skips double quotes buffer position
  
  /* if actual length is lesser than desired length,
    use the actual length instead */
  if (kStr_length > end - start)
    strncpy(set_str, start, end - start);
  else
    strncpy(set_str, start, kStr_length);
}

/* fetch number json type by parsing string,
  return value converted to double type */
static json_number_kt
json_number_set(char **p_buffer)
{
  char *start = *p_buffer;
  char *end = start;

  if ('-' == *end){
    ++end; //skips minus sign
  }

  assert(isdigit(*end)); //interrupt if char isn't digit

  while (isdigit(*++end))
    continue; //skips while char is digit

  if ('.' == *end){
    while (isdigit(*++end))
      continue;
  }

  //if exponent found skips its signal and numbers
  if (('e' == *end) || ('E' == *end)){
    ++end;
    if (('+' == *end) || ('-' == *end)){ 
      ++end;
    }
    assert(isdigit(*end));
    while (isdigit(*++end))
      continue;
  }

  char get_numstr[MAX_DIGITS] = {0};
  strncpy(get_numstr, start, end-start);

  json_number_kt set_number;
  sscanf(get_numstr,"%lf",&set_number); //@todo: replace sscanf?

  *p_buffer = end;

  return set_number;
}

/* get and return value from given json type */
static json_item_st*
json_item_set_value(json_type_et get_type, json_item_st *item, char **p_buffer)
{
  item->type = get_type;

  switch (item->type){
  case JSON_STRING:
      item->string = json_string_set(p_buffer);
      break;
  case JSON_NUMBER:
      item->number = json_number_set(p_buffer);
      break;
  case JSON_BOOLEAN:
      if (**p_buffer == 't'){
        *p_buffer += 4; //skips length of "true"
        item->boolean = 1;
        break;
      }
      *p_buffer += 5; //skips length of "false"
      item->boolean = 0;
      break;
  case JSON_NULL:
      *p_buffer += 4; //skips length of "null"
      break;
  case JSON_ARRAY:
  case JSON_OBJECT:
      //nothing to do, array and object values are its properties
      ++*p_buffer;
      break;
  default:
      fprintf(stderr,"ERROR: invalid datatype %ld\n", item->type);
      exit(EXIT_FAILURE);
  }

  return item;
}

/* Create nested object and return the nested object address. 
  This is used for arrays and objects type json */
static json_item_st*
json_item_set_incomplete(json_item_st *item, json_string_kt *get_p_key, json_type_et get_type, char **p_buffer)
{
  item = json_item_property_create(item);
  item->p_key = get_p_key;
  item = json_item_set_value(get_type, item, p_buffer);

  return item;
}

/* Wrap array or object type json, which means
  all of its properties have been created */
static json_item_st*
json_item_wrap(json_item_st *item){
  return json_item_get_parent(item);
}

/* Create a property. The "complete" means all
  of its value is fetched at creation, as opposite
  of array or object type json */
static json_item_st*
json_item_set_complete(json_item_st *item, json_string_kt *get_p_key, json_type_et get_type, char **p_buffer)
{
  item = json_item_set_incomplete(item, get_p_key, get_type, p_buffer);
  return json_item_wrap(item);
}

/* this will be active if the current item is of array type json,
  whatever item is created here will be this array's property.
  if a ']' token is found then the array is wrapped up */
static json_item_st*
json_item_build_array(json_item_st *item, char **p_buffer)
{
  json_item_st* (*item_setter)(json_item_st*,json_string_kt*,json_type_et,char**);
  json_type_et set_type;

  switch (**p_buffer){
  case ']':/*ARRAY WRAPPER DETECTED*/
      ++*p_buffer;
      return json_item_wrap(item);
  case '{':/*OBJECT DETECTED*/
      set_type = JSON_OBJECT;
      item_setter = &json_item_set_incomplete;
      break;
  case '[':/*ARRAY DETECTED*/
      set_type = JSON_ARRAY;
      item_setter = &json_item_set_incomplete;
      break;
  case '\"':/*STRING DETECTED*/
      set_type = JSON_STRING;
      item_setter = &json_item_set_complete;
      break;
  case 't':/*CHECK FOR*/
  case 'f':/* BOOLEAN */
      if (!STRNEQ(*p_buffer,"true",4) && !STRNEQ(*p_buffer,"false",5)){
        goto error;
      }
      set_type = JSON_BOOLEAN;
      item_setter = &json_item_set_complete;
      break;
  case 'n':/*CHECK FOR NULL*/
      if (!STRNEQ(*p_buffer,"null",4)){
        goto error;
      }
      set_type = JSON_NULL;
      item_setter = &json_item_set_complete;
      break;
  case ',': /*NEXT ELEMENT TOKEN*/
      ++*p_buffer;
      return item;
  default:
      /*IGNORE CONTROL CHARACTER*/
      if (isspace(**p_buffer) || iscntrl(**p_buffer)){
        ++*p_buffer;
        return item;
      }
      /*CHECK FOR NUMBER*/
      if (!isdigit(**p_buffer) && ('-' != **p_buffer)){
        goto error;
      }
      set_type = JSON_NUMBER;
      item_setter = &json_item_set_complete;
      break;
  }

  return (*item_setter)(item,json_set_array_key(item),set_type,p_buffer);


  error:
    fprintf(stderr,"ERROR: invalid json token %c\n", **p_buffer);
    exit(EXIT_FAILURE);
}

/* this will be active if the current item is of object type json,
  whatever item is created here will be this object's property.
  if a '}' token is found then the object is wrapped up */
static json_item_st*
json_item_build_object(json_item_st *item, json_string_kt **p_key, char **p_buffer)
{
  json_item_st* (*item_setter)(json_item_st*,json_string_kt*,json_type_et,char**);
  json_type_et set_type;
  char set_key[KEY_LENGTH] = {0};

  switch (**p_buffer){
  case '}':/*OBJECT WRAPPER DETECTED*/
      ++*p_buffer;
      return json_item_wrap(item);
  case '\"':/*KEY STRING DETECTED*/
      json_string_set_static(p_buffer,set_key,KEY_LENGTH);
      *p_key = json_get_key(set_key);
      return item;
  case ':':/*VALUE DETECTED*/
      do { //skips space and control characters before next switch
        ++*p_buffer;
      } while (isspace(**p_buffer) || iscntrl(**p_buffer));

      switch (**p_buffer){ //fix: move to function
      case '{':/*OBJECT DETECTED*/
          set_type = JSON_OBJECT;
          item_setter = &json_item_set_incomplete;
          break;
      case '[':/*ARRAY DETECTED*/
          set_type = JSON_ARRAY;
          item_setter = &json_item_set_incomplete;
          break;
      case '\"':/*STRING DETECTED*/
          set_type = JSON_STRING;
          item_setter = &json_item_set_complete;
          break;
      case 't':/*CHECK FOR*/
      case 'f':/* BOOLEAN */
          if (!STRNEQ(*p_buffer,"true",4) && !STRNEQ(*p_buffer,"false",5)){
            goto error;
          }
          set_type = JSON_BOOLEAN;
          item_setter = &json_item_set_complete;
          break;
      case 'n':/*CHECK FOR NULL*/
          if (!STRNEQ(*p_buffer,"null",4)){
            goto error; 
          }
          set_type = JSON_NULL;
          item_setter = &json_item_set_complete;
          break;
      default:
          /*CHECK FOR NUMBER*/
          if (!isdigit(**p_buffer) && ('-' != **p_buffer)){
            goto error;
          }
          set_type = JSON_NUMBER;
          item_setter = &json_item_set_complete;
          break;
      }
      return (*item_setter)(item,*p_key,set_type,p_buffer);
  case ',': //ignore comma
      ++*p_buffer;
      return item;
  default:
      /*IGNORE CONTROL CHARACTER*/
      if (isspace(**p_buffer) || iscntrl(**p_buffer)){
        ++*p_buffer;
        return item;
      }
      goto error;
  }


  error:
    fprintf(stderr,"ERROR: invalid json token %c\n", **p_buffer);
    exit(EXIT_FAILURE);
}

/* this call will only be used once, at the first iteration,
  it also allows the creation of a json that's not part of an
  array or object. ex: json_item_parse("10") */
static json_item_st*
json_item_build_entity(json_item_st *item, char **p_buffer)
{
  json_type_et set_type;

  switch (**p_buffer){
  case '{':/*OBJECT DETECTED*/
      set_type = JSON_OBJECT;
      break;
  case '[':/*ARRAY DETECTED*/
      set_type = JSON_ARRAY;
      break;
  case '\"':/*STRING DETECTED*/
      set_type = JSON_STRING;
      break;
  case 't':/*CHECK FOR*/
  case 'f':/* BOOLEAN */
      if (!STRNEQ(*p_buffer,"true",4) && !STRNEQ(*p_buffer,"false",5)){
        goto error;
      }
      set_type = JSON_BOOLEAN;
      break;
  case 'n':/*CHECK FOR NULL*/
      if (!STRNEQ(*p_buffer,"null",4)){
        goto error;
      }
      set_type = JSON_NULL;
      break;
  default:
      /*IGNORE CONTROL CHARACTER*/
      if (isspace(**p_buffer) || iscntrl(**p_buffer)){
        ++*p_buffer;
        return item;
      }
      /*CHECK FOR NUMBER*/
      if (!isdigit(**p_buffer) && ('-' != **p_buffer)){
        goto error;
      }
      set_type = JSON_NUMBER;
      break;
  }

  return json_item_set_value(set_type, item, p_buffer);


  error:
    fprintf(stderr,"ERROR: invalid json token %c\n",**p_buffer);
    exit(EXIT_FAILURE);
}

/* build json item by evaluating buffer's current position token */
static json_item_st*
json_item_build(json_item_st *item, json_string_kt **p_key, char **p_buffer)
{
  switch(item->type){
  case JSON_OBJECT:
      return json_item_build_object(item,p_key,p_buffer);
  case JSON_ARRAY:
      return json_item_build_array(item,p_buffer);
  case JSON_UNDEFINED://this should be true only at the first call
      return json_item_build_entity(item,p_buffer);
  default: //nothing else to build, check buffer for potential error
      if (isspace(**p_buffer) || iscntrl(**p_buffer)){
        ++*p_buffer; //moves if cntrl character found ('\n','\b',..)
        return item;
      }
      goto error;
  }

  error:
    fprintf(stderr,"ERROR: invalid json token %c\n",**p_buffer);
    exit(EXIT_FAILURE);
}

/* parse contents from buffer into a json item object
  and return its root */
json_item_st*
json_item_parse(char *buffer)
{
  json_item_st *root = calloc(1, sizeof *root);

  json_string_kt *p_set_key;
  json_item_st *item = root;
  //build while item and buffer aren't nulled
  while ((NULL != item) && ('\0' != *buffer)){
    item = json_item_build(item,&p_set_key,&buffer);
  }

  return root;
}

/* apply user defined function to contents being parsed */
static void
apply_reviver(json_item_st *item, void (*reviver)(json_item_st*))
{
  (*reviver)(item);
  for (size_t i=0; i < item->num_branch; ++i){
    apply_reviver(item->branch[i], reviver);
  }
}

/* like original parse, but with an option to modify json
  content during parsing */
json_item_st*
json_item_parse_reviver(char *buffer, void (*reviver)(json_item_st*))
{
  json_item_st *root = json_item_parse(buffer);

  if (NULL != reviver){
    apply_reviver(root, reviver);
  }

  return root;
}

