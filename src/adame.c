/*
 * ADAME - Atari Disk And Modem Emulator
 * uh-DOM-may
 */

#include <stdio.h>
#include <termio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define WAIT( x )
// usleep( x * 1000 )

void
err( const char *s )
{
fprintf( stderr, "%d:", errno );
fprintf( stderr, "%s\n", s );
exit( 1 );
}

int atari;
int numdisks;
unsigned char *data[ 8 ];
int flags[ 8 ];

void raw( void )
{
struct termios it;

if (tcgetattr(atari,&it)<0) err( "get attr" );
it.c_lflag &= 0; //~(ICANON|ISIG|ECHO);
it.c_iflag &= 0; // ~(INPCK|ISTRIP|IXON);
//it.c_iflag |= IGNPAR;
it.c_oflag &=0; // ~(OPOST);
it.c_cc[VMIN] = 1;
it.c_cc[VTIME] = 0;

if (cfsetospeed( &it, B19200 )<0) err( "set o speed" );
if (cfsetispeed( &it, B19200 )<0) err( "set i speed" );
if (tcsetattr(atari,TCSANOW,&it)<0) err( "set attr" );
}

void
ack( unsigned char c )
{
printf( "[a" );
if (write( atari, &c, 1 )<0) err( "ack failed\n" );
printf( "ck]" );
}

void
senddata( unsigned char *buf, int len )
{
int i, sum = 0;

for( i=0; i<len; i++ )
  {
  write( atari, &buf[i], 1 );
  sum = sum + buf[i];
  sum = (sum & 0xff) + (sum >> 8);
//  printf( "%x.", buf[i] & 0xff );
  if (!(i%16)) printf( "." );
  }
write( atari, &sum, 1 );
}

int
recvdata( unsigned char *buf, int len )
{
int i, sum = 0;
unsigned char mybuf[ 2048 ];

for( i=0; i<len; i++ )
  {
  read( atari, &mybuf[i], 1 );
  sum = sum + mybuf[i];
  sum = (sum & 0xff) + (sum >> 8);
//  printf( "%x.", mybuf[i] & 0xff );
  if (!(i%16)) printf( "." );
  }
read( atari, &i, 1 );
if ((i & 0xff) != (sum & 0xff)) printf( "[DATASUM] " );
                           else memcpy( buf+i, mybuf, len );
}

int
offset( int disk, int sec )
{
int res;
if (flags[disk] && (sec>3)) { printf( "[dd]" ); res = (sec-4) * 256 + 3*128; }
                       else res = (sec-1) * 128;
printf( "[from=%d]", res );
return res;
}

int
size( int disk, int sec )
{
if (flags[disk] && (sec>3)) return 256;
                       else return 128;
}

void
decode( unsigned char *buf )
{
int sum, disk = -1, rs = -1, sec;
printf( "%x %x %x %x %x: ", buf[0], buf[1], buf[2], buf[3], buf[4] );
sum = buf[0] + buf[1] + buf[2] + buf[3];
sum = (sum & 0xff) + (sum >> 8);
if ((sum & 0xff) != (buf[4] & 0xff)) printf( "[SUM] " );
if ((buf[0] & 0x7f) == 'A')
  if ((buf[1] & 0x7f) == 'B')
    if ((buf[2] & 0x7f) == 'R')
      if ((buf[3] & 0x7f) == 'A')
	if ((buf[4] & 0x7f) == 'K')
	  { printf( "Magic detected\n" ); exit( 0 ); }

switch( buf[0] ) {
  case 0x31: printf( "D1: " ); disk = 0; break;
  case 0x32: printf( "D2: " ); disk = 1; break;
  case 0x33: printf( "D3: " ); disk = 2; break;
  case 0x34: printf( "D4: " ); disk = 3; break;
  case 0x40: printf( "P: " ); break;
  case 0x50: printf( "R1: " ); rs = 0; break;
  case 0x51: printf( "R2: " ); rs = 1; break;
  case 0x52: printf( "R3: " ); rs = 2; break;
  case 0x53: printf( "R4: " ); rs = 3; break;
  default: printf( "???: " ); break;
  }
if (!data[disk]) { printf( "[ignored]\n" ); return; }
WAIT( 100 );
ack( 0x41 );
switch( buf[1] ) {
  case 0x52:
    sec = buf[2] + 256*buf[3];
    WAIT( 100 );
    ack( 0x43 );
    printf( "read (%d) ", sec );
    senddata( &(data[disk][ offset( disk, sec ) ]), size( disk, sec ) );
    break;
  case 0x57:
    sec = buf[2] + 256*buf[3];
    WAIT( 100 );
    printf( "write (%d) ", sec );
    recvdata( &(data[disk][ offset( disk, sec ) ]), size( disk, sec ) );
    ack( 0x41 );
    ack( 0x43 );
    break;
  case 0x53:
    printf( "status " );
    {
    static char status[] = { 0x10, 0x00, 1, 0 };
    ack( 0x43 );
    senddata( status, 4 );
    }
    break;
  case 0x50:
    printf( "put " );
    break;
  case 0x21:
    printf( "format " );
    break;
  case 0x20:
    printf( "download " );
    break;
  case 0x54:
    printf( "readaddr " );
    break;
  case 0x51:
    printf( "readspin " );
    break;
  case 0x55:
    printf( "motoron " );
    break;
  case 0x56:
    printf( "verify " );
    break;
  default:
    printf( "??? " );
    break;
  }
  printf( "\n" );
}

int
loaddisk( char *path, char *buf, int skip )
{
int f = open( path, O_RDONLY ), i;

if (f<0) err( "Can't open disk." );
lseek( f, skip, SEEK_SET );
i = read( f, buf, 180*1024 );
if (i<0) err( "Read failed." );
if (i%256) err( "That's not a disk!" );
printf( "(%dK)", i / 1024 );
}

void
main( int argc, char *argv[] )
{
unsigned char buf[ 20 ];
int i, dd = 0;
int off = 16;

setvbuf( stdout, NULL, _IONBF, 0 );
setvbuf( stderr, NULL, _IONBF, 0 );

for( i=1; i<argc; i++ )
  {
  if (*(argv[i]) == '-')
    {
    switch( (argv[i])[1] ) {
      case 'a': off=16; break;
      case 'x': off=0; break;
      case 'd': dd=1; break;
      default: err( "Bad command line argument." );
      }
    }
  else
    {
    printf( " File: %d:%s ", ++numdisks, argv[i] );
    if (dd) { printf( "(dd)" ); flags[ numdisks -1 ] = 1; dd=0; }
    data[ numdisks - 1 ] = malloc( 1024*1024 );
    loaddisk( argv[i], data[ numdisks - 1 ], off );
    printf( "\n" );
    }
  }

printf( "Open" );
atari = open( "/dev/cua0", O_RDWR );
if (atari == -1) err( "Can't open COM port: " );
printf( "." );
raw();
printf( "\n" );

while( 1 ) {
//  if ((i=read( atari, buf, 5 ))<5) { printf( "!!! : %d !!!\n", i ); continue; }
  printf( ":" );
  alarm( 0 );
  if (read( atari, &buf[0], 1 )<1) { printf( "!!!\n" ); continue; }
  alarm( 10 );
  if ((buf[0] != 0x31) && (buf[0] != 0x32) && (buf[0] != 0x33) && (buf[0] != 0x34) && (buf[0]!=('A' | 0x80)))
    { printf( "[%x,%c]", (buf[0] & 0xff), (buf[0] & 0xff) ); continue; }
  if (read( atari, &buf[1], 1 )<1) { printf( "?!\n" ); continue; }
  if (read( atari, &buf[2], 1 )<1) { printf( "?!\n" ); continue; }
  if (read( atari, &buf[3], 1 )<1) { printf( "?!\n" ); continue; }
  if (read( atari, &buf[4], 1 )<1) { printf( "?!\n" ); continue; }
  alarm( 0 );
  decode( buf );
  }
}