/*
 * celldoom.c
 * PlayStation(R)3 SDK Compatibility Definitions
 * Copyright (C) 2020 Jacopo Maltagliati
 * Released under the MIT License
 */

#include "celldoom.h"

///////////////////////////////////////////////////////////////////////////////
// CD_PosixCompat

/*
 * Fakes a system environment. TODO: proper logic.
 */
char *getenv(char *env_id) { return SYS_APP_HOME; }

/*
 * Bridge between POSIX access() and cellFsStat()
 */
int access(const char *pathname, int mode) {
  // Refer to fs_external.h for CellFsStat, CellFsMode and CellFsErrno values.
  CellFsStat status;
  if (CD_FSTestAccess(pathname, &status) != CELL_FS_SUCCEEDED)
    return -1;
  // Testing for file existence is implicit: if the above fails, there is no
  // file. If the file exists, we only test for RWX permission for current user,
  // as UGO permissions on CellFs are, at least in our case, useless.
  boolean t = true;
  t &= (~((mode & R_OK) >> 2) | ((status.st_mode & CELL_FS_S_IRUSR) >> 8));
  t &= (~((mode & W_OK) >> 1) | ((status.st_mode & CELL_FS_S_IWUSR) >> 7));
  t &= (~((mode & X_OK)) | ((status.st_mode & CELL_FS_S_IXUSR) >> 6));
  if (t)
    return 0;
  return -1;
}

/*
 * Bridge between POSIX malloc() and sys_memory_allocate()
 */
// void *alloca(size_t sz) { return malloc(sz); }

///////////////////////////////////////////////////////////////////////////////
// CD_Logic

/*
 * Initiates logging and calls module loading fxs.
 */
void CD_DoomMain() {
  printf("\n-- CELLDOOM LOG START --\n\nCD_DoomMain: Init at %d.\n\n",
         sys_time_get_system_time());
  printf("CD_DoomMain: Loading CellOS FileSystem module...\n");
  CD_FSLoadModule();
  printf("CD_DoomMain: Initializing video...\n");
  //CD_VideoInit();
  return;
};

///////////////////////////////////////////////////////////////////////////////
// CD_Timing

inline cd_systime CD_GetSysTime() { return sys_time_get_system_time(); };

inline void CD_USleep(unsigned long long amount) { sys_timer_usleep(amount); };

///////////////////////////////////////////////////////////////////////////////
// CD_FS

/*
 * Loads the PRX module to access the PlayStation 3 File System.
 */
void CD_FSLoadModule() {
  int ret = cellSysmoduleLoadModule(CELL_SYSMODULE_FS);
  if (ret) {
    printf("-- CELLDOOM TRAP --\n"
           "CD_FSLoadModule: Unable to load module. Reason: 0x%x\n",
           ret);
    exit(-1);
  }
  CellFsStat status;
  printf("CD_FSLoadModule: Mounting");
  for (int i = 0; i < 15; i++) {
    if (cellFsStat(SYS_APP_HOME, &status) == CELL_FS_SUCCEEDED) {
      printf(". Done.\n");
      return;
    }
    sys_timer_sleep(1);
    printf(".");
  }
  printf("-- CELLDOOM TRAP --\n"
         "CD_FSLoadModule: Unable to mount \"\\app_home\". Reason: timeout.");
  exit(-1);
}

/*
 * Determines whether a file is accessible or not and what are its parameters.
 */
int CD_FSTestAccess(const char *fname, CellFsStat *status) {
  int ret;
  printf("CD_FSTestAccess: Trying %-24s ", fname);
  // Refer to fs_external.h for CellFsErrno values.
  ret = cellFsStat(fname, status);
  if (ret == CELL_FS_SUCCEEDED)
    printf(" Succeded!\n\n");
  else
    printf(" Failed. Reason: 0x%x\n", ret);
  return ret;
}

///////////////////////////////////////////////////////////////////////////////
// CD_Video

void CD_VideoInit() {

  // Initialize an SPU that will be assigned to PSGL exclusively to aid with
  // memory transfers.
  //sys_spu_initialize(6, 1);

  CellVideoOutState video_state;

  // Wait for video device to become ready (HDMI only!).
  printf("CD_VideoInit: Waiting for video device");
  do {
    printf(".");
    sys_timer_sleep(1);
    cellVideoOutGetState(CELL_VIDEO_OUT_PRIMARY, 0, &video_state);
  } while (video_state.state != CELL_VIDEO_OUT_OUTPUT_STATE_ENABLED);
  printf(". Done.\n");

  // Does not work (y tho?)
  PSGLdevice *device = psglCreateDeviceAuto(GL_ARGB_SCE, GL_NONE, GL_MULTISAMPLING_NONE_SCE);

  // (5) create context
  PSGLcontext *context = psglCreateContext();
  psglMakeCurrent(context, device);
  psglResetCurrentContext();

  // Init PSGL and draw
  CD_PSGLInit(device);

  // Destroy the context, then the device (before psglExit)
  psglDestroyContext(context);
  psglDestroyDevice(device);
  psglExit();
}

void CD_PSGLInit(PSGLdevice *device) {
  // get render target buffer dimensions and set viewport
  GLuint renderWidth, renderHeight;
  psglGetRenderBufferDimensions(device, &renderWidth, &renderHeight);

  glViewport(0, 0, renderWidth, renderHeight);

  // get display aspect ratio (width / height) and set projection
  // (it is important to use this value and NOT renderWidth/renderHeight since
  // pixel ratios do not necessarily match the 16/9 or 4/3 display aspect
  // ratios)
  GLfloat aspectRatio = psglGetDeviceAspectRatio(device);

  float l = aspectRatio, r = -l, b = -1, t = 1;
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrthof(l, r, b, t, 0, 1);

  glClearColor(0.f, 0.f, 0.f, 1.f);
  glDisable(GL_CULL_FACE);

  // PSGL doesn't clear the screen on startup, so let's do that here.
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  psglSwap();
}

///////////////////////////////////////////////////////////////////////////////
// CD_Memory

/*
void *CD_MemoryAllocate(size_t size) {
  sys_addr_t *maddr;
  // CELLOS Lv2 provides the sys_memory_allocate() facility
  // to allocate memory, which unlike malloc() allocates memory
  // in pages of constant size.
  size_t alloc = SYS_MEMORY_PAGE_SIZE_1M;
  while (size > alloc) {
    alloc += SYS_MEMORY_PAGE_SIZE_1M;
  }
  int ret = sys_memory_allocate(alloc, SYS_MEMORY_PAGE_SIZE_1M, maddr);
  if (ret != CELL_OK) {
    printf("-- CELLDOOM TRAP --\n"
           "Function:\tcelldoom.c\\malloc()\n"
           "Reason:\t%d\n",
           ret);
    for (;;) {
      ; // busy loop
    }
  }
  return (void *)maddr;
}
*/
