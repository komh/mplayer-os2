/*
    Slave mode of MPlayer interface example
    Copyright (C) 2008 by KO Myung-Hun <komh@chollian.net>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Changes :
        KO Myung-Hun <komh@chollian.net> 2008/03/08
            - Use '-idle' option
*/

#define INCL_KBD
#define INCL_DOS
#include <os2.h>

#include <stdio.h>
#include <string.h>
#include <process.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <io.h>

#include "osdep/keycodes.h"

HPIPE   hpipe;
int     pidMP;

void pipeOpen()
{
    CHAR    szPipeName[ 100 ];
    ULONG   rc;

    sprintf( szPipeName, "\\PIPE\\MPLAYER\\%x", pidMP );

    DosCreateNPipe( szPipeName, &hpipe, NP_ACCESS_DUPLEX, 1, 1024, 1024, 0 );
}

void pipeClose( void )
{
    DosClose( hpipe );
}

void pipeWrite( const char *s )
{
    ULONG cbActual;

    // MPlayer quitted ?
    if( waitpid( pidMP, NULL, WNOHANG ) != 0 )
        return;

    DosConnectNPipe( hpipe );

    DosWrite( hpipe, s, strlen( s ), &cbActual );

    // Wait for ACK
    DosRead( hpipe, &cbActual, sizeof( ULONG ), &cbActual );

    DosDisConnectNPipe( hpipe );
}

int main( int argc, char *argv[])
{
    KBDKEYINFO  kki;
    char    command[ 100 ];

    if( argc < 2 )
    {
        fprintf( stderr, "No input file!!!\n");
        return 1;
    }

    pidMP = spawnl( P_NOWAIT, "mplayer.exe", "mplayer.exe", "-idle", "-slave", "-quiet", NULL );

    pipeOpen();

    /* main routine of server side start */

    // load file
    _fnslashify( argv[ 1 ]);
    sprintf( command, "loadfile \"%s\"\n", argv[ 1 ]);
    pipeWrite( command );

    do
    {
        if( KbdCharIn( &kki, IO_WAIT, 0 ))
            break;

        printf("Pressed char = '%c'\n", kki.chChar );

        switch( kki.chChar )
        {
            case 'l' :
            case 'r' :
            {
                sprintf( command, "key_down_event %d\n",
                         kki.chChar == 'l' ? KEY_LEFT : KEY_RIGHT );
                pipeWrite( command );
                break;
            }

            case 'q' :
                pipeWrite("quit\n");
                break;
        }
    } while( kki.chChar != 'e' );

    /* main routine of server side end */

    // MPlayer still running ?
    if( waitpid( pidMP, NULL, WNOHANG ) == 0 )
    {
        printf("Terminating MPlayer...\n");

        pipeWrite("quit\n");

        waitpid( pidMP, NULL, 0 );
    }

    pipeClose();

    return 0;
}

