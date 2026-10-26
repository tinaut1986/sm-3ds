// ftp_server.h for the UI preview: a server that is never running.
#include <stdio.h>

#include "ftp_server.h"

bool FtpServer_Start(void) { return false; }
void FtpServer_Stop(void) {}
bool FtpServer_Running(void) { return false; }
void FtpServer_Address(char *out, unsigned cap) { snprintf(out, cap, "STOPPED"); }
void FtpServer_Stats(unsigned *sent, unsigned *received, bool *connected) {
  if (sent) *sent = 0;
  if (received) *received = 0;
  if (connected) *connected = false;
}
const char *FtpServer_Notice(void) { return NULL; }
void FtpServer_Tick(void) {}
bool FtpServer_TakeRelaunch(void) { return false; }
bool FtpServer_InstallOnly(void) { return false; }
void FtpServer_Cancel(void) {}
int FtpServer_Progress(unsigned long long *done, unsigned long long *total) { (void)done; (void)total; return -1; }
