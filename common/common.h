#ifndef COMMON_H
# define COMMON_H

# include <fcntl.h>

# include <stdint.h>
# include <stdbool.h>

# include "../config.h"

# define MIN(A, B) ((A) < (B) ? (A) : (B))

#define container_of(ptr, type, member) ({                              \
      const decltype( ((type *)0)->member ) *__mptr = (ptr);            \
      (type *)( (char *)__mptr - offsetof(type,member) );})

# define VST_BRIDGE_TPL_DLL "VST-BRIDGE-TPL-DLL"
# define VST_BRIDGE_TPL_HOST "VST-BRIDGE-TPL-HOST"
# define VST_BRIDGE_TPL_WINEPREFIX "VST-BRIDGE-TPL-WINEPREFIX"
# define VST_BRIDGE_TPL_PATH INSTALL_PREFIX "/lib/vst-bridge/vst-bridge-plugin-tpl.so"
# define VST_BRIDGE_HOST32_PATH INSTALL_PREFIX "/lib/vst-bridge/vst-bridge-host-32.exe"
# define VST_BRIDGE_HOST64_PATH INSTALL_PREFIX "/lib/vst-bridge/vst-bridge-host-64.exe"

enum vst_bridge_cmd {
  VST_BRIDGE_CMD_PING,
  VST_BRIDGE_CMD_PLUGIN_MAIN,
  VST_BRIDGE_CMD_PLUGIN_DATA,
  VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK,
  VST_BRIDGE_CMD_EFFECT_DISPATCHER,
  VST_BRIDGE_CMD_PROCESS,
  VST_BRIDGE_CMD_PROCESS_DOUBLE,
  VST_BRIDGE_CMD_SET_PARAMETER,
  VST_BRIDGE_CMD_GET_PARAMETER,
  VST_BRIDGE_CMD_SHOW_WINDOW,
  VST_BRIDGE_CMD_SET_SCHEDPARAM,
};

struct vst_bridge_effect_request {
  int32_t opcode;
  int32_t index;
  int64_t value;
  float   opt;
  uint8_t data[0];
} __attribute__((packed));

struct vst_bridge_audio_master_request {
  int32_t opcode;
  int32_t index;
  int64_t value;
  float   opt;
  uint8_t data[0];
} __attribute__((packed));

struct vst_bridge_frames {
  uint32_t nframes;
  float    frames[0];
} __attribute__((packed));

struct vst_bridge_frames_double {
  uint32_t nframes;
  double   frames[0];
} __attribute__((packed));

struct vst_bridge_effect_parameter {
  uint32_t index;
  float    value;
} __attribute__((packed));

struct vst_bridge_plugin_data {
  bool    hasSetParameter;
  bool    hasGetParameter;
  bool    hasProcessReplacing;
  bool    hasProcessDoubleReplacing;
  int32_t numPrograms;
  int32_t numParams;
  int32_t numInputs;
  int32_t numOutputs;
  int32_t flags;
  int32_t initialDelay;
  int32_t uniqueID;
  int32_t version;
} __attribute__((packed));

struct vst_bridge_midi_event {
  int32_t type;
  int32_t byteSize;
  int32_t deltaFrames;
  int32_t flags;
  uint8_t data[0];
} __attribute__((packed));

struct vst_bridge_midi_events {
  uint32_t nb;
  struct vst_bridge_midi_event events[0];
} __attribute__((packed));

struct vst_bridge_schedparam {
  int policy;
  int priority;
} __attribute__((packed));

struct vst_bridge_request {
  uint32_t tag;
  uint32_t cmd;
  union {
    uint8_t data[128 * 1024];
    struct vst_bridge_effect_request erq;
    struct vst_bridge_audio_master_request amrq;
    struct vst_bridge_frames frames;
    struct vst_bridge_frames_double framesd;
    struct vst_bridge_effect_parameter param;
    struct vst_bridge_plugin_data plugin_data;
    struct vst_bridge_schedparam schedparam;
  };
} __attribute__((packed));

#define VST_BRIDGE_RQ_LEN 8
#define VST_BRIDGE_CHUNK_SIZE (96 * 1024)
#define VST_BRIDGE_ERQ_LEN(X) ((X) + 8 + sizeof (struct vst_bridge_effect_request))
#define VST_BRIDGE_AMRQ_LEN(X) ((X) + 8 + sizeof (struct vst_bridge_audio_master_request))
#define VST_BRIDGE_PARAM_LEN (8 + sizeof (struct vst_bridge_effect_parameter))
#define VST_BRIDGE_FRAMES_LEN(X) ((X) * sizeof (float) + 8 + sizeof (struct vst_bridge_frames))
#define VST_BRIDGE_FRAMES_DOUBLE_LEN(X) ((X) * sizeof (double) + 8 + sizeof (struct vst_bridge_frames_double))
#define VST_BRIDGE_SCHEDPARAM_LEN (8 + sizeof(struct vst_bridge_schedparam))

  static const char * const vst_bridge_effect_opcode_name[] = {
    "effOpen",
    "effClose",
    "effSetProgram",
    "effGetProgram",
    "effSetProgramName",
    "effGetProgramName",
    "effGetParamLabel",
    "effGetParamDisplay",
    "effGetParamName",
    "effGetVu",
    "effSetSampleRate",
    "effSetBlockSize",
    "effMainsChanged",
    "effEditGetRect",
    "effEditOpen",
    "effEditClose",
    "effEditDraw",
    "effEditMouse",
    "effEditKey",
    "effEditIdle",
    "effEditTop",
    "effEditSleep",
    "effIdentify",
    "effGetChunk",
    "effSetChunk",
    "effProcessEvents",
    "effCanBeAutomated",
    "effString2Parameter",
    "effGetNumProgramCategories",
    "effGetProgramNameIndexed",
    "effCopyProgram",
    "effConnectInput",
    "effConnectOutput",
    "effGetInputProperties",
    "effGetOutputProperties",
    "effGetPlugCategory",
    "effGetCurrentPosition",
    "effGetDestinationBuffer",
    "effOfflineNotify",
    "effOfflinePrepare",
    "effOfflineRun",
    "effProcessVarIo",
    "effSetSpeakerArrangement",
    "effSetBlockSizeAndSampleRate",
    "effSetBypass",
    "effGetEffectName",
    "effGetErrorText",
    "effGetVendorString",
    "effGetProductString",
    "effGetVendorVersion",
    "effVendorSpecific",
    "effCanDo",
    "effGetTailSize",
    "effIdle",
    "effGetIcon",
    "effSetViewPosition",
    "effGetParameterProperties",
    "effKeysRequired",
    "effGetVstVersion",
    "effEditKeyDown",
    "effEditKeyUp",
    "effSetEditKnobMode",
    "effGetMidiProgramName",
    "effGetCurrentMidiProgram",
    "effGetMidiProgramCategory",
    "effHasMidiProgramsChanged",
    "effGetMidiKeyName",
    "effBeginSetProgram",
    "effEndSetProgram",
    "effGetSpeakerArrangement",
    "effShellGetNextPlugin",
    "effStartProcess",
    "effStopProcess",
    "effSetTotalSampleToProcess",
    "effSetPanLaw",
    "effBeginLoadBank",
    "effBeginLoadProgram",
    "effSetProcessPrecision",
    "effGetNumMidiInputChannels",
    "effGetNumMidiOutputChannels",
  };

static const char * const vst_bridge_audio_master_opcode_name[] = {
  "audioMasterAutomate",
  "audioMasterVersion",
  "audioMasterCurrentId",
  "audioMasterIdle",
  "audioMasterPinConnected",
  "",
  "audioMasterWantMidi",
  "audioMasterGetTime",
  "audioMasterProcessEvents",
  "audioMasterSetTime",
  "audioMasterTempoAt",
  "audioMasterGetNumAutomatableParameters",
  "audioMasterGetParameterQuantization",
  "audioMasterIOChanged",
  "audioMasterNeedIdle",
  "audioMasterSizeWindow",
  "audioMasterGetSampleRate",
  "audioMasterGetBlockSize",
  "audioMasterGetInputLatency",
  "audioMasterGetOutputLatency",
  "audioMasterGetPreviousPlug",
  "audioMasterGetNextPlug",
  "audioMasterWillReplaceOrAccumulate",
  "audioMasterGetCurrentProcessLevel",
  "audioMasterGetAutomationState",
  "audioMasterOfflineStart",
  "audioMasterOfflineRead",
  "audioMasterOfflineWrite",
  "audioMasterOfflineGetCurrentPass",
  "audioMasterOfflineGetCurrentMetaPass",
  "audioMasterSetOutputSampleRate",
  "audioMasterGetOutputSpeakerArrangement",
  "audioMasterGetVendorString",
  "audioMasterGetProductString",
  "audioMasterGetVendorVersion",
  "audioMasterVendorSpecific",
  "audioMasterSetIcon",
  "audioMasterCanDo",
  "audioMasterGetLanguage",
  "audioMasterOpenWindow",
  "audioMasterCloseWindow",
  "audioMasterGetDirectory",
  "audioMasterUpdateDisplay",
  "audioMasterBeginEdit",
  "audioMasterEndEdit",
  "audioMasterOpenFileSelector",
  "audioMasterCloseFileSelector",
  "audioMasterEditFile",
  "audioMasterGetChunkFile",
  "audioMasterGetInputSpeakerArrangement",
};

/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY			0
#define XEMBED_WINDOW_ACTIVATE  		1
#define XEMBED_WINDOW_DEACTIVATE  		2
#define XEMBED_REQUEST_FOCUS	 		3
#define XEMBED_FOCUS_IN 				4
#define XEMBED_FOCUS_OUT  				5
#define XEMBED_FOCUS_NEXT 				6
#define XEMBED_FOCUS_PREV 				7
/* 8-9 were used for XEMBED_GRAB_KEY/XEMBED_UNGRAB_KEY */
#define XEMBED_MODALITY_ON 				10
#define XEMBED_MODALITY_OFF 			11
#define XEMBED_REGISTER_ACCELERATOR     12
#define XEMBED_UNREGISTER_ACCELERATOR   13
#define XEMBED_ACTIVATE_ACCELERATOR     14

/* Details for  XEMBED_FOCUS_IN: */
#define XEMBED_FOCUS_CURRENT			0
#define XEMBED_FOCUS_FIRST 				1
#define XEMBED_FOCUS_LAST				2

#define XEMBED_MAPPED 					(1 << 0)

#endif /* !COMMON_H */
