#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <limits>
#include "octree.hpp"
#include "winged/face.hpp"
#include "winged/vertex.hpp"
#include "primitive/triangle.hpp"
#include "primitive/ray.hpp"
#include "intersection.hpp"
#include "fwd-winged.hpp"
#include "adjacent-iterator.hpp"
#include "id-map.hpp"
#include "winged/face-intersection.hpp"
#include "primitive/aabox.hpp"

#ifdef DILAY_RENDER_OCTREE
#include "mesh.hpp"
#include "renderer.hpp"
#include "color.hpp"
#include "render-mode.hpp"
#endif

namespace {
  typedef std::unique_ptr <OctreeNode::Impl> Child;

  /** Container for face to insert */
  class FaceToInsert {
    public:
      FaceToInsert (const WingedFace& f, const PrimTriangle& t, bool s)
        : id               (f.id           ())
        , edge             (f.edge         ())
        , faceIndex        (f.index        ())
        , primitive        (t)
        , center           (t.center       ())
        , oneDimExtent     (t.oneDimExtent ())
        , savePrimitive    (s)
        {}

      const Id            id;
      WingedEdge* const   edge;
      const unsigned int  faceIndex;
      const PrimTriangle& primitive;
      const glm::vec3     center;
      const float         oneDimExtent;
      const bool          savePrimitive;
  };
}

/* node indices:
 *   (-,-,-) -> 0
 *   (-,-,+) -> 1
 *   (-,+,-) -> 2
 *   (-,+,+) -> 3
 *   (+,-,-) -> 4
 *   (+,-,+) -> 5
 *   (+,+,-) -> 6
 *   (+,+,+) -> 7
 */

/** Octree node implementation */
struct OctreeNode::Impl {
  IdObject             id;
  OctreeNode           node;
  const glm::vec3      center;
  const float          width;
  std::vector <Child>  children;
  const int            depth;
  Faces                faces;
  OctreeNode::Impl*    parent;
  IdMap <PrimTriangle> primitives;
  // cf. move constructor

  static constexpr float relativeMinFaceExtent = 0.1f;

#ifdef DILAY_RENDER_OCTREE
  Mesh mesh;
#endif

  Impl (const glm::vec3& c, float w, int d, Impl* p) 
    : node   (this)
    , center (c)
    , width  (w)
    , depth  (d)
    , parent (p) 
  {
      static_assert (Impl::relativeMinFaceExtent < 0.5f, "relativeMinFaceExtent must be smaller than 0.5f");

#ifdef DILAY_RENDER_OCTREE
      float q = w * 0.5f;
      this->mesh.addVertex (glm::vec3 (-q, -q, -q)); 
      this->mesh.addVertex (glm::vec3 (-q, -q,  q)); 
      this->mesh.addVertex (glm::vec3 (-q,  q, -q)); 
      this->mesh.addVertex (glm::vec3 (-q,  q,  q)); 
      this->mesh.addVertex (glm::vec3 ( q, -q, -q)); 
      this->mesh.addVertex (glm::vec3 ( q, -q,  q)); 
      this->mesh.addVertex (glm::vec3 ( q,  q, -q)); 
      this->mesh.addVertex (glm::vec3 ( q,  q,  q)); 

      this->mesh.addIndex (0); this->mesh.addIndex (1); 
      this->mesh.addIndex (1); this->mesh.addIndex (3); 
      this->mesh.addIndex (3); this->mesh.addIndex (2); 
      this->mesh.addIndex (2); this->mesh.addIndex (0); 

      this->mesh.addIndex (4); this->mesh.addIndex (5); 
      this->mesh.addIndex (5); this->mesh.addIndex (7); 
      this->mesh.addIndex (7); this->mesh.addIndex (6); 
      this->mesh.addIndex (6); this->mesh.addIndex (4); 

      this->mesh.addIndex (1); this->mesh.addIndex (5); 
      this->mesh.addIndex (5); this->mesh.addIndex (7); 
      this->mesh.addIndex (7); this->mesh.addIndex (3); 
      this->mesh.addIndex (3); this->mesh.addIndex (1); 

      this->mesh.addIndex (4); this->mesh.addIndex (6); 
      this->mesh.addIndex (6); this->mesh.addIndex (2); 
      this->mesh.addIndex (2); this->mesh.addIndex (0); 
      this->mesh.addIndex (0); this->mesh.addIndex (4); 

      this->mesh.position   (this->center);
      this->mesh.renderMode (RenderMode::Wireframe);
      this->mesh.bufferData ();
#endif
  }
        Impl            (const Impl&) = delete;
  const Impl& operator= (const Impl&) = delete;

  Impl (Impl&& source) 
    : node       (std::move (this))
    , center     (std::move (source.center))
    , width      (std::move (source.width))
    , children   (std::move (source.children))
    , depth      (std::move (source.depth))
    , faces      (std::move (source.faces))
    , parent     (std::move (source.parent))
    , primitives (std::move (source.primitives))
#ifdef DILAY_RENDER_OCTREE
    , mesh     (std::move (source.mesh))
  { this->mesh.bufferData (); }
#else
  {}
#endif

  void render () {
#ifdef DILAY_RENDER_OCTREE
    this->mesh.renderBegin ();
    glDisable (GL_DEPTH_TEST);
    Renderer :: setColor3 (Color (1.0f, 1.0f, 0.0f));
    glDrawElements (GL_LINES, this->mesh.numIndices (), GL_UNSIGNED_INT, (void*)0);
    glEnable (GL_DEPTH_TEST);
    this->mesh.renderEnd ();

    for (Child& c : this->children) 
      c->render ();
#else
    assert (false);
#endif
  }

  bool approxContains (const glm::vec3& v) const {
    const glm::vec3 min = this->center - glm::vec3 (this->width * 0.5f);
    const glm::vec3 max = this->center + glm::vec3 (this->width * 0.5f);
    return glm::all ( glm::lessThanEqual (min, v) )
       &&  glm::all ( glm::lessThanEqual (v, max) );
  }

  bool approxContains (const FaceToInsert& f) const {
    return this->approxContains (f.center) && f.oneDimExtent <= this->width;
  }

  unsigned int childIndex (const glm::vec3& pos) const {
    unsigned int index = 0;
    if (this->center.x < pos.x) {
      index += 4;
    }
    if (this->center.y < pos.y) {
      index += 2;
    }
    if (this->center.z < pos.z) {
      index += 1;
    }
    return index;
  }

  void makeChildren () {
    assert (this->children.size () == 0);
    float q          = this->width * 0.25f;
    float childWidth = this->width * 0.5f;
    int   childDepth = this->depth + 1;

    auto add = [this,childWidth,childDepth] (const glm::vec3& v) {
      this->children.emplace_back (new Impl (v,childWidth,childDepth,this));
    };
    
    this->children.reserve (8);
    add (this->center + glm::vec3 (-q, -q, -q)); // order is crucial
    add (this->center + glm::vec3 (-q, -q,  q));
    add (this->center + glm::vec3 (-q,  q, -q));
    add (this->center + glm::vec3 (-q,  q,  q));
    add (this->center + glm::vec3 ( q, -q, -q));
    add (this->center + glm::vec3 ( q, -q,  q));
    add (this->center + glm::vec3 ( q,  q, -q));
    add (this->center + glm::vec3 ( q,  q,  q));
  }

  Faces::iterator insertIntoChild (const FaceToInsert& f) {
    if (this->children.empty ()) {
      this->makeChildren           ();
      return this->insertIntoChild (f);
    }
    else {
      return this->children[this->childIndex (f.center)]->insertFace (f);
    }
  }

  Faces::iterator insertFace (const FaceToInsert& f) {
    if (f.oneDimExtent <= this->width * Impl::relativeMinFaceExtent) {
      return this->insertIntoChild (f);
    }
    else {
      if (f.savePrimitive) {
        this->primitives.insert (f.id, f.primitive);
      }
      this->faces.emplace_front ( f.edge 
                                , f.id 
                                , &this->node
                                , f.faceIndex);
      return this->faces.begin ();
    }
  }

  bool isEmpty () const {
    return this->faces.empty () && this->children.empty ();
  }

  void deleteFace (Faces::iterator& faceIterator) {
    this->primitives.remove (faceIterator->id ());
    this->faces.erase       (faceIterator);
    if (this->isEmpty () && this->parent) {
      this->parent->childEmptyNotification ();
      // don't call anything after calling childEmptyNotification
    }
  }

  void childEmptyNotification () {
    for (Child& c : this->children) {
      if (c->isEmpty () == false) {
        return;
      }
    }
    children.clear ();
    if (this->isEmpty () && this->parent) {
      this->parent->childEmptyNotification ();
      // don't call anything after calling childEmptyNotification
    }
  }

  PrimAABox looseAABox () const {
    const float looseWidth = this->width * 2.0f;
    return PrimAABox (this->center, looseWidth, looseWidth, looseWidth);
  }

  void facesIntersectRay (WingedMesh& mesh, const PrimRay& ray, WingedFaceIntersection& intersection) {
    for (WingedFace& face : this->faces) {
      PrimTriangle triangle = face.triangle (mesh);
      glm::vec3    p;

      if (IntersectionUtil::intersects (ray, triangle, &p)) {
        intersection.update (glm::distance (ray.origin (), p), p, triangle.normal (), mesh, face);
      }
    }
  }

  void facesIntersectRay (const PrimRay& ray, Intersection& intersection) {
    for (auto& element : this->primitives) {
      glm::vec3 p;

      if (IntersectionUtil::intersects (ray, element.second, &p)) {
        intersection.update (glm::distance (ray.origin (), p), p, element.second.normal ());
      }
    }
  }

  bool intersects (WingedMesh& mesh, const PrimRay& ray, WingedFaceIntersection& intersection) {
    if (IntersectionUtil::intersects (ray, this->looseAABox ())) {
      this->facesIntersectRay (mesh,ray,intersection);
      for (Child& c : this->children) 
        c->intersects (mesh,ray,intersection);
    }
    return intersection.isIntersection ();
  }

  bool intersects (const PrimRay& ray, Intersection& intersection) {
    if (IntersectionUtil::intersects (ray, this->looseAABox ())) {
      this->facesIntersectRay (ray,intersection);
      for (Child& c : this->children) 
        c->intersects (ray,intersection);
    }
    return intersection.isIntersection ();
  }

  bool intersects ( const WingedMesh& mesh, const PrimSphere& sphere
                  , std::vector <WingedFace*>& iFaces) {
    if (IntersectionUtil :: intersects (sphere, this->looseAABox ())) {
      for (WingedFace& face : this->faces) {
        if (IntersectionUtil :: intersects (sphere, mesh, face)) {
          iFaces.push_back (&face);
        }
      }
      for (Child& c : this->children) {
        c->intersects (mesh, sphere, iFaces);
      }
    }
    return iFaces.empty () == false;
  }

  unsigned int numFaces () const { return this->faces.size (); }

  void updateStatistics (OctreeStatistics& stats) const {
    const unsigned int d  = this->depth;
    const unsigned int f  = this->numFaces ();
    stats.numNodes        = stats.numNodes + 1;
    stats.numFaces        = stats.numFaces + f;
    stats.minDepth        = std::min <int> (stats.minDepth, d);
    stats.maxDepth        = std::max <int> (stats.maxDepth, d);
    stats.maxFacesPerNode = std::max <int> (stats.maxFacesPerNode, f);

    OctreeStatistics::DepthMap::iterator e = stats.numFacesPerDepth.find (d);
    if (e == stats.numFacesPerDepth.end ()) {
      stats.numFacesPerDepth.emplace (d, f);
    }
    else {
      e->second = e->second + f;
    }
    e = stats.numNodesPerDepth.find (d);
    if (e == stats.numNodesPerDepth.end ()) {
      stats.numNodesPerDepth.emplace (d, 1);
    }
    else {
      e->second = e->second + 1;
    }
    for (const Child& c : this->children) {
      c->updateStatistics (stats);
    }
  }
};

OctreeNode :: OctreeNode (OctreeNode::Impl* i) : impl (i) { }

ID             (OctreeNode)
GETTER_CONST   (int, OctreeNode, depth)
GETTER_CONST   (const glm::vec3&, OctreeNode, center)
GETTER_CONST   (float, OctreeNode, width)

/** Octree class */
struct Octree::Impl {
  Child                   root;
  glm::vec3               rootPosition;
  float                   rootWidth;
  bool                    rootWasSetup;
  IdMap <Faces::iterator> idMap;
  const bool              savePrimitives;

  Impl (bool s) 
    : rootWasSetup   (false)
    , savePrimitives (s) 
  {}

  WingedFace& insertFace (const WingedFace& face, const PrimTriangle& geometry) {
    assert (! this->hasFace (face.id ())); 
    FaceToInsert faceToInsert (face,geometry,this->savePrimitives);
    return this->insertFace (faceToInsert);
  }

  WingedFace& realignFace (const WingedFace& face, const PrimTriangle& geometry, bool* sameNode) {
    assert (this->hasFace   (face.id ())); 
    assert (face.octreeNode ()); 
    OctreeNode* formerNode = face.octreeNode ();

    FaceToInsert faceToInsert (face,geometry,this->savePrimitives);
    this->deleteFace (face);
    WingedFace& newFace = this->insertFace (faceToInsert);

    if (sameNode) {
      *sameNode = formerNode == newFace.octreeNode ();
    }
    return newFace;
  }

  WingedFace& insertFace (const FaceToInsert& faceToInsert) {
    if (this->hasRoot () == false) {
      this->initRoot (faceToInsert);
    }

    if (this->root->approxContains (faceToInsert)) {
      Faces::iterator it = this->root->insertFace (faceToInsert);
      this->idMap.insert (it->id (), it);
      return *it;
    }
    else {
      this->makeParent (faceToInsert);
      return this->insertFace (faceToInsert);
    }
  }

  void deleteFace (const WingedFace& face) {
    const Id id = face.id ();

    assert (face.octreeNode ());
    assert (this->hasFace (id)); 

    face.octreeNodeRef ().impl->deleteFace (this->idMap.element (id));
    this->idMap.remove (id);

    if (this->root->isEmpty ()) {
      this->root.reset ();
    }
    else {
      this->shrinkRoot ();
    }
  }

  bool hasFace (const Id& id) const { return this->idMap.hasElement (id); }

  WingedFace* face (const Id& id) {
    auto it = this->idMap.iterator (id);
    if (it == this->idMap.end ()) {
      return nullptr;
    }
    else {
      return &*it->second;
    }
  }

  void makeParent (const FaceToInsert& f) {
    assert (this->root);

    glm::vec3 parentCenter;
    glm::vec3 rootCenter    = this->root->center;
    float     rootWidth     = this->root->width;
    float     halfRootWidth = this->root->width * 0.5f;
    int       index         = 0;

    if (rootCenter.x < f.center.x) 
      parentCenter.x = rootCenter.x + halfRootWidth;
    else {
      parentCenter.x = rootCenter.x - halfRootWidth;
      index         += 4;
    }
    if (rootCenter.y < f.center.y) 
      parentCenter.y = rootCenter.y + halfRootWidth;
    else {
      parentCenter.y = rootCenter.y - halfRootWidth;
      index         += 2;
    }
    if (rootCenter.z < f.center.z) 
      parentCenter.z = rootCenter.z + halfRootWidth;
    else {
      parentCenter.z = rootCenter.z - halfRootWidth;
      index         += 1;
    }

    OctreeNode::Impl* newRoot = new OctreeNode::Impl ( parentCenter
                                                     , rootWidth * 2.0f
                                                     , this->root->depth - 1
                                                     , nullptr );
    newRoot->makeChildren ();
    newRoot->children [index] = std::move (this->root);
    this->root.reset (newRoot);
    this->root->children [index]->parent = &*this->root;
  }

  void render () { 
#ifdef DILAY_RENDER_OCTREE
    if (this->hasRoot ()) 
      this->root->render ();
#else
    assert (false && "compiled without rendering support for octrees");
#endif
  }

  bool intersects (WingedMesh& mesh, const PrimRay& ray, WingedFaceIntersection& intersection) {
    if (this->hasRoot ()) {
      return this->root->intersects (mesh,ray,intersection);
    }
    return false;
  }

  bool intersects (const PrimRay& ray, Intersection& intersection) {
    if (this->hasRoot ()) {
      return this->root->intersects (ray,intersection);
    }
    return false;
  }

  bool intersects ( const WingedMesh& mesh, const PrimSphere& sphere
                  , std::vector <WingedFace*>& faces) 
  {
    if (this->hasRoot ()) {
      return this->root->intersects (mesh,sphere,faces);
    }
    return false;
  }

  void reset () { 
    this->idMap.reset ();
    this->root .reset ();
    this->rootWasSetup = false;
  }

  void setupRoot (const glm::vec3& position, float width) {
    assert (this->root == false);
    this->rootWasSetup = true;
    this->rootPosition = position;
    this->rootWidth    = width;
  }

  void initRoot (const FaceToInsert& faceToInsert) {
    assert (this->root == false);

    if (this->rootWasSetup == false) {
      this->rootPosition = faceToInsert.center;
      this->rootWidth    = faceToInsert.oneDimExtent + std::numeric_limits <float>::epsilon ();
    }
    this->root = Child (new OctreeNode::Impl (this->rootPosition, this->rootWidth, 0, nullptr));
  }

  void shrinkRoot () {
    if (this->hasRoot () && this->root->faces.empty    () == true 
                         && this->root->children.empty () == false) {
      int singleNonEmptyChildIndex = -1;
      for (int i = 0; i < 8; i++) {
        const Child& c = this->root->children [i];
        if (c->isEmpty () == false) {
          if (singleNonEmptyChildIndex == -1) 
            singleNonEmptyChildIndex = i;
          else
            return;
        }
      }
      if (singleNonEmptyChildIndex != -1) {
        Child newRoot      = std::move (this->root->children [singleNonEmptyChildIndex]);
        this->root         = std::move (newRoot);
        this->root->parent = nullptr;
        this->shrinkRoot ();
      }
    }
  }

  bool hasRoot () const { return bool (this->root); }

  unsigned int numFaces () const { return this->idMap.size (); }

  OctreeStatistics statistics () const {
    OctreeStatistics stats { 0, 0
                           , std::numeric_limits <int>::max ()
                           , std::numeric_limits <int>::min ()
                           , 0 
                           , OctreeStatistics::DepthMap ()
                           , OctreeStatistics::DepthMap () };
    if (this->hasRoot ()) {
      this->root->updateStatistics (stats);
    }
    assert (stats.numFaces == this->numFaces ());
    return stats;
  }

  void forEachFace (const std::function <void (WingedFace&)>& f) {
    for (auto& pair : this->idMap) {
      f (*pair.second);
    }
  }
  void forEachConstFace (const std::function <void (const WingedFace&)>& f) const {
    for (const auto& pair : this->idMap) {
      f (*pair.second);
    }
  }
};

DELEGATE1_BIG4  (Octree,bool)
DELEGATE2       (WingedFace& , Octree, insertFace, const WingedFace&, const PrimTriangle&)
DELEGATE3       (WingedFace& , Octree, realignFace, const WingedFace&, const PrimTriangle&, bool*)
DELEGATE1       (void        , Octree, deleteFace, const WingedFace&)
DELEGATE1_CONST (bool        , Octree, hasFace, const Id&)
DELEGATE1       (WingedFace* , Octree, face, const Id&)
DELEGATE        (void, Octree, render)
DELEGATE3       (bool, Octree, intersects, WingedMesh&, const PrimRay&, WingedFaceIntersection&)
DELEGATE2       (bool, Octree, intersects, const PrimRay&, Intersection&)
DELEGATE3       (bool, Octree, intersects, const WingedMesh&, const PrimSphere&, std::vector<WingedFace*>&)
DELEGATE        (void, Octree, reset)
DELEGATE2       (void, Octree, setupRoot, const glm::vec3&, float)
DELEGATE        (void, Octree, shrinkRoot)
DELEGATE_CONST  (bool, Octree, hasRoot)
DELEGATE_CONST  (unsigned int, Octree, numFaces)
DELEGATE_CONST  (OctreeStatistics, Octree, statistics)
DELEGATE1       (void            , Octree, forEachFace, const std::function <void (WingedFace&)>&)
DELEGATE1_CONST (void            , Octree, forEachConstFace, const std::function <void (const WingedFace&)>&)
