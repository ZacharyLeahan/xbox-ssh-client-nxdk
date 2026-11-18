#!/usr/bin/env python3
"""Execute the production auth branch with mocked SSH transport calls."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parent.parent
main=(root/'src/main.c').read_text()
branch=main.split('static int ssh_auth_complete(App *a) {',1)[1].split('    /* A moderate receive window',1)[0]
probe=r'''
#include "connect_form.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {void *session; XsshConnectForm form;} App;
static int keys, passwords, fail, empty_phrase;
static void status(App *a,const char *s){(void)a;(void)s;}
static int libssh2_userauth_publickey_fromfile_ex(void *s,const char *u,unsigned int n,const char *pub,const char *key,const char *phrase){
 (void)s;assert(!strcmp(u,"tester"));assert(n==6);assert(!pub);assert(key[0]);
 assert(empty_phrase ? phrase==NULL : !strcmp(phrase,"secret"));keys++;return fail;
}
static int libssh2_userauth_password(void *s,const char *u,const char *pw){
 (void)s;(void)u;assert(!strcmp(pw,"secret"));passwords++;return fail;
}
static int authenticate(App *a){
''' + branch + r'''
return 0;
}
int main(int argc,char **argv){
 assert(argc==2);App a={0};xssh_form_init(&a.form,"10.0.0.1",22,"tester");
 strcpy(a.form.password,"secret");assert(!authenticate(&a));assert(passwords==1 && !keys);assert(!a.form.password[0]);
 strcpy(a.form.key_file,argv[1]);strcpy(a.form.password,"secret");
 assert(!authenticate(&a));assert(keys==1 && passwords==1);assert(!a.form.password[0]);
 empty_phrase=1;assert(xssh_form_validate(&a.form,XSSH_FORM_PASSWORD));
 assert(!authenticate(&a));assert(keys==2);
 fail=1;assert(authenticate(&a)==-1);assert(keys==3 && passwords==1);
 strcpy(a.form.key_file,"missing-private-key");assert(authenticate(&a)==-1);assert(keys==3);
 a.form.key_file[0]=0;assert(!xssh_form_validate(&a.form,XSSH_FORM_PASSWORD));
 a.form.field=XSSH_FORM_KEY_FILE;assert(xssh_form_validate(&a.form,a.form.field));
 const char *p="D:\\id_rsa";for(;*p;p++)assert(xssh_form_type(&a.form,*p));
 assert(!strcmp(a.form.key_file,"D:\\id_rsa"));
 strcpy(a.form.password,"secret");xssh_form_zero_password(&a.form);
 for(unsigned i=0;i<sizeof(a.form.password);i++)assert(a.form.password[i]==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 d=Path(td);(d/'probe.c').write_text(probe);(d/'key').write_text('mock key')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'include'),str(d/'probe.c'),str(root/'src/connect_form.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test'),str(d/'key')],check=True)
print('PASS: password/key selection, missing/rejected keys, no password fallback, empty passphrase, secret scrub, key-field input')
