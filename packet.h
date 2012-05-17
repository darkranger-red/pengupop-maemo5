#ifndef PACKET_H_
#define PACKET_H_ 1

enum packet_type
{
  /* Server messages */
  packet_create_game = 0x00,
  packet_join_game = 0x01,
  packet_start_game = 0x02,
  packet_abort_game = 0x03,
  packet_join_random_game = 0x04,
  packet_identify = 0x05,
  packet_login = 0x07,
  packet_chat = 0x08,
  packet_message = 0x09,
  packet_eventlog = 0x0a,
  packet_logout = 0x0b,
  packet_please_reconnect = 0x0a,

  /* Client messages */
  packet_movement = 0x10
};

enum message
{
  message_unknown_user = 0x00,
  message_incorrect_password = 0x01,
  message_login_ok = 0x02,
};

struct network_mbubble
{
  unsigned char x[4];
  unsigned char y[4];
  unsigned char velx[4];
  unsigned char vely[4];
  unsigned char falling;
  unsigned char color;
};

#define EVENT_SHOOT 0x80UL
#define EVENT_EVIL  0x40UL
#define EVENT_LEFT  0x20UL
#define EVENT_RIGHT 0x10UL
#define EVENT_START 0x08UL
#define EVENT_WIN   0x04UL
#define EVENT_LOSE  0x02UL

struct event
{
  unsigned char tick[3];
  unsigned char data;
};

struct data_packet
{
  unsigned short payload_size;
  unsigned char packet_type;

  union
  {
    struct
    {
      char name[32];
    } create_game;

    struct
    {
      char name[32];
    } join_game;

    struct
    {
      unsigned char registered_only;
    } join_random_game;

    struct
    {
      unsigned char seed[4];
      unsigned char other_host[4];
      unsigned char port_lo;
      unsigned char port_hi;
      unsigned char is_server;
      unsigned char peer_version;
    } start_game;

    struct
    {
      char reason[32];
    } abort_game;

    struct
    {
      char game[32];
      unsigned char port_lo;
      unsigned char port_hi;
      char id[32];
    } identify;

    struct
    {
      unsigned char angle[4];
      unsigned char flags;
      unsigned char bubbles;

      unsigned char field[53];
      unsigned char mbubble_count;
      struct network_mbubble mbubbles[128];
    } movement;

    struct
    {
      char username[16];
      char password[16];
    } login;

    struct
    {
      unsigned char is_private;
      unsigned char message[536];
    } chat;

    struct
    {
      unsigned char idx;
    } message;

    struct
    {
      struct event events[256];
    } eventlog;

    unsigned char padding[65535];
  };
};

#endif /* PACKET_H_ */
