#include <limits.h>

Datum vec_to_stats_transfn(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(vec_to_stats_transfn);

/**
 * Returns an array of 4 elements
 * after processing the output of subarray_to_sum_stats
 * element 1 is the sum
 * element 2 is the min
 * element 3 is the max
 * element 4 is the sum
 *
 * by Todd Hay
 */
Datum
vec_to_stats_transfn(PG_FUNCTION_ARGS)
{
  Oid elemTypeId;
  int16 elemTypeWidth;
  bool elemTypeByValue;
  char elemTypeAlignmentCode;
  int currentLength;
  MemoryContext aggContext;
  VecArrayBuildState *state = NULL;
  ArrayType *currentArray;
  int arrayLength;
  Datum *currentVals;
  bool *currentNulls;
  int i;
  int minIndex, maxIndex;

  if (!AggCheckCallContext(fcinfo, &aggContext)) {
    elog(ERROR, "vec_to_stats_transfn called in non-aggregate context");
  }

  // PG_ARGISNULL tests for SQL NULL,
  // but after the first pass we can have a
  // value that is non-SQL-NULL but still is C NULL.
  if (!PG_ARGISNULL(0)) {
    state = (VecArrayBuildState *)PG_GETARG_POINTER(0);
  }

  if (PG_ARGISNULL(1)) {
    // just return the current state unchanged (possibly still NULL)
    PG_RETURN_POINTER(state);
  }
  currentArray = PG_GETARG_ARRAYTYPE_P(1);

  if (state == NULL) {
    // Since we have our first not-null argument
    // we can initialize the state to match its length.
    elemTypeId = ARR_ELEMTYPE(currentArray);
    if (elemTypeId != INT4OID) {
      ereport(ERROR, (errmsg("vec_to_stats input must be array of INTEGER")));
    }
    if (ARR_NDIM(currentArray) != 1) {
      ereport(ERROR, (errmsg("One-dimensional arrays are required")));
    }
    arrayLength = (ARR_DIMS(currentArray))[0];

    if (arrayLength != 4) {
      ereport(ERROR, (errmsg("vec_to_stats input length must be 4.")));
    }

    // Start with all zeros:
    state = initVecArrayResultWithNulls(elemTypeId, elemTypeId, aggContext, arrayLength);
    switch (elemTypeId) {
    case INT4OID:
      state->vecvalues[0].i32 = 0; // sum
      /* state->vecvalues[1].i32 = INT_MAX; // minIndex */
      state->vecvalues[1].i32 = 50000; // minIndex
      state->vecvalues[2].i32 = -1; // maxIndex
      state->vecvalues[3].i32 = 0; // count
      break;
    default:
      elog(ERROR, "Unknown elemTypeId!");
    }
    for (i=0; i < arrayLength; i++)
      state->state.dnulls[i] = false;
  } else {
    elemTypeId = state->inputElementType;
    arrayLength = state->state.nelems;
  }

  get_typlenbyvalalign(elemTypeId, &elemTypeWidth, &elemTypeByValue, &elemTypeAlignmentCode);
  deconstruct_array(currentArray, elemTypeId, elemTypeWidth, elemTypeByValue, elemTypeAlignmentCode,
                    &currentVals, &currentNulls, &currentLength);
  if (currentLength != arrayLength) {
    ereport(ERROR, (errmsg("All arrays must be the same length, but we got %d vs %d", currentLength, arrayLength)));
  }

  if (currentNulls[i]) {
    // do nothing: nulls can't change the result.
  } else {
    state->state.dnulls[i] = false;
    switch (elemTypeId) {
    case INT4OID:
      state->vecvalues[0].i32 += DatumGetInt32(currentVals[0]);
      minIndex = DatumGetInt32(currentVals[1]);
      if (minIndex < state->vecvalues[1].i32) {
        state->vecvalues[1].i32 = minIndex;
      }
      maxIndex = DatumGetInt32(currentVals[2]);
      if (maxIndex > state->vecvalues[2].i32) {
        state->vecvalues[2].i32 = maxIndex;
      }
      state->vecvalues[3].i32 += DatumGetInt32(currentVals[3]);
      break;
    default:
      elog(ERROR, "Unknown elemTypeId!");
    }
  }
  PG_RETURN_POINTER(state);
}

Datum vec_to_stats_finalfn(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(vec_to_stats_finalfn);

Datum
vec_to_stats_finalfn(PG_FUNCTION_ARGS)
{
  Datum result;
  VecArrayBuildState *state;
  int dims[1];
  int lbs[1];
  int i;

  Assert(AggCheckCallContext(fcinfo, NULL));

  state = PG_ARGISNULL(0) ? NULL : (VecArrayBuildState *)PG_GETARG_POINTER(0);

  if (state == NULL)
    PG_RETURN_NULL();

  // Convert from our pgnums to Datums:
  for (i = 0; i < state->state.nelems; i++) {
    if (state->state.dnulls[i]) continue;
    switch (state->inputElementType) {
    case INT4OID:
      state->state.dvalues[i] = Int32GetDatum(state->vecvalues[i].i32);
      break;
    }
  }

  dims[0] = state->state.nelems;
  lbs[0] = 1;

  result = makeMdArrayResult(&state->state, 1, dims, lbs, CurrentMemoryContext, false);
  PG_RETURN_DATUM(result);
}
