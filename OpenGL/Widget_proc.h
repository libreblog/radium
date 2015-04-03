#ifndef OPENGL_WIDGET_PROC_H
#define OPENGL_WIDGET_PROC_H

#ifdef __cplusplus
#include <QWidget>

QWidget *GL_create_widget(QWidget *parent);
void GL_stop_widget(QWidget *widget);
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern void GL_EnsureMakeCurrentIsCalled(void);
  
extern void GL_erase_estimated_vblank(void);
extern double GL_get_estimated_vblank(void);

extern void GL_set_vsync(bool onoff);
extern bool GL_get_vsync(void);

extern void GL_set_multisample(int size);
extern int GL_get_multisample(void);

extern void GL_lock(void);
extern void GL_unlock(void);

volatile extern char *GE_vendor_string;
volatile extern char *GE_renderer_string;
volatile extern char *GE_version_string;
volatile extern uint32_t GE_opengl_version_flags;
  
extern void GE_set_curr_realline(int curr_realline);

#ifdef __cplusplus
}
#endif

#endif
