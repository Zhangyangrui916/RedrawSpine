#ifndef EMBEDDED_SHADERS_H
#define EMBEDDED_SHADERS_H

namespace EmbeddedShaders {

constexpr const char* VS =
#include "embedded_shaders/vs.glsl.inc"
;

constexpr const char* RGB_FS =
#include "embedded_shaders/rgb.glsl.inc"
;

constexpr const char* UV_REDRAW_FS =
#include "embedded_shaders/uv_redraw.glsl.inc"
;

constexpr const char* CTRL_FS =
#include "embedded_shaders/ctrl.glsl.inc"
;

constexpr const char* CLEAN_ATTACHMENT_VS =
#include "embedded_shaders/cleanAttachment.vs.glsl.inc"
;

constexpr const char* CLEAN_ATTACHMENT_FS =
#include "embedded_shaders/cleanAttachment.fs.glsl.inc"
;

}

#endif
