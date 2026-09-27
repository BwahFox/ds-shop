#pragma once

/* A browsable list the server provides. What the user can DO with a list
   (browse it, jump to a random title, search it) is decided by the UI. */
typedef struct {
    const char *name;        /* display name in menus / detail screen          */
    const char *server_cat;  /* value sent as ?cat= to the server              */
    const char *url_prefix;  /* /roms/<url_prefix>/<file>  ("" for plain DS)    */
    const char *local_dir;   /* SD-card directory downloads are saved to        */
    int         kind;        /* KIND_ROM: one file; KIND_THEME: a whole folder */
} Category;

#define KIND_ROM    0
#define KIND_THEME  1
