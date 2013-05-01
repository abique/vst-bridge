#ifndef COMMON_H
# define COMMON_H

# include <fcntl.h>

# include <stdint.h>
# include <stdbool.h>

# include "../config.h"

# define VST_BRIDGE_TPL_PLUGIN_PATH "VST-BRIDGE-TPL-PATH"
# define VST_BRIDGE_BUFFER_SIZE (1024 * 1024)

enum vst_bridge_cmd {
  VST_BRIDGE_CMD_PING,
  VST_BRIDGE_CMD_PLUGIN_MAIN,
  VST_BRIDGE_CMD_AUDIO_MASTER_CALLBACK,
  VST_BRIDGE_CMD_EFFECT_DISPATCHER,
  VST_BRIDGE_CMD_PROCESS,
  VST_BRIDGE_CMD_PROCESS_DOUBLE,
  VST_BRIDGE_CMD_SET_PARAMETER,
  VST_BRIDGE_CMD_GET_PARAMETER,
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
};

struct vst_bridge_midi_event {
  int32_t type;
  int32_t byteSize;
  int32_t deltaFrames;
  int32_t flags;
  uint8_t data[0];
};

struct vst_bridge_midi_events {
  uint32_t nb;
  struct vst_bridge_midi_event events[0];
};

struct vst_bridge_request {
  uint32_t tag;
  uint32_t cmd;
  union {
    uint8_t data[16 * 1024];
    struct vst_bridge_effect_request erq;
    struct vst_bridge_audio_master_request amrq;
    struct vst_bridge_frames frames;
    struct vst_bridge_frames_double framesd;
    struct vst_bridge_effect_parameter param;
    struct vst_bridge_plugin_data plugin_data;
  };
} __attribute__((packed));

union vst_bridge_buffer {
  struct vst_bridge_request rq;
  uint8_t buffer[VST_BRIDGE_BUFFER_SIZE];
};

#endif /* !COMMON_H */