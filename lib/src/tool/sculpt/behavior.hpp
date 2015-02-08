#ifndef DILAY_TOOL_SCULPT_BEHAVIOR
#define DILAY_TOOL_SCULPT_BEHAVIOR

#include <glm/fwd.hpp>
#include "macro.hpp"

class ConfigProxy;
class QDoubleSpinBox;
class SculptBrush;
class State;
class ViewCursor;
class ViewProperties;
class ViewToolTip;
class WingedFaceIntersection;

class ToolSculptBehavior {
  public:
    DECLARE_BIG3_VIRTUAL (ToolSculptBehavior, ConfigProxy&, State&)

    void setupBrushAndCursor   ();
    void setupProperties       (ViewProperties&);
    void setupToolTip          (ViewToolTip&);
    void render                () const;
    void mouseMoveEvent        (const glm::ivec2&, bool);
    void mouseLeftPressEvent   (const glm::ivec2&);
    void mouseLeftReleaseEvent ();
    void mouseWheelEvent       (bool);
    void close                 ();

  protected:
    ConfigProxy& config              () const;
    State&       state               () const;
    ViewCursor&  cursor              () const;
    bool         intersectsSelection (const glm::ivec2&, WingedFaceIntersection&) const;
    void         setupBrush          (SculptBrush&) const;
    void         sculpt              (const SculptBrush&);
    void         sculpt              ();

  private:
    IMPLEMENTATION

    virtual SculptBrush& brush                  () const = 0;
    virtual void         runSetupBrush          () = 0;
    virtual void         runSetupProperties     (ViewProperties&) = 0;
    virtual void         runSetupToolTip        (ViewToolTip&) = 0;
    virtual void         runMouseLeftPressEvent (const glm::ivec2&) = 0;
    virtual void         runMouseMoveEvent      (const glm::ivec2&, bool) = 0;
};

#define DECLARE_TOOL_BEHAVIOR(name)                                                         \
  class name : public ToolSculptBehavior {                                                  \
    public:  DECLARE_BIG3 (name, ConfigProxy&, State&)                                      \
    private: IMPLEMENTATION                                                                 \
             SculptBrush& brush                  () const;                                  \
             void         runSetupBrush          ();                                        \
             void         runSetupProperties     (ViewProperties&);                         \
             void         runSetupToolTip        (ViewToolTip&);                            \
             void         runMouseLeftPressEvent (const glm::ivec2&);                       \
             void         runMouseMoveEvent      (const glm::ivec2&, bool);                 \
  };

#define DELEGATE_TOOL_BEHAVIOR(name)                                                        \
  DELEGATE_BIG3_BASE (name, (ConfigProxy& c, State& s), (this), ToolSculptBehavior, (c,s))  \
  GETTER_CONST (SculptBrush&, name, brush)                                                  \
  DELEGATE     (void, name, runSetupBrush)                                                  \
  DELEGATE1    (void, name, runSetupProperties, ViewProperties&);                           \
  DELEGATE1    (void, name, runSetupToolTip, ViewToolTip&);                                 \
  DELEGATE1    (void, name, runMouseLeftPressEvent, const glm::ivec2&)                      \
  DELEGATE2    (void, name, runMouseMoveEvent, const glm::ivec2&, bool)

#endif
