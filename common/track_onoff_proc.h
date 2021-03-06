/* Copyright 2000 Kjetil S. Matheussen

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

#ifndef _RADIUM_COMMON_TRACK_ONOFF_PROC_H
#define _RADIUM_COMMON_TRACK_ONOFF_PROC_H

extern LANGSPEC void SwitchTrackOnOff_CurrPos(
	struct Tracker_Windows *window
);

extern LANGSPEC void SoloTrack_CurrPos(
                                       struct Tracker_Windows *window
);

extern LANGSPEC void AllTracksOn_CurrPos(
                                         struct Tracker_Windows *window
);

extern LANGSPEC void TRACK_OF_switch_spesified_CurrPos(
	struct Tracker_Windows *window,
	NInt tracknum
);

extern LANGSPEC void TRACK_set_on_off(
                                      struct Tracker_Windows *window,
                                      struct Tracks *track,
                                      bool is_on
                                      );

extern LANGSPEC void TRACK_OF_solo_spesified_CurrPos(
                                                     struct Tracker_Windows *window,
                                                     NInt tracknum
                                                     );

void TRACK_OF_switch_solo_spesified_CurrPos(
                                            struct Tracker_Windows *window,
                                            NInt tracknum
                                            );

#endif
