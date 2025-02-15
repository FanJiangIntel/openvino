// Copyright (C) 2018-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <gtest/gtest.h>

#include <low_precision/concat.hpp>
#include <low_precision/fake_quantize_decomposition.hpp>
#include <low_precision/max_pool.hpp>
#include <low_precision/strided_slice.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <transformations/init_node_info.hpp>
#include <transformations/utils/utils.hpp>

#include "common_test_utils/ngraph_test_utils.hpp"
#include "layer_transformation.hpp"
#include "lpt_ngraph_functions/common/fake_quantize_on_data.hpp"
#include "lpt_ngraph_functions/concat_function.hpp"
#include "simple_low_precision_transformer.hpp"

using namespace testing;
using namespace ngraph;
using namespace ngraph::pass;

namespace {

class ConcatTransformationActualValues {
public:
    ngraph::builder::subgraph::FakeQuantizeOnData fakeQuantize1;
    ngraph::builder::subgraph::FakeQuantizeOnData fakeQuantize2;
};

inline std::ostream& operator<<(std::ostream& out, const ConcatTransformationActualValues& values) {
    return out << "_" << values.fakeQuantize1 << "_" << values.fakeQuantize2;
}

class ConcatTransformationResultValues {
public:
    ngraph::builder::subgraph::FakeQuantizeOnData fakeQuantize1;
    ngraph::builder::subgraph::FakeQuantizeOnData fakeQuantize2;
    ngraph::builder::subgraph::DequantizationOperations dequantizationBefore;
    ngraph::element::Type precisionBeforeConcat;
    ngraph::element::Type precisionAfterConcat;
    ngraph::builder::subgraph::DequantizationOperations dequantizationAfter1;
    ngraph::builder::subgraph::DequantizationOperations dequantizationAfter2;
};

inline std::ostream& operator<<(std::ostream& out, const ConcatTransformationResultValues& values) {
    return out << "_" << values.fakeQuantize1 << "_" << values.fakeQuantize2 << "_" << values.dequantizationAfter1
               << "_" << values.dequantizationAfter2;
}

class ConcatTransformationTestValues {
public:
    TestTransformationParams params;
    bool multiChannels;
    bool ssBeforeConcat;
    bool ssAfterConcat;
    ConcatTransformationActualValues actual;
    ConcatTransformationResultValues result;
};

inline std::ostream& operator<<(std::ostream& out, const ConcatTransformationTestValues& values) {
    return out << "_" << values.multiChannels << "_" << values.actual << "_" << values.result;
}

typedef std::tuple<ngraph::element::Type, ngraph::PartialShape, ConcatTransformationTestValues>
    ConcatTransformationParams;

class ConcatWithStridedSliceTransformation : public LayerTransformation,
                                             public testing::WithParamInterface<ConcatTransformationParams> {
public:
    void SetUp() override {
        const ngraph::element::Type precision = std::get<0>(GetParam());
        const ngraph::PartialShape shape = std::get<1>(GetParam());
        ConcatTransformationTestValues testValues = std::get<2>(GetParam());

        actualFunction =
            ngraph::builder::subgraph::ConcatFunction::getOriginalWithStridedSlice(precision,
                                                                                   shape,
                                                                                   testValues.actual.fakeQuantize1,
                                                                                   testValues.actual.fakeQuantize2,
                                                                                   testValues.ssBeforeConcat,
                                                                                   testValues.ssAfterConcat);

        auto supportedPrecisions = std::vector<ngraph::pass::low_precision::PrecisionsRestriction>(
            {ngraph::pass::low_precision::PrecisionsRestriction::create<ngraph::opset1::Convolution>({
                {{0}, testValues.params.precisionsOnActivations},
                {{1}, testValues.params.precisionsOnWeights},
            })});

        auto quantizationRestrictions =
            testValues.multiChannels ? std::vector<ngraph::pass::low_precision::QuantizationGranularityRestriction>()
                                     : std::vector<ngraph::pass::low_precision::QuantizationGranularityRestriction>(
                                           {ngraph::pass::low_precision::QuantizationGranularityRestriction::create<
                                               ngraph::opset1::Convolution>()});

        SimpleLowPrecisionTransformer transform(supportedPrecisions, quantizationRestrictions);
        transform.add<ngraph::pass::low_precision::ConcatTransformation, ngraph::opset1::Concat>(testValues.params);
        transform
            .add<ngraph::pass::low_precision::FakeQuantizeDecompositionTransformation, ngraph::opset1::FakeQuantize>(
                testValues.params);
        transform.add<ngraph::pass::low_precision::MaxPoolTransformation, ngraph::opset1::MaxPool>(testValues.params);
        transform.add<ngraph::pass::low_precision::StridedSliceTransformation, ngraph::opset1::StridedSlice>(
            testValues.params);
        transform.transform(actualFunction);

        referenceFunction = ngraph::builder::subgraph::ConcatFunction::getReferenceWithStridedSlice(
            precision,
            shape,
            testValues.result.fakeQuantize1,
            testValues.result.fakeQuantize2,
            testValues.result.dequantizationBefore,
            testValues.result.precisionBeforeConcat,
            testValues.result.precisionAfterConcat,
            testValues.ssBeforeConcat,
            testValues.ssAfterConcat,
            testValues.result.dequantizationAfter1,
            testValues.result.dequantizationAfter2);
    }

    static std::string getTestCaseName(testing::TestParamInfo<ConcatTransformationParams> obj) {
        const ngraph::element::Type precision = std::get<0>(obj.param);
        const ngraph::PartialShape shape = std::get<1>(obj.param);
        const ConcatTransformationTestValues testValues = std::get<2>(obj.param);

        std::ostringstream result;
        result << LayerTransformation::getTestCaseNameByParams(precision, shape, testValues.params) << "_"
               << (testValues.multiChannels ? "multiChannels_" : "notMultiChannels_")
               << (testValues.ssBeforeConcat ? "SS_before_concat_" : "")
               << (testValues.ssAfterConcat ? "SS_after_cancat_" : "") << testValues.actual << "_" << testValues.result
               << "_";
        return result.str();
    }
};

TEST_P(ConcatWithStridedSliceTransformation, CompareFunctions) {
    actualFunction->validate_nodes_and_infer_types();
    auto res = compare_functions(actualFunction, referenceFunction, true);
    ASSERT_TRUE(res.first) << res.second;
}

const std::vector<ngraph::element::Type> precisions = {
    ngraph::element::f32,
    // ngraph::element::f16
};

const std::vector<ngraph::PartialShape> shapes = {
    {1, 4, 9, 9},
    {4, 4, 9, 9},
    {Dimension::dynamic(), 4, Dimension::dynamic(), Dimension::dynamic()}};

const std::vector<ConcatTransformationTestValues> testValues = {
    // FQ with the same values, ss before concat, ss after concat
    {LayerTransformation::createParamsU8I8(),
     true,
     true,
     true,
     {{256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {2.55f}},
      {256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {2.55f}}},
     {{256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {255.f}},
      {256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {255.f}},
      {ngraph::element::f32, {}, {0.01f}},
      ngraph::element::u8,
      ngraph::element::u8,
      {ngraph::element::f32, {}, {0.01f}},
      {ngraph::element::f32, {}, {0.01f}}}},
    // FQ with different values, ss before concat, ss after concat
    {LayerTransformation::createParamsU8I8(),
     true,
     true,
     true,
     {{256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {2.55f}},
      {256ul, ngraph::Shape({}), {0.f}, {25.5f}, {0.f}, {25.5f}}},
     {{256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {255.f}},
      {256ul, ngraph::Shape({}), {0.f}, {25.5f}, {0.f}, {255.f}},
      {ngraph::element::f32, {}, {0.01f}},
      ngraph::element::u8,
      ngraph::element::u8,
      {ngraph::element::f32, {}, {{0.01f, 0.01f, 0.1f, 0.1f}}},
      {ngraph::element::f32, {}, {{0.01f, 0.01f, 0.1f, 0.1f, 0.1f, 0.1f}}}}},
    // FQ with different values, ss after concat
    {LayerTransformation::createParamsU8I8(),
     true,
     false,
     true,
     {{256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {2.55f}},
      {256ul, ngraph::Shape({}), {0.f}, {25.5f}, {0.f}, {25.5f}}},
     {{256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {255.f}},
      {256ul, ngraph::Shape({}), {0.f}, {25.5f}, {0.f}, {255.f}},
      {ngraph::element::f32, {}, {0.01f}},
      ngraph::element::u8,
      ngraph::element::u8,
      {ngraph::element::f32, {}, {{0.01f, 0.01f, 0.01f, 0.01f, 0.1f, 0.1f}}},
      {ngraph::element::f32, {}, {{0.01f, 0.01f, 0.01f, 0.01f, 0.1f, 0.1f, 0.1f, 0.1f}}}}},
    // FQ with different values, ss before concat
    {LayerTransformation::createParamsU8I8(),
     true,
     true,
     false,
     {{256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {2.55f}},
      {256ul, ngraph::Shape({}), {0.f}, {25.5f}, {0.f}, {25.5f}}},
     {{256ul, ngraph::Shape({}), {0.f}, {2.55f}, {0.f}, {255.f}},
      {256ul, ngraph::Shape({}), {0.f}, {25.5f}, {0.f}, {255.f}},
      {ngraph::element::f32, {}, {0.01f}},
      ngraph::element::u8,
      ngraph::element::u8,
      {ngraph::element::f32, {}, {{0.01f, 0.01f, 0.1f, 0.1f, 0.1f, 0.1f}}},
      {ngraph::element::f32, {}, {{0.01f, 0.01f, 0.1f, 0.1f, 0.1f, 0.1f}}}}},
    // FQ with zero-point, ss before concat, ss after concat
    {LayerTransformation::createParamsU8I8(),
     true,
     true,
     true,
     {{256ul, {}, {0.f}, {2.55f}, {0.f}, {2.55f}}, {256ul, {}, {1.275f}, {2.55f}, {1.275f}, {2.55f}}},
     {{256ul, {}, {0.f}, {2.55f}, {0.f}, {255.f}},
      {256ul, {}, {1.275f}, {2.55f}, {0.f}, {255.f}},
      {ngraph::element::f32, {}, {0.01f}},
      ngraph::element::u8,
      ngraph::element::u8,
      {ngraph::element::f32, {{0.f, 0.f, -255.f, -255.f}}, {{0.01f, 0.01f, 0.005f, 0.005f}}},
      {ngraph::element::f32,
       {{0.f, 0.f, -255.f, -255.f, -255.f, -255.f}},
       {{0.01f, 0.01f, 0.005f, 0.005f, 0.005f, 0.005f}}}}},
    // not multi channels concat, ss before concat, ss after concat
    {LayerTransformation::createParamsU8I8(),
     false,
     true,
     true,
     {{256ul, {}, {0.f}, {2.55f}, {0.f}, {2.55f}}, {256ul, {}, {-1.28f}, {1.27f}, {-1.28f}, {1.27f}}},
     {{256ul, {}, {0.f}, {2.55f}, {85.f}, {255.f}},
      {256ul, {}, {-1.28f}, {1.27f}, {0.f}, {170.f}},
      {ngraph::element::f32, {85}, {0.015f}},
      ngraph::element::u8,
      ngraph::element::u8,
      {ngraph::element::f32, {85}, {0.015f}},
      {ngraph::element::f32, {85}, {0.015f}}}},
};

INSTANTIATE_TEST_SUITE_P(smoke_LPT,
                         ConcatWithStridedSliceTransformation,
                         ::testing::Combine(::testing::ValuesIn(precisions),
                                            ::testing::ValuesIn(shapes),
                                            ::testing::ValuesIn(testValues)),
                         ConcatWithStridedSliceTransformation::getTestCaseName);
}  // namespace
