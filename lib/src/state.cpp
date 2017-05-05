/* This file is part of Dilay
 * Copyright © 2015-2017 Alexander Bau
 * Use and redistribute under the terms of the GNU General Public License
 */
#include <QPushButton>
#include <QShortcut>
#include <memory>
#include "cache.hpp"
#include "camera.hpp"
#include "config.hpp"
#include "history.hpp"
#include "mesh-util.hpp"
#include "mesh.hpp"
#include "scene.hpp"
#include "state.hpp"
#include "tool.hpp"
#include "tools.hpp"
#include "view/gl-widget.hpp"
#include "view/info-pane.hpp"
#include "view/main-window.hpp"
#include "view/tool-pane.hpp"
#include "view/tool-tip.hpp"

struct State::Impl
{
  State*                  self;
  ViewMainWindow&         mainWindow;
  Config&                 config;
  Cache&                  cache;
  Camera                  camera;
  History                 history;
  Scene                   scene;
  std::unique_ptr<Tool>   toolPtr;
  const char*             previousToolKey;
  std::vector<QShortcut*> shortcuts;

  Impl (State* s, ViewMainWindow& mW, Config& cfg, Cache& cch)
    : self (s)
    , mainWindow (mW)
    , config (cfg)
    , cache (cch)
    , camera (this->config)
    , history (this->config)
    , scene (this->config)
    , previousToolKey (nullptr)
  {
    this->scene.newDynamicMesh (this->config, MeshUtil::icosphere (4));
    this->resetTool ();
  }

  bool hasTool () const
  {
    return bool(this->toolPtr);
  }

  Tool& tool ()
  {
    assert (this->hasTool ());
    return *this->toolPtr;
  }

  template <typename T> void addToolShortcut (ViewToolTip& tip, ViewToolTip::Event event)
  {
    const QKeySequence keys = ViewToolTip::toQKeySequence (event, ViewToolTip::Modifier::None);

    tip.add (event, this->mainWindow.toolPane ().button (T::classKey ()).text ());
    this->shortcuts.push_back (new QShortcut (keys, &this->mainWindow));

    QObject::connect (this->shortcuts.back (), &QShortcut::activated,
                      [this]() { this->setTool (std::move (*new T (*this->self))); });
  }

  void addToggleToolShortcut (ViewToolTip& tip, ViewToolTip::Event event)
  {
    const QKeySequence keys = ViewToolTip::toQKeySequence (event, ViewToolTip::Modifier::None);

    tip.add (event, QObject::tr ("Toggle back"));
    this->shortcuts.push_back (new QShortcut (keys, &this->mainWindow));

    QObject::connect (this->shortcuts.back (), &QShortcut::activated, [this]() {
      assert (this->previousToolKey);
      this->mainWindow.toolPane ().button (this->previousToolKey).click ();
    });
  }

  void addExitToolShortcut (ViewToolTip& tip, ViewToolTip::Event event)
  {
    const QKeySequence keys = ViewToolTip::toQKeySequence (event, ViewToolTip::Modifier::None);

    tip.add (event, QObject::tr ("Exit"));
    this->shortcuts.push_back (new QShortcut (keys, &this->mainWindow));

    QObject::connect (this->shortcuts.back (), &QShortcut::activated, [this]() {
#ifndef NDEBUG
      if (this->hasTool ())
      {
        this->resetTool ();
      }
      else
      {
        this->mainWindow.close ();
      }
#else
      assert (this->hasTool ());
      this->resetTool ();
#endif
    });
  }

  void resetToolTip ()
  {
    for (QShortcut* s : this->shortcuts)
    {
      delete s;
    }
    this->shortcuts.clear ();
    this->mainWindow.infoPane ().resetToolTip ();
  }

  void addPermanentToolTip ()
  {
    ViewToolTip tip;
    tip.add (ViewToolTip::Event::MouseMiddle, QObject::tr ("Drag to rotate"));
    tip.add (ViewToolTip::Event::MouseMiddle, ViewToolTip::Modifier::Shift,
             QObject::tr ("Drag to move"));
    tip.add (ViewToolTip::Event::MouseMiddle, ViewToolTip::Modifier::Ctrl, QObject::tr ("Gaze"));

    this->mainWindow.infoPane ().addToolTip (tip);
  }

  void addSelectionToolTip ()
  {
    ViewToolTip tip;
    switch (this->mainWindow.toolPane ().selection ())
    {
      case ViewToolPaneSelection::Sculpt:
        if (this->hasTool () == false)
        {
          this->addToolShortcut<ToolSculptDraw> (tip, ViewToolTip::Event::D);
          this->addToolShortcut<ToolSculptCrease> (tip, ViewToolTip::Event::C);
          this->addToolShortcut<ToolSculptGrab> (tip, ViewToolTip::Event::G);
          this->addToolShortcut<ToolSculptFlatten> (tip, ViewToolTip::Event::F);
          this->addToolShortcut<ToolSculptSmooth> (tip, ViewToolTip::Event::S);
          this->addToolShortcut<ToolSculptPinch> (tip, ViewToolTip::Event::P);
          this->addToolShortcut<ToolSculptReduce> (tip, ViewToolTip::Event::R);
#ifndef NDEBUG
          this->addExitToolShortcut (tip, ViewToolTip::Event::Esc);
#endif
        }
        else
        {
          if (this->toolPtr->key () != ToolSculptSmooth::classKey ())
          {
            this->addToolShortcut<ToolSculptSmooth> (tip, ViewToolTip::Event::S);
          }
          else if (this->previousToolKey)
          {
            this->addToggleToolShortcut (tip, ViewToolTip::Event::S);
          }
          this->addExitToolShortcut (tip, ViewToolTip::Event::Esc);
        }
        break;
      case ViewToolPaneSelection::Sketch:
        if (this->hasTool () == false)
        {
#ifndef NDEBUG
          this->addExitToolShortcut (tip, ViewToolTip::Event::Esc);
#endif
        }
        else
        {
          this->addExitToolShortcut (tip, ViewToolTip::Event::Esc);
        }
        break;
    }
    this->mainWindow.infoPane ().addToolTip (tip);
  }

  void setTool (Tool&& tool)
  {
    if (this->hasTool ())
    {
      this->resetTool ();
    }
    this->toolPtr.reset (&tool);
    this->mainWindow.toolPane ().button (this->toolPtr->key ()).setChecked (true);
    this->resetToolTip ();

    ToolResponse initResponse = tool.initialize ();
    this->addSelectionToolTip ();
    this->addPermanentToolTip ();

    switch (initResponse)
    {
      case ToolResponse::None:
        this->handleToolResponse (ToolResponse::Redraw);
        break;
      default:
        this->handleToolResponse (initResponse);
        break;
    }
  }

  void resetTool ()
  {
    if (this->hasTool ())
    {
      this->previousToolKey = this->toolPtr->key ();
      this->mainWindow.toolPane ().button (this->toolPtr->key ()).setChecked (false);
      this->toolPtr->close ();
      this->toolPtr.reset ();

      this->mainWindow.toolPane ().resetProperties ();
      this->mainWindow.update ();
    }
    this->resetToolTip ();
    this->addSelectionToolTip ();
    this->addPermanentToolTip ();
  }

  void fromConfig ()
  {
    this->camera.fromConfig (this->config);
    this->history.fromConfig (this->config);
    this->scene.fromConfig (this->config);

    if (this->hasTool ())
    {
      this->toolPtr->fromConfig ();
    }
  }

  void undo ()
  {
    this->history.undo (*this->self);
    this->mainWindow.update ();
  }

  void redo ()
  {
    this->history.redo (*this->self);
    this->mainWindow.update ();
  }

  void handleToolResponse (ToolResponse response)
  {
    assert (this->hasTool ());
    switch (response)
    {
      case ToolResponse::None:
        break;
      case ToolResponse::Redraw:
        this->mainWindow.update ();
        break;
      case ToolResponse::Terminate:
        this->resetTool ();
        break;
    }
  }
};

DELEGATE3_BIG2_SELF (State, ViewMainWindow&, Config&, Cache&)

GETTER (ViewMainWindow&, State, mainWindow)
GETTER (Config&, State, config)
GETTER (Cache&, State, cache)
GETTER (Camera&, State, camera)
GETTER (History&, State, history)
GETTER (Scene&, State, scene)
DELEGATE (bool, State, hasTool)
DELEGATE (Tool&, State, tool)
DELEGATE1 (void, State, setTool, Tool&&)
DELEGATE (void, State, resetTool)
DELEGATE (void, State, fromConfig)
DELEGATE (void, State, undo)
DELEGATE (void, State, redo)
DELEGATE1 (void, State, handleToolResponse, ToolResponse)
