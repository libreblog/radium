/* Copyright 2012 Kjetil S. Matheussen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */

// Based on: (although probably not much left of anymore)


/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the demonstration applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <unistd.h>

#include <QTimer>
#include <QFont>
#include <QMainWindow>
#include <QGraphicsSceneMouseEvent>
#include <QAction>
#include <QMenu>

#include "../common/nsmtracker.h"
#include "../common/visual_proc.h"

#include "QM_MixerWidget.h"
#include "QM_view.h"
#include "QM_chip.h"

#include "../Qt/Qt_MyQButton.h"
#include "../Qt/Qt_MyQSlider.h"
#include "../Qt/mQt_mixer_widget_callbacks.h"

#include "../common/vector_proc.h"
#include "../common/hashmap_proc.h"
#include "../common/instruments_proc.h"
#include "../common/patch_proc.h"

#include "../Qt/EditorWidget.h"
#include "../Qt/Qt_instruments_proc.h"

#include "../common/undo.h"
#include "undo_mixer_proc.h"
#include "undo_mixer_connections_proc.h"
#include "undo_chip_position_proc.h"

#include "../audio/SoundProducer_proc.h"
#include "../audio/Mixer_proc.h"
#include "../audio/SoundPluginRegistry_proc.h"
#include "../audio/SoundPlugin_proc.h"
#include "../audio/audio_instrument_proc.h"


extern EditorWidget *g_editor;

struct HelpText : public QTimer{
  QGraphicsTextItem *_text;

  HelpText(MyScene *scene){
    QFont font = g_editor->main_window->font();
    font.setPointSize(15);
    font.setPixelSize(15);
    
    _text = scene->addText(
                           // "* Add objects with right mouse button\n"
                           "* Move objects with right mouse button.\n"
                           "\n"
                           "* Delete objects or connections by pressing SHIFT and click left.\n"
                           "  - Alternatively, click with middle mouse button.\n"
                           "\n"
                           "* Select more than one object by holding CTRL when clicking.\n"
                           "  - Alternatively, mark an area of objects with left mouse button.\n"
                           "\n"
                           "* To autoconnect a new object to an existing object, right click at the input or output of an existing object.\n"
                           "\n"
                           "* Double-click the name of a VST object to open GUI.\n"
                           "\n"
                           "* Zoom in and out by pressing CTRL and using the scroll wheel.\n"
                           ,
                           font);

    _text->setDefaultTextColor(g_editor->colors[11].light(70));
    _text->setPos(-150,-150);
    _text->setZValue(-1000);

    setSingleShot(true);
    setInterval(1000*60);
    start();
  }

  void 	timerEvent ( QTimerEvent * e ){
    delete _text;
    delete this;
  }
};


class SlotIndicatorItem : public QGraphicsItem
 {
 public:
     QRectF boundingRect() const
     {
         return QRectF(0,0,grid_width,grid_height);
     }

     void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                QWidget *widget)
     {
       QColor color(59,68,155,40);
       painter->setPen(color);
       painter->setBrush(QBrush(color,Qt::SolidPattern));
       painter->drawRoundedRect(port_width, 0, grid_width-(port_width*2), grid_height, grid_border, grid_border);
       painter->setBrush(QBrush());
     }
 };

static SlotIndicatorItem *_slot_indicator = NULL;

MyScene::MyScene(QWidget *parent)
  : _parent(parent)
  , _current_connection(NULL)
  , _current_from_chip(NULL)
  , _current_to_chip(NULL)
{
#if 0
  connect(this,SIGNAL(changed( const QList<QRectF> &)),
          this,SLOT(on_scene_changed(const QList<QRectF> &)));
#endif

  QColor color(40,40,40);
  _slot_indicator = new SlotIndicatorItem(); //addRect(0,0,chip_width,chip_width, QPen(color));//, Qt::SolidPattern);
  _slot_indicator->setZValue(-2);

  //new HelpText(this);
}

static void get_slot_coordinates(int slot_x, int slot_y, float &x1, float &y1, float &x2, float &y2){
  //const int border_width = (grid_width-slot_width)/2;

  x1 = slot_x*grid_width;// + port_width;
  x2 = x1 + grid_width;// - (port_width*2);

  y1 = slot_y*grid_height;// + border_width;
  y2 = y1 + grid_height;// - border_width;
}

static int get_slot_x(int x){
  if(x<0)
    return x/grid_width - 1;
  else
    return x/grid_width;
}

static int get_slot_y(int y){
  if(y<0)
    return y/grid_height - 1;
  else
    return y/grid_height;
}

static bool x_is_placed_on_right_side(float x){
  int slot_x_pos = get_slot_x(x)*grid_width;
  if(x < slot_x_pos+grid_width/2)
    return false;
  else
    return true;
}

static bool x_is_placed_on_left_side(float x){
  return !x_is_placed_on_right_side(x);
}

static int get_chip_slot_x(Chip *chip){
  return get_slot_x(chip->pos().x()+chip_width/2);
}

static int get_chip_slot_y(Chip *chip){
  return get_slot_y(chip->pos().y()+chip_height/2);
}

static bool chip_body_is_placed_at(Chip *chip, float mouse_x, float mouse_y){
  if(mouse_x < (get_chip_slot_x(chip)*grid_width + port_width))
    return false;

  else if(mouse_x > (get_chip_slot_x(chip)*grid_width + grid_width - port_width))
    return false;

  else
    return true;
}

static Chip *get_chip_with_port_at(QGraphicsScene *scene,int x, int y);
static void draw_slot(MyScene *myscene, float x, float y){
  float x1,y1,x2,y2;
  int slot_x = get_slot_x(x);
  int slot_y = get_slot_y(y);

  get_slot_coordinates(slot_x, slot_y, x1,y1,x2,y2);

#if 0
  if(_current_slot==NULL){
    QColor color(40,40,40);
    _current_slot = myscene->addRect(0,0,x2-x1,y2-y1, QPen(color), Qt::SolidPattern);
    //_current_slot->setZValue(-1);
  }
#endif

  _slot_indicator->setPos(x1,y1);
}

static void move_moving_chips(MyScene *myscene, float mouse_x, float mouse_y){
  //QPointF pos=event->scenePos();
  //printf("x: %f. y: %f\n",pos.x(),pos.y());

  for(unsigned int i=0;i<myscene->_moving_chips.size(); i++){
    Chip *chip = myscene->_moving_chips.at(i);
    float x = mouse_x + chip->_moving_x_offset;
    float y = mouse_y + chip->_moving_y_offset;
    chip->setPos(x,y);
  }

  draw_slot(myscene,mouse_x,mouse_y);

  // Find out whether the new position is on top of another chip. If so, set cursor.
  if(myscene->_moving_chips.size()>0){
    Chip *chip = MW_get_chip_at(mouse_x,mouse_y, myscene->_moving_chips.at(0));
    if(chip!=NULL){
      //printf("got chip at %f / %f %p / %p\n",mouse_x,mouse_y,chip,myscene->_moving_chips.at(0));      

      if(mouse_x < chip->pos().x()+(chip_width/2)){

        myscene->_parent->setCursor(QCursor(Qt::SizeBDiagCursor));

      }else{

        myscene->_parent->setCursor(QCursor(Qt::SizeFDiagCursor));

      }

    }else{

      //printf("got no chip\n");      
      myscene->_parent->setCursor(QCursor(Qt::SizeHorCursor));

    }
  }
}

void MW_set_selected_chip(Chip *chip){
  printf("MW_set_selected_chip called\n");
  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();
  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip2 = dynamic_cast<Chip*>(das_items.at(i));
    if(chip2!=NULL && chip2!=chip)
      chip2->setSelected(false);
  }
  chip->setSelected(true);
}

void MW_update_all_chips(void){
  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();
  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip2 = dynamic_cast<Chip*>(das_items.at(i));
    if(chip2!=NULL)
      chip2->update();
  }
}

static void start_moving_chips(MyScene *myscene, QGraphicsSceneMouseEvent * event, Chip *main_chip, float mouse_x, float mouse_y){
  Undo_Open();

  if(main_chip->isSelected()==false){
    if(event->modifiers() & Qt::ControlModifier)
      main_chip->setSelected(true);
    else
      MW_set_selected_chip(main_chip);
  }

  myscene->_moving_chips.push_back(main_chip);

  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip = dynamic_cast<Chip*>(das_items.at(i));
    if(chip!=NULL && chip->isSelected()==true){

      Undo_ChipPos_CurrPos(SP_get_plugin(chip->_sound_producer)->patch);

      chip->_moving_x_offset = chip->scenePos().x() - mouse_x;
      chip->_moving_y_offset = chip->scenePos().y() - mouse_y;

      if(chip!=main_chip)
        myscene->_moving_chips.push_back(chip);
    }
  }

  myscene->addItem(_slot_indicator);
  draw_slot(myscene,mouse_x,mouse_y);
}

static void move_chip_to_slot(Chip *chip, float x, float y){
  float x1,x2,y1,y2;

  get_slot_coordinates(get_slot_x(x), get_slot_y(y), x1,y1,x2,y2);
  chip->setPos(x1,y1+grid_border);
}

static Connection *find_clean_connection_at(MyScene *scene, float x, float y);

// Also kicks.
static void autoconnect_chip(MyScene *myscene, Chip *chip, float x, float y){
  bool do_autoconnect = chip->_connections.size()==0; // don't autoconnect if the chip already has connections.

  Chip *chip_under = MW_get_chip_at(x,y,chip);

  if(chip_under != NULL){
    if(x_is_placed_on_left_side(x)){

      CHIP_kick_right(chip_under);
      if(do_autoconnect){
        Undo_MixerConnections_CurrPos();
        CHIP_connect_right(myscene, chip, chip_under);
      }

    }else{

      CHIP_kick_left(chip_under);
      if(do_autoconnect){
        Undo_MixerConnections_CurrPos();
        CHIP_connect_left(myscene,chip_under, chip);
      }
    }

  }else if(do_autoconnect){

    Connection *connection = find_clean_connection_at(myscene, x, y);
    if(connection!=NULL){
      Undo_MixerConnections_CurrPos();

      Chip *from = connection->from;
      Chip *to = connection->to;
      CONNECTION_delete_connection(connection);
      CHIP_connect_chips(myscene, from, chip);
      CHIP_connect_chips(myscene, chip, to);
    }

  }
}

static bool cleanup_a_chip_position(MyScene *myscene){
  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip = dynamic_cast<Chip*>(das_items.at(i));
    if(chip!=NULL){
      Chip *chip_under = MW_get_chip_at(chip->pos().x()+grid_width/2, chip->pos().y()+grid_height/2, chip);
      if(chip_under!=NULL){
        CHIP_kick_left(chip_under);
        return true;
      }
    }
  }

  return false;
}

// Make sure no chips are placed in the same slot
static void cleanup_chip_positions(MyScene *myscene){
  while(cleanup_a_chip_position(myscene)==true);
}

static void stop_moving_chips(MyScene *myscene, float mouse_x, float mouse_y){

  myscene->removeItem(_slot_indicator);
  myscene->_parent->setCursor(QCursor(Qt::ArrowCursor));

  Chip *main_chip = myscene->_moving_chips.at(0);
  float main_chip_x = main_chip->pos().x();
  float main_chip_y = main_chip->pos().y();

  for(unsigned int i=0;i<myscene->_moving_chips.size();i++){
    Chip *chip = myscene->_moving_chips.at(i);
    //float x = chip->_moving_x_offset+mouse_x;
    //float y = chip->_moving_y_offset+mouse_y;
    float x = mouse_x + (chip->pos().x() - main_chip_x);
    float y = mouse_y + (chip->pos().y() - main_chip_y);
    //printf("x: %d, mouse_x: %d, chip_x: %d, main_chip_x: %d, diff: %d\n",(int)x,(int)mouse_x,(int)chip->pos().x(),(int)main_chip_x,(int)
    move_chip_to_slot(chip, x, y);
    if(myscene->_moving_chips.size()==1)
      autoconnect_chip(myscene, chip, mouse_x, mouse_y);
  }

  cleanup_chip_positions(myscene);

  Undo_Close();

  myscene->_moving_chips.clear();
}

void MyScene::mouseMoveEvent ( QGraphicsSceneMouseEvent * event ){
  //printf("mousemove: %p\n",_current_connection);
  
  QPointF pos=event->scenePos();

  if(_current_connection != NULL){

    int x1,y1;

    if(_current_from_chip != NULL){
      x1 = CHIP_get_output_port_x(_current_from_chip);
      y1 = CHIP_get_port_y(_current_from_chip);
    }else{
      x1 = CHIP_get_input_port_x(_current_to_chip);
      y1 = CHIP_get_port_y(_current_to_chip);
    }

    int x2 = pos.x();
    int y2 = pos.y();

    _current_connection->setLine(x1,y1,x2,y2);

    event->accept();

  } else if(_moving_chips.size()>0){

    move_moving_chips(this,pos.x(),pos.y());
        
  }else{
    
    QGraphicsScene::mouseMoveEvent(event);
  
  }

}

Chip *MW_get_chip_at(float x, float y, Chip *except){
  int slot_x = get_slot_x(x);
  int slot_y = get_slot_y(y);

  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip = dynamic_cast<Chip*>(das_items.at(i));
    if(chip!=NULL && chip!=except){
      QPointF pos = chip->pos();
      if(get_slot_x(pos.x()+grid_width/2)==slot_x && get_slot_y(pos.y()+grid_height/2)==slot_y)
        return chip;
    }
  }

  return NULL;
}

static Connection *find_clean_connection_at(MyScene *scene, float x, float y){
  int slot_x = get_slot_x(x);
  int slot_y = get_slot_y(y);

  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  for (int i = 0; i < das_items.size(); ++i) {
    Connection *connection = dynamic_cast<Connection*>(das_items.at(i));
    if(connection != NULL){
      if(get_chip_slot_y(connection->from)==slot_y && get_chip_slot_y(connection->to)==slot_y){
        if(get_chip_slot_x(connection->from)<slot_x && get_chip_slot_x(connection->to)>slot_x)
          return connection;
      }
    }
  }

  return NULL;
}

static Chip *get_chip_with_port_at(QGraphicsScene *scene,int x, int y){
  QList<QGraphicsItem *> das_items = scene->items();

  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip = dynamic_cast<Chip*>(das_items.at(i));
    if(chip!=NULL){
      if(CHIP_is_at_input_port(chip,x,y))
        return chip;
      if(CHIP_is_at_output_port(chip,x,y))
        return chip;
    }
  }

  return NULL;
}

// Returns false if there are more than one before or after chip.
// Also returns false if there are 0 before chips, or 0 after chips.
// Neither 'before' nor 'after' is set if the function returns false.
static bool get_before_and_after_chip(Chip *chip, Chip **before, Chip **after){
  std::vector<Connection*> connections=chip->_connections;
  if(connections.size()!=2)
    return false;

  if(connections.at(0)->to==chip && connections.at(1)->to==chip)
    return false;

  if(connections.at(0)->from==chip && connections.at(1)->from==chip)
    return false;

  if(connections.at(0)->to==chip){
    *before = connections.at(0)->from;
    *after  = connections.at(1)->to;
  }else{
    *before = connections.at(1)->from;
    *after  = connections.at(0)->to;
  }

  return true;
}

static bool mousepress_delete_chip(MyScene *scene, QGraphicsSceneMouseEvent * event, QGraphicsItem *item, float mouse_x, float mouse_y){
  printf("Going to delete\n");
  Chip *chip = dynamic_cast<Chip*>(item);
  if(chip!=NULL){
    printf("Got chip\n");

    Chip *before=NULL;
    Chip *after=NULL;
    get_before_and_after_chip(chip, &before, &after);
    
    struct Instruments *instrument = get_audio_instrument();
    VECTOR_FOR_EACH(struct Patch *,patch,&instrument->patches){
      if(patch->patchdata==SP_get_plugin(chip->_sound_producer)){
        printf("Found patch\n");
        PATCH_delete(patch);
        break;
      }
    }END_VECTOR_FOR_EACH;

    if(before!=NULL)
      CHIP_connect_chips(scene, before, after);

    event->accept();
    return true;
  }

  return false;
}

static bool mousepress_start_connection(MyScene *scene, QGraphicsSceneMouseEvent * event, QGraphicsItem *item, float mouse_x, float mouse_y){

  Chip *chip = get_chip_with_port_at(scene,mouse_x,mouse_y);
  if(chip!=NULL){

    if(CHIP_is_at_output_port(chip,mouse_x,mouse_y))
      scene->_current_from_chip = chip;

    else if(CHIP_is_at_input_port(chip,mouse_x,mouse_y))
      scene->_current_to_chip = chip;

    if(scene->_current_from_chip!=NULL || scene->_current_to_chip!=NULL){
      //printf("x: %d, y: %d. Item: %p. input/output: %d/%d\n",(int)mouse_x,(int)mouse_y,item,_current_input_port,_current_output_port);
            
      scene->_current_connection = new Connection(scene);
      scene->addItem(scene->_current_connection);
              
      scene->_current_connection->setLine(mouse_x,mouse_y,mouse_x,mouse_y);
              
      event->accept();
      return true;
    }
  }

  return false;
}

static bool mousepress_delete_connection(MyScene *scene, QGraphicsSceneMouseEvent * event, QGraphicsItem *item, float mouse_x, float mouse_y){
  Connection *connection = dynamic_cast<Connection*>(item);
  if(connection==NULL){
    QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();
    for (int i = 0; i < das_items.size(); ++i) {
      connection = dynamic_cast<Connection*>(das_items.at(i));
      if(connection!=NULL && connection->isUnderMouse()==true)
        break;
      else
        connection = NULL;
    }
  }

  if(connection!=NULL){
    Undo_MixerConnections_CurrPos();
    CONNECTION_delete_connection(connection);
    event->accept();
    return true;
  }
  return false;
}

static bool mousepress_select_chip(MyScene *scene, QGraphicsSceneMouseEvent * event, QGraphicsItem *item, float mouse_x, float mouse_y){
  Chip *chip = dynamic_cast<Chip*>(item);

  if(chip!=NULL){
    struct Patch *patch = SP_get_plugin(chip->_sound_producer)->patch;
    struct Instruments *instrument = get_audio_instrument();
    printf("Calling pp_update\n");
    instrument->PP_Update(instrument,patch);
    
    start_moving_chips(scene,event,chip,mouse_x,mouse_y);
    event->accept();
    return true;
  }

  return false;
}

static bool mousepress_create_chip(MyScene *scene, QGraphicsSceneMouseEvent * event, QGraphicsItem *item, float mouse_x, float mouse_y){

  Chip *chip_under = MW_get_chip_at(mouse_x,mouse_y,NULL);
  if(chip_under!=NULL){
    if(chip_body_is_placed_at(chip_under, mouse_x, mouse_y)==true)
      return false;
  }

  scene->addItem(_slot_indicator);
  draw_slot(scene,mouse_x,mouse_y);

  Undo_Open();{

    SoundPlugin *plugin = add_new_audio_instrument_widget(NULL,mouse_x,mouse_y,false,NULL);

    if(plugin!=NULL){

      Chip *chip = find_chip_for_plugin(scene, plugin);
      
      autoconnect_chip(scene, chip, mouse_x, mouse_y);
    }

  }Undo_Close();

  scene->removeItem(_slot_indicator);
  event->accept();
  return true;
}

static bool event_can_delete(QGraphicsSceneMouseEvent *event){
  if(event->button()==Qt::MiddleButton)
    return true;

  else if(event->button()==Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier))
    return true;

  else
    return false;
}

void MyScene::mousePressEvent(QGraphicsSceneMouseEvent *event){
  printf("mousepress: %p\n",_current_connection);

  QPointF pos=event->scenePos();
  float mouse_x = pos.x();
  float mouse_y = pos.y();

  QGraphicsItem *item = itemAt(event->scenePos());

  printf("mouse button: %d %d\n",event->button(),Qt::MiddleButton);

  if(event_can_delete(event))
    if(mousepress_delete_chip(this,event,item,mouse_x,mouse_y)==true)
      return;

  if(event_can_delete(event))
    if(mousepress_delete_connection(this,event,item,mouse_x,mouse_y)==true)
      return;

  if(event->button()==Qt::LeftButton)
    if(mousepress_start_connection(this,event,item,mouse_x,mouse_y)==true)
      return;

  if(event->button()==Qt::RightButton){
    if(mousepress_create_chip(this,event,item,mouse_x,mouse_y)==true)
      return;

    if(mousepress_select_chip(this,event,item,mouse_x,mouse_y)==true)
      return;
  }

  QGraphicsScene::mousePressEvent(event);
}

void MyScene::mouseReleaseEvent ( QGraphicsSceneMouseEvent * event ){
  printf("mouse release: %p\n",_current_connection);

  QPointF pos=event->scenePos();
  float mouse_x = pos.x();
  float mouse_y = pos.y();

  if(_current_connection!=NULL){

    Chip *chip = get_chip_with_port_at(this,mouse_x,mouse_y);

    if(chip!=NULL){ // TODO: Must check if the connection is already made.

      if(_current_from_chip != NULL){

        if(CHIP_is_at_input_port(chip, mouse_x, mouse_y)){
          Undo_MixerConnections_CurrPos();
          CHIP_connect_chips(this, _current_from_chip, chip);
        }

      }else if(_current_to_chip != NULL){

        if(CHIP_is_at_output_port(chip, mouse_x, mouse_y)){
          Undo_MixerConnections_CurrPos();
          CHIP_connect_chips(this, chip, _current_to_chip);
        }

      }
    }

    removeItem(_current_connection);
    delete _current_connection;     
    _current_connection = NULL;
    _current_from_chip = NULL;
    _current_to_chip = NULL;
    event->accept();
    
  }else if(_moving_chips.size()>0){

    stop_moving_chips(this,mouse_x,mouse_y);
    event->accept();

  }else{

    QGraphicsScene::mouseReleaseEvent(event);

  }
}

MixerWidget *g_mixer_widget = NULL;

MixerWidget::MixerWidget(QWidget *parent)
    : QWidget(parent)
    , scene(this)
{
  if(g_mixer_widget!=NULL){
    fprintf(stderr,"Error. More than one MixerWidget created.\n");
    abort();
  }

  g_mixer_widget = this;

    populateScene();

    QGridLayout *gridLayout = new QGridLayout(this);  
    gridLayout->setContentsMargins(0, 0, 0, 0);

#if 0
    View *view = new View("Das mixer",this);
    view->view()->setScene(&scene);
    gridLayout->addWidget(view, 0, 0, 1, 1);

#else

    Mixer_widget *mixer_widget = new Mixer_widget(this);
    mixer_widget->view->setScene(&scene);
    gridLayout->addWidget(mixer_widget, 0, 0, 1, 1);

#endif

#if 0
    h1Splitter = new QSplitter;
    h2Splitter = new QSplitter;
    
    QSplitter *vSplitter = new QSplitter;
    vSplitter->setOrientation(Qt::Vertical);
    vSplitter->addWidget(h1Splitter);
    vSplitter->addWidget(h2Splitter);

    View *view = new View("Top left view");
    view->view()->setScene(scene);
    h1Splitter->addWidget(view);

    view = new View("Top right view");
    view->view()->setScene(scene);
    h1Splitter->addWidget(view);

    view = new View("Bottom left view");
    view->view()->setScene(scene);
    h2Splitter->addWidget(view);

    view = new View("Bottom right view");
    view->view()->setScene(scene);
    h2Splitter->addWidget(view);

    QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget(vSplitter);
    setLayout(layout);
#endif

    setWindowTitle(tr("Chip Demo"));
}

bool GFX_MixerIsVisible(void){
  return !g_mixer_widget->isHidden();
}
void GFX_ShowMixer(void){
  g_mixer_widget->show();
}
void GFX_HideMixer(void){
  g_mixer_widget->hide();
}

void GFX_showHideMixerWidget(void){
  if(g_mixer_widget->isHidden())
    g_mixer_widget->show();
  else
    g_mixer_widget->hide();
}

static int g_main_pipe_patch_id = -1;

SoundPlugin *get_main_pipe(void){
  struct Patch *patch = PATCH_get_from_id(g_main_pipe_patch_id);
  SoundPlugin *plugin = (SoundPlugin*)patch->patchdata;

  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();
  for (int i = 0; i < das_items.size(); ++i){
    Chip *chip = dynamic_cast<Chip*>(das_items.at(i));
    if(chip!=NULL){
      if(plugin==SP_get_plugin(chip->_sound_producer))
        return plugin;
    }
  }

  RError("no system bus");
  return NULL;
}

void MixerWidget::populateScene()
{
#if 0
  SoundPluginType *type1 = PR_get_plugin(0);
  SoundPlugin *plugin1 = PLUGIN_create_plugin(type1);

  SoundPluginType *type2 = PR_get_plugin(1);
  SoundPlugin *plugin2 = PLUGIN_create_plugin(type2);

  SoundProducer *sound_producer1 = SP_create(plugin1);
  SoundProducer *sound_producer2 = SP_create(plugin2);

  Chip *from = new Chip(&scene,sound_producer1,20,30);    
  Chip *to = new Chip(&scene,sound_producer2,50,80);

  connect_chips(&scene,from, 0, to, 1);
#endif

  // NB! main_pipe must be created first. The patch id of main_pipe must be 0.
  SoundPluginType *pipe_type = PR_get_plugin_type_by_name("Pipe","Pipe");
  SoundPlugin *main_pipe = add_new_audio_instrument_widget(pipe_type, grid_width, 0,false,"Main Pipe");
  g_main_pipe_patch_id = main_pipe->patch->id;

  SoundPluginType *system_out = PR_get_plugin_type_by_name("Jack","System Out");
  SoundPlugin *system_out_plugin = add_new_audio_instrument_widget(system_out, grid_width*2, 0,false,"Main Out");

  SoundPluginType *bus1 = PR_get_plugin_type_by_name("Bus","Bus 1");
  SoundPlugin *bus1_plugin = add_new_audio_instrument_widget(bus1, 0, 0,false,"Bus 1");

  SoundPluginType *bus2 = PR_get_plugin_type_by_name("Bus","Bus 2");
  SoundPlugin *bus2_plugin = add_new_audio_instrument_widget(bus2, 0, grid_height,false,"Bus 2");


  CHIP_connect_chips(&scene, main_pipe, system_out_plugin);
  CHIP_connect_chips(&scene, bus1_plugin, main_pipe);
  CHIP_connect_chips(&scene, bus2_plugin, main_pipe);
}

void MW_autoconnect_plugin(SoundPlugin *plugin){
  SoundPlugin *main_pipe = get_main_pipe();

  if(plugin->type->num_outputs>0)
    CHIP_connect_chips(&g_mixer_widget->scene, plugin, main_pipe);
}


static float find_next_autopos_y(Chip *system_chip){
  int x = system_chip->x()-grid_width;
  int y = system_chip->y();
  while(MW_get_chip_at(x,y,NULL)!=NULL)
    y+=grid_height;
  return y;
}

// MW_add_plugin/MW_delete_plugin are one of two entry points for audio plugins.
// Creating/deleting a plugin goes through here, not through audio/.
//
// The other entry point is CHIP_create_from_state, which is called from undo/redo and load.
//
SoundPlugin *MW_add_plugin(SoundPluginType *plugin_type, float x, float y){
  SoundPlugin     *plugin         = PLUGIN_create_plugin(plugin_type);
  if(plugin==NULL)
    return NULL;

  SoundProducer   *sound_producer = SP_create(plugin);
  if(x<=-100000){
    SoundPlugin *main_pipe    = get_main_pipe();
    Chip        *system_chip  = find_chip_for_plugin(&g_mixer_widget->scene, main_pipe);
    x                         = system_chip->x()-grid_width;
    y                         = find_next_autopos_y(system_chip);
    printf("Adding at pos %f %f\n",x,y);
  }
  Chip *chip = new Chip(&g_mixer_widget->scene,sound_producer,x,y);

  move_chip_to_slot(chip, x,y);
  return plugin;
}

void MW_delete_plugin(SoundPlugin *plugin){
  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip = dynamic_cast<Chip*>(das_items.at(i));
    if(chip!=NULL){
      SoundProducer *producer = chip->_sound_producer;
      if(SP_get_plugin(producer)==plugin){
        delete chip; // audio connections are deleted via ~Chip(). (Yes, it's somewhat messy)
        SP_delete(producer);
        struct Patch *patch = plugin->patch;
        PLUGIN_delete_plugin(plugin);
        patch->patchdata = NULL; // Correct thing to do. A subtle bug in GFX_update_all_instrument_widgets prompted me to do add it (QT tabs are note updated right away). Somewhat messy this too.
        return;
      }
    }
  }
}

namespace{
  struct MyQAction : public QAction{
    MyQAction(const char* name, QMenu *menu, SoundPluginType *plugin_type)
      : QAction(name,menu)
      , plugin_type(plugin_type)
    {}
    SoundPluginType *plugin_type;
  };
}

static unsigned int entries_i;
static void menu_up(QMenu *menu, std::vector<PluginMenuEntry> entries){
  while(entries_i < entries.size()){
    PluginMenuEntry entry = entries.at(entries_i);
    entries_i++;

    if(entry.type==PluginMenuEntry::IS_SEPARATOR){
      menu->insertSeparator();

    }else if(entry.type==PluginMenuEntry::IS_LEVEL_UP){
      const char *name = entry.level_up_name;
      QMenu *new_menu = new QMenu(name,menu);
      menu->addMenu(new_menu);
      menu_up(new_menu,entries);

    }else if(entry.type==PluginMenuEntry::IS_LEVEL_DOWN){
      return;

    }else{
      const char *name = entry.plugin_type->name;
      menu->addAction(new MyQAction(name,menu,entry.plugin_type));
    }
  }
}

SoundPluginType *MW_popup_plugin_selector(void){
  QMenu menu(0);
  entries_i = 0;

  menu_up(&menu, PR_get_menu_entries());

  MyQAction *action = dynamic_cast<MyQAction*>(menu.exec(QCursor::pos()));

  if(action!=NULL)
    return action->plugin_type;
  else
    return NULL;
}

#if 0
static bool delete_a_connection(){
  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  for (int i = 0; i < das_items.size(); ++i) {
    Connection *connection = dynamic_cast<Connection*>(das_items.at(i));
    if(connection!=NULL){
      CONNECTION_delete_connection(connection);
      return true;
    }
  }
  return false;
}
#endif

static void MW_cleanup_connections(void){
  //while(delete_a_connection());
  std::vector<SoundProducer*> producers;
  std::vector<Connection*> connections;
  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip = dynamic_cast<Chip*>(das_items.at(i));
    if(chip!=NULL)
      producers.push_back(chip->_sound_producer);
    else{
      Connection *connection = dynamic_cast<Connection*>(das_items.at(i));
      if(connection!=NULL)
        connections.push_back(connection);
    }
  }

  SP_remove_all_links(producers);

  for(unsigned int i=0;i<connections.size();i++)
    CONNECTION_delete_a_connection_where_all_links_have_been_removed(connections.at(i));
}



static bool delete_a_chip(){
  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip = dynamic_cast<Chip*>(das_items.at(i));
    if(chip!=NULL){
      MW_delete_plugin(SP_get_plugin(chip->_sound_producer));
      return true;
    }
  }
  return false;
}

void MW_cleanup(void){
  while(delete_a_chip()); // remove all chips. All connections are removed as well when removing all chips.
}

static hash_t *MW_get_chips_state(void){
  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  hash_t *chips = HASH_create(das_items.size()/2);
    
  int num_chips=0;
  for (int i = 0; i < das_items.size(); ++i) {
    Chip *chip = dynamic_cast<Chip*>(das_items.at(i));
    if(chip!=NULL)
      HASH_put_hash_at(chips, num_chips++, CHIP_get_state(chip));
  }
  
  HASH_put_int(chips, "num_chips", num_chips);

  return chips;
}

hash_t *MW_get_connections_state(void){
  QList<QGraphicsItem *> das_items = g_mixer_widget->scene.items();

  hash_t *connections = HASH_create(das_items.size());

  int num_connections=0;
  for (int i = 0; i < das_items.size(); ++i) {
    Connection *connection = dynamic_cast<Connection*>(das_items.at(i));
    if(connection!=NULL)
      if(connection->from!=NULL && connection->to!=NULL) // dont save ongoing connections.
        HASH_put_hash_at(connections, num_connections++, CONNECTION_get_state(connection));
  }
  
  HASH_put_int(connections, "num_connections", num_connections);

  return connections;
}

hash_t *MW_get_state(void){
  hash_t *state = HASH_create(2);

  HASH_put_hash(state, "chips", MW_get_chips_state());
  HASH_put_hash(state, "connections", MW_get_connections_state());

  return state;
}

static void MW_create_chips_from_state(hash_t *chips){
  for(int i=0;i<HASH_get_int(chips, "num_chips");i++)
    CHIP_create_from_state(HASH_get_hash_at(chips, i));
}

static void MW_create_connections_from_state_internal(hash_t *connections){
  for(int i=0;i<HASH_get_int(connections, "num_connections");i++)
    CONNECTION_create_from_state(&g_mixer_widget->scene, HASH_get_hash_at(connections, i));
}

void MW_create_connections_from_state(hash_t *connections){
  MW_cleanup_connections();
  MW_create_connections_from_state_internal(connections);
}

// Patches must be created before calling this one.
// However, patch->patchdata are created here.
void MW_create_from_state(hash_t *state){

  MW_cleanup();

  MW_create_chips_from_state(HASH_get_hash(state, "chips"));
  MW_create_connections_from_state_internal(HASH_get_hash(state, "connections"));

  GFX_update_all_instrument_widgets();
}

// This function is called when loading a song saved with a version of radium made before the audio system was added.
void MW_create_plain(void){
  MW_cleanup();
  g_mixer_widget->populateScene();
}

#include "mQM_MixerWidget.cpp"
