/*
 *  EEPROM dump functions
 *
 *  Copyright (C) 2016
 *    Sandor Zsuga (Jubatian)
 *  Uzem (the base of CUzeBox) is copyright (C)
 *    David Etherton,
 *    Eric Anderton,
 *    Alec Bourque (Uze),
 *    Filipe Rinaldi,
 *    Sandor Zsuga (Jubatian),
 *    Matt Pandina (Artcfox)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/



#include "eepdump.h"
#include "filesys.h"



/* EEPROM dump file */
static const char eepdump_file[] = "eeprom.bin";

/* Standard Uzebox EEPROM block 0, matching Bootloader 5's formatter except
** for the initial boot choice: GAME (0x16=1). A valid header is required or
** the firmware would format the erased EEPROM and restore its MENU default.
** Other blocks stay erased/free so games can allocate their save blocks. */
static const uint8 eepdump_initial_header[32] = {
 0x5AU, 0x55U, 1U, 32U, 1U, 1U, 0U, 0x38U, 0x08U,
 [0x16] = 1U
};



/*
** Tries to load EEPROM state from an eeprom dump. If the EEPROM dump does not
** exist or cannot be read completely, initialize a fresh Uzebox EEPROM with
** GAME as the bootloader default. Complete existing dumps are kept verbatim.
*/
void eepdump_load(uint8* eeprom)
{
 auint rv;

 if (!filesys_open(FILESYS_CH_ROM, eepdump_file)){ goto ex_fail; }

 rv = filesys_read(FILESYS_CH_ROM, eeprom, 2048U);
 if (rv != 2048){ goto ex_fail; }

 filesys_flush(FILESYS_CH_ROM);
 return;

ex_fail:
 filesys_flush(FILESYS_CH_ROM);
 memset(eeprom, 0xFFU, 2048U);
 memcpy(eeprom, eepdump_initial_header, sizeof(eepdump_initial_header));
 return;
}



/*
** Tries to write out EEPROM state into an eeprom dump. It fails silently if
** this is not possible.
*/
void eepdump_save(uint8 const* eeprom)
{
 (void)(filesys_open(FILESYS_CH_ROM, eepdump_file));   /* Might fail since tries to open for reading */

 (void)(filesys_write(FILESYS_CH_ROM, eeprom, 2048U)); /* Don't care for faults (can't do anything about them) */

 filesys_flush(FILESYS_CH_ROM);
 return;
}
