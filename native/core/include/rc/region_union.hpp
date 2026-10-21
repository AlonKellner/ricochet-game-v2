// Stage 123: the visibility region drawn as the union of its cells. Drawn cell
// by cell, neighbouring cells' shared edges agree only to rounding (and a
// pencil's two sheets can overlap), so pixels along their seams were missed or
// covered twice: hairline cracks and overlaps. The union (Clipper2, a 1e-4 px
// grid) is one set of polygons -- outer rings and their holes -- with no edge
// between cells left; its triangulation is the drawn fill, and the presenter
// morphs its rings.
#pragma once

#include "rc/visibility.hpp"

#include <vector>

namespace rc::vis {

// The region's outline: outer rings (positive signed area, x right y down's
// shoelace) and holes (negative), each hole with its outer ring.
struct Outline {
	std::vector<std::vector<Pt>> rings;
	std::vector<int> parent; // a hole's outer ring (-1: an outer ring)
};

// The union of the region's cells (their rings as fill_triangles makes them:
// conforming, clipped to the domain). Empty for an unsupported region.
Outline union_outline(const Result &r);

// The outline triangulated (each outer ring with its holes, by earcut).
Fill fill_of(const Outline &o);

} // namespace rc::vis
