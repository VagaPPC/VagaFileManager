#ifndef VAGAFILEMANAGER_STRINGS_H
#define VAGAFILEMANAGER_STRINGS_H


/****************************************************************************/

/* This file was generated from VagaFileManager.cd */


#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifdef CATCOMP_CODE
#ifndef CATCOMP_BLOCK
#define CATCOMP_ARRAY
#endif
#endif

#ifdef CATCOMP_ARRAY
#ifndef CATCOMP_NUMBERS
#define CATCOMP_NUMBERS
#endif
#ifndef CATCOMP_STRINGS
#define CATCOMP_STRINGS
#endif
#endif

#ifdef CATCOMP_BLOCK
#ifndef CATCOMP_STRINGS
#define CATCOMP_STRINGS
#endif
#endif


/****************************************************************************/


#ifdef CATCOMP_NUMBERS

#define MSG_APP_TITLE 0
#define MSG_VIEW_ICONS_ONLY 1
#define MSG_VIEW_ALL_FILES 2
#define MSG_SORT_BY_TYPE 3
#define MSG_SORT_ALPHABETICAL 4
#define MSG_SORT_BY_NAME 5
#define MSG_SORT_BY_DATE 6
#define MSG_SORT_BY_SIZE 7
#define MSG_TYPE_DIR_LINK 8
#define MSG_TYPE_LINK 9
#define MSG_TYPE_DRAWER 10
#define MSG_TYPE_FILE 11
#define MSG_TYPE_SOFTLINK 12
#define MSG_TYPE_OTHER 13
#define MSG_TYPE_DEVICE 14
#define MSG_TYPE_VOLUME 15
#define MSG_TYPE_ASSIGN 16
#define MSG_TYPE_LATE_ASSIGN 17
#define MSG_TYPE_NONBINDING 18
#define MSG_ROOT_ASSIGNS 19
#define MSG_STATUS_FORMAT 20
#define MSG_BTN_OK 21
#define MSG_BTN_CANCEL 22
#define MSG_BTN_OK_CANCEL 23
#define MSG_RENAME_TITLE 24
#define MSG_RENAME 25
#define MSG_RENAME_SELECT_ONE 26
#define MSG_RENAME_FAILED 27
#define MSG_ERROR_TITLE 28
#define MSG_DELETE 29
#define MSG_DELETE_NONE 30
#define MSG_DELETE_CONFIRM_ONE 31
#define MSG_DELETE_CONFIRM_MANY 32
#define MSG_DELETE_CONFIRM_TITLE 33
#define MSG_DELETE_FAILED 34
#define MSG_DUPLICATE 35
#define MSG_DUPLICATE_ROOT 36
#define MSG_DUPLICATE_NONE 37
#define MSG_DUPLICATE_ERRORS 38
#define MSG_COPY_PREFIX 39
#define MSG_COPY 40
#define MSG_COPY_NONE 41
#define MSG_PASTE 42
#define MSG_PASTE_ROOT 43
#define MSG_PASTE_EMPTY 44
#define MSG_PASTE_ERRORS 45
#define MSG_INFO 46
#define MSG_INFO_TITLE 47
#define MSG_INFO_SELECT_ONE 48
#define MSG_DROP_COPY_FAILED 49
#define MSG_DRAGDROP_TITLE 50
#define MSG_DROP_ROOT 51
#define MSG_DROP_COPY_ERROR_NAME 52
#define MSG_COL_NAME 53
#define MSG_COL_TYPE 54
#define MSG_COL_SIZE 55
#define MSG_COL_DATE 56
#define MSG_STATUS_EMPTY 57
#define MSG_BTN_ICONS 58
#define MSG_BTN_DETAILS 59
#define MSG_BTN_UP 60
#define MSG_BTN_OPEN 61
#define MSG_BTN_REFRESH 62
#define MSG_BTN_HOME 63
#define MSG_LABEL_VIEW 64
#define MSG_LABEL_LIST 65
#define MSG_LABEL_PATH 66
#define MSG_DROP_NO_TARGET 67
#define MSG_COL_COMMENT 68
#define MSG_BTN_BACK 69
#define MSG_BTN_FORWARD 70
#define MSG_MENU_PROGRAM 71
#define MSG_MENU_NEWWINDOW 72
#define MSG_MENU_INFO 73
#define MSG_MENU_QUIT 74
#define MSG_MENU_EDIT 75
#define MSG_MENU_VIEW 76
#define MSG_MENU_BOOKMARKS 77
#define MSG_MENU_ADDBOOKMARK 78
#define MSG_BOOKMARK_ADDED 79
#define MSG_MENU_MANAGE 80
#define MSG_CUT 81
#define MSG_FILTER 82
#define MSG_SORT 83
#define MSG_ABOUT_VERSION 84
#define MSG_ABOUT_AUTHOR 85
#define MSG_ABOUT_EMAIL 86
#define MSG_ABOUT_LICENSE 87
#define MSG_MOVE 88
#define MSG_PROG_TITLE 89
#define MSG_PROG_STATUS 90
#define MSG_PROG_PAUSE 91
#define MSG_PROG_CONTINUE 92
#define MSG_MENU_SHOW 93
#define MSG_MENU_SCRIPT 94
#define MSG_SCRIPT_REFRESH 95
#define MSG_SCRIPT_OPENDIR 96
#define MSG_CALC_SIZE 97
#define MSG_SIZE_TITLE 98
#define MSG_SIZE_SELECT_DIR 99
#define MSG_SIZE_RESULT 100
#define MSG_BTN_SPLIT_HINT 101
#define MSG_ABOUT_BY 102
#define MSG_ABOUT_AUTHOR_NAME 103
#define MSG_ABOUT_AUTHOR_SUFFIX 104
#define MSG_ABOUT_MAIL 105
#define MSG_ABOUT_THANKS 106
#define MSG_MENU_WBREPLACE 107
#define MSG_MENU_WBRESTORE 108
#define MSG_FIND_TITLE 109
#define MSG_FIND_NONE 110
#define MSG_FIND_RESULTS 111
#define MSG_GREP_TITLE 112
#define MSG_GREP_NONE 113
#define MSG_GREP_RESULTS 114
#define MSG_MENU_SHOWINFO 115
#define MSG_BTN_SHELL_HINT 116
#define MSG_MENU_SHELL 117

#endif /* CATCOMP_NUMBERS */


/****************************************************************************/


#ifdef CATCOMP_STRINGS

#define MSG_APP_TITLE_STR "VagaFileManager"
#define MSG_VIEW_ICONS_ONLY_STR "by icons"
#define MSG_VIEW_ALL_FILES_STR "all files"
#define MSG_SORT_BY_TYPE_STR "type"
#define MSG_SORT_ALPHABETICAL_STR "Alphabetical"
#define MSG_SORT_BY_NAME_STR "name"
#define MSG_SORT_BY_DATE_STR "date"
#define MSG_SORT_BY_SIZE_STR "size"
#define MSG_TYPE_DIR_LINK_STR "Dir (link)"
#define MSG_TYPE_LINK_STR "Link"
#define MSG_TYPE_DRAWER_STR "Drawer"
#define MSG_TYPE_FILE_STR "File"
#define MSG_TYPE_SOFTLINK_STR "Softlink"
#define MSG_TYPE_OTHER_STR "Other"
#define MSG_TYPE_DEVICE_STR "Device"
#define MSG_TYPE_VOLUME_STR "Volume"
#define MSG_TYPE_ASSIGN_STR "Assign"
#define MSG_TYPE_LATE_ASSIGN_STR "Late assign"
#define MSG_TYPE_NONBINDING_STR "Non-binding"
#define MSG_ROOT_ASSIGNS_STR "(Devices and assigns)"
#define MSG_STATUS_FORMAT_STR "Items: %ld  |  Selected: %lu"
#define MSG_BTN_OK_STR "OK"
#define MSG_BTN_CANCEL_STR "Cancel"
#define MSG_BTN_OK_CANCEL_STR "OK|Cancel"
#define MSG_RENAME_TITLE_STR "Rename: %s"
#define MSG_RENAME_STR "Rename"
#define MSG_RENAME_SELECT_ONE_STR "Select a single item to rename."
#define MSG_RENAME_FAILED_STR "Unable to rename:\\n%s"
#define MSG_ERROR_TITLE_STR "Error"
#define MSG_DELETE_STR "Delete"
#define MSG_DELETE_NONE_STR "No item selected."
#define MSG_DELETE_CONFIRM_ONE_STR "Delete the selected item?"
#define MSG_DELETE_CONFIRM_MANY_STR "Delete %ld selected items?"
#define MSG_DELETE_CONFIRM_TITLE_STR "Confirm deletion"
#define MSG_DELETE_FAILED_STR "Unable to delete:\\n%s\\n(The drawer may not be empty)"
#define MSG_DUPLICATE_STR "Duplicate"
#define MSG_DUPLICATE_ROOT_STR "Operation not available in this view."
#define MSG_DUPLICATE_NONE_STR "No item selected."
#define MSG_DUPLICATE_ERRORS_STR "Some items could not be duplicated."
#define MSG_COPY_PREFIX_STR "Copy_%ld_%s"
#define MSG_COPY_STR "Copy"
#define MSG_COPY_NONE_STR "No item selected."
#define MSG_PASTE_STR "Paste"
#define MSG_PASTE_ROOT_STR "Cannot paste into the assigns list."
#define MSG_PASTE_EMPTY_STR "Clipboard is empty."
#define MSG_PASTE_ERRORS_STR "Some items could not be pasted."
#define MSG_INFO_STR "Info"
#define MSG_INFO_TITLE_STR "Information"
#define MSG_INFO_SELECT_ONE_STR "Select a single item."
#define MSG_DROP_COPY_FAILED_STR "Error while copying the dropped item."
#define MSG_DRAGDROP_TITLE_STR "Drag & Drop"
#define MSG_DROP_ROOT_STR "Cannot copy into the assigns list."
#define MSG_DROP_COPY_ERROR_NAME_STR "Error copying:\\n%s"
#define MSG_COL_NAME_STR "Name"
#define MSG_COL_TYPE_STR "Type"
#define MSG_COL_SIZE_STR "Size"
#define MSG_COL_DATE_STR "Date"
#define MSG_STATUS_EMPTY_STR "Items: 0  |  Selected: 0"
#define MSG_BTN_ICONS_STR "Icons"
#define MSG_BTN_DETAILS_STR "Details"
#define MSG_BTN_UP_STR "Up"
#define MSG_BTN_OPEN_STR "Open"
#define MSG_BTN_REFRESH_STR "Refresh"
#define MSG_BTN_HOME_STR "Home"
#define MSG_LABEL_VIEW_STR "View:"
#define MSG_LABEL_LIST_STR "List:"
#define MSG_LABEL_PATH_STR "Path:"
#define MSG_DROP_NO_TARGET_STR "No valid drop target found."
#define MSG_COL_COMMENT_STR "Comment"
#define MSG_BTN_BACK_STR "Back"
#define MSG_BTN_FORWARD_STR "Forward"
#define MSG_MENU_PROGRAM_STR "Program"
#define MSG_MENU_NEWWINDOW_STR "New..."
#define MSG_MENU_INFO_STR "Information..."
#define MSG_MENU_QUIT_STR "Quit"
#define MSG_MENU_EDIT_STR "Edit"
#define MSG_MENU_VIEW_STR "View"
#define MSG_MENU_BOOKMARKS_STR "Bookmarks"
#define MSG_MENU_ADDBOOKMARK_STR "Add to favorites"
#define MSG_BOOKMARK_ADDED_STR "Added to favorites."
#define MSG_MENU_MANAGE_STR "Manage..."
#define MSG_CUT_STR "Cut"
#define MSG_FILTER_STR "Filter"
#define MSG_SORT_STR "Sort by"
#define MSG_ABOUT_VERSION_STR "Version 1.0 (19.11.2026)"
#define MSG_ABOUT_AUTHOR_STR "VagaPPC (Benedetto Lorello)"
#define MSG_ABOUT_EMAIL_STR "mail: vagappc@yahoo.it"
#define MSG_ABOUT_LICENSE_STR "Free software: GNU General Public License v3 or later"
#define MSG_MOVE_STR "Move"
#define MSG_PROG_TITLE_STR "File operation"
#define MSG_PROG_STATUS_STR "Copying..."
#define MSG_PROG_PAUSE_STR "Pause"
#define MSG_PROG_CONTINUE_STR "Continue"
#define MSG_MENU_SHOW_STR "Show by"
#define MSG_MENU_SCRIPT_STR "Script"
#define MSG_SCRIPT_REFRESH_STR "Refresh list"
#define MSG_SCRIPT_OPENDIR_STR "Open plugin drawer"
#define MSG_CALC_SIZE_STR "Calculate size"
#define MSG_SIZE_TITLE_STR "Size"
#define MSG_SIZE_SELECT_DIR_STR "Select a drawer or a volume."
#define MSG_SIZE_RESULT_STR "Size: %s (%lld bytes)   Files: %lld   Drawers: %lld"
#define MSG_BTN_SPLIT_HINT_STR "Split / close second pane"
#define MSG_ABOUT_BY_STR "by"
#define MSG_ABOUT_AUTHOR_NAME_STR "VagaPPC"
#define MSG_ABOUT_AUTHOR_SUFFIX_STR "(Benedetto Lorello)"
#define MSG_ABOUT_MAIL_STR "Mail: vagappc@yahoo.it"
#define MSG_ABOUT_THANKS_STR "Special thanks: ldg72 and Flash"
#define MSG_MENU_WBREPLACE_STR "Replace Workbench"
#define MSG_MENU_WBRESTORE_STR "Restore Workbench"
#define MSG_FIND_TITLE_STR "Find file"
#define MSG_FIND_NONE_STR "No file found."
#define MSG_FIND_RESULTS_STR "Search results"
#define MSG_GREP_TITLE_STR "grep"
#define MSG_GREP_NONE_STR "No match in the selected files."
#define MSG_GREP_RESULTS_STR "grep results"
#define MSG_MENU_SHOWINFO_STR "Show file info"
#define MSG_BTN_SHELL_HINT_STR "Open shell here"
#define MSG_MENU_SHELL_STR "Shell"

#endif /* CATCOMP_STRINGS */


/****************************************************************************/


#ifdef CATCOMP_ARRAY

struct CatCompArrayType
{
    LONG         cca_ID;
    CONST_STRPTR cca_Str;
};

STATIC CONST struct CatCompArrayType CatCompArray[] =
{
    { MSG_APP_TITLE, (CONST_STRPTR)MSG_APP_TITLE_STR },
    { MSG_VIEW_ICONS_ONLY, (CONST_STRPTR)MSG_VIEW_ICONS_ONLY_STR },
    { MSG_VIEW_ALL_FILES, (CONST_STRPTR)MSG_VIEW_ALL_FILES_STR },
    { MSG_SORT_BY_TYPE, (CONST_STRPTR)MSG_SORT_BY_TYPE_STR },
    { MSG_SORT_ALPHABETICAL, (CONST_STRPTR)MSG_SORT_ALPHABETICAL_STR },
    { MSG_SORT_BY_NAME, (CONST_STRPTR)MSG_SORT_BY_NAME_STR },
    { MSG_SORT_BY_DATE, (CONST_STRPTR)MSG_SORT_BY_DATE_STR },
    { MSG_SORT_BY_SIZE, (CONST_STRPTR)MSG_SORT_BY_SIZE_STR },
    { MSG_TYPE_DIR_LINK, (CONST_STRPTR)MSG_TYPE_DIR_LINK_STR },
    { MSG_TYPE_LINK, (CONST_STRPTR)MSG_TYPE_LINK_STR },
    { MSG_TYPE_DRAWER, (CONST_STRPTR)MSG_TYPE_DRAWER_STR },
    { MSG_TYPE_FILE, (CONST_STRPTR)MSG_TYPE_FILE_STR },
    { MSG_TYPE_SOFTLINK, (CONST_STRPTR)MSG_TYPE_SOFTLINK_STR },
    { MSG_TYPE_OTHER, (CONST_STRPTR)MSG_TYPE_OTHER_STR },
    { MSG_TYPE_DEVICE, (CONST_STRPTR)MSG_TYPE_DEVICE_STR },
    { MSG_TYPE_VOLUME, (CONST_STRPTR)MSG_TYPE_VOLUME_STR },
    { MSG_TYPE_ASSIGN, (CONST_STRPTR)MSG_TYPE_ASSIGN_STR },
    { MSG_TYPE_LATE_ASSIGN, (CONST_STRPTR)MSG_TYPE_LATE_ASSIGN_STR },
    { MSG_TYPE_NONBINDING, (CONST_STRPTR)MSG_TYPE_NONBINDING_STR },
    { MSG_ROOT_ASSIGNS, (CONST_STRPTR)MSG_ROOT_ASSIGNS_STR },
    { MSG_STATUS_FORMAT, (CONST_STRPTR)MSG_STATUS_FORMAT_STR },
    { MSG_BTN_OK, (CONST_STRPTR)MSG_BTN_OK_STR },
    { MSG_BTN_CANCEL, (CONST_STRPTR)MSG_BTN_CANCEL_STR },
    { MSG_BTN_OK_CANCEL, (CONST_STRPTR)MSG_BTN_OK_CANCEL_STR },
    { MSG_RENAME_TITLE, (CONST_STRPTR)MSG_RENAME_TITLE_STR },
    { MSG_RENAME, (CONST_STRPTR)MSG_RENAME_STR },
    { MSG_RENAME_SELECT_ONE, (CONST_STRPTR)MSG_RENAME_SELECT_ONE_STR },
    { MSG_RENAME_FAILED, (CONST_STRPTR)MSG_RENAME_FAILED_STR },
    { MSG_ERROR_TITLE, (CONST_STRPTR)MSG_ERROR_TITLE_STR },
    { MSG_DELETE, (CONST_STRPTR)MSG_DELETE_STR },
    { MSG_DELETE_NONE, (CONST_STRPTR)MSG_DELETE_NONE_STR },
    { MSG_DELETE_CONFIRM_ONE, (CONST_STRPTR)MSG_DELETE_CONFIRM_ONE_STR },
    { MSG_DELETE_CONFIRM_MANY, (CONST_STRPTR)MSG_DELETE_CONFIRM_MANY_STR },
    { MSG_DELETE_CONFIRM_TITLE, (CONST_STRPTR)MSG_DELETE_CONFIRM_TITLE_STR },
    { MSG_DELETE_FAILED, (CONST_STRPTR)MSG_DELETE_FAILED_STR },
    { MSG_DUPLICATE, (CONST_STRPTR)MSG_DUPLICATE_STR },
    { MSG_DUPLICATE_ROOT, (CONST_STRPTR)MSG_DUPLICATE_ROOT_STR },
    { MSG_DUPLICATE_NONE, (CONST_STRPTR)MSG_DUPLICATE_NONE_STR },
    { MSG_DUPLICATE_ERRORS, (CONST_STRPTR)MSG_DUPLICATE_ERRORS_STR },
    { MSG_COPY_PREFIX, (CONST_STRPTR)MSG_COPY_PREFIX_STR },
    { MSG_COPY, (CONST_STRPTR)MSG_COPY_STR },
    { MSG_COPY_NONE, (CONST_STRPTR)MSG_COPY_NONE_STR },
    { MSG_PASTE, (CONST_STRPTR)MSG_PASTE_STR },
    { MSG_PASTE_ROOT, (CONST_STRPTR)MSG_PASTE_ROOT_STR },
    { MSG_PASTE_EMPTY, (CONST_STRPTR)MSG_PASTE_EMPTY_STR },
    { MSG_PASTE_ERRORS, (CONST_STRPTR)MSG_PASTE_ERRORS_STR },
    { MSG_INFO, (CONST_STRPTR)MSG_INFO_STR },
    { MSG_INFO_TITLE, (CONST_STRPTR)MSG_INFO_TITLE_STR },
    { MSG_INFO_SELECT_ONE, (CONST_STRPTR)MSG_INFO_SELECT_ONE_STR },
    { MSG_DROP_COPY_FAILED, (CONST_STRPTR)MSG_DROP_COPY_FAILED_STR },
    { MSG_DRAGDROP_TITLE, (CONST_STRPTR)MSG_DRAGDROP_TITLE_STR },
    { MSG_DROP_ROOT, (CONST_STRPTR)MSG_DROP_ROOT_STR },
    { MSG_DROP_COPY_ERROR_NAME, (CONST_STRPTR)MSG_DROP_COPY_ERROR_NAME_STR },
    { MSG_COL_NAME, (CONST_STRPTR)MSG_COL_NAME_STR },
    { MSG_COL_TYPE, (CONST_STRPTR)MSG_COL_TYPE_STR },
    { MSG_COL_SIZE, (CONST_STRPTR)MSG_COL_SIZE_STR },
    { MSG_COL_DATE, (CONST_STRPTR)MSG_COL_DATE_STR },
    { MSG_STATUS_EMPTY, (CONST_STRPTR)MSG_STATUS_EMPTY_STR },
    { MSG_BTN_ICONS, (CONST_STRPTR)MSG_BTN_ICONS_STR },
    { MSG_BTN_DETAILS, (CONST_STRPTR)MSG_BTN_DETAILS_STR },
    { MSG_BTN_UP, (CONST_STRPTR)MSG_BTN_UP_STR },
    { MSG_BTN_OPEN, (CONST_STRPTR)MSG_BTN_OPEN_STR },
    { MSG_BTN_REFRESH, (CONST_STRPTR)MSG_BTN_REFRESH_STR },
    { MSG_BTN_HOME, (CONST_STRPTR)MSG_BTN_HOME_STR },
    { MSG_LABEL_VIEW, (CONST_STRPTR)MSG_LABEL_VIEW_STR },
    { MSG_LABEL_LIST, (CONST_STRPTR)MSG_LABEL_LIST_STR },
    { MSG_LABEL_PATH, (CONST_STRPTR)MSG_LABEL_PATH_STR },
    { MSG_DROP_NO_TARGET, (CONST_STRPTR)MSG_DROP_NO_TARGET_STR },
    { MSG_COL_COMMENT, (CONST_STRPTR)MSG_COL_COMMENT_STR },
    { MSG_BTN_BACK, (CONST_STRPTR)MSG_BTN_BACK_STR },
    { MSG_BTN_FORWARD, (CONST_STRPTR)MSG_BTN_FORWARD_STR },
    { MSG_MENU_PROGRAM, (CONST_STRPTR)MSG_MENU_PROGRAM_STR },
    { MSG_MENU_NEWWINDOW, (CONST_STRPTR)MSG_MENU_NEWWINDOW_STR },
    { MSG_MENU_INFO, (CONST_STRPTR)MSG_MENU_INFO_STR },
    { MSG_MENU_QUIT, (CONST_STRPTR)MSG_MENU_QUIT_STR },
    { MSG_MENU_EDIT, (CONST_STRPTR)MSG_MENU_EDIT_STR },
    { MSG_MENU_VIEW, (CONST_STRPTR)MSG_MENU_VIEW_STR },
    { MSG_MENU_BOOKMARKS, (CONST_STRPTR)MSG_MENU_BOOKMARKS_STR },
    { MSG_MENU_ADDBOOKMARK, (CONST_STRPTR)MSG_MENU_ADDBOOKMARK_STR },
    { MSG_BOOKMARK_ADDED, (CONST_STRPTR)MSG_BOOKMARK_ADDED_STR },
    { MSG_MENU_MANAGE, (CONST_STRPTR)MSG_MENU_MANAGE_STR },
    { MSG_CUT, (CONST_STRPTR)MSG_CUT_STR },
    { MSG_FILTER, (CONST_STRPTR)MSG_FILTER_STR },
    { MSG_SORT, (CONST_STRPTR)MSG_SORT_STR },
    { MSG_ABOUT_VERSION, (CONST_STRPTR)MSG_ABOUT_VERSION_STR },
    { MSG_ABOUT_AUTHOR, (CONST_STRPTR)MSG_ABOUT_AUTHOR_STR },
    { MSG_ABOUT_EMAIL, (CONST_STRPTR)MSG_ABOUT_EMAIL_STR },
    { MSG_ABOUT_LICENSE, (CONST_STRPTR)MSG_ABOUT_LICENSE_STR },
    { MSG_MOVE, (CONST_STRPTR)MSG_MOVE_STR },
    { MSG_PROG_TITLE, (CONST_STRPTR)MSG_PROG_TITLE_STR },
    { MSG_PROG_STATUS, (CONST_STRPTR)MSG_PROG_STATUS_STR },
    { MSG_PROG_PAUSE, (CONST_STRPTR)MSG_PROG_PAUSE_STR },
    { MSG_PROG_CONTINUE, (CONST_STRPTR)MSG_PROG_CONTINUE_STR },
    { MSG_MENU_SHOW, (CONST_STRPTR)MSG_MENU_SHOW_STR },
    { MSG_MENU_SCRIPT, (CONST_STRPTR)MSG_MENU_SCRIPT_STR },
    { MSG_SCRIPT_REFRESH, (CONST_STRPTR)MSG_SCRIPT_REFRESH_STR },
    { MSG_SCRIPT_OPENDIR, (CONST_STRPTR)MSG_SCRIPT_OPENDIR_STR },
    { MSG_CALC_SIZE, (CONST_STRPTR)MSG_CALC_SIZE_STR },
    { MSG_SIZE_TITLE, (CONST_STRPTR)MSG_SIZE_TITLE_STR },
    { MSG_SIZE_SELECT_DIR, (CONST_STRPTR)MSG_SIZE_SELECT_DIR_STR },
    { MSG_SIZE_RESULT, (CONST_STRPTR)MSG_SIZE_RESULT_STR },
    { MSG_BTN_SPLIT_HINT, (CONST_STRPTR)MSG_BTN_SPLIT_HINT_STR },
    { MSG_ABOUT_BY, (CONST_STRPTR)MSG_ABOUT_BY_STR },
    { MSG_ABOUT_AUTHOR_NAME, (CONST_STRPTR)MSG_ABOUT_AUTHOR_NAME_STR },
    { MSG_ABOUT_AUTHOR_SUFFIX, (CONST_STRPTR)MSG_ABOUT_AUTHOR_SUFFIX_STR },
    { MSG_ABOUT_MAIL, (CONST_STRPTR)MSG_ABOUT_MAIL_STR },
    { MSG_ABOUT_THANKS, (CONST_STRPTR)MSG_ABOUT_THANKS_STR },
    { MSG_MENU_WBREPLACE, (CONST_STRPTR)MSG_MENU_WBREPLACE_STR },
    { MSG_MENU_WBRESTORE, (CONST_STRPTR)MSG_MENU_WBRESTORE_STR },
    { MSG_FIND_TITLE, (CONST_STRPTR)MSG_FIND_TITLE_STR },
    { MSG_FIND_NONE, (CONST_STRPTR)MSG_FIND_NONE_STR },
    { MSG_FIND_RESULTS, (CONST_STRPTR)MSG_FIND_RESULTS_STR },
    { MSG_GREP_TITLE, (CONST_STRPTR)MSG_GREP_TITLE_STR },
    { MSG_GREP_NONE, (CONST_STRPTR)MSG_GREP_NONE_STR },
    { MSG_GREP_RESULTS, (CONST_STRPTR)MSG_GREP_RESULTS_STR },
    { MSG_MENU_SHOWINFO, (CONST_STRPTR)MSG_MENU_SHOWINFO_STR },
    { MSG_BTN_SHELL_HINT, (CONST_STRPTR)MSG_BTN_SHELL_HINT_STR },
    { MSG_MENU_SHELL, (CONST_STRPTR)MSG_MENU_SHELL_STR },
};

#endif /* CATCOMP_ARRAY */


/****************************************************************************/


#ifdef CATCOMP_BLOCK

STATIC CONST UBYTE CatCompBlock[] =
{
    "\000\000\000\000\000\020VagaFileManager\000"
    "\000\000\000\001\000\012by icons\000\000"
    "\000\000\000\002\000\012all files\000"
    "\000\000\000\003\000\006type\000\000"
    "\000\000\000\004\000\016Alphabetical\000\000"
    "\000\000\000\005\000\006name\000\000"
    "\000\000\000\006\000\006date\000\000"
    "\000\000\000\007\000\006size\000\000"
    "\000\000\000\010\000\014Dir (link)\000\000"
    "\000\000\000\011\000\006Link\000\000"
    "\000\000\000\012\000\010Drawer\000\000"
    "\000\000\000\013\000\006File\000\000"
    "\000\000\000\014\000\012Softlink\000\000"
    "\000\000\000\015\000\006Other\000"
    "\000\000\000\016\000\010Device\000\000"
    "\000\000\000\017\000\010Volume\000\000"
    "\000\000\000\020\000\010Assign\000\000"
    "\000\000\000\021\000\014Late assign\000"
    "\000\000\000\022\000\014Non-binding\000"
    "\000\000\000\023\000\026(Devices and assigns)\000"
    "\000\000\000\024\000\036Items: %ld  |  Selected: %lu\000\000"
    "\000\000\000\025\000\004OK\000\000"
    "\000\000\000\026\000\010Cancel\000\000"
    "\000\000\000\027\000\012OK|Cancel\000"
    "\000\000\000\030\000\014Rename: %s\000\000"
    "\000\000\000\031\000\010Rename\000\000"
    "\000\000\000\032\000 Select a single item to rename.\000"
    "\000\000\000\033\000\026Unable to rename:\\n%s\000"
    "\000\000\000\034\000\006Error\000"
    "\000\000\000\035\000\010Delete\000\000"
    "\000\000\000\036\000\022No item selected.\000"
    "\000\000\000\037\000\032Delete the selected item?\000"
    "\000\000\000 \000\034Delete %ld selected items?\000\000"
    "\000\000\000!\000\022Confirm deletion\000\000"
    "\000\000\000\"\0006Unable to delete:\\n%s\\n(The drawer may not be empty)\000\000"
    "\000\000\000#\000\012Duplicate\000"
    "\000\000\000$\000&Operation not available in this view.\000"
    "\000\000\000%\000\022No item selected.\000"
    "\000\000\000&\000$Some items could not be duplicated.\000"
    "\000\000\000'\000\014Copy_%ld_%s\000"
    "\000\000\000(\000\006Copy\000\000"
    "\000\000\000)\000\022No item selected.\000"
    "\000\000\000*\000\006Paste\000"
    "\000\000\000+\000$Cannot paste into the assigns list.\000"
    "\000\000\000,\000\024Clipboard is empty.\000"
    "\000\000\000-\000 Some items could not be pasted.\000"
    "\000\000\000.\000\006Info\000\000"
    "\000\000\000/\000\014Information\000"
    "\000\000\0000\000\026Select a single item.\000"
    "\000\000\0001\000&Error while copying the dropped item.\000"
    "\000\000\0002\000\014Drag & Drop\000"
    "\000\000\0003\000$Cannot copy into the assigns list.\000\000"
    "\000\000\0004\000\024Error copying:\\n%s\000\000"
    "\000\000\0005\000\006Name\000\000"
    "\000\000\0006\000\006Type\000\000"
    "\000\000\0007\000\006Size\000\000"
    "\000\000\0008\000\006Date\000\000"
    "\000\000\0009\000\032Items: 0  |  Selected: 0\000\000"
    "\000\000\000:\000\006Icons\000"
    "\000\000\000;\000\010Details\000"
    "\000\000\000<\000\004Up\000\000"
    "\000\000\000=\000\006Open\000\000"
    "\000\000\000>\000\010Refresh\000"
    "\000\000\000?\000\006Home\000\000"
    "\000\000\000@\000\006View:\000"
    "\000\000\000A\000\006List:\000"
    "\000\000\000B\000\006Path:\000"
    "\000\000\000C\000\034No valid drop target found.\000"
    "\000\000\000D\000\010Comment\000"
    "\000\000\000E\000\006Back\000\000"
    "\000\000\000F\000\010Forward\000"
    "\000\000\000G\000\010Program\000"
    "\000\000\000H\000\010New...\000\000"
    "\000\000\000I\000\020Information...\000\000"
    "\000\000\000J\000\006Quit\000\000"
    "\000\000\000K\000\006Edit\000\000"
    "\000\000\000L\000\006View\000\000"
    "\000\000\000M\000\012Bookmarks\000"
    "\000\000\000N\000\022Add to favorites\000\000"
    "\000\000\000O\000\024Added to favorites.\000"
    "\000\000\000P\000\012Manage...\000"
    "\000\000\000Q\000\004Cut\000"
    "\000\000\000R\000\010Filter\000\000"
    "\000\000\000S\000\010Sort by\000"
    "\000\000\000T\000\032Version 1.0 (19.11.2026)\000\000"
    "\000\000\000U\000\034VagaPPC (Benedetto Lorello)\000"
    "\000\000\000V\000\030mail: vagappc@yahoo.it\000\000"
    "\000\000\000W\0006Free software: GNU General Public License v3 or later\000"
    "\000\000\000X\000\006Move\000\000"
    "\000\000\000Y\000\020File operation\000\000"
    "\000\000\000Z\000\014Copying...\000\000"
    "\000\000\000[\000\006Pause\000"
    "\000\000\000\\\000\012Continue\000\000"
    "\000\000\000]\000\010Show by\000"
    "\000\000\000^\000\010Script\000\000"
    "\000\000\000_\000\016Refresh list\000\000"
    "\000\000\000`\000\024Open plugin drawer\000\000"
    "\000\000\000a\000\020Calculate size\000\000"
    "\000\000\000b\000\006Size\000\000"
    "\000\000\000c\000\036Select a drawer or a volume.\000\000"
    "\000\000\000d\0004Size: %s (%lld bytes)   Files: %lld   Drawers: %lld\000"
    "\000\000\000e\000\032Split / close second pane\000"
    "\000\000\000f\000\004by\000\000"
    "\000\000\000g\000\010VagaPPC\000"
    "\000\000\000h\000\024(Benedetto Lorello)\000"
    "\000\000\000i\000\030Mail: vagappc@yahoo.it\000\000"
    "\000\000\000j\000 Special thanks: ldg72 and Flash\000"
    "\000\000\000k\000\022Replace Workbench\000"
    "\000\000\000l\000\022Restore Workbench\000"
    "\000\000\000m\000\012Find file\000"
    "\000\000\000n\000\020No file found.\000\000"
    "\000\000\000o\000\020Search results\000\000"
    "\000\000\000p\000\006grep\000\000"
    "\000\000\000q\000 No match in the selected files.\000"
    "\000\000\000r\000\016grep results\000\000"
    "\000\000\000s\000\020Show file info\000\000"
    "\000\000\000t\000\020Open shell here\000"
    "\000\000\000u\000\006Shell\000"
};

#endif /* CATCOMP_BLOCK */


/****************************************************************************/


#ifndef PROTO_LOCALE_H
#include <proto/locale.h>
#endif

struct LocaleInfo
{
    struct LocaleIFace *li_ILocale;
    struct Catalog     *li_Catalog;
};

#ifdef __cplusplus
extern "C" {
#endif
CONST_STRPTR GetString(struct LocaleInfo *li, LONG stringNum);
#ifdef __cplusplus
}
#endif

#ifdef CATCOMP_CODE

CONST_STRPTR GetString(struct LocaleInfo *li, LONG stringNum)
{
    struct LocaleIFace *ILocale = li ? li->li_ILocale : NULL;
    LONG *l;
    UWORD *w;
    CONST_STRPTR builtIn = NULL;

    l = (LONG *)CatCompBlock;
    while ((UBYTE *)l + 6 <= (CatCompBlock + sizeof(CatCompBlock)) && *l != stringNum)
    {
        w = (UWORD *)((ULONG)l + 4);
        l = (LONG *)((ULONG)l + (ULONG)*w + 6);
    }
    if ((UBYTE *)l + 6 <= (CatCompBlock + sizeof(CatCompBlock)) && *l == stringNum)
    {
        builtIn = (CONST_STRPTR)((ULONG)l + 6);
    }

    if (ILocale)
        return ILocale->GetCatalogStr(li->li_Catalog, stringNum, builtIn);
    return builtIn;
}

#endif /* CATCOMP_CODE */


/****************************************************************************/

#endif /* VAGAFILEMANAGER_STRINGS_H */
