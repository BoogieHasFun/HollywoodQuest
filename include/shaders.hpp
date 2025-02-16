#pragma once

#include "shaders/gamma_convert_vs.glsl.hpp"
#include "shaders/gamma_rgb_convert_fs.glsl.hpp"
#include "shaders/gamma_yuv_convert_fs.glsl.hpp"
#include "opengl_replay/Shader.hpp"

static Shader shaderRGBGammaConvert() {
    return {gamma_convert_vs_glsl, gamma_rgb_convert_fs_glsl};
}

static Shader shaderYUVGammaConvert() {
    return {gamma_convert_vs_glsl, gamma_yuv_convert_fs_glsl};
}