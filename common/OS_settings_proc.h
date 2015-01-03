/* Copyright 2012 Kjetil S. Matheussen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */

#ifndef OS_SETTINGS_PROC_H
#define OS_SETTINGS_PROC_H

extern LANGSPEC void OS_set_loading_path(const char *filename);
extern LANGSPEC void OS_unset_loading_path(void);
extern LANGSPEC const char *OS_loading_get_resolved_file_path(const char *path);

extern LANGSPEC const char *OS_saving_get_relative_path_if_possible(const char *filepath);  
extern LANGSPEC void OS_set_saving_path(const char *filename);

extern LANGSPEC const char *OS_get_directory_separator(void);
extern LANGSPEC void OS_set_argv0(char *argv0);
extern LANGSPEC const char *OS_get_program_path(void);

extern LANGSPEC bool OS_config_key_is_color(const char *key);
extern LANGSPEC char *OS_get_config_filename(const char *key);
extern LANGSPEC char *OS_get_conf_filename(const char *filename);
extern LANGSPEC char *OS_get_keybindings_conf_filename(void);
extern LANGSPEC char *OS_get_menues_conf_filename(void);

extern LANGSPEC void OS_make_config_file_expired(const char *key);

// locale independent.
extern LANGSPEC double OS_get_double_from_string(const char *s);
extern LANGSPEC char *OS_get_string_from_double(double d);


#endif
