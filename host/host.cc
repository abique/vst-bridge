#include <sys/uio.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <wchar.h>
#include <pthread.h>
#include <poll.h>

#include <list>

#include <windows.h>

#ifdef _WIN64
# ifndef __LP64__
#  define __LP64__
# endif
#endif

#include "../vstsdk2.4/pluginterfaces/vst2.x/aeffectx.h"

#include "../common/common.h"

#define APPLICATION_CLASS_NAME "VST-BRIDGE"

#define VST_BRIDGE_WMSG_IO 19041
#define VST_BRIDGE_WMSG_EDIT_OPEN 19042

#if 0
# define LOG(Args...) fprintf(stderr, Args)
#else
# define LOG(Args...)
#endif

#define CRIT(Args...) fprintf(stderr, Args)

typedef AEffect *(VSTCALLBACK *plug_main_f)(audioMasterCallback audioMaster);

struct vst_bridge_host {
  typedef std::list<vst_bridge_request> pending_type;

  int                            socket;
  struct AEffect                *e;
  uint32_t                       next_tag;
  bool                           stop;
  struct VstEvents              *ves;
  struct VstTimeInfo             time_info;
  HWND                           hwnd;
  DWORD                          main_thread_id;
  pthread_mutex_t                lock;
  pending_type                   pending;
  struct vst_bridge_plugin_data  plugin_data;
};

struct vst_bridge_host g_host = {
  -1,
  NULL,
  1,
  false,
  NULL
};

void copy_plugin_data(void)
{
  g_host.plugin_data.hasSetParameter           = g_host.e->setParameter;
  g_host.plugin_data.hasGetParameter           = g_host.e->getParameter;
  g_host.plugin_data.hasProcessReplacing       = g_host.e->processReplacing;
  g_host.plugin_data.hasProcessDoubleReplacing = g_host.e->processDoubleReplacing;
  g_host.plugin_data.numPrograms               = g_host.e->numPrograms;
  g_host.plugin_data.numParams                 = g_host.e->numParams;
  g_host.plugin_data.numInputs                 = g_host.e->numInputs;
  g_host.plugin_data.numOutputs                = g_host.e->numOutputs;
  g_host.plugin_data.flags                     = g_host.e->flags;
  g_host.plugin_data.initialDelay              = g_host.e->initialDelay;
  g_host.plugin_data.uniqueID                  = g_host.e->uniqueID;
  g_host.plugin_data.version                   = g_host.e->version;
}

void check_plugin_data(void)
{
  if (!g_host.e)
    return;

#define CHECK_FIELD(X) (g_host.plugin_data.X != g_host.e->X)
  if (CHECK_FIELD(numPrograms) ||
      CHECK_FIELD(numParams) ||
      CHECK_FIELD(numInputs) ||
      CHECK_FIELD(numOutputs) ||
      CHECK_FIELD(flags) ||
      CHECK_FIELD(initialDelay) ||
      CHECK_FIELD(uniqueID) ||
      CHECK_FIELD(version)) {
    copy_plugin_data();

    struct vst_bridge_request rq;
    rq.tag = 0;
    rq.cmd = VST_BRIDGE_CMD_PLUGIN_DATA;
    memcpy(&rq.plugin_data, &g_host.plugin_data, sizeof (rq.plugin_data));
    write(g_host.socket, &rq, 8 + sizeof (rq.plugin_data));
  }
#undef CHECK_FIELD(X)
}

bool serve_request2(struct vst_bridge_request *rq);

bool wait_response(struct vst_bridge_request *rq,
                   uint32_t tag)
{
  ssize_t len;

  while (true) {
    for (vst_bridge_host::pending_type::iterator it = g_host.pending.begin();
         it != g_host.pending.end(); ++it) {
      if (it->tag == tag) {
        *rq = *it;
        g_host.pending.erase(it);
        return true;
      }
    }
    len = read(g_host.socket, rq, sizeof (*rq));
    if (len <= 0)
      return false;
    assert(len > 8);
    if (rq->tag == tag)
      return true;
    if (rq->cmd != VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK) {
      serve_request2(rq);
      continue;
    }
    g_host.pending.push_back(*rq);
  }
}

bool serve_request2(struct vst_bridge_request *rq)
{
  LOG("[%p] serve request: tag: %d, cmd: %d\n",
      pthread_self(), rq->tag, rq->cmd);

  switch (rq->cmd) {
  case VST_BRIDGE_CMD_EFFECT_DISPATCHER:
    switch (rq->erq.opcode) {
    case __effIdleDeprecated:
    case effEditIdle:
    case effSetSampleRate:
    case effSetBlockSize:
    case effSetProgram:
    case effGetProgram:
    case effOpen:
    case effSetProgramName:
    case __effConnectOutputDeprecated:
    case __effConnectInputDeprecated:
    case effGetVstVersion:
    case effGetPlugCategory:
    case effEditClose:
    case effMainsChanged:
    case effStartProcess:
    case effStopProcess:
    case effSetTotalSampleToProcess:
    case effSetPanLaw:
    case effSetProcessPrecision:
    case effGetNumMidiInputChannels:
    case effGetNumMidiOutputChannels:
    case effEditKeyUp:
    case effEditKeyDown:
    case effSetEditKnobMode:
    case effBeginSetProgram:
    case effEndSetProgram:
    case effGetVendorVersion:
      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                           rq->erq.value, rq->erq.data, rq->erq.opt);
      write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(0));
      return true;

    case effGetOutputProperties:
    case effGetInputProperties:
      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                           rq->erq.value, rq->erq.data, rq->erq.opt);
      write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(sizeof (VstPinProperties)));
      return true;

    case effGetParameterProperties:
      memset(rq->erq.data, 0, sizeof (VstParameterProperties));
      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                           rq->erq.value, rq->erq.data, rq->erq.opt);
      write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(sizeof (VstParameterProperties)));
      return true;

    case effGetParamLabel:
    case effGetParamDisplay:
    case effGetParamName:
    case effGetProgramName:
    case effGetEffectName:
    case effGetVendorString:
    case effGetProductString:
    case effGetProgramNameIndexed:
    case effCanDo:
      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                           rq->erq.value, rq->erq.data, rq->erq.opt);
      write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(strlen((char *)rq->erq.data) + 1));
      return true;

    case effClose:
      // quit
      g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                           rq->erq.value, rq->erq.data, rq->erq.opt);
      exit(0);
      return true;

    case effEditOpen: {
      rq->erq.value = g_host.e->dispatcher(g_host.e, effEditOpen, 0, 0, g_host.hwnd, 0);
      write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(0));
      ERect * rect = NULL;
      g_host.e->dispatcher(g_host.e, effEditGetRect, 0, 0, &rect, 0);
      if (rect) {
        SetWindowPos(g_host.hwnd, 0, 0, 0,
                     rect->right + 6,
                     rect->bottom + 22,
                     SWP_NOACTIVATE | SWP_NOMOVE);
        // No borders: SWP_NOOWNERZORDER | SWP_NOZORDER);
      }
      ShowWindow(g_host.hwnd, SW_SHOWNORMAL);
      UpdateWindow(g_host.hwnd);
      return true;
    }

    case effEditGetRect: {
      ERect * rect = NULL;
      rq->erq.value = g_host.e->dispatcher(g_host.e, effEditGetRect, 0, 0, &rect, 0);
      if (rect)
        memcpy(rq->erq.data, rect, sizeof (*rect));
      write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(sizeof (*rect)));
      return true;
    }

    case effSetSpeakerArrangement:
      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                           VstIntPtr(rq->erq.data), rq->erq.data,
                                           rq->erq.opt);
      write(g_host.socket, rq, sizeof (*rq));
      return true;

    case effGetChunk: {
      void *ptr;
      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                           rq->erq.value, &ptr, rq->erq.opt);
      for (size_t off = 0; off < rq->erq.value; ) {
        size_t can_write = MIN(VST_BRIDGE_CHUNK_SIZE, rq->erq.value - off);
        memcpy(rq->erq.data, ptr + off, can_write);
        off += can_write;
        write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(can_write));
      }
      return true;
    }

    case effSetChunk: {
      void *data = malloc(rq->erq.value);
      if (!data && rq->erq.value > 0) {
        write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(0));
        return true;
      }

      for (size_t off = 0; off < rq->erq.value; ) {
        size_t can_read = MIN(VST_BRIDGE_CHUNK_SIZE, rq->erq.value - off);
        memcpy(data + off, rq->erq.data, can_read);
        off += can_read;
        if (off == rq->erq.value)
          break;
        if (!wait_response(rq, rq->tag))
          return 0;
      }
      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                           rq->erq.value, data, rq->erq.opt);
      write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(0));
      free(data);
      return true;
    }

    case effProcessEvents: {
      struct vst_bridge_midi_events *mes = (struct vst_bridge_midi_events *)rq->erq.data;
      struct VstEvents *ves = (struct VstEvents *)realloc(
        (void*)g_host.ves, sizeof (*ves) + mes->nb * sizeof (void*));
      assert(ves);
      ves->numEvents = mes->nb;
      ves->reserved  = 0;
      struct vst_bridge_midi_event *me = mes->events;
      for (int i = 0; i < mes->nb; ++i) {
        ves->events[i] = (VstEvent*)me;
        me = (struct vst_bridge_midi_event *)(me->data + me->byteSize);
      }

      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                           rq->erq.value, ves, rq->erq.opt);
      write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(0));
      return true;
    }

    case effVendorSpecific:
      switch (rq->erq.index) {
      case effGetParamDisplay:
        rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                             rq->erq.value, rq->erq.data, rq->erq.opt);
        write(g_host.socket, rq, VST_BRIDGE_ERQ_LEN(strlen((const char *)rq->erq.data) + 1));
        return true;
      }
      return true;

    default:
      CRIT(" !!!!!!!!!! effectDispatcher unsupported: opcode: (%s, %d), index: %d,"
           " value: %d, opt: %f\n", vst_bridge_effect_opcode_name[rq->erq.opcode],
           rq->erq.opcode, rq->erq.index, rq->erq.value, rq->erq.opt);
      write(g_host.socket, rq, sizeof (*rq));
      return true;
    }

  case VST_BRIDGE_CMD_SET_PARAMETER:
    g_host.e->setParameter(g_host.e, rq->param.index, rq->param.value);
    return true;

  case VST_BRIDGE_CMD_GET_PARAMETER:
    rq->param.value = g_host.e->getParameter(g_host.e, rq->param.index);
    write(g_host.socket, rq, VST_BRIDGE_PARAM_LEN);
    return true;

  case VST_BRIDGE_CMD_PROCESS: {
    float *inputs[g_host.e->numInputs];
    float *outputs[g_host.e->numOutputs];

    struct vst_bridge_request rq2;
    rq2.cmd = rq->cmd;
    rq2.tag = rq->tag;
    rq2.frames.nframes = rq->frames.nframes;

    for (int i = 0; i < g_host.e->numInputs; ++i)
      inputs[i] = rq->frames.frames + i * rq->frames.nframes;
    for (int i = 0; i < g_host.e->numOutputs; ++i)
      outputs[i] = rq2.frames.frames + i * rq->frames.nframes;

    g_host.e->processReplacing(g_host.e, inputs, outputs, rq->frames.nframes);
    write(g_host.socket, &rq2,
          VST_BRIDGE_FRAMES_LEN(g_host.e->numOutputs * rq->framesd.nframes));
    return true;
  }

  case VST_BRIDGE_CMD_PROCESS_DOUBLE: {
    double *inputs[g_host.e->numInputs];
    double *outputs[g_host.e->numOutputs];

    struct vst_bridge_request rq2;
    rq2.cmd = rq->cmd;
    rq2.tag = rq->tag;
    rq2.framesd.nframes = rq->framesd.nframes;

    for (int i = 0; i < g_host.e->numInputs; ++i)
      inputs[i] = rq->framesd.frames + i * rq->framesd.nframes;
    for (int i = 0; i < g_host.e->numOutputs; ++i)
      outputs[i] = rq2.framesd.frames + i * rq->framesd.nframes;

    g_host.e->processDoubleReplacing(g_host.e, inputs, outputs, rq->framesd.nframes);
    write(g_host.socket, &rq2,
          VST_BRIDGE_FRAMES_DOUBLE_LEN(g_host.e->numOutputs * rq->framesd.nframes));
    return true;
  }

  case VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK:
    CRIT("  !!!!!!!!!!! UNEXPECTED AMC: tag: %d, opcode: %d\n",
        rq->tag, rq->amrq.opcode);
    return true;

  default:
    CRIT("  !!!!!!!!!!! UNEXPECTED CMD: tag: %d, cmd: %d\n", rq->tag, rq->cmd);
    return true;
  }
}

bool serve_request(void)
{
  uint32_t tag;
  struct vst_bridge_request rq;

  pthread_mutex_lock(&g_host.lock);

  ssize_t len = read(g_host.socket, &rq, sizeof (rq));
  if (len <= 0) {
    pthread_mutex_unlock(&g_host.lock);
    return false;
  }

  bool ret = serve_request2(&rq);
  check_plugin_data();

  pthread_mutex_unlock(&g_host.lock);
  return ret;
}

VstIntPtr VSTCALLBACK host_audio_master2(AEffect*  effect,
                                         VstInt32  opcode,
                                         VstInt32  index,
                                         VstIntPtr value,
                                         void*     ptr,
                                         float     opt)
{
  ssize_t len;
  struct vst_bridge_request rq;

  LOG("[%p] host_audio_master(%d, %d, %d, %p, %f) => %d\n",
      pthread_self(), opcode, index, value, ptr, opt, g_host.next_tag);

  switch (opcode) {
    // no additional data
  case audioMasterAutomate:
  case audioMasterVersion:
  case audioMasterCurrentId:
  case audioMasterIdle:
  case __audioMasterPinConnectedDeprecated:
  case audioMasterIOChanged:
  case audioMasterSizeWindow:
  case audioMasterGetSampleRate:
  case audioMasterGetBlockSize:
  case audioMasterGetInputLatency:
  case audioMasterGetOutputLatency:
  case audioMasterGetCurrentProcessLevel:
  case audioMasterGetAutomationState:
  case __audioMasterWantMidiDeprecated:
  case __audioMasterNeedIdleDeprecated:
  case audioMasterGetVendorVersion:
    rq.tag           = g_host.next_tag;
    rq.cmd           = VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK;
    rq.amrq.opcode   = opcode;
    rq.amrq.index    = index;
    rq.amrq.value    = value;
    rq.amrq.opt      = opt;
    g_host.next_tag += 2;

    write(g_host.socket, &rq, VST_BRIDGE_AMRQ_LEN(0));
    wait_response(&rq, rq.tag);
    return rq.amrq.value;

  case audioMasterCanDo:
    rq.tag           = g_host.next_tag;
    rq.cmd           = VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK;
    rq.amrq.opcode   = opcode;
    rq.amrq.index    = index;
    rq.amrq.value    = value;
    rq.amrq.opt      = opt;
    g_host.next_tag += 2;
    strcpy((char*)rq.amrq.data, (char*)ptr);

    write(g_host.socket, &rq, VST_BRIDGE_AMRQ_LEN(strlen((char*)ptr) + 1));
    wait_response(&rq, rq.tag);
    return rq.amrq.value;

  case __audioMasterTempoAtDeprecated:
  case audioMasterUpdateDisplay:
  case audioMasterBeginEdit:
  case audioMasterEndEdit:
    rq.tag           = g_host.next_tag;
    rq.cmd           = VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK;
    rq.amrq.opcode   = opcode;
    rq.amrq.index    = index;
    rq.amrq.value    = value;
    rq.amrq.opt      = opt;
    g_host.next_tag += 2;

    write(g_host.socket, &rq, sizeof (rq));
    wait_response(&rq, rq.tag);
    return rq.amrq.value;

  case audioMasterProcessEvents: {
    struct VstEvents *evs = (struct VstEvents *)ptr;
    struct vst_bridge_midi_events *mes = (struct vst_bridge_midi_events *)rq.erq.data;

    rq.tag           = g_host.next_tag;
    rq.cmd           = VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK;
    rq.amrq.opcode   = opcode;
    rq.amrq.index    = index;
    rq.amrq.value    = value;
    rq.amrq.opt      = opt;
    g_host.next_tag += 2;

    mes->nb = evs->numEvents;
    struct vst_bridge_midi_event *me = mes->events;
    for (int i = 0; i < evs->numEvents; ++i) {
      memcpy(me, evs->events[i], sizeof (*me) + evs->events[i]->byteSize);
      me = (struct vst_bridge_midi_event *)(me->data + me->byteSize);
    }

    write(g_host.socket, &rq, ((uint8_t*)me) - ((uint8_t*)&rq));
    wait_response(&rq, rq.tag);
    return rq.amrq.value;
  }

  case audioMasterGetTime:
    rq.tag           = g_host.next_tag;
    rq.cmd           = VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK;
    rq.amrq.opcode   = opcode;
    rq.amrq.index    = index;
    rq.amrq.value    = value;
    rq.amrq.opt      = opt;
    g_host.next_tag += 2;

    write(g_host.socket, &rq, VST_BRIDGE_AMRQ_LEN(0));
    wait_response(&rq, rq.tag);
    if (!rq.amrq.value)
      return 0;
    memcpy(&g_host.time_info, rq.amrq.data, sizeof (g_host.time_info));
    return VstIntPtr(&g_host.time_info);

  case audioMasterGetProductString:
  case audioMasterGetVendorString:
    rq.tag           = g_host.next_tag;
    rq.cmd           = VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK;
    rq.amrq.opcode   = opcode;
    rq.amrq.index    = index;
    rq.amrq.value    = value;
    rq.amrq.opt      = opt;
    g_host.next_tag += 2;

    write(g_host.socket, &rq, VST_BRIDGE_AMRQ_LEN(0));
    if (!wait_response(&rq, rq.tag))
      return 0;
    strcpy((char*)ptr, (const char*)rq.amrq.data);
    return rq.amrq.value;

  case audioMasterOpenFileSelector:
    return false;

  default:
    CRIT("  !!!!!!!!!!!!!! audioMaster unsupported: opcode: (%s, %d), index: %d,"
         " value: %d, ptr: %p, opt: %f\n",
         vst_bridge_audio_master_opcode_name[opcode], opcode, index, value, ptr, opt);
    return 0;
  }
}

VstIntPtr VSTCALLBACK host_audio_master(AEffect*  effect,
                                        VstInt32  opcode,
                                        VstInt32  index,
                                        VstIntPtr value,
                                        void*     ptr,
                                        float     opt)
{
  pthread_mutex_lock(&g_host.lock);
  check_plugin_data();
  VstIntPtr ret = host_audio_master2(effect, opcode, index, value, ptr, opt);
  check_plugin_data();
  pthread_mutex_unlock(&g_host.lock);
  LOG("  => audio master finished\n");
  return ret;
}

LRESULT WINAPI
MainProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg) {
  case WM_CLOSE:
    ShowWindow(g_host.hwnd, SW_HIDE);
    return TRUE;
  }

  return DefWindowProc(hWnd, msg, wParam, lParam);
}

DWORD WINAPI vst_bridge_audio_thread(void *arg)
{
  while (!g_host.stop)
    if (!serve_request())
      break;
  g_host.stop = true;
  return 0;
}

int main(int argc, char **argv)
{
  HMODULE module;
  const char *plugin_path = argv[1];

  g_host.main_thread_id = GetCurrentThreadId();
  {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_host.lock, &attr);
    pthread_mutexattr_destroy(&attr);
  }

  module = LoadLibrary(plugin_path);
  if (!module) {
    fprintf(stderr, "failed to load %s: %m\n", plugin_path);
    return 1;
  }

  // check the channel
  g_host.socket = atoi(argv[2]);
  {
    struct vst_bridge_request rq;
    read(g_host.socket, &rq, sizeof (rq));
    assert(rq.cmd == VST_BRIDGE_CMD_PLUGIN_MAIN);
  }

  // get the plugin entry
  plug_main_f plug_main = NULL;
  plug_main = (plug_main_f)GetProcAddress((HMODULE)module, "VSTPluginMain");

  if (!plug_main) {
    plug_main = (plug_main_f)GetProcAddress((HMODULE)module, "main");
    if (!plug_main) {
      fprintf(stderr, "failed to find entry symbol in %s\n", plugin_path);
      return 1;
    }
  }

  // init pluging
  g_host.e = plug_main(host_audio_master);

  if (!g_host.e) {
    LOG("failed to initialize plugin\n");
    return 1;
  }

  // send plugin main finished
  {
    copy_plugin_data();

    struct vst_bridge_request rq;
    rq.tag = 0;
    rq.cmd = VST_BRIDGE_CMD_PLUGIN_MAIN;
    memcpy(&rq.plugin_data, &g_host.plugin_data, sizeof (rq.plugin_data));
    write(g_host.socket, &rq, sizeof (rq));
  }

  WNDCLASSEX wclass;
  wclass.cbSize        = sizeof (WNDCLASSEX);
  wclass.style         = 0;
  wclass.lpfnWndProc   = MainProc;
  wclass.cbClsExtra    = 0;
  wclass.cbWndExtra    = 0;
  wclass.hInstance     = GetModuleHandle(NULL);
  wclass.hIcon         = LoadIcon(GetModuleHandle(NULL), APPLICATION_CLASS_NAME);
  wclass.hCursor       = LoadCursor(0, IDI_APPLICATION);
  wclass.lpszMenuName  = "MENU_VST_BRIDGE";
  wclass.lpszClassName = APPLICATION_CLASS_NAME;
  wclass.hIconSm       = 0;

  if (!RegisterClassEx(&wclass))
    LOG("failed to register Windows application class\n");

  g_host.hwnd = CreateWindow(APPLICATION_CLASS_NAME,
                             "app name",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             0, 0, GetModuleHandle(NULL), 0);
  if (!g_host.hwnd)
    LOG("failed to create window\n");

  // HANDLE audio_thread = CreateThread(
  //   NULL, 8 * 1024 * 1024, vst_bridge_audio_thread, NULL, 0, NULL);
  // if (!audio_thread) {
  //   dprintf(g_host.logfd, "failed to create audio thread: %m\n");
  //   return 1;
  // }

  sleep(1);

  struct pollfd pfd;
  MSG msg;

  while (true) {
    pfd.fd = g_host.socket;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 50);
    if (pfd.revents & POLLIN &&
        !serve_request())
      break;

    while (GetQueueStatus(QS_ALLINPUT)) {
      if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        DispatchMessage(&msg);
    }
  }

  FreeLibrary(module);
  return 0;
}
