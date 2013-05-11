#include <sys/types.h>
#include <sys/socket.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define __cdecl

#include "../config.h"
#include "../common/common.h"

const char g_plugin_path[PATH_MAX] = VST_BRIDGE_TPL_PLUGIN_PATH;
const char g_host_path[PATH_MAX] = INSTALL_PREFIX "/lib/vst-bridge/vst-bridge-host-32.exe";

#include <vst2.x/aeffectx.h>

struct vst_bridge_effect {
  struct AEffect           e;
  int                      socket;
  pid_t                    child;
  uint32_t                 next_tag;
  audioMasterCallback      audio_master;
  int                      logfd;
  void                    *chunk;
};

bool vst_bridge_handle_audio_master(struct vst_bridge_effect *vbe,
                                    struct vst_bridge_request *rq)
{
  dprintf(vbe->logfd, "audio_master: opcode: %d, tag: %d\n",
          rq->amrq.opcode, rq->tag);

  switch (rq->amrq.opcode) {
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
  case audioMasterGetProductString:
    rq->amrq.value = vbe->audio_master(&vbe->e, rq->amrq.opcode, rq->amrq.index,
                                       rq->amrq.value, rq->amrq.data, rq->amrq.opt);
    break;

  case audioMasterGetTime: {
    VstTimeInfo *time_info = (VstTimeInfo *)vbe->audio_master(
      &vbe->e, rq->amrq.opcode, rq->amrq.index, rq->amrq.value, rq->amrq.data,
      rq->amrq.opt);
    if (!time_info)
      rq->amrq.value = 0;
    else {
      rq->amrq.value = 1;
      memcpy(rq->amrq.data, time_info, sizeof (*time_info));
    }
    break;
  }

  default:
    dprintf(vbe->logfd, "audio master callback (unhandled): op: %d,"
            " index: %d, value: %d, opt: %f\n",
            rq->amrq.opcode, rq->amrq.index, rq->amrq.value, rq->amrq.opt);
    break;
  }
  write(vbe->socket, rq, sizeof (*rq));
}

bool vst_bridge_wait_response(struct vst_bridge_effect *vbe,
                              struct vst_bridge_request *rq,
                              uint32_t tag)
{
  ssize_t len;

  while (true) {
    dprintf(vbe->logfd, " ==> wait for tag: %d\n", tag);
    len = ::read(vbe->socket, rq, sizeof (*rq));
    if (len <= 0)
      return false;
    assert(len > 8);
    if (len > sizeof (*rq)) {
      dprintf(vbe->logfd, "  !!!!!!!!!!!!!!!!!!!!!!! got big len: %d\n", len);
      assert(len <= sizeof (*rq));
    }
    if (rq->tag == tag) {
      dprintf(vbe->logfd, "     ==> got expected tag: %d\n", tag);
      return true;
    }
    // handle request
    if (rq->cmd != VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK)
      dprintf(vbe->logfd, " !!!!!!!!!!! cmd: %d, wtag: %d, gtag: %d, op: %d,"
              " index: %d, value: %d, opt: %f\n",
              rq->cmd, tag, rq->tag, rq->amrq.opcode, rq->amrq.index, rq->amrq.value, rq->amrq.opt);
    assert(rq->cmd == VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK);
    vst_bridge_handle_audio_master(vbe, rq);
  }
}

void vst_bridge_call_process(AEffect* effect,
                             float**  inputs,
                             float**  outputs,
                             VstInt32 sampleFrames)
{
  struct vst_bridge_effect *vbe = (struct vst_bridge_effect *)effect->user;
  struct vst_bridge_request rq;

  rq.tag             = vbe->next_tag;
  rq.cmd             = VST_BRIDGE_CMD_PROCESS;
  rq.frames.nframes  = sampleFrames;
  vbe->next_tag     += 2;

  for (int i = 0; i < vbe->e.numInputs; ++i)
    memcpy(rq.frames.frames + i * sampleFrames, inputs[i],
           sizeof (float) * sampleFrames);

  write(vbe->socket, &rq, sizeof (rq));

  dprintf(vbe->logfd, "waiting for tag: %d\n", rq.tag);

  vst_bridge_wait_response(vbe, &rq, rq.tag);

  dprintf(vbe->logfd, "got tag: %d\n", rq.tag);

  for (int i = 0; i < vbe->e.numOutputs; ++i)
    memcpy(outputs[i], rq.frames.frames + i * sampleFrames,
           sizeof (float) * sampleFrames);
}

void vst_bridge_call_process_double(AEffect* effect,
                                    double**  inputs,
                                    double**  outputs,
                                    VstInt32 sampleFrames)
{
  struct vst_bridge_effect *vbe = (struct vst_bridge_effect *)effect->user;
  struct vst_bridge_request rq;

  rq.tag              = vbe->next_tag;
  rq.cmd              = VST_BRIDGE_CMD_PROCESS_DOUBLE;
  rq.framesd.nframes  = sampleFrames;
  vbe->next_tag      += 2;

  for (int i = 0; i < vbe->e.numInputs; ++i)
    memcpy(rq.framesd.frames + i * sampleFrames, inputs[i],
           sizeof (double) * sampleFrames);

  write(vbe->socket, &rq, sizeof (rq));
  vst_bridge_wait_response(vbe, &rq, rq.tag);
  for (int i = 0; i < vbe->e.numOutputs; ++i)
    memcpy(outputs[i], rq.framesd.frames + i * sampleFrames,
           sizeof (double) * sampleFrames);
}

float vst_bridge_call_get_parameter(AEffect* effect,
                                    VstInt32 index)
{
  struct vst_bridge_effect *vbe = (struct vst_bridge_effect *)effect->user;
  struct vst_bridge_request rq;
  rq.tag         = vbe->next_tag;
  rq.cmd         = VST_BRIDGE_CMD_GET_PARAMETER;
  rq.param.index = index;
  vbe->next_tag += 2;

  write(vbe->socket, &rq, sizeof (rq));
  vst_bridge_wait_response(vbe, &rq, rq.tag);
  return rq.param.value;
}

void vst_bridge_call_set_parameter(AEffect* effect,
                                   VstInt32 index,
                                   float    parameter)
{
  struct vst_bridge_effect *vbe = (struct vst_bridge_effect *)effect->user;
  struct vst_bridge_request rq;
  rq.tag         = vbe->next_tag;
  rq.cmd         = VST_BRIDGE_CMD_SET_PARAMETER;
  rq.param.index = index;
  rq.param.value = parameter;
  vbe->next_tag += 2;

  write(vbe->socket, &rq, sizeof (rq));
}

VstIntPtr vst_bridge_call_effect_dispatcher(AEffect*  effect,
                                            VstInt32  opcode,
                                            VstInt32  index,
                                            VstIntPtr value,
                                            void*     ptr,
                                            float     opt)
{
  struct vst_bridge_effect *vbe = (struct vst_bridge_effect *)effect->user;
  struct vst_bridge_request rq;
  ssize_t len;

  switch (opcode) {
  case effOpen:
  case effClose:
  case effSetProgram:
  case effGetProgram:
  case effGetOutputProperties:
  case effGetInputProperties:
  case effGetPlugCategory:
  case effGetVstVersion:
  case effGetVendorVersion:
  case effSetSampleRate:
  case effSetBlockSize:
  case effMainsChanged:
  case effBeginSetProgram:
  case effEndSetProgram:
  case effStartProcess:
  case effStopProcess:
  case __effConnectOutputDeprecated:
  case __effConnectInputDeprecated:
  case effEditClose:
  case effEditIdle:
  case effEditKeyUp:
  case effEditKeyDown:
  case effSetEditKnobMode:
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, sizeof (rq));
    vst_bridge_wait_response(vbe, &rq, rq.tag);
    return rq.amrq.value;

  case effSetProgramName:
  case effGetProgramName:
  case effGetParamLabel:
  case effGetParamDisplay:
  case effGetParamName:
  case effGetEffectName:
  case effGetVendorString:
  case effGetProductString:
  case effGetProgramNameIndexed:
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, sizeof (rq));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;
    strcpy((char*)ptr, (const char *)rq.erq.data);
    return rq.amrq.value;

  case effCanDo:
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;
    strcpy((char*)rq.erq.data, (const char *)ptr);

    write(vbe->socket, &rq, sizeof (rq));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;
    return rq.erq.value;

  case effGetChunk: {
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, sizeof (rq));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;
    void *chunk = realloc(vbe->chunk, rq.erq.value);
    if (!chunk)
      return 0;
    vbe->chunk = chunk;
    memcpy(vbe->chunk, rq.erq.data, rq.erq.value);
    *((void **)ptr) = chunk;
    return rq.erq.value;
  }

  case effSetSpeakerArrangement: {
    struct VstSpeakerArrangement *ar = (struct VstSpeakerArrangement *)ptr;
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;
    memcpy(rq.erq.data, ptr, 8 + ar->numChannels * sizeof (ar->speakers[0]));

    write(vbe->socket, &rq, sizeof (rq));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;
    memcpy(ptr, rq.erq.data, 8 + ar->numChannels * sizeof (ar->speakers[0]));
    return rq.amrq.value;
  }

  case effProcessEvents: {
    // compute the size
    struct VstEvents *evs = (struct VstEvents *)ptr;
    struct vst_bridge_midi_events *mes = (struct vst_bridge_midi_events *)rq.erq.data;
    if (!mes)
      return 0;

    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    mes->nb = evs->numEvents;
    struct vst_bridge_midi_event *me = mes->events;
    for (int i = 0; i < evs->numEvents; ++i) {
      memcpy(me, evs->events[i], sizeof (*me) + evs->events[i]->byteSize);
      me = (struct vst_bridge_midi_event *)(me->data + me->byteSize);
    }

    write(vbe->socket, &rq, sizeof (rq));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;
    return rq.amrq.value;
  }

  default:
    dprintf(vbe->logfd, "effectDispatcher unsupported: opcode: %d, index: %d,"
            " value: %d, ptr: %p, opt: %f\n", opcode, index, value, ptr, opt);
    return 0;
  }
}

bool vst_bridge_call_plugin_main(struct vst_bridge_effect *vbe)
{
  struct vst_bridge_request rq;

  rq.tag = 0;
  rq.cmd = VST_BRIDGE_CMD_PLUGIN_MAIN;
  if (write(vbe->socket, &rq, sizeof (rq)) != sizeof (rq))
    return false;

  while (true) {
    ssize_t rbytes = read(vbe->socket, &rq, sizeof (rq));
    if (rbytes <= 0)
      return false;

    dprintf(vbe->logfd, "cmd: %d, tag: %d, bytes: %d\n",
            rq.cmd, rq.tag, rbytes);

    switch (rq.cmd) {
    case VST_BRIDGE_CMD_PLUGIN_MAIN:
      vbe->e.numPrograms  = rq.plugin_data.numPrograms;
      vbe->e.numParams    = rq.plugin_data.numParams;
      vbe->e.numInputs    = rq.plugin_data.numInputs;
      vbe->e.numOutputs   = rq.plugin_data.numOutputs;
      vbe->e.flags        = rq.plugin_data.flags;
      vbe->e.initialDelay = rq.plugin_data.initialDelay;
      vbe->e.uniqueID     = rq.plugin_data.uniqueID;
      vbe->e.version      = rq.plugin_data.version;
      if (!rq.plugin_data.hasSetParameter)
        vbe->e.setParameter = NULL;
      if (!rq.plugin_data.hasGetParameter)
        vbe->e.getParameter = NULL;
      if (!rq.plugin_data.hasProcessReplacing)
        vbe->e.processReplacing = NULL;
      if (!rq.plugin_data.hasProcessDoubleReplacing)
        vbe->e.processDoubleReplacing = NULL;
      return true;

    case VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK:
      vst_bridge_handle_audio_master(vbe, &rq);
      break;

    default:
      dprintf(vbe->logfd, "UNEXPECTED COMMAND: %d\n", rq.cmd);
      break;
    }
  }
}

extern "C" {
  AEffect* VSTPluginMain(audioMasterCallback audio_master);
}

AEffect* VSTPluginMain(audioMasterCallback audio_master)
{
  struct vst_bridge_effect *vbe = NULL;
  int fds[2];

  // allocate the context
  vbe = (struct vst_bridge_effect *)calloc(sizeof (*vbe), 1);
  if (!vbe)
    goto failed;

  // set the back reference
  vbe->audio_master             = audio_master;
  vbe->e.user                   = vbe;
  vbe->e.magic                  = kEffectMagic;
  vbe->e.dispatcher             = vst_bridge_call_effect_dispatcher;
  vbe->e.setParameter           = vst_bridge_call_set_parameter;
  vbe->e.getParameter           = vst_bridge_call_get_parameter;
  vbe->e.processReplacing       = vst_bridge_call_process;
  vbe->e.processDoubleReplacing = vst_bridge_call_process_double;

  // init the logger
  vbe->logfd = open("/tmp/vst-bridge-pluging.log",
                    O_CREAT | O_TRUNC | O_APPEND | O_WRONLY, 0644);

  // initialize sockets
  if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds))
    goto failed_sockets;
  vbe->socket = fds[0];

  // fork
  vbe->child = fork();
  if (vbe->child == -1)
    goto failed_fork;

  if (!vbe->child) {
    // in the child
    char buff[8];
    close(fds[0]);
    snprintf(buff, sizeof (buff), "%d", fds[1]);
    execl("/bin/sh", "/bin/sh", g_host_path, g_plugin_path, buff, NULL);
    assert(false);
    exit(1);
  }

  // in the father
  close(fds[1]);

  // forward plugin main
  if (!vst_bridge_call_plugin_main(vbe)) {
    close(vbe->socket);
    free(vbe);
    return NULL;
  }

  dprintf(vbe->logfd, " => PluginMain done!\n");

  // Return the VST AEffect structure
  return &vbe->e;

  failed_fork:
  close(fds[0]);
  close(fds[1]);
  failed_sockets:
  close(vbe->logfd);
  failed:
  free(vbe);
  return NULL;
}
