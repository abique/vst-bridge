#include <sys/uio.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include <windows.h>

#include </usr/include/vst2.x/aeffectx.h>

#include "../common/common.h"

typedef AEffect *(*plug_main_f)(audioMasterCallback audioMaster);

struct vst_bridge_host {
  int                      logfd;
  int                      socket;
  struct AEffect          *e;
  uint32_t                 next_tag;
  struct VstTimeInfo       time_info;
  union vst_bridge_buffer  buffer;
};

struct vst_bridge_host g_host = {
  -1,
  -1,
  NULL,
  1
};

bool serve_request2(struct vst_bridge_request *rq);

bool wait_response(struct vst_bridge_request *rq,
                   uint32_t tag)
{
  ssize_t len;

  while (true) {
    len = read(g_host.socket, rq, sizeof (*rq));
    if (len <= 0)
      return false;
    assert(len > 8);
    if (rq->tag == tag)
      return true;
    if (!serve_request2(rq))
      return false;
  }
}

bool serve_request2(struct vst_bridge_request *rq)
{
  switch (rq->cmd) {
  case VST_BRIDGE_CMD_EFFECT_DISPATCHER:
    switch (rq->erq.opcode) {
    case effOpen:
    case effClose:
    case effSetProgram:
    case effGetProgram:
    case effSetProgramName:
    case effGetProgramName:
    case effGetOutputProperties:
    case effGetInputProperties:
    case effGetPlugCategory:
    case effGetVstVersion:
    case effGetVendorVersion:
    case effGetEffectName:
    case effGetVendorString:
    case effGetProductString:
    case effCanDo:
    case effSetSampleRate:
    case effSetBlockSize:
    case effMainsChanged:
    case effSetSpeakerArrangement:
    case effGetParamLabel:
    case effGetParamDisplay:
    case effGetParamName:
    case effBeginSetProgram:
    case effEndSetProgram:
    case effStartProcess:
    case effStopProcess:
    case effGetProgramNameIndexed:
    case __effConnectOutputDeprecated:
    case __effConnectInputDeprecated:
    case effEditClose:
    case effEditIdle:
    case effEditKeyUp:
    case effEditKeyDown:
    case effSetEditKnobMode:
      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                           rq->erq.value, rq->erq.data, rq->erq.opt);
      write(g_host.socket, rq, sizeof (*rq));
      return true;

    case effGetChunk: {
      void *ptr;
      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                          rq->erq.value, &ptr, rq->erq.opt);
      if (rq->erq.value > sizeof (*rq) - 8 - sizeof (rq->erq))
        dprintf(g_host.logfd, " !!!!!!!!!!!!!! very big effGetChunk: %d\n", rq->erq.value);
      memcpy(rq->erq.data, ptr, rq->erq.value);
      write(g_host.socket, rq, sizeof (*rq));
      return true;
    }

    case effProcessEvents: {
      struct vst_bridge_midi_events *mes = (struct vst_bridge_midi_events *)rq->erq.data;
      struct VstEvents *ves = (struct VstEvents *)malloc(sizeof (*ves) + mes->nb * sizeof (void*));
      ves->numEvents = mes->nb;
      ves->reserved  = 0;
      struct vst_bridge_midi_event *me = mes->events;
      for (int i = 0; i < mes->nb; ++i) {
        ves->events[i] = (VstEvent*)me;
        me = (struct vst_bridge_midi_event *)(me->data + me->byteSize);
      }

      rq->erq.value = g_host.e->dispatcher(g_host.e, rq->erq.opcode, rq->erq.index,
                                          rq->erq.value, ves, rq->erq.opt);
      free(ves);
      write(g_host.socket, rq, sizeof (*rq));
      return true;
    }

    default:
      dprintf(g_host.logfd, "effectDispatcher unsupported: opcode: %d, index: %d,"
              " value: %d, opt: %f\n", rq->erq.opcode, rq->erq.index,
              rq->erq.value, rq->erq.opt);
      write(g_host.socket, rq, sizeof (*rq));
      return true;
    }

  case VST_BRIDGE_CMD_SET_PARAMETER:
    g_host.e->setParameter(g_host.e, rq->param.index, rq->param.value);
    return true;

  case VST_BRIDGE_CMD_GET_PARAMETER:
    rq->param.value = g_host.e->getParameter(g_host.e, rq->param.index);
    write(g_host.socket, rq, sizeof (*rq));
    return true;

  case VST_BRIDGE_CMD_PROCESS: {
    dprintf(g_host.logfd, "numInputs: %d, numOutputs: %d\n",
            g_host.e->numInputs, g_host.e->numOutputs);

    float *inputs[g_host.e->numInputs];
    float *outputs[g_host.e->numOutputs];

    struct vst_bridge_request *rq2 = (struct vst_bridge_request *)malloc(sizeof (*rq2));
    rq2->cmd = rq->cmd;
    rq2->tag = rq->tag;
    rq2->frames.nframes = rq->frames.nframes;

    for (int i = 0; i < g_host.e->numInputs; ++i)
      inputs[i] = rq->frames.frames + i * rq->frames.nframes;
    for (int i = 0; i < g_host.e->numOutputs; ++i)
      outputs[i] = rq2->frames.frames + i * rq->frames.nframes;

    g_host.e->processReplacing(g_host.e, inputs, outputs, rq->frames.nframes);
    dprintf(g_host.logfd, "cmd: %d, tag: %d\n", rq2->cmd, rq2->tag);
    write(g_host.socket, rq2, sizeof (*rq2));
    free(rq2);
    return true;
  }

  case VST_BRIDGE_CMD_PROCESS_DOUBLE: {
    double *inputs[g_host.e->numInputs];
    double *outputs[g_host.e->numOutputs];

    struct vst_bridge_request *rq2 = (struct vst_bridge_request *)malloc(sizeof (*rq2));
    rq2->cmd = rq->cmd;
    rq2->tag = rq->tag;
    rq2->framesd.nframes = rq->framesd.nframes;

    for (int i = 0; i < g_host.e->numInputs; ++i)
      inputs[i] = rq->framesd.frames + i * rq->framesd.nframes;
    for (int i = 0; i < g_host.e->numOutputs; ++i)
      outputs[i] = rq2->framesd.frames + i * rq->framesd.nframes;

    g_host.e->processDoubleReplacing(g_host.e, inputs, outputs, rq->framesd.nframes);
    write(g_host.socket, rq2, sizeof (*rq2));
    free(rq2);
    return true;
  }

  default:
    dprintf(g_host.logfd, "  !!!!!!!!!!! UNEXPECTED: tag: %d, cmd: %d\n",
            rq->tag, rq->cmd);
    return true;
  }
}

bool serve_request(void)
{
  uint32_t tag;
  struct vst_bridge_request & rq = g_host.buffer.rq;
  ssize_t len = read(g_host.socket, &g_host.buffer, sizeof (g_host.buffer));
  if (len <= 0)
    return false;

  sigset_t _signals;
  sigemptyset(&_signals);
  sigaddset(&_signals, SIGHUP);
  sigaddset(&_signals, SIGINT);
  sigaddset(&_signals, SIGQUIT);
  sigaddset(&_signals, SIGPIPE);
  sigaddset(&_signals, SIGTERM);
  sigaddset(&_signals, SIGUSR1);
  sigaddset(&_signals, SIGUSR2);
  sigaddset(&_signals, SIGCHLD);
  sigaddset(&_signals, SIGALRM);
  sigaddset(&_signals, SIGURG);
  pthread_sigmask(SIG_BLOCK, &_signals, 0);
  bool ret = serve_request2(&rq);
  pthread_sigmask(SIG_UNBLOCK, &_signals, 0);

  return ret;
}

VstIntPtr VSTCALLBACK host_audio_master(AEffect*  effect,
                                        VstInt32  opcode,
                                        VstInt32  index,
                                        VstIntPtr value,
                                        void*     ptr,
                                        float     opt)
{
  ssize_t len;
  struct vst_bridge_request *rq = (struct vst_bridge_request *)malloc(sizeof (*rq));;

  switch (opcode) {
    /* basic forward */
  case audioMasterAutomate:
  case audioMasterVersion:
  case audioMasterCurrentId:
  case audioMasterIdle:
  case audioMasterIOChanged:
  case audioMasterSizeWindow:
  case audioMasterGetSampleRate:
  case audioMasterGetBlockSize:
  case audioMasterGetInputLatency:
  case audioMasterGetOutputLatency:
  case audioMasterGetCurrentProcessLevel:
  case audioMasterGetAutomationState:
  case __audioMasterWantMidiDeprecated:
    rq->tag = g_host.next_tag;
    rq->cmd = VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK;
    rq->amrq.opcode = opcode;
    rq->amrq.index = index;
    rq->amrq.value = value;
    rq->amrq.opt = opt;
    g_host.next_tag += 2;

    write(g_host.socket, rq, sizeof (*rq));
    wait_response(rq, rq->tag);
    value = rq->amrq.value;
    break;

  case audioMasterGetTime:
    rq->tag = g_host.next_tag;
    rq->cmd = VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK;
    rq->amrq.opcode = opcode;
    rq->amrq.index = index;
    rq->amrq.value = value;
    rq->amrq.opt = opt;
    g_host.next_tag += 2;

    write(g_host.socket, rq, sizeof (*rq));
    wait_response(rq, rq->tag);
    value = rq->amrq.value;
    if (!value)
      break;
    memcpy(&g_host.time_info, rq->amrq.data, sizeof (g_host.time_info));
    value = (VstIntPtr)&g_host.time_info;
    break;

  case audioMasterGetProductString:
    rq->tag           = g_host.next_tag;
    rq->cmd           = VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK;
    rq->amrq.opcode   = opcode;
    rq->amrq.index    = index;
    rq->amrq.value    = value;
    rq->amrq.opt      = opt;
    g_host.next_tag += 2;

    write(g_host.socket, rq, sizeof (*rq));
    if (!wait_response(rq, rq->tag)) {
      value = 0;
      break;
    }
    strcpy((char*)ptr, (const char*)rq->amrq.data);
    dprintf(g_host.logfd, "audioMasterGetProductString: %s\n", (char*)ptr);
    value = rq->amrq.value;
    break;

  default:
    value = 0;
    dprintf(g_host.logfd, "audioMaster unsupported: opcode: %d, index: %d,"
            " value: %d, ptr: %p, opt: %f\n", opcode, index, value, ptr, opt);
    break;
  }

  free(rq);
  return value;
}

int main(int argc, char **argv)
{
  HMODULE module;
  const char *plugin_path = argv[1];

  module = LoadLibrary(plugin_path);
  if (!module) {
    fprintf(stderr, "failed to load %s: %m\n", plugin_path);
    return 1;
  }

  g_host.logfd = open("/tmp/vst-bridge-host.log",
                      O_CREAT | O_TRUNC | O_APPEND | O_WRONLY, 0644);
  if (g_host.logfd < 0)
    return 1;

  // check the channel
  g_host.socket = atoi(argv[2]);
  ssize_t rbytes = read(g_host.socket, &g_host.buffer, sizeof (g_host.buffer));
  assert(g_host.buffer.rq.cmd == VST_BRIDGE_CMD_PLUGIN_MAIN);

  // get the plugin entry
  plug_main_f plug_main = NULL;
  plug_main = (plug_main_f)GetProcAddress((HMODULE)module, "VSTPluginMain");

  // init pluging
  g_host.e = plug_main(host_audio_master);

  if (!g_host.e) {
    dprintf(g_host.logfd, "failed to initialize plugin\n");
    return 1;
  }

  // send plugin main finished
  {
    struct vst_bridge_request vbr;
    vbr.tag = 0;
    vbr.cmd = VST_BRIDGE_CMD_PLUGIN_MAIN;
    vbr.plugin_data.hasSetParameter           = g_host.e->setParameter;
    vbr.plugin_data.hasGetParameter           = g_host.e->getParameter;
    vbr.plugin_data.hasProcessReplacing       = g_host.e->processReplacing;
    vbr.plugin_data.hasProcessDoubleReplacing = g_host.e->processDoubleReplacing;
    vbr.plugin_data.numPrograms               = g_host.e->numPrograms;
    vbr.plugin_data.numParams                 = g_host.e->numParams;
    vbr.plugin_data.numInputs                 = g_host.e->numInputs;
    vbr.plugin_data.numOutputs                = g_host.e->numOutputs;
    vbr.plugin_data.flags                     = g_host.e->flags;
    vbr.plugin_data.initialDelay              = g_host.e->initialDelay;
    vbr.plugin_data.uniqueID                  = g_host.e->uniqueID;
    vbr.plugin_data.version                   = g_host.e->version;
    write(g_host.socket, &vbr, sizeof (vbr));
  }

  // serve requests
  while (serve_request())
    ;

  FreeLibrary(module);
  return 0;
}
