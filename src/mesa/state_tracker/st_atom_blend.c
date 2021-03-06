/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  *   Brian Paul
  */
 

#include "st_context.h"
#include "st_atom.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "cso_cache/cso_context.h"

#include "framebuffer.h"
#include "main/macros.h"

/**
 * Convert GLenum blend tokens to pipe tokens.
 * Both blend factors and blend funcs are accepted.
 */
static GLuint
translate_blend(GLenum blend)
{
   switch (blend) {
   /* blend functions */
   case GL_FUNC_ADD:
      return PIPE_BLEND_ADD;
   case GL_FUNC_SUBTRACT:
      return PIPE_BLEND_SUBTRACT;
   case GL_FUNC_REVERSE_SUBTRACT:
      return PIPE_BLEND_REVERSE_SUBTRACT;
   case GL_MIN:
      return PIPE_BLEND_MIN;
   case GL_MAX:
      return PIPE_BLEND_MAX;

   /* blend factors */
   case GL_ONE:
      return PIPE_BLENDFACTOR_ONE;
   case GL_SRC_COLOR:
      return PIPE_BLENDFACTOR_SRC_COLOR;
   case GL_SRC_ALPHA:
      return PIPE_BLENDFACTOR_SRC_ALPHA;
   case GL_DST_ALPHA:
      return PIPE_BLENDFACTOR_DST_ALPHA;
   case GL_DST_COLOR:
      return PIPE_BLENDFACTOR_DST_COLOR;
   case GL_SRC_ALPHA_SATURATE:
      return PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE;
   case GL_CONSTANT_COLOR:
      return PIPE_BLENDFACTOR_CONST_COLOR;
   case GL_CONSTANT_ALPHA:
      return PIPE_BLENDFACTOR_CONST_ALPHA;
   case GL_SRC1_COLOR:
      return PIPE_BLENDFACTOR_SRC1_COLOR;
   case GL_SRC1_ALPHA:
      return PIPE_BLENDFACTOR_SRC1_ALPHA;
   case GL_ZERO:
      return PIPE_BLENDFACTOR_ZERO;
   case GL_ONE_MINUS_SRC_COLOR:
      return PIPE_BLENDFACTOR_INV_SRC_COLOR;
   case GL_ONE_MINUS_SRC_ALPHA:
      return PIPE_BLENDFACTOR_INV_SRC_ALPHA;
   case GL_ONE_MINUS_DST_COLOR:
      return PIPE_BLENDFACTOR_INV_DST_COLOR;
   case GL_ONE_MINUS_DST_ALPHA:
      return PIPE_BLENDFACTOR_INV_DST_ALPHA;
   case GL_ONE_MINUS_CONSTANT_COLOR:
      return PIPE_BLENDFACTOR_INV_CONST_COLOR;
   case GL_ONE_MINUS_CONSTANT_ALPHA:
      return PIPE_BLENDFACTOR_INV_CONST_ALPHA;
   case GL_ONE_MINUS_SRC1_COLOR:
      return PIPE_BLENDFACTOR_INV_SRC1_COLOR;
   case GL_ONE_MINUS_SRC1_ALPHA:
      return PIPE_BLENDFACTOR_INV_SRC1_ALPHA;
   default:
      assert("invalid GL token in translate_blend()" == NULL);
      return 0;
   }
}

/**
 * Figure out if colormasks are different per rt.
 */
static GLboolean
colormask_per_rt(const struct gl_context *ctx)
{
   /* a bit suboptimal have to compare lots of values */
   unsigned i;
   for (i = 1; i < ctx->Const.MaxDrawBuffers; i++) {
      if (memcmp(ctx->Color.ColorMask[0], ctx->Color.ColorMask[i], 4)) {
         return GL_TRUE;
      }
   }
   return GL_FALSE;
}

/**
 * Figure out if blend enables/state are different per rt.
 */
static GLboolean
blend_per_rt(const struct gl_context *ctx)
{
   if (ctx->Color.BlendEnabled &&
      (ctx->Color.BlendEnabled != ((1U << ctx->Const.MaxDrawBuffers) - 1))) {
      /* This can only happen if GL_EXT_draw_buffers2 is enabled */
      return GL_TRUE;
   }
   if (ctx->Color._BlendFuncPerBuffer || ctx->Color._BlendEquationPerBuffer) {
      /* this can only happen if GL_ARB_draw_buffers_blend is enabled */
      return GL_TRUE;
   }
   return GL_FALSE;
}

void
st_update_blend( struct st_context *st )
{
   struct pipe_blend_state *blend = &st->state.blend;
   const struct gl_context *ctx = st->ctx;
   unsigned num_state = 1;
   unsigned i, j;

   memset(blend, 0, sizeof(*blend));

   if (blend_per_rt(ctx) || colormask_per_rt(ctx)) {
      num_state = ctx->Const.MaxDrawBuffers;
      blend->independent_blend_enable = 1;
   }
   if (ctx->Color.ColorLogicOpEnabled) {
      /* logicop enabled */
      blend->logicop_enable = 1;
      blend->logicop_func = ctx->Color._LogicOp;
   }
   else if (ctx->Color.BlendEnabled && !ctx->Color._AdvancedBlendMode) {
      /* blending enabled */
      for (i = 0, j = 0; i < num_state; i++) {

         blend->rt[i].blend_enable = (ctx->Color.BlendEnabled >> i) & 0x1;

         if (ctx->Extensions.ARB_draw_buffers_blend)
            j = i;

         blend->rt[i].rgb_func =
            translate_blend(ctx->Color.Blend[j].EquationRGB);

         if (ctx->Color.Blend[i].EquationRGB == GL_MIN ||
             ctx->Color.Blend[i].EquationRGB == GL_MAX) {
            /* Min/max are special */
            blend->rt[i].rgb_src_factor = PIPE_BLENDFACTOR_ONE;
            blend->rt[i].rgb_dst_factor = PIPE_BLENDFACTOR_ONE;
         }
         else {
            blend->rt[i].rgb_src_factor =
               translate_blend(ctx->Color.Blend[j].SrcRGB);
            blend->rt[i].rgb_dst_factor =
               translate_blend(ctx->Color.Blend[j].DstRGB);
         }

         blend->rt[i].alpha_func =
            translate_blend(ctx->Color.Blend[j].EquationA);

         if (ctx->Color.Blend[i].EquationA == GL_MIN ||
             ctx->Color.Blend[i].EquationA == GL_MAX) {
            /* Min/max are special */
            blend->rt[i].alpha_src_factor = PIPE_BLENDFACTOR_ONE;
            blend->rt[i].alpha_dst_factor = PIPE_BLENDFACTOR_ONE;
         }
         else {
            blend->rt[i].alpha_src_factor =
               translate_blend(ctx->Color.Blend[j].SrcA);
            blend->rt[i].alpha_dst_factor =
               translate_blend(ctx->Color.Blend[j].DstA);
         }
      }
   }
   else {
      /* no blending / logicop */
   }

   /* Colormask - maybe reverse these bits? */
   for (i = 0; i < num_state; i++) {
      if (ctx->Color.ColorMask[i][0])
         blend->rt[i].colormask |= PIPE_MASK_R;
      if (ctx->Color.ColorMask[i][1])
         blend->rt[i].colormask |= PIPE_MASK_G;
      if (ctx->Color.ColorMask[i][2])
         blend->rt[i].colormask |= PIPE_MASK_B;
      if (ctx->Color.ColorMask[i][3])
         blend->rt[i].colormask |= PIPE_MASK_A;
   }

   blend->dither = ctx->Color.DitherFlag;

   if (_mesa_is_multisample_enabled(ctx) &&
       !(ctx->DrawBuffer->_IntegerBuffers & 0x1)) {
      /* Unlike in gallium/d3d10 these operations are only performed
       * if both msaa is enabled and we have a multisample buffer.
       */
      blend->alpha_to_coverage = ctx->Multisample.SampleAlphaToCoverage;
      blend->alpha_to_one = ctx->Multisample.SampleAlphaToOne;
   }

   cso_set_blend(st->cso_context, blend);
}

void
st_update_blend_color(struct st_context *st)
{
   struct pipe_blend_color bc;

   COPY_4FV(bc.color, st->ctx->Color.BlendColorUnclamped);
   cso_set_blend_color(st->cso_context, &bc);
}
