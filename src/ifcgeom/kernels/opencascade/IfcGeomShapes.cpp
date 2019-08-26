﻿/********************************************************************************
 *                                                                              *
 * This file is part of IfcOpenShell.                                           *
 *                                                                              *
 * IfcOpenShell is free software: you can redistribute it and/or modify         *
 * it under the terms of the Lesser GNU General Public License as published by  *
 * the Free Software Foundation, either version 3.0 of the License, or          *
 * (at your option) any later version.                                          *
 *                                                                              *
 * IfcOpenShell is distributed in the hope that it will be useful,              *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of               *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                 *
 * Lesser GNU General Public License for more details.                          *
 *                                                                              *
 * You should have received a copy of the Lesser GNU General Public License     *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.         *
 *                                                                              *
 ********************************************************************************/

/********************************************************************************
 *                                                                              *
 * Implementations of the various conversion functions defined in IfcRegister.h *
 *                                                                              *
 ********************************************************************************/

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Mat.hxx>
#include <gp_Mat2d.hxx>
#include <gp_GTrsf.hxx>
#include <gp_GTrsf2d.hxx>
#include <gp_Trsf.hxx>
#include <gp_Trsf2d.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Ax2d.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>

#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>

#include <Geom_Line.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_CylindricalSurface.hxx>

#include <BRepOffsetAPI_Sewing.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_CompSolid.hxx>

#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>

#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeShell.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>

#include <ShapeFix_Shape.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <ShapeFix_Solid.hxx>

#include <TopLoc_Location.hxx>

#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>

#include <Standard_Version.hxx>

#include <TopTools_ListIteratorOfListOfShape.hxx>

#include "OpenCascadeKernel.h"

#include <memory>

#include "../../../ifcparse/IfcLogger.h"
#include "../../../ifcgeom/kernels/opencascade/OpenCascadeConversionResult.h"

using namespace ifcopenshell::geometry; 
using namespace ifcopenshell::geometry::kernels;

bool OpenCascadeKernel::convert(const taxonomy::extrusion* extrusion, TopoDS_Shape& shape) {
	const double& height = extrusion->depth;

	if (height < precision_) {
		Logger::Error("Non-positive extrusion height encountered for:", extrusion->instance);
		return false;
	}

	TopoDS_Shape face;
	if (!convert(&extrusion->basis, face)) {
		return false;
	}

	gp_GTrsf gtrsf;
	if (!convert(&extrusion->matrix, gtrsf)) {
		Logger::Error("Unable to move extrusion");
	}
	auto trsf = gtrsf.Trsf();
	
	auto fs = extrusion->direction.components.data();
	gp_Dir dir(fs[0], fs[1], fs[2]);
	
	shape.Nullify();

	if (face.ShapeType() == TopAbs_COMPOUND) {
		
		// For compounds (most likely the result of a IfcCompositeProfileDef) 
		// create a compound solid shape.
		
		TopExp_Explorer exp(face, TopAbs_FACE);
		
		TopoDS_CompSolid compound;
		BRep_Builder builder;
		builder.MakeCompSolid(compound);
		
		int num_faces_extruded = 0;
		for (; exp.More(); exp.Next(), ++num_faces_extruded) {
			builder.Add(compound, BRepPrimAPI_MakePrism(exp.Current(), height*dir));
		}

		if (num_faces_extruded) {
			shape = compound;
		}

	}
	
	if (shape.IsNull()) {	
		shape = BRepPrimAPI_MakePrism(face, height*dir);
	}

	if (!shape.IsNull()) {
		// IfcSweptAreaSolid.Position (trsf) is an IfcAxis2Placement3D
		// and therefore has a unit scale factor
		shape.Move(trsf);
	}

	return !shape.IsNull();
}

namespace {
	/* Returns whether wire conforms to a polyhedron, i.e. only edges with linear curves*/
	bool is_polyhedron(const TopoDS_Wire& wire) {
		double a, b;
		TopLoc_Location l;

		TopoDS_Iterator it(wire, false, false);
		for (; it.More(); it.Next()) {
			auto crv = BRep_Tool::Curve(TopoDS::Edge(it.Value()), l, a, b);
			if (!crv || crv->DynamicType() != STANDARD_TYPE(Geom_Line)) {
				return false;
			}
		}

		return true;
	}

	/* Returns whether wire conforms to a polyhedron, i.e. only edges with linear curves*/
	bool is_polyhedron(const taxonomy::loop* wire) {
		for (auto& edge : wire->children_as<taxonomy::edge>()) {
			if (edge->basis) {
				if (edge->basis->kind() != taxonomy::LINE) {
					return false;
				}
			}
		}
		return true;
	}

	/* A temporary structure to store the intermediate data for the face conversion */
	class face_definition {
	private:
		Handle(Geom_Surface) surface_;
		std::vector<TopoDS_Wire> wires_;
		bool all_outer_;
	public:
		face_definition() : surface_(), all_outer_(false) {}

		typedef std::vector<TopoDS_Wire>::const_iterator wire_it;

		bool& all_outer() {
			return all_outer_;
		}

		bool all_outer() const {
			return all_outer_;
		}

		Handle(Geom_Surface)& surface() {
			return surface_;
		}

		const Handle(Geom_Surface)& surface() const {
			return surface_;
		}

		std::vector<TopoDS_Wire>& wires() {
			return wires_;
		}

		const TopoDS_Wire& outer_wire() const {
			return wires_.front();
		}

		std::pair<wire_it, wire_it> inner_wires() const {
			return { wires_.begin() + 1, wires_.end() };
		}
	};
}

#include <TopTools_DataMapOfShapeInteger.hxx>
#include <Geom_Plane.hxx>
#include <BRepLib_FindSurface.hxx>
#include <ShapeFix_Edge.hxx>

bool OpenCascadeKernel::convert(const taxonomy::face* face, TopoDS_Shape& result) {
	std::vector<taxonomy::loop*> bounds;
	std::transform(face->children.begin(), face->children.end(), std::back_inserter(bounds), [](auto item){
		return static_cast<taxonomy::loop*>(item);
	});

	face_definition fd;

	const bool is_face_surface = false; /* todo */

	/*
	if (is_face_surface) {
		IfcSchema::IfcFaceSurface* fs = (IfcSchema::IfcFaceSurface*) l;
		fs->FaceSurface();
		// FIXME: Surfaces are interpreted as a TopoDS_Shape
		TopoDS_Shape surface_shape;
		if (!convert_shape(fs->FaceSurface(), surface_shape)) return false;

		// FIXME: Assert this obtaines the only face
		TopExp_Explorer exp(surface_shape, TopAbs_FACE);
		if (!exp.More()) return false;

		TopoDS_Face surface = TopoDS::Face(exp.Current());
		fd.surface() = BRep_Tool::Surface(surface);
	}
	*/

	const int num_bounds = bounds.size();
	int num_outer_bounds = 0;

	for (auto& bound: bounds) {
		if (bound->external.get_value_or(false)) {
			num_outer_bounds++;
		}
	}

	// The number of outer bounds should be one according to the schema. Also Open Cascade
	// expects this, but it is not strictly checked. Regardless, if the number is greater,
	// the face will still be processed as long as there are no holes. A compound of faces
	// is returned in that case.
	if (num_bounds > 1 && num_outer_bounds > 1 && num_bounds != num_outer_bounds) {
		Logger::Message(Logger::LOG_ERROR, "Invalid configuration of boundaries for:", face->instance);
		return false;
	}

	if (num_outer_bounds > 1) {
		Logger::Message(Logger::LOG_WARNING, "Multiple outer boundaries for:", face->instance);
		fd.all_outer() = true;
	}

	TopTools_DataMapOfShapeInteger wire_senses;

	for (int process_interior = 0; process_interior <= 1; ++process_interior) {
		for (auto& bound : bounds) {
			bool same_sense = true; /* todo bound->Orientation(); */

			const bool is_interior =
				!bound->external.get_value_or(false) &&
				(num_bounds > 1) &&
				(num_outer_bounds < num_bounds);

			// The exterior face boundary is processed first
			if (is_interior == !process_interior) continue;

			TopoDS_Wire wire;
			if (faceset_helper_ && is_polyhedron(bound)) {
				if (!faceset_helper_->wire(bound, wire)) {
					Logger::Message(Logger::LOG_WARNING, "Face boundary loop not included", bound->instance);
					continue;
				}
			} else if (!convert(bound, wire)) {
				Logger::Message(Logger::LOG_ERROR, "Failed to process face boundary loop", bound->instance);
				return false;
			}

			if (!same_sense) {
				wire.Reverse();
			}

			wire_senses.Bind(wire.Oriented(TopAbs_FORWARD), same_sense ? TopAbs_FORWARD : TopAbs_REVERSED);

			fd.wires().emplace_back(wire);
		}
	}

	if (fd.wires().empty()) {
		Logger::Warning("Face with no boundaries", face->instance);
		return false;
	}

	if (fd.surface().IsNull()) {
		// Use the first wire to find a plane manually for polygonal wires
		const TopoDS_Wire& wire = fd.wires().front();
		if (is_polyhedron(wire)) {
			TopExp_Explorer exp(wire, TopAbs_EDGE);
			int count = 0;
			TopoDS_Edge edges[2];
			for (; exp.More(); exp.Next(), count++) {
				if (count < 2) {
					edges[count] = TopoDS::Edge(exp.Current());
				}
			}

			if (count == 3) {
				// Help Open Cascade by finding the plane more efficiently
				double _, __;
				Handle(Geom_Line) c1 = Handle(Geom_Line)::DownCast(BRep_Tool::Curve(edges[0], _, __));
				Handle(Geom_Line) c2 = Handle(Geom_Line)::DownCast(BRep_Tool::Curve(edges[1], _, __));

				const gp_Vec ab = c1->Position().Direction();
				const gp_Vec ac = c2->Position().Direction();
				const gp_Vec cross = ab.Crossed(ac);

				if (cross.SquareMagnitude() > ALMOST_ZERO) {
					const gp_Dir n = cross;
					fd.surface() = new Geom_Plane(c1->Position().Location(), n);
				}
			} else {
				gp_Pln pln;
				if (approximate_plane_through_wire(wire, pln)) {
					fd.surface() = new Geom_Plane(pln);
				}
			}
		}
	}

	if (fd.surface().IsNull()) {
		// BRepLib_FindSurface is used in case no surface is found or provided

		const TopoDS_Wire& wire = fd.wires().front();

		BRepLib_FindSurface fs(wire, precision_, true, true);
		if (fs.Found()) {
			fd.surface() = fs.Surface();
			ShapeFix_ShapeTolerance ftol;
			ftol.SetTolerance(wire, fs.ToleranceReached(), TopAbs_WIRE);
		}
	}

	TopTools_ListOfShape face_list;

	if (fd.surface().IsNull()) {
		// The set of wires is triangulated in case no surface can be found
		Logger::Message(Logger::LOG_WARNING, "Triangulating face boundaries for face", face->instance);

		if (fd.all_outer()) {
			for (const auto& w : fd.wires()) {
				TopTools_ListOfShape fl;
				triangulate_wire({ w }, fl);
				face_list.Append(fl);
			}
		} else {
			triangulate_wire(fd.wires(), face_list);
		}
	} else if (!fd.all_outer()) {
		BRepBuilderAPI_MakeFace mf(fd.surface(), fd.outer_wire());

		if (mf.IsDone()) {
			// Is this necessary
			TopoDS_Face f = mf.Face();
			mf.Init(f);

			for (auto it = fd.inner_wires().first; it != fd.inner_wires().second; ++it) {
				mf.Add(*it);
			}

			face_list.Append(mf.Face());
		}
	} else {
		for (const auto& w : fd.wires()) {
			BRepBuilderAPI_MakeFace mf(fd.surface(), w);
			if (mf.IsDone()) {
				face_list.Append(mf.Face());
			}
		}
	}

	if (!fd.surface().IsNull()) {
		// Some fixes for orientation and p-curves. If we have no surface, it
		// means the face has been triangulated in which case none of these
		// fixes are necessary.

		if (fd.surface()->DynamicType() != STANDARD_TYPE(Geom_Plane)) {
			// In case of (non-planar) face surface, p-curves need to be computed.
			// For planar faces, Open Cascade generates p-curves on the fly.

			for (TopTools_ListIteratorOfListOfShape it(face_list); it.More(); it.Next()) {
				// Small chance there are multiple faces
				const TopoDS_Face& occ_face = TopoDS::Face(it.Value());
				for (TopExp_Explorer exp2(occ_face, TopAbs_EDGE); exp2.More(); exp2.Next()) {
					const TopoDS_Edge& edge = TopoDS::Edge(exp2.Current());
					ShapeFix_Edge fix_edge;
					fix_edge.FixAddPCurve(edge, occ_face, false, precision_);
				}
			}
		}

		for (TopTools_ListIteratorOfListOfShape it(face_list); it.More(); it.Next()) {
			const TopoDS_Face& occ_face = TopoDS::Face(it.Value());

			ShapeFix_Face sfs(TopoDS::Face(occ_face));
			TopTools_DataMapOfShapeListOfShape wire_map;
			sfs.FixOrientation(wire_map);

			TopoDS_Iterator jt(occ_face, false);
			for (; jt.More(); jt.Next()) {
				const TopoDS_Wire& w = TopoDS::Wire(jt.Value());
				// tfk: @todo if wire_map contains w, I would assume wire_senses also contains w,
				// this is not the case in github issue #405.
				if (wire_map.IsBound(w) && wire_senses.IsBound(w)) {
					const TopTools_ListOfShape& shapes = wire_map.Find(w);
					TopTools_ListIteratorOfListOfShape kt(shapes);
					for (; kt.More(); kt.Next()) {
						// Apparently the wire got reversed, so register it with opposite orientation in the map
						wire_senses.Bind(kt.Value(), wire_senses.Find(w) == TopAbs_FORWARD ? TopAbs_REVERSED : TopAbs_FORWARD);
					}
				}
			}

			it.Value() = sfs.Face();
		}

		for (TopTools_ListIteratorOfListOfShape it(face_list); it.More(); it.Next()) {
			TopoDS_Face& occ_face = TopoDS::Face(it.Value());

			bool all_reversed = true;
			TopoDS_Iterator jt(occ_face, false);
			for (; jt.More(); jt.Next()) {
				const TopoDS_Wire& w = TopoDS::Wire(jt.Value());
				if (!wire_senses.IsBound(w.Oriented(TopAbs_FORWARD)) || (w.Orientation() == wire_senses.Find(w.Oriented(TopAbs_FORWARD)))) {
					all_reversed = false;
				}
			}

			if (all_reversed) {
				occ_face.Reverse();
			}
		}
	}

	if (face_list.Extent() > 1) {
		TopoDS_Compound compound;
		BRep_Builder builder;
		builder.MakeCompound(compound);
		for (TopTools_ListIteratorOfListOfShape it(face_list); it.More(); it.Next()) {
			TopoDS_Face& occ_face = TopoDS::Face(it.Value());
			builder.Add(compound, occ_face);
		}
		result = compound;
	} else {
		result = face_list.First();
	}

	return true;
}

#include <Geom_Curve.hxx>
#include <Geom_Line.hxx>

namespace {
	/* A compile-time for loop over the curve kinds */
	template <typename T, size_t N=0>
	struct dispatch_curve_creation {
		static bool dispatch(const ifcopenshell::geometry::taxonomy::item* item, T visitor) {
			// @todo it should be possible to eliminate this dynamic_cast when there is a static equivalent to kind()
			const ifcopenshell::geometry::taxonomy::curves::type<N>* v = dynamic_cast<const ifcopenshell::geometry::taxonomy::curves::type<N>*>(item);
			if (v) {
				visitor(*v);
				return true;
			} else {
				return dispatch_curve_creation<T, N + 1>::dispatch(item, visitor);
			}
		}
	};

	template <typename T>
	struct dispatch_curve_creation<T, ifcopenshell::geometry::taxonomy::curves::max> {
		static bool dispatch(const ifcopenshell::geometry::taxonomy::item* item, T visitor) {
			Logger::Error("No conversion for " + std::to_string(item->kind()));
			return false;
		}
	};

	template <typename T, typename U>
	T convert_xyz(const U& u) {
		const double* vs = u.components.data();
		return T(vs[0], vs[1], vs[2]);
	}

	struct curve_creation_visitor {
		typedef boost::variant<Handle(Geom_Curve), TopoDS_Wire> result_type;
		result_type result;

		result_type operator()(const taxonomy::bspline_curve&) {
			throw std::runtime_error("Not implemented");
		}

		result_type operator()(const taxonomy::line& l) {
			return result = Handle(Geom_Curve)(new Geom_Line(convert_xyz<gp_Pnt>(l.origin), convert_xyz<gp_Dir>(l.direction)));
		}

		result_type operator()(const taxonomy::circle& c) {
			return result = Handle(Geom_Curve)(new Geom_Circle(gp_Ax2(convert_xyz<gp_Pnt>(c.origin), convert_xyz<gp_Dir>(c.z), convert_xyz<gp_Dir>(c.x)), c.radius));
		}

		result_type operator()(const taxonomy::ellipse& e) {
			return result = Handle(Geom_Curve)(new Geom_Ellipse(gp_Ax2(convert_xyz<gp_Pnt>(e.origin), convert_xyz<gp_Dir>(e.z), convert_xyz<gp_Dir>(e.x)), e.radius, e.radius2));
		}
	};

	curve_creation_visitor::result_type convert_curve(const taxonomy::item* curve) {
		curve_creation_visitor v;
		if (dispatch_curve_creation<curve_creation_visitor, 0>::dispatch(curve, v)) {
			return v.result;
		} else {
			throw std::runtime_error("No curve created");
		}
	}
}

#include <ShapeBuild_ReShape.hxx>
#include <GC_MakeCircle.hxx>

namespace {
	// Returns the other vertex of an edge
	TopoDS_Vertex other(const TopoDS_Edge& e, const TopoDS_Vertex& v) {
		TopoDS_Vertex a, b;
		TopExp::Vertices(e, a, b);
		return v.IsSame(b) ? a : b;
	}

	TopoDS_Edge first_edge(const TopoDS_Wire& w) {
		TopoDS_Vertex v1, v2;
		TopExp::Vertices(w, v1, v2);
		TopTools_IndexedDataMapOfShapeListOfShape wm;
		TopExp::MapShapesAndAncestors(w, TopAbs_VERTEX, TopAbs_EDGE, wm);
		return TopoDS::Edge(wm.FindFromKey(v1).First());
	}

	// Returns new wire with the edge replaced by a linear edge with the vertex v moved to p
	TopoDS_Wire adjust(const TopoDS_Wire& w, const TopoDS_Vertex& v, const gp_Pnt& p) {
		TopTools_IndexedDataMapOfShapeListOfShape map;
		TopExp::MapShapesAndAncestors(w, TopAbs_VERTEX, TopAbs_EDGE, map);

		bool all_linear = true, single_circle = false, first = true;

		const TopTools_ListOfShape& edges = map.FindFromKey(v);
		TopTools_ListIteratorOfListOfShape it(edges);
		for (; it.More(); it.Next()) {
			const TopoDS_Edge& e = TopoDS::Edge(it.Value());
			double _, __;
			Handle(Geom_Curve) crv = BRep_Tool::Curve(e, _, __);
			const bool is_line = crv->DynamicType() == STANDARD_TYPE(Geom_Line);
			const bool is_circle = crv->DynamicType() == STANDARD_TYPE(Geom_Circle);
			all_linear = all_linear && is_line;
			single_circle = first && is_circle;
		}

		if (all_linear) {
			BRep_Builder b;
			TopoDS_Vertex v2;
			b.MakeVertex(v2, p, BRep_Tool::Tolerance(v));

			ShapeBuild_ReShape reshape;
			reshape.Replace(v.Oriented(TopAbs_FORWARD), v2);

			return TopoDS::Wire(reshape.Apply(w));
		} else if (single_circle) {
			TopoDS_Vertex v1, v2;
			TopExp::Vertices(w, v1, v2);

			gp_Pnt p1, p2, p3;
			p1 = v.IsEqual(v1) ? p : BRep_Tool::Pnt(v1);
			p3 = v.IsEqual(v2) ? p : BRep_Tool::Pnt(v2);

			double a, b;
			Handle(Geom_Curve) crv = BRep_Tool::Curve(TopoDS::Edge(edges.First()), a, b);
			crv->D0((a + b) / 2., p2);

			GC_MakeCircle mc(p1, p2, p3);
			if (!mc.IsDone()) {
				throw std::runtime_error("Failed to adjust circle");
			}

			TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(mc.Value(), p1, p3).Edge();
			BRepBuilderAPI_MakeWire builder;
			builder.Add(edge);
			return builder.Wire();
		} else {
			throw std::runtime_error("Unexpected wire to adjust");
		}
	}

	// A wrapper around BRepBuilderAPI_MakeWire that makes sure segments are connected either by moving end points or by adding intermediate segments
	class wire_builder {
	private:
		BRepBuilderAPI_MakeWire mw_;
		double p_;
		bool override_next_;
		gp_Pnt next_override_;
		const IfcUtil::IfcBaseClass* inst_;

	public:
		wire_builder(double p, const IfcUtil::IfcBaseClass* inst = 0) : p_(p), override_next_(false), inst_(inst) {}

		void operator()(const TopoDS_Shape& a) {
			const TopoDS_Wire& w = TopoDS::Wire(a);
			if (override_next_) {
				override_next_ = false;
				TopoDS_Edge e = first_edge(w);
				mw_.Add(adjust(w, TopExp::FirstVertex(e, true), next_override_));
			} else {
				mw_.Add(w);
			}
		}

		void operator()(const TopoDS_Shape& a, const TopoDS_Shape& b, bool last) {
			TopoDS_Wire w1 = TopoDS::Wire(a);
			const TopoDS_Wire& w2 = TopoDS::Wire(b);

			if (override_next_) {
				override_next_ = false;
				TopoDS_Edge e = first_edge(w1);
				w1 = adjust(w1, TopExp::FirstVertex(e, true), next_override_);
			}

			TopoDS_Vertex w11, w12, w21, w22;
			TopExp::Vertices(w1, w11, w12);
			TopExp::Vertices(w2, w21, w22);

			gp_Pnt p1 = BRep_Tool::Pnt(w12);
			gp_Pnt p2 = BRep_Tool::Pnt(w21);

			double dist = p1.Distance(p2);

			// Distance is within tolerance, this is fine
			if (dist < p_) {
				mw_.Add(w1);
				goto check;
			}

			// Distance is too large for attempting to move end points, add intermediate edge
			if (dist > 1000. * p_) {
				mw_.Add(w1);
				mw_.Add(BRepBuilderAPI_MakeEdge(p1, p2));
				Logger::Warning("Added additional segment to close gap with length " + boost::lexical_cast<std::string>(dist) + " to:", inst_);
				goto check;
			}

			{
				TopTools_IndexedDataMapOfShapeListOfShape wmap1, wmap2;

				// Find edges connected to end- and begin vertex
				TopExp::MapShapesAndAncestors(w1, TopAbs_VERTEX, TopAbs_EDGE, wmap1);
				TopExp::MapShapesAndAncestors(w2, TopAbs_VERTEX, TopAbs_EDGE, wmap2);

				const TopTools_ListOfShape& last_edges = wmap1.FindFromKey(w12);
				const TopTools_ListOfShape& first_edges = wmap2.FindFromKey(w21);

				double _, __;
				if (last_edges.Extent() == 1 && first_edges.Extent() == 1) {
					Handle(Geom_Curve) c1 = BRep_Tool::Curve(TopoDS::Edge(last_edges.First()), _, __);
					Handle(Geom_Curve) c2 = BRep_Tool::Curve(TopoDS::Edge(first_edges.First()), _, __);

					const bool is_line1 = c1->DynamicType() == STANDARD_TYPE(Geom_Line);
					const bool is_line2 = c2->DynamicType() == STANDARD_TYPE(Geom_Line);

					const bool is_circle1 = c1->DynamicType() == STANDARD_TYPE(Geom_Circle);
					const bool is_circle2 = c2->DynamicType() == STANDARD_TYPE(Geom_Circle);

					// Preferably adjust the segment that is linear
					if (is_line1 || (is_circle1 && !is_line2)) {
						mw_.Add(adjust(w1, w12, p2));
						Logger::Notice("Adjusted edge end-point with distance " + boost::lexical_cast<std::string>(dist) + " on:", inst_);
					} else if ((is_line2 || is_circle2) && !last) {
						mw_.Add(w1);
						override_next_ = true;
						next_override_ = p1;
						Logger::Notice("Adjusted edge end-point with distance " + boost::lexical_cast<std::string>(dist) + " on:", inst_);
					} else {
						// In all other cases an edge is added
						mw_.Add(w1);
						mw_.Add(BRepBuilderAPI_MakeEdge(p1, p2));
						Logger::Warning("Added additional segment to close gap with length " + boost::lexical_cast<std::string>(dist) + " to:", inst_);
					}
				} else {
					Logger::Error("Internal error, inconsistent wire segments", inst_);
					mw_.Add(w1);
				}
			}

		check:
			if (mw_.Error() == BRepBuilderAPI_NonManifoldWire) {
				Logger::Error("Non-manifold curve segments:", inst_);
			} else if (mw_.Error() == BRepBuilderAPI_DisconnectedWire) {
				Logger::Error("Failed to join curve segments:", inst_);
			}
		}

		const TopoDS_Wire& wire() { return mw_.Wire(); }
	};

	template <typename Fn>
	void shape_pair_enumerate(TopTools_ListIteratorOfListOfShape& it, Fn& fn, bool closed) {
		bool is_first = true;
		TopoDS_Shape first, previous, current;
		for (; it.More(); it.Next(), is_first = false) {
			current = it.Value();
			if (is_first) {
				first = current;
			} else {
				fn(previous, current, false);
			}
			previous = current;
		}
		if (closed) {
			fn(current, first, true);
		} else {
			fn(current);
		}
	}
}

bool OpenCascadeKernel::convert(const taxonomy::loop* loop, TopoDS_Wire& wire) {
	auto segments = loop->children_as<taxonomy::edge>();

	TopTools_ListOfShape converted_segments;

	for (auto& segment : segments) {
		TopoDS_Wire segment_wire = boost::get<TopoDS_Wire>(convert_curve(segment));

		if (!segment->orientation) {
			segment_wire.Reverse();
		}

		ShapeFix_ShapeTolerance FTol;
		FTol.SetTolerance(segment_wire, precision_, TopAbs_WIRE);

		converted_segments.Append(segment_wire);
	}

	if (converted_segments.Extent() == 0) {
		Logger::Message(Logger::LOG_ERROR, "No segment succesfully converted:", loop->instance);
		return false;
	}

	BRepBuilderAPI_MakeWire w;
	TopoDS_Vertex wire_first_vertex, wire_last_vertex, edge_first_vertex, edge_last_vertex;

	TopTools_ListIteratorOfListOfShape it(converted_segments);

	/*
	@todo
	IfcEntityList::ptr profile = l->data().getInverse(&IfcSchema::IfcProfileDef::Class(), -1);
	const bool force_close = profile && profile->size() > 0;
	*/
	const bool force_close = false;

	wire_builder bld(precision_, loop->instance);
	shape_pair_enumerate(it, bld, force_close);
	wire = bld.wire();

	return true;
}

bool OpenCascadeKernel::convert_impl(const taxonomy::extrusion* extrusion, ifcopenshell::geometry::ConversionResults& results) {
	TopoDS_Shape shape;
	if (!convert(extrusion, shape)) {
		return false;
	}
	results.emplace_back(ConversionResult(
		extrusion->instance->data().id(),
		extrusion->matrix,
		new OpenCascadeShape(shape),
		extrusion->surface_style
	));
	return true;
}

bool OpenCascadeKernel::convert(const taxonomy::matrix4* matrix, gp_GTrsf& trsf) {
	// @todo check
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 4; ++i) {
			trsf.SetValue(i + 1, j + 1, matrix->components(i, j));
		}
	}
	return true;
}

#include <BRepTools_WireExplorer.hxx>

bool OpenCascadeKernel::approximate_plane_through_wire(const TopoDS_Wire& wire, gp_Pln& plane, double eps) {
	// Newell's Method is used for the normal calculation
	// as a simple edge cross product can give opposite results
	// for a concave face boundary.
	// Reference: Graphics Gems III p. 231

	const double eps_ = eps < 1. ? precision_ : eps;
	const double eps2 = eps_ * eps_;

	double x = 0, y = 0, z = 0;
	gp_Pnt current, previous, first;
	gp_XYZ center;
	int n = 0;

	BRepTools_WireExplorer exp(wire);

	for (;; exp.Next()) {
		const bool has_more = exp.More() != 0;
		if (has_more) {
			const TopoDS_Vertex& v = exp.CurrentVertex();
			current = BRep_Tool::Pnt(v);
			center += current.XYZ();
		} else {
			current = first;
		}
		if (n) {
			const double& xn = previous.X();
			const double& yn = previous.Y();
			const double& zn = previous.Z();
			const double& xn1 = current.X();
			const double& yn1 = current.Y();
			const double& zn1 = current.Z();
			x += (yn - yn1)*(zn + zn1);
			y += (xn + xn1)*(zn - zn1);
			z += (xn - xn1)*(yn + yn1);
		} else {
			first = current;
		}
		if (!has_more) {
			break;
		}
		previous = current;
		++n;
	}

	if (n < 3) {
		return false;
	}

	plane = gp_Pln(center / n, gp_Dir(x, y, z));

	exp.Init(wire);
	for (; exp.More(); exp.Next()) {
		const TopoDS_Vertex& v = exp.CurrentVertex();
		current = BRep_Tool::Pnt(v);
		if (plane.SquareDistance(current) > eps2) {
			return false;
		}
	}

	return true;
}


bool OpenCascadeKernel::triangulate_wire(const std::vector<TopoDS_Wire>& wires, TopTools_ListOfShape& faces) {
	// This is a bit of a precarious approach, but seems to work for the 
	// versions of OCCT tested for. OCCT has a Delaunay triangulation function 
	// BRepMesh_Delaun, but it is notoriously hard to interpret the results 
	// (due to the Bowyer-Watson super triangle perhaps?). Therefore 
	// alternatively we use the regular OCCT incremental mesher on a new face 
	// created from the UV coordinates of the original wire. Pray to our gods 
	// that the vertex coordinates are unaffected by the meshing algorithm and 
	// map them back to 3d coordinates when iterating over the mesh triangles.

	// In addition, to maintain a manifold shell, we need to make sure that 
	// every edge from the input wire is used exactly once in the list of 
	// resulting faces. And that other internal edges are used twice.

	typedef std::pair<double, double> uv_node;

	gp_Pln pln;
	if (!approximate_plane_through_wire(wires.front(), pln, std::numeric_limits<double>::infinity())) {
		return false;
	}

	const gp_XYZ& udir = pln.Position().XDirection().XYZ();
	const gp_XYZ& vdir = pln.Position().YDirection().XYZ();
	const gp_XYZ& pnt = pln.Position().Location().XYZ();

	std::map<uv_node, TopoDS_Vertex> mapping;
	std::map<std::pair<uv_node, uv_node>, TopoDS_Edge> existing_edges, new_edges;

	std::unique_ptr<BRepBuilderAPI_MakeFace> mf;

	for (auto it = wires.begin(); it != wires.end(); ++it) {
		const TopoDS_Wire& wire = *it;
		BRepTools_WireExplorer exp(wire);
		BRepBuilderAPI_MakePolygon mp;

		// Add UV coordinates to a newly created polygon
		for (; exp.More(); exp.Next()) {
			// Project onto plane
			const TopoDS_Vertex& V = exp.CurrentVertex();
			gp_Pnt p = BRep_Tool::Pnt(V);
			double u = (p.XYZ() - pnt).Dot(udir);
			double v = (p.XYZ() - pnt).Dot(vdir);
			mp.Add(gp_Pnt(u, v, 0.));

			mapping.insert(std::make_pair(std::make_pair(u, v), V));

			// Store existing edges in a map so that triangles can
			// actually reference the preexisting edges.
			const TopoDS_Edge& e = exp.Current();
			TopoDS_Vertex V0, V1;
			TopExp::Vertices(e, V0, V1, true);
			gp_Pnt p0 = BRep_Tool::Pnt(V0);
			gp_Pnt p1 = BRep_Tool::Pnt(V1);
			double u0 = (p0.XYZ() - pnt).Dot(udir);
			double v0 = (p0.XYZ() - pnt).Dot(vdir);
			double u1 = (p1.XYZ() - pnt).Dot(udir);
			double v1 = (p1.XYZ() - pnt).Dot(vdir);
			uv_node uv0 = std::make_pair(u0, v0);
			uv_node uv1 = std::make_pair(u1, v1);
			existing_edges.insert(std::make_pair(std::make_pair(uv0, uv1), e));
			existing_edges.insert(std::make_pair(std::make_pair(uv1, uv0), TopoDS::Edge(e.Reversed())));
		}

		// Not closed by default
		mp.Close();

		if (mf) {
			if (it - 1 == wires.begin()) {
				// @todo is this necessary?
				TopoDS_Face f = mf->Face();
				mf->Init(f);
			}
			mf->Add(mp.Wire());
		} else {
			mf.reset(new BRepBuilderAPI_MakeFace(mp.Wire()));
		}
	}

	const TopoDS_Face& face = mf->Face();

	// Create a triangular mesh from the face
	BRepMesh_IncrementalMesh(face, Precision::Confusion());

	int n123[3];
	TopLoc_Location loc;
	Handle_Poly_Triangulation tri = BRep_Tool::Triangulation(face, loc);

	if (!tri.IsNull()) {
		const TColgp_Array1OfPnt& nodes = tri->Nodes();

		const Poly_Array1OfTriangle& triangles = tri->Triangles();
		for (int i = 1; i <= triangles.Length(); ++i) {
			if (face.Orientation() == TopAbs_REVERSED)
				triangles(i).Get(n123[2], n123[1], n123[0]);
			else triangles(i).Get(n123[0], n123[1], n123[2]);

			// Create polygons from the mesh vertices
			BRepBuilderAPI_MakeWire mp2;
			for (int j = 0; j < 3; ++j) {

				uv_node uvnodes[2];
				TopoDS_Vertex vs[2];

				for (int k = 0; k < 2; ++k) {
					const gp_Pnt& uv = nodes.Value(n123[(j + k) % 3]);
					uvnodes[k] = std::make_pair(uv.X(), uv.Y());

					auto it = mapping.find(uvnodes[k]);
					if (it == mapping.end()) {
						Logger::Error("Internal error: unable to unproject uv-mesh");
						return false;
					}

					vs[k] = it->second;
				}

				auto it = existing_edges.find(std::make_pair(uvnodes[0], uvnodes[1]));
				if (it != existing_edges.end()) {
					// This is a boundary edge, reuse existing edge from wire
					mp2.Add(it->second);
				} else {
					auto jt = new_edges.find(std::make_pair(uvnodes[0], uvnodes[1]));
					if (jt != new_edges.end()) {
						// We have already added the reverse as part of another
						// triangle, reuse this edge.
						mp2.Add(TopoDS::Edge(jt->second));
					} else {
						// This is a new internal edge. Register the reverse
						// for reuse later. We need to be sure to reuse vertices
						// for the edge construction because otherwise the wire
						// builder will use geometrical proximity for vertex
						// connections in which case the edge will be copied
						// and no longer partner with other edges from the shell.
						TopoDS_Edge ne = BRepBuilderAPI_MakeEdge(vs[0], vs[1]);
						mp2.Add(ne);
						// Store the reverse to be picked up later.
						new_edges.insert(std::make_pair(std::make_pair(uvnodes[1], uvnodes[0]), TopoDS::Edge(ne.Reversed())));
					}
				}
			}

			BRepBuilderAPI_MakeFace mft(mp2.Wire());
			if (mft.IsDone()) {
				TopoDS_Face triangle_face = mft.Face();
				TopoDS_Iterator jt(triangle_face, false);
				for (; jt.More(); jt.Next()) {
					const TopoDS_Wire& w = TopoDS::Wire(jt.Value());
					if (w.Orientation() != wires.front().Orientation()) {
						triangle_face.Reverse();
					}
				}
				faces.Append(triangle_face);
			} else {
				Logger::Error("Internal error: missing face");
				return false;
			}
		}
	}

	TopTools_IndexedDataMapOfShapeListOfShape mape, mapn;
	for (auto& wire : wires) {
		TopExp::MapShapesAndAncestors(wire, TopAbs_EDGE, TopAbs_WIRE, mape);
	}
	TopTools_ListIteratorOfListOfShape it(faces);
	for (; it.More(); it.Next()) {
		TopExp::MapShapesAndAncestors(it.Value(), TopAbs_EDGE, TopAbs_WIRE, mapn);
	}

	// Validation

	for (int i = 1; i <= mape.Extent(); ++i) {
#if OCC_VERSION_HEX >= 0x70000
		TopTools_ListOfShape val;
		if (!mapn.FindFromKey(mape.FindKey(i), val)) {
#else
		bool contains = false;
		try {
			TopTools_ListOfShape val = mapn.FindFromKey(mape.FindKey(i));
			contains = true;
		} catch (Standard_NoSuchObject&) {}
		if (!contains) {
#endif
			// All existing edges need to exist in the new faces
			Logger::Error("Internal error, missing edge from triangulation");
			if (faceset_helper_ != nullptr) {
				faceset_helper_->non_manifold() = true;
			}
		}
		}

	for (int i = 1; i <= mapn.Extent(); ++i) {
		const TopoDS_Shape& v = mapn.FindKey(i);
		int n = mapn.FindFromIndex(i).Extent();
		// Existing edges are boundaries with use 1
		// New edges are internal with use 2
		if (n != (mape.Contains(v) ? 1 : 2)) {
			Logger::Error("Internal error, non-manifold result from triangulation");
			if (faceset_helper_ != nullptr) {
				faceset_helper_->non_manifold() = true;
			}
		}
	}

	return true;
	}