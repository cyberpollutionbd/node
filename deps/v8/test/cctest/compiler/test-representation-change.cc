// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/cctest/compiler/graph-builder-tester.h"
#include "test/cctest/compiler/value-helper.h"

#include "src/compiler/node-matchers.h"
#include "src/compiler/representation-change.h"

namespace v8 {
namespace internal {
namespace compiler {

class RepresentationChangerTester : public HandleAndZoneScope,
                                    public GraphAndBuilders {
 public:
  explicit RepresentationChangerTester(int num_parameters = 0)
      : GraphAndBuilders(main_zone()),
        javascript_(main_zone()),
        jsgraph_(main_isolate(), main_graph_, &main_common_, &javascript_,
                 &main_simplified_, &main_machine_),
        changer_(&jsgraph_, main_isolate()) {
    Node* s = graph()->NewNode(common()->Start(num_parameters));
    graph()->SetStart(s);
  }

  JSOperatorBuilder javascript_;
  JSGraph jsgraph_;
  RepresentationChanger changer_;

  Isolate* isolate() { return main_isolate(); }
  Graph* graph() { return main_graph_; }
  CommonOperatorBuilder* common() { return &main_common_; }
  JSGraph* jsgraph() { return &jsgraph_; }
  RepresentationChanger* changer() { return &changer_; }

  // TODO(titzer): use ValueChecker / ValueUtil
  void CheckInt32Constant(Node* n, int32_t expected) {
    Int32Matcher m(n);
    CHECK(m.HasValue());
    CHECK_EQ(expected, m.Value());
  }

  void CheckUint32Constant(Node* n, uint32_t expected) {
    Uint32Matcher m(n);
    CHECK(m.HasValue());
    CHECK_EQ(static_cast<int>(expected), static_cast<int>(m.Value()));
  }

  void CheckFloat64Constant(Node* n, double expected) {
    Float64Matcher m(n);
    CHECK(m.HasValue());
    CheckDoubleEq(expected, m.Value());
  }

  void CheckFloat32Constant(Node* n, float expected) {
    CHECK_EQ(IrOpcode::kFloat32Constant, n->opcode());
    float fval = OpParameter<float>(n->op());
    CheckDoubleEq(expected, fval);
  }

  void CheckHeapConstant(Node* n, HeapObject* expected) {
    HeapObjectMatcher m(n);
    CHECK(m.HasValue());
    CHECK_EQ(expected, *m.Value());
  }

  void CheckNumberConstant(Node* n, double expected) {
    NumberMatcher m(n);
    CHECK_EQ(IrOpcode::kNumberConstant, n->opcode());
    CHECK(m.HasValue());
    CheckDoubleEq(expected, m.Value());
  }

  Node* Parameter(int index = 0) {
    Node* n = graph()->NewNode(common()->Parameter(index), graph()->start());
    NodeProperties::SetType(n, Type::Any());
    return n;
  }

  void CheckTypeError(MachineRepresentation from, Type* from_type,
                      MachineRepresentation to) {
    changer()->testing_type_errors_ = true;
    changer()->type_error_ = false;
    Node* n = Parameter(0);
    Node* c = changer()->GetRepresentationFor(n, from, from_type, to);
    CHECK(changer()->type_error_);
    CHECK_EQ(n, c);
  }

  void CheckNop(MachineRepresentation from, Type* from_type,
                MachineRepresentation to) {
    Node* n = Parameter(0);
    Node* c = changer()->GetRepresentationFor(n, from, from_type, to);
    CHECK_EQ(n, c);
  }
};


const MachineType kMachineTypes[] = {
    MachineType::Float32(), MachineType::Float64(),  MachineType::Int8(),
    MachineType::Uint8(),   MachineType::Int16(),    MachineType::Uint16(),
    MachineType::Int32(),   MachineType::Uint32(),   MachineType::Int64(),
    MachineType::Uint64(),  MachineType::AnyTagged()};


TEST(BoolToBit_constant) {
  RepresentationChangerTester r;

  Node* true_node = r.jsgraph()->TrueConstant();
  Node* true_bit = r.changer()->GetRepresentationFor(
      true_node, MachineRepresentation::kTagged, Type::None(),
      MachineRepresentation::kBit);
  r.CheckInt32Constant(true_bit, 1);

  Node* false_node = r.jsgraph()->FalseConstant();
  Node* false_bit = r.changer()->GetRepresentationFor(
      false_node, MachineRepresentation::kTagged, Type::None(),
      MachineRepresentation::kBit);
  r.CheckInt32Constant(false_bit, 0);
}


TEST(BitToBool_constant) {
  RepresentationChangerTester r;

  for (int i = -5; i < 5; i++) {
    Node* node = r.jsgraph()->Int32Constant(i);
    Node* val = r.changer()->GetRepresentationFor(
        node, MachineRepresentation::kBit, Type::Boolean(),
        MachineRepresentation::kTagged);
    r.CheckHeapConstant(val, i == 0 ? r.isolate()->heap()->false_value()
                                    : r.isolate()->heap()->true_value());
  }
}


TEST(ToTagged_constant) {
  RepresentationChangerTester r;

  {
    FOR_FLOAT64_INPUTS(i) {
      Node* n = r.jsgraph()->Float64Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat64, Type::None(),
          MachineRepresentation::kTagged);
      r.CheckNumberConstant(c, *i);
    }
  }

  {
    FOR_FLOAT64_INPUTS(i) {
      Node* n = r.jsgraph()->Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat64, Type::None(),
          MachineRepresentation::kTagged);
      r.CheckNumberConstant(c, *i);
    }
  }

  {
    FOR_FLOAT32_INPUTS(i) {
      Node* n = r.jsgraph()->Float32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat32, Type::None(),
          MachineRepresentation::kTagged);
      r.CheckNumberConstant(c, *i);
    }
  }

  {
    FOR_INT32_INPUTS(i) {
      Node* n = r.jsgraph()->Int32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kWord32, Type::Signed32(),
          MachineRepresentation::kTagged);
      r.CheckNumberConstant(c, *i);
    }
  }

  {
    FOR_UINT32_INPUTS(i) {
      Node* n = r.jsgraph()->Int32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kWord32, Type::Unsigned32(),
          MachineRepresentation::kTagged);
      r.CheckNumberConstant(c, *i);
    }
  }
}


TEST(ToFloat64_constant) {
  RepresentationChangerTester r;

  {
    FOR_FLOAT64_INPUTS(i) {
      Node* n = r.jsgraph()->Float64Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat64, Type::None(),
          MachineRepresentation::kFloat64);
      CHECK_EQ(n, c);
    }
  }

  {
    FOR_FLOAT64_INPUTS(i) {
      Node* n = r.jsgraph()->Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kTagged, Type::None(),
          MachineRepresentation::kFloat64);
      r.CheckFloat64Constant(c, *i);
    }
  }

  {
    FOR_FLOAT32_INPUTS(i) {
      Node* n = r.jsgraph()->Float32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat32, Type::None(),
          MachineRepresentation::kFloat64);
      r.CheckFloat64Constant(c, *i);
    }
  }

  {
    FOR_INT32_INPUTS(i) {
      Node* n = r.jsgraph()->Int32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kWord32, Type::Signed32(),
          MachineRepresentation::kFloat64);
      r.CheckFloat64Constant(c, *i);
    }
  }

  {
    FOR_UINT32_INPUTS(i) {
      Node* n = r.jsgraph()->Int32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kWord32, Type::Unsigned32(),
          MachineRepresentation::kFloat64);
      r.CheckFloat64Constant(c, *i);
    }
  }
}


static bool IsFloat32Int32(int32_t val) {
  return val >= -(1 << 23) && val <= (1 << 23);
}


static bool IsFloat32Uint32(uint32_t val) { return val <= (1 << 23); }


TEST(ToFloat32_constant) {
  RepresentationChangerTester r;

  {
    FOR_FLOAT32_INPUTS(i) {
      Node* n = r.jsgraph()->Float32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat32, Type::None(),
          MachineRepresentation::kFloat32);
      CHECK_EQ(n, c);
    }
  }

  {
    FOR_FLOAT32_INPUTS(i) {
      Node* n = r.jsgraph()->Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kTagged, Type::None(),
          MachineRepresentation::kFloat32);
      r.CheckFloat32Constant(c, *i);
    }
  }

  {
    FOR_FLOAT32_INPUTS(i) {
      Node* n = r.jsgraph()->Float64Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat64, Type::None(),
          MachineRepresentation::kFloat32);
      r.CheckFloat32Constant(c, *i);
    }
  }

  {
    FOR_INT32_INPUTS(i) {
      if (!IsFloat32Int32(*i)) continue;
      Node* n = r.jsgraph()->Int32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kWord32, Type::Signed32(),
          MachineRepresentation::kFloat32);
      r.CheckFloat32Constant(c, static_cast<float>(*i));
    }
  }

  {
    FOR_UINT32_INPUTS(i) {
      if (!IsFloat32Uint32(*i)) continue;
      Node* n = r.jsgraph()->Int32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kWord32, Type::Unsigned32(),
          MachineRepresentation::kFloat32);
      r.CheckFloat32Constant(c, static_cast<float>(*i));
    }
  }
}


TEST(ToInt32_constant) {
  RepresentationChangerTester r;

  {
    FOR_INT32_INPUTS(i) {
      Node* n = r.jsgraph()->Int32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kWord32, Type::Signed32(),
          MachineRepresentation::kWord32);
      r.CheckInt32Constant(c, *i);
    }
  }

  {
    FOR_INT32_INPUTS(i) {
      if (!IsFloat32Int32(*i)) continue;
      Node* n = r.jsgraph()->Float32Constant(static_cast<float>(*i));
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat32, Type::Signed32(),
          MachineRepresentation::kWord32);
      r.CheckInt32Constant(c, *i);
    }
  }

  {
    FOR_INT32_INPUTS(i) {
      Node* n = r.jsgraph()->Float64Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat64, Type::Signed32(),
          MachineRepresentation::kWord32);
      r.CheckInt32Constant(c, *i);
    }
  }

  {
    FOR_INT32_INPUTS(i) {
      Node* n = r.jsgraph()->Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kTagged, Type::Signed32(),
          MachineRepresentation::kWord32);
      r.CheckInt32Constant(c, *i);
    }
  }
}


TEST(ToUint32_constant) {
  RepresentationChangerTester r;

  {
    FOR_UINT32_INPUTS(i) {
      Node* n = r.jsgraph()->Int32Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kWord32, Type::Unsigned32(),
          MachineRepresentation::kWord32);
      r.CheckUint32Constant(c, *i);
    }
  }

  {
    FOR_UINT32_INPUTS(i) {
      if (!IsFloat32Uint32(*i)) continue;
      Node* n = r.jsgraph()->Float32Constant(static_cast<float>(*i));
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat32, Type::Unsigned32(),
          MachineRepresentation::kWord32);
      r.CheckUint32Constant(c, *i);
    }
  }

  {
    FOR_UINT32_INPUTS(i) {
      Node* n = r.jsgraph()->Float64Constant(*i);
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kFloat64, Type::Unsigned32(),
          MachineRepresentation::kWord32);
      r.CheckUint32Constant(c, *i);
    }
  }

  {
    FOR_UINT32_INPUTS(i) {
      Node* n = r.jsgraph()->Constant(static_cast<double>(*i));
      Node* c = r.changer()->GetRepresentationFor(
          n, MachineRepresentation::kTagged, Type::Unsigned32(),
          MachineRepresentation::kWord32);
      r.CheckUint32Constant(c, *i);
    }
  }
}


static void CheckChange(IrOpcode::Value expected, MachineRepresentation from,
                        Type* from_type, MachineRepresentation to) {
  RepresentationChangerTester r;

  Node* n = r.Parameter();
  Node* c = r.changer()->GetRepresentationFor(n, from, from_type, to);

  CHECK_NE(c, n);
  CHECK_EQ(expected, c->opcode());
  CHECK_EQ(n, c->InputAt(0));
}


static void CheckTwoChanges(IrOpcode::Value expected2,
                            IrOpcode::Value expected1,
                            MachineRepresentation from, Type* from_type,
                            MachineRepresentation to) {
  RepresentationChangerTester r;

  Node* n = r.Parameter();
  Node* c1 = r.changer()->GetRepresentationFor(n, from, from_type, to);

  CHECK_NE(c1, n);
  CHECK_EQ(expected1, c1->opcode());
  Node* c2 = c1->InputAt(0);
  CHECK_NE(c2, n);
  CHECK_EQ(expected2, c2->opcode());
  CHECK_EQ(n, c2->InputAt(0));
}


TEST(SingleChanges) {
  CheckChange(IrOpcode::kChangeBoolToBit, MachineRepresentation::kTagged,
              Type::None(), MachineRepresentation::kBit);
  CheckChange(IrOpcode::kChangeBitToBool, MachineRepresentation::kBit,
              Type::None(), MachineRepresentation::kTagged);

  CheckChange(IrOpcode::kChangeInt32ToTagged, MachineRepresentation::kWord32,
              Type::Signed32(), MachineRepresentation::kTagged);
  CheckChange(IrOpcode::kChangeUint32ToTagged, MachineRepresentation::kWord32,
              Type::Unsigned32(), MachineRepresentation::kTagged);
  CheckChange(IrOpcode::kChangeFloat64ToTagged, MachineRepresentation::kFloat64,
              Type::None(), MachineRepresentation::kTagged);

  CheckChange(IrOpcode::kChangeTaggedToInt32, MachineRepresentation::kTagged,
              Type::Signed32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeTaggedToUint32, MachineRepresentation::kTagged,
              Type::Unsigned32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeTaggedToFloat64, MachineRepresentation::kTagged,
              Type::None(), MachineRepresentation::kFloat64);

  // Int32,Uint32 <-> Float64 are actually machine conversions.
  CheckChange(IrOpcode::kChangeInt32ToFloat64, MachineRepresentation::kWord32,
              Type::Signed32(), MachineRepresentation::kFloat64);
  CheckChange(IrOpcode::kChangeUint32ToFloat64, MachineRepresentation::kWord32,
              Type::Unsigned32(), MachineRepresentation::kFloat64);
  CheckChange(IrOpcode::kChangeFloat64ToInt32, MachineRepresentation::kFloat64,
              Type::Signed32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeFloat64ToUint32, MachineRepresentation::kFloat64,
              Type::Unsigned32(), MachineRepresentation::kWord32);

  CheckChange(IrOpcode::kTruncateFloat64ToFloat32,
              MachineRepresentation::kFloat64, Type::None(),
              MachineRepresentation::kFloat32);

  // Int32,Uint32 <-> Float32 require two changes.
  CheckTwoChanges(IrOpcode::kChangeInt32ToFloat64,
                  IrOpcode::kTruncateFloat64ToFloat32,
                  MachineRepresentation::kWord32, Type::Signed32(),
                  MachineRepresentation::kFloat32);
  CheckTwoChanges(IrOpcode::kChangeUint32ToFloat64,
                  IrOpcode::kTruncateFloat64ToFloat32,
                  MachineRepresentation::kWord32, Type::Unsigned32(),
                  MachineRepresentation::kFloat32);
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToInt32,
                  MachineRepresentation::kFloat32, Type::Signed32(),
                  MachineRepresentation::kWord32);
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToUint32,
                  MachineRepresentation::kFloat32, Type::Unsigned32(),
                  MachineRepresentation::kWord32);

  // Float32 <-> Tagged require two changes.
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kChangeFloat64ToTagged,
                  MachineRepresentation::kFloat32, Type::None(),
                  MachineRepresentation::kTagged);
  CheckTwoChanges(IrOpcode::kChangeTaggedToFloat64,
                  IrOpcode::kTruncateFloat64ToFloat32,
                  MachineRepresentation::kTagged, Type::None(),
                  MachineRepresentation::kFloat32);
}


TEST(SignednessInWord32) {
  RepresentationChangerTester r;

  CheckChange(IrOpcode::kChangeTaggedToInt32, MachineRepresentation::kTagged,
              Type::Signed32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeTaggedToUint32, MachineRepresentation::kTagged,
              Type::Unsigned32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kChangeInt32ToFloat64, MachineRepresentation::kWord32,
              Type::None(), MachineRepresentation::kFloat64);
  CheckChange(IrOpcode::kChangeFloat64ToInt32, MachineRepresentation::kFloat64,
              Type::Signed32(), MachineRepresentation::kWord32);
  CheckChange(IrOpcode::kTruncateFloat64ToInt32,
              MachineRepresentation::kFloat64, Type::Number(),
              MachineRepresentation::kWord32);

  CheckTwoChanges(IrOpcode::kChangeInt32ToFloat64,
                  IrOpcode::kTruncateFloat64ToFloat32,
                  MachineRepresentation::kWord32, Type::None(),
                  MachineRepresentation::kFloat32);
  CheckTwoChanges(IrOpcode::kChangeFloat32ToFloat64,
                  IrOpcode::kTruncateFloat64ToInt32,
                  MachineRepresentation::kFloat32, Type::Number(),
                  MachineRepresentation::kWord32);
}


TEST(Nops) {
  RepresentationChangerTester r;

  // X -> X is always a nop for any single representation X.
  for (size_t i = 0; i < arraysize(kMachineTypes); i++) {
    r.CheckNop(kMachineTypes[i].representation(), Type::None(),
               kMachineTypes[i].representation());
  }

  // 32-bit floats.
  r.CheckNop(MachineRepresentation::kFloat32, Type::None(),
             MachineRepresentation::kFloat32);
  r.CheckNop(MachineRepresentation::kFloat32, Type::Number(),
             MachineRepresentation::kFloat32);

  // 32-bit words can be used as smaller word sizes and vice versa, because
  // loads from memory implicitly sign or zero extend the value to the
  // full machine word size, and stores implicitly truncate.
  r.CheckNop(MachineRepresentation::kWord32, Type::Signed32(),
             MachineRepresentation::kWord8);
  r.CheckNop(MachineRepresentation::kWord32, Type::Signed32(),
             MachineRepresentation::kWord16);
  r.CheckNop(MachineRepresentation::kWord32, Type::Signed32(),
             MachineRepresentation::kWord32);
  r.CheckNop(MachineRepresentation::kWord8, Type::Signed32(),
             MachineRepresentation::kWord32);
  r.CheckNop(MachineRepresentation::kWord16, Type::Signed32(),
             MachineRepresentation::kWord32);

  // kRepBit (result of comparison) is implicitly a wordish thing.
  r.CheckNop(MachineRepresentation::kBit, Type::None(),
             MachineRepresentation::kWord8);
  r.CheckNop(MachineRepresentation::kBit, Type::None(),
             MachineRepresentation::kWord16);
  r.CheckNop(MachineRepresentation::kBit, Type::None(),
             MachineRepresentation::kWord32);
  r.CheckNop(MachineRepresentation::kBit, Type::None(),
             MachineRepresentation::kWord64);
  r.CheckNop(MachineRepresentation::kBit, Type::Boolean(),
             MachineRepresentation::kWord8);
  r.CheckNop(MachineRepresentation::kBit, Type::Boolean(),
             MachineRepresentation::kWord16);
  r.CheckNop(MachineRepresentation::kBit, Type::Boolean(),
             MachineRepresentation::kWord32);
  r.CheckNop(MachineRepresentation::kBit, Type::Boolean(),
             MachineRepresentation::kWord64);
}


TEST(TypeErrors) {
  RepresentationChangerTester r;

  // Wordish cannot be implicitly converted to/from comparison conditions.
  r.CheckTypeError(MachineRepresentation::kWord8, Type::None(),
                   MachineRepresentation::kBit);
  r.CheckTypeError(MachineRepresentation::kWord16, Type::None(),
                   MachineRepresentation::kBit);
  r.CheckTypeError(MachineRepresentation::kWord32, Type::None(),
                   MachineRepresentation::kBit);
  r.CheckTypeError(MachineRepresentation::kWord64, Type::None(),
                   MachineRepresentation::kBit);

  // Floats cannot be implicitly converted to/from comparison conditions.
  r.CheckTypeError(MachineRepresentation::kFloat64, Type::None(),
                   MachineRepresentation::kBit);
  r.CheckTypeError(MachineRepresentation::kBit, Type::None(),
                   MachineRepresentation::kFloat64);
  r.CheckTypeError(MachineRepresentation::kBit, Type::Boolean(),
                   MachineRepresentation::kFloat64);

  // Floats cannot be implicitly converted to/from comparison conditions.
  r.CheckTypeError(MachineRepresentation::kFloat32, Type::None(),
                   MachineRepresentation::kBit);
  r.CheckTypeError(MachineRepresentation::kBit, Type::None(),
                   MachineRepresentation::kFloat32);
  r.CheckTypeError(MachineRepresentation::kBit, Type::Boolean(),
                   MachineRepresentation::kFloat32);

  // Word64 is internal and shouldn't be implicitly converted.
  r.CheckTypeError(MachineRepresentation::kWord64, Type::None(),
                   MachineRepresentation::kTagged);
  r.CheckTypeError(MachineRepresentation::kTagged, Type::None(),
                   MachineRepresentation::kWord64);
  r.CheckTypeError(MachineRepresentation::kTagged, Type::Boolean(),
                   MachineRepresentation::kWord64);

  // Word64 / Word32 shouldn't be implicitly converted.
  r.CheckTypeError(MachineRepresentation::kWord64, Type::None(),
                   MachineRepresentation::kWord32);
  r.CheckTypeError(MachineRepresentation::kWord32, Type::None(),
                   MachineRepresentation::kWord64);
  r.CheckTypeError(MachineRepresentation::kWord32, Type::Signed32(),
                   MachineRepresentation::kWord64);
  r.CheckTypeError(MachineRepresentation::kWord32, Type::Unsigned32(),
                   MachineRepresentation::kWord64);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
