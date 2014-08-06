#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>

#include <list>

#include <X11/Xlib.h>

#define __cdecl

#include "../config.h"
#include "../common/common.h"

const char g_plugin_path[PATH_MAX] = VST_BRIDGE_TPL_MAGIC;
const char g_host_path[PATH_MAX] = VST_BRIDGE_HOST32_PATH;

#ifdef DEBUG

# define LOG(Args...)                           \
  do {                                          \
    fprintf(g_log ? : stderr, "P: " Args);      \
    fflush(g_log ? : stderr);                   \
  } while (0)
#else
# define LOG(Args...) do { ; } while (0)
#endif

#define CRIT(Args...)                                   \
  do {                                                  \
    fprintf(g_log ? : stderr, "[CRIT] P: " Args);       \
    fflush(g_log ? : stderr);                           \
  } while (0)

#include "../vstsdk2.4/pluginterfaces/vst2.x/aeffectx.h"

static FILE *g_log = NULL;

struct vst_bridge_effect {
  vst_bridge_effect()
    : socket(-1),
      child(-1),
      next_tag(0),
      chunk(NULL)
  {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&lock, &attr);
    pthread_mutexattr_destroy(&attr);
  }

  ~vst_bridge_effect()
  {
    if (socket >= 0)
      close(socket);
    free(chunk);
    pthread_mutex_destroy(&lock);
    int st;
    waitpid(child, &st, 0);
    if (display)
      XCloseDisplay(display);
  }

  struct AEffect                 e;
  int                            socket;
  pid_t                          child;
  uint32_t                       next_tag;
  audioMasterCallback            audio_master;
  void                          *chunk;
  pthread_mutex_t                lock;
  ERect                          rect;
  bool                           close_flag;
  std::list<vst_bridge_request>  pending;
  Display                       *display;
  bool                           show_window;
};

void copy_plugin_data(struct vst_bridge_effect *vbe,
                      struct vst_bridge_request *rq)
{
  vbe->e.numPrograms  = rq->plugin_data.numPrograms;
  vbe->e.numParams    = rq->plugin_data.numParams;
  vbe->e.numInputs    = rq->plugin_data.numInputs;
  vbe->e.numOutputs   = rq->plugin_data.numOutputs;
  vbe->e.flags        = rq->plugin_data.flags;
  vbe->e.initialDelay = rq->plugin_data.initialDelay;
  vbe->e.uniqueID     = rq->plugin_data.uniqueID;
  vbe->e.version      = rq->plugin_data.version;
  if (!rq->plugin_data.hasSetParameter)
    vbe->e.setParameter = NULL;
  if (!rq->plugin_data.hasGetParameter)
    vbe->e.getParameter = NULL;
  if (!rq->plugin_data.hasProcessReplacing)
    vbe->e.processReplacing = NULL;
  if (!rq->plugin_data.hasProcessDoubleReplacing)
    vbe->e.processDoubleReplacing = NULL;
}

void vst_bridge_handle_audio_master(struct vst_bridge_effect *vbe,
                                    struct vst_bridge_request *rq)
{
  LOG("audio_master(%s, %d, %d, %f) <= tag %d\n",
      vst_bridge_audio_master_opcode_name[rq->amrq.opcode],
      rq->amrq.index, rq->amrq.value, rq->amrq.opt, rq->tag);

  switch (rq->amrq.opcode) {
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
  case audioMasterCanDo:
  case audioMasterGetVendorVersion:
  case audioMasterBeginEdit:
  case audioMasterEndEdit:
  case audioMasterUpdateDisplay:
  case __audioMasterTempoAtDeprecated:
    rq->amrq.value = vbe->audio_master(&vbe->e, rq->amrq.opcode, rq->amrq.index,
                                       rq->amrq.value, rq->amrq.data, rq->amrq.opt);
    write(vbe->socket, rq, VST_BRIDGE_AMRQ_LEN(0));
    break;

  case audioMasterGetProductString:
  case audioMasterGetVendorString:
    rq->amrq.value = vbe->audio_master(&vbe->e, rq->amrq.opcode, rq->amrq.index,
                                       rq->amrq.value, rq->amrq.data, rq->amrq.opt);
    write(vbe->socket, rq, VST_BRIDGE_AMRQ_LEN(strlen((const char *)rq->amrq.data) + 1));
    break;

  case audioMasterProcessEvents: {
    struct vst_bridge_midi_events *mes = (struct vst_bridge_midi_events *)rq->amrq.data;
    struct VstEvents *ves = (struct VstEvents *)malloc(sizeof (*ves) + mes->nb * sizeof (void*));
    ves->numEvents = mes->nb;
    ves->reserved  = 0;
    struct vst_bridge_midi_event *me = mes->events;
    for (size_t i = 0; i < mes->nb; ++i) {
      ves->events[i] = (VstEvent*)me;
      me = (struct vst_bridge_midi_event *)(me->data + me->byteSize);
    }

    rq->amrq.value = vbe->audio_master(&vbe->e, rq->amrq.opcode, rq->amrq.index,
                                       rq->amrq.value, ves, rq->amrq.opt);
    free(ves);
    write(vbe->socket, rq, ((uint8_t*)me) - ((uint8_t*)rq));
    break;
  }

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
    write(vbe->socket, rq, VST_BRIDGE_AMRQ_LEN(sizeof (*time_info)));
    break;
  }

  default:
    CRIT("  !!!!!!! audio master callback (unhandled): op: %d,"
         " index: %d, value: %ld, opt: %f\n",
         rq->amrq.opcode, rq->amrq.index, rq->amrq.value, rq->amrq.opt);
    break;
  }
}

bool vst_bridge_wait_response(struct vst_bridge_effect *vbe,
                              struct vst_bridge_request *rq,
                              uint32_t tag)
{
  ssize_t len;

  while (true) {
    std::list<vst_bridge_request>::iterator it;
    for (it = vbe->pending.begin(); it != vbe->pending.end(); ++it) {
      if (it->tag != tag)
        continue;
      *rq = *it; // XXX could be optimized?
      vbe->pending.erase(it);
      return true;
    }

    LOG("     <=== Waiting for tag %d\n", tag);

    len = ::read(vbe->socket, rq, sizeof (*rq));
    if (len <= 0)
      return false;
    assert(len >= VST_BRIDGE_RQ_LEN);

    LOG("     ===> Got tag %d\n", rq->tag);

    if (rq->tag == tag)
      return true;

    // handle request
    if (rq->cmd == VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK) {
      vst_bridge_handle_audio_master(vbe, rq);
      continue;
    } else if (rq->cmd == VST_BRIDGE_CMD_PLUGIN_DATA) {
      copy_plugin_data(vbe, rq);
      continue;
    }

    vbe->pending.push_back(*rq);
  }
}

void vst_bridge_show_window(struct vst_bridge_effect *vbe)
{
  struct vst_bridge_request rq;
  if (vbe->show_window) {
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_SHOW_WINDOW;
    vbe->next_tag += 2;

    vbe->show_window = false;
    write(vbe->socket, &rq, VST_BRIDGE_RQ_LEN);
    vst_bridge_wait_response(vbe, &rq, rq.tag);
  }
}

void vst_bridge_call_process(AEffect* effect,
                             float**  inputs,
                             float**  outputs,
                             VstInt32 sampleFrames)
{
  struct vst_bridge_effect *vbe = container_of(effect, struct vst_bridge_effect, e);
  struct vst_bridge_request rq;

  pthread_mutex_lock(&vbe->lock);

  rq.tag             = vbe->next_tag;
  rq.cmd             = VST_BRIDGE_CMD_PROCESS;
  rq.frames.nframes  = sampleFrames;
  vbe->next_tag     += 2;

  for (int i = 0; i < vbe->e.numInputs; ++i)
    memcpy(rq.frames.frames + i * sampleFrames, inputs[i],
           sizeof (float) * sampleFrames);

  write(vbe->socket, &rq, VST_BRIDGE_FRAMES_LEN(vbe->e.numInputs * sampleFrames));
  vst_bridge_wait_response(vbe, &rq, rq.tag);

  for (int i = 0; i < vbe->e.numOutputs; ++i)
    memcpy(outputs[i], rq.frames.frames + i * sampleFrames,
           sizeof (float) * sampleFrames);

  pthread_mutex_unlock(&vbe->lock);
}

void vst_bridge_call_process_double(AEffect* effect,
                                    double**  inputs,
                                    double**  outputs,
                                    VstInt32 sampleFrames)
{
  struct vst_bridge_effect *vbe = container_of(effect, struct vst_bridge_effect, e);
  struct vst_bridge_request rq;

  pthread_mutex_lock(&vbe->lock);

  rq.tag              = vbe->next_tag;
  rq.cmd              = VST_BRIDGE_CMD_PROCESS_DOUBLE;
  rq.framesd.nframes  = sampleFrames;
  vbe->next_tag      += 2;

  for (int i = 0; i < vbe->e.numInputs; ++i)
    memcpy(rq.framesd.frames + i * sampleFrames, inputs[i],
           sizeof (double) * sampleFrames);

  write(vbe->socket, &rq, VST_BRIDGE_FRAMES_DOUBLE_LEN(vbe->e.numInputs * sampleFrames));
  vst_bridge_wait_response(vbe, &rq, rq.tag);
  for (int i = 0; i < vbe->e.numOutputs; ++i)
    memcpy(outputs[i], rq.framesd.frames + i * sampleFrames,
           sizeof (double) * sampleFrames);

  pthread_mutex_unlock(&vbe->lock);
}

float vst_bridge_call_get_parameter(AEffect* effect,
                                    VstInt32 index)
{
  struct vst_bridge_effect *vbe = container_of(effect, struct vst_bridge_effect, e);
  struct vst_bridge_request rq;

  pthread_mutex_lock(&vbe->lock);

  rq.tag         = vbe->next_tag;
  rq.cmd         = VST_BRIDGE_CMD_GET_PARAMETER;
  rq.param.index = index;
  vbe->next_tag += 2;
  write(vbe->socket, &rq, VST_BRIDGE_PARAM_LEN);
  vst_bridge_wait_response(vbe, &rq, rq.tag);
  pthread_mutex_unlock(&vbe->lock);
  return rq.param.value;
}

void vst_bridge_call_set_parameter(AEffect* effect,
                                   VstInt32 index,
                                   float    parameter)
{
  struct vst_bridge_effect *vbe = container_of(effect, struct vst_bridge_effect, e);
  struct vst_bridge_request rq;

  pthread_mutex_lock(&vbe->lock);
  rq.tag         = vbe->next_tag;
  rq.cmd         = VST_BRIDGE_CMD_SET_PARAMETER;
  rq.param.index = index;
  rq.param.value = parameter;
  vbe->next_tag += 2;
  write(vbe->socket, &rq, VST_BRIDGE_PARAM_LEN);
  pthread_mutex_unlock(&vbe->lock);
}

VstIntPtr vst_bridge_call_effect_dispatcher2(AEffect*  effect,
                                             VstInt32  opcode,
                                             VstInt32  index,
                                             VstIntPtr value,
                                             void*     ptr,
                                             float     opt)
{
  struct vst_bridge_effect *vbe = container_of(effect, struct vst_bridge_effect, e);
  struct vst_bridge_request rq;
  ssize_t len;

  LOG("[%p] effect_dispatcher(%s, %d, %d, %p, %f) => next_tag: %d\n",
      pthread_self(), vst_bridge_effect_opcode_name[opcode], index, value,
      ptr, opt, vbe->next_tag);

  switch (opcode) {
  case effSetBlockSize:
  case effSetProgram:
  case effSetSampleRate:
  case effEditIdle:
  case effGetProgram:
  case __effIdleDeprecated:
  case effSetTotalSampleToProcess:
  case effStartProcess:
  case effStopProcess:
  case effSetPanLaw:
  case effSetProcessPrecision:
  case effGetNumMidiInputChannels:
  case effGetNumMidiOutputChannels:
  case effEditClose:
  case effCanBeAutomated:
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(0));
    vst_bridge_wait_response(vbe, &rq, rq.tag);
    return rq.amrq.value;

  case effGetOutputProperties:
  case effGetInputProperties:
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(0));
    vst_bridge_wait_response(vbe, &rq, rq.tag);
    memcpy(ptr, rq.erq.data, sizeof (VstPinProperties));
    return rq.erq.value;

  case effOpen:
  case effGetPlugCategory:
  case effGetVstVersion:
  case effGetVendorVersion:
  case effMainsChanged:
  case effBeginSetProgram:
  case effEndSetProgram:
  case __effConnectOutputDeprecated:
  case __effConnectInputDeprecated:
  case effSetEditKnobMode:
  case effEditKeyUp:
  case effEditKeyDown:
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

  case effClose:
    // quit
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(0));
    vbe->close_flag = true;
    return 0;

  case effEditOpen: {
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(0));
    vst_bridge_wait_response(vbe, &rq, rq.tag);

    Window   parent  = (Window)ptr;
    Window   child   = (Window)rq.erq.index;

    if (!vbe->display)
      vbe->display = XOpenDisplay(NULL);

    XReparentWindow(vbe->display, child, parent, 0, 0);

#if 0
    XEvent ev;

    memset(&ev, 0, sizeof (ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = child;
    ev.xclient.message_type = XInternAtom(vbe->display, "_XEMBED", false);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = XEMBED_EMBEDDED_NOTIFY;
    ev.xclient.data.l[3] = parent;
    XSendEvent(vbe->display, child, false, NoEventMask, &ev);
#endif

    XSync(vbe->display, false);

    XFlush(vbe->display);

    vbe->show_window = true;
    vst_bridge_show_window(vbe);
    return rq.erq.value;
  }

  case effEditGetRect: {
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(0));
    vst_bridge_wait_response(vbe, &rq, rq.tag);
    memcpy(&vbe->rect, rq.erq.data, sizeof (vbe->rect));
    ERect **r = (ERect **)ptr;
    *r = &vbe->rect;
    return rq.erq.value;
  }

  case effSetProgramName:
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    strcpy((char*)rq.erq.data, (const char *)ptr);
    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(strlen((const char *)ptr) + 1));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;
    return rq.amrq.value;

  case effGetProgramName:
  case effGetParamLabel:
  case effGetParamDisplay:
  case effGetParamName:
  case effGetEffectName:
  case effGetVendorString:
  case effGetProductString:
  case effGetProgramNameIndexed:
  case effGetMidiKeyName:
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(0));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;
    strcpy((char*)ptr, (const char *)rq.erq.data);
    LOG("Got string: %s\n", (char *)ptr);
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

  case effGetParameterProperties:
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(0));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;

    if (ptr && rq.amrq.value)
      memcpy(ptr, rq.erq.data, sizeof (VstParameterProperties));
    return rq.amrq.value;

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
    for (size_t off = 0; rq.erq.value > 0; ) {
      size_t can_read = MIN(VST_BRIDGE_CHUNK_SIZE, rq.erq.value - off);
      memcpy(static_cast<uint8_t *>(vbe->chunk) + off, rq.erq.data, can_read);
      off += can_read;
      if (off == static_cast<size_t>(rq.erq.value))
        break;
      if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
        return 0;
    }
    *((void **)ptr) = chunk;
    return rq.erq.value;
  }

  case effSetChunk: {
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;

    for (size_t off = 0; off < static_cast<size_t>(value); ) {
      size_t can_write = MIN(VST_BRIDGE_CHUNK_SIZE, value - off);
      memcpy(rq.erq.data, static_cast<uint8_t *>(ptr) + off, can_write);
      write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(can_write));
      off += can_write;
    }
    vst_bridge_wait_response(vbe, &rq, rq.tag);
    return rq.erq.value;
  }

  case effSetSpeakerArrangement: {
    struct VstSpeakerArrangement *ar = (struct VstSpeakerArrangement *)value;
    rq.tag         = vbe->next_tag;
    rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
    rq.erq.opcode  = opcode;
    rq.erq.index   = index;
    rq.erq.value   = value;
    rq.erq.opt     = opt;
    vbe->next_tag += 2;
    size_t len = 8 + ar->numChannels * sizeof (ar->speakers[0]);
    memcpy(rq.erq.data, ptr, len);

    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(len));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;
    memcpy(ptr, rq.erq.data, 8 + ar->numChannels * sizeof (ar->speakers[0]));
    return rq.amrq.value;
  }

  case effProcessEvents: {
    // compute the size
    struct VstEvents *evs = (struct VstEvents *)ptr;
    struct vst_bridge_midi_events *mes = (struct vst_bridge_midi_events *)rq.erq.data;

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

    write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(((uint8_t *)me) - rq.erq.data));
    if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
      return 0;
    return rq.amrq.value;
  }

  case effVendorSpecific: {
    switch (index) {
    case effGetParamDisplay:
      rq.tag         = vbe->next_tag;
      rq.cmd         = VST_BRIDGE_CMD_EFFECT_DISPATCHER;
      rq.erq.opcode  = opcode;
      rq.erq.index   = index;
      rq.erq.value   = value;
      rq.erq.opt     = opt;
      vbe->next_tag += 2;

      write(vbe->socket, &rq, VST_BRIDGE_ERQ_LEN(0));
      if (!vst_bridge_wait_response(vbe, &rq, rq.tag))
        return 0;
      strcpy((char*)ptr, (const char *)rq.erq.data);
      LOG("Got string: %s\n", (char *)ptr);
      return rq.amrq.value;

    default:
      // fall through
      break;
    }
  }

  default:
    CRIT("[%p] !!!!!!!!!! UNHANDLED effect_dispatcher(%s, %d, %d, %p, %f)\n",
         pthread_self(), vst_bridge_effect_opcode_name[opcode], index, value,
         ptr);
    return 0;
  }
}

VstIntPtr vst_bridge_call_effect_dispatcher(AEffect*  effect,
                                            VstInt32  opcode,
                                            VstInt32  index,
                                            VstIntPtr value,
                                            void*     ptr,
                                            float     opt)
{
  struct vst_bridge_effect *vbe = container_of(effect, struct vst_bridge_effect, e);

  pthread_mutex_lock(&vbe->lock);
  VstIntPtr ret =  vst_bridge_call_effect_dispatcher2(
    effect, opcode, index, value, ptr, opt);
  pthread_mutex_unlock(&vbe->lock);

  if (!vbe->close_flag)
    return ret;

  delete vbe;
  return ret;
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

    LOG("cmd: %d, tag: %d, bytes: %d\n", rq.cmd, rq.tag, rbytes);

    switch (rq.cmd) {
    case VST_BRIDGE_CMD_PLUGIN_DATA:
      copy_plugin_data(vbe, &rq);
      break;

    case VST_BRIDGE_CMD_PLUGIN_MAIN:
      copy_plugin_data(vbe, &rq);
      return true;

    case VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK:
      vst_bridge_handle_audio_master(vbe, &rq);
      break;

    default:
      LOG("UNEXPECTED COMMAND: %d\n", rq.cmd);
      break;
    }
  }
}

extern "C" {
  AEffect* VSTPluginMain(audioMasterCallback audio_master);
  AEffect* VSTPluginMain2(audioMasterCallback audio_master) asm ("main");
}

AEffect* VSTPluginMain2(audioMasterCallback audio_master)
{
  return VSTPluginMain(audio_master);
}

AEffect* VSTPluginMain(audioMasterCallback audio_master)
{
  struct vst_bridge_effect *vbe = NULL;
  int fds[2];

  if (!g_log) {
#ifdef DEBUG
      char path[128];
      snprintf(path, sizeof (path), "/tmp/vst-bridge-plugin.%d.log", getpid());
      g_log = fopen(path, "w+");
#else
      g_log = stdout;
#endif
  }

  // allocate the context
  vbe = new vst_bridge_effect;
  if (!vbe)
    goto failed;
  memset(vbe, 0, sizeof(vst_bridge_effect));

  // XXX move to the class description
  vbe->audio_master             = audio_master;
  vbe->e.user                   = NULL;
  vbe->e.magic                  = kEffectMagic;
  vbe->e.dispatcher             = vst_bridge_call_effect_dispatcher;
  vbe->e.setParameter           = vst_bridge_call_set_parameter;
  vbe->e.getParameter           = vst_bridge_call_get_parameter;
  vbe->e.processReplacing       = vst_bridge_call_process;
  vbe->e.processDoubleReplacing = vst_bridge_call_process_double;
  vbe->close_flag               = false;
  vbe->show_window              = false;
  vbe->display                  = NULL;

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
    CRIT("Failed to spawn child process: /bin/sh %s %s %s\n", g_host_path, g_plugin_path, buff);
    exit(1);
  }

  // in the father
  close(fds[1]);

  // forward plugin main
  if (!vst_bridge_call_plugin_main(vbe)) {
    close(vbe->socket);
    delete vbe;
    return NULL;
  }

  LOG(" => PluginMain done!\n");

  // Return the VST AEffect structure
  return &vbe->e;

  failed_fork:
  close(fds[0]);
  close(fds[1]);
  failed_sockets:
  failed:
  delete vbe;
  return NULL;
}
