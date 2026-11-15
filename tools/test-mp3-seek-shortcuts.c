#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "hid_keys.h"
typedef struct {XsshKeyKind kinds[32];int n;} Capture;
static void sink(void *v,XsshKeyEvent e){Capture*c=v;assert(c->n<32);c->kinds[c->n++]=e.kind;}
static void send(XsshHidKeys *h,Capture *c,uint8_t mod,uint8_t key,int t){uint8_t r[8]={mod,0,key,0,0,0,0,0};xssh_hid_keys_report(h,r,t,sink,c);}
int main(void){
 XsshHidKeys h;Capture c={0};xssh_hid_keys_reset(&h);
 send(&h,&c,0x11,0x50,10);assert(c.kinds[c.n-1]==XSSH_KEY_MUSIC_PREVIOUS);
 send(&h,&c,0,0,20);
 send(&h,&c,0x11,0x4f,30);assert(c.kinds[c.n-1]==XSSH_KEY_MUSIC_NEXT);
 send(&h,&c,0,0,40);
 send(&h,&c,0x33,0x50,50);assert(c.kinds[c.n-1]==XSSH_KEY_MUSIC_SEEK_BACK);
 int n=c.n;xssh_hid_keys_tick(&h,1000,sink,&c);assert(c.n==n); /* no hold seeking */
 send(&h,&c,0x33,0x50,1010);assert(c.n==n); /* same report is not another press */
 send(&h,&c,0x33,0,1020);assert(c.n==n); /* release only resets edge */
 send(&h,&c,0x33,0x4f,1030);assert(c.kinds[c.n-1]==XSSH_KEY_MUSIC_SEEK_FORWARD);
 send(&h,&c,0x33,0,1040);
 send(&h,&c,0x33,0x4f,1050);assert(c.kinds[c.n-1]==XSSH_KEY_MUSIC_SEEK_FORWARD); /* new press */
 puts("MP3 seek/skip HID shortcuts passed");return 0;
}
