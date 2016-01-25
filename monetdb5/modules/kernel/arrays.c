#include "monetdb_config.h"
#include "mal_exception.h"
#include "arrays.h"

#include <algebra.h>
#include <gdk_arrays.h>

static BUN jumpSize(gdk_array *array, unsigned int dimNum) {
	unsigned short i=0;
	BUN skip = 1;
	for(i=0; i<dimNum; i++)
		skip *= array->dims[i]->elsNum;
	return skip;
}

static BUN arrayCellsNum(gdk_array *array) {
	return jumpSize(array, array->dimsNum);
}

static BAT*
newempty(const char *func)
{
    BAT *bn = BATnew(TYPE_void, TYPE_void, 0, TRANSIENT);
    if (bn == NULL) {
        GDKerror("%s: memory allocation error", func);
        return NULL;
    }
    BATseqbase(bn, 0);
    BATseqbase(BATmirror(bn), 0);
    return bn;
}

static int computeOids(oid** oidsRes, BUN currentOidsPos, unsigned int dimNum , gdk_array *array, gdk_array *dims, BUN jumpSize) {
	unsigned int idx = 0;
	gdk_dimension *currDim = dims->dims[dimNum];
	gdk_dimension *currDimOrig = array->dims[dimNum];

	jumpSize /= currDimOrig->elsNum;

	if(dimNum == 0) {
		/*increase as many posistions as the elements in the dimension*/
		if(currDim->idxs) {
			unsigned int i=0;
			for(i=0; i<currDim->elsNum; i++)
                 (*oidsRes)[currentOidsPos] = currDim->idxs[i];
		} else {
			for(idx=currDim->min; idx<=currDim->max; idx+=currDim->step, currentOidsPos++) {
				(*oidsRes)[currentOidsPos] = idx;
			}
		}
		return currentOidsPos;
	}

	/*for each idx of the dimension*/
	if(currDim->idxs) {
		unsigned int i=0;
		for(i=0; i<currDim->elsNum; i++) {
			/*let the other dimensions increase as many elemens as necessary*/
			BUN nextOidsPos = computeOids(oidsRes, currentOidsPos, dimNum-1, array, dims, jumpSize);
			/*update all the oids that were update by the other dimensions */
			for(; currentOidsPos<nextOidsPos; currentOidsPos++) {
				(*oidsRes)[currentOidsPos] += jumpSize*currDim->idxs[i];
			}
		}
	} else {
		for(idx=currDim->min; idx<=currDim->max; idx+=currDim->step) {
			/*let the other dimensions increase as many elemens as necessary*/
			BUN nextOidsPos = computeOids(oidsRes, currentOidsPos, dimNum-1, array, dims, jumpSize);
			/*update all the oids that were update by the other dimensions */
			for(; currentOidsPos<nextOidsPos; currentOidsPos++) {
				(*oidsRes)[currentOidsPos] += jumpSize*idx;
			}
		}
	}

	return currentOidsPos;
}

#if 0
/*UPDATED*/
static BUN oidToIdx(oid oidVal, int dimNum, int currentDimNum, BUN skipCells, gdk_array *array) {
	BUN oid = 0;

	while(currentDimNum < dimNum) {
		skipCells*=array->dims[currentDimNum]->elsNum;
		currentDimNum++;
	}

	if(currentDimNum == array->dimsNum-1)
		oid = oidVal;
	else
		oid = oidToIdx(oidVal, dimNum, currentDimNum+1, skipCells*array->dims[currentDimNum]->elsNum, array);

	if(currentDimNum == dimNum) //in the last one we do not compute module
		return oid/skipCells;
	return oid%skipCells;
}

/*UPDATED*/
static BUN* oidToIdx_bulk(oid* oidVals, int valsNum, int dimNum, int currentDimNum, BUN skipCells, gdk_array *array) {
	BUN *oids = GDKmalloc(valsNum*sizeof(BUN));
	int i;

	if(!oids) {
		GDKerror("Problem allocating space");
		return NULL;
	}

	while(currentDimNum < dimNum) {
		skipCells*=array->dims[currentDimNum]->elsNum;
		currentDimNum++;
	}

	if(currentDimNum == array->dimsNum-1) { //last dimension, do not go any deeper
		if(currentDimNum == dimNum) {//in the dimension of interest we do not compute the module
			for(i=0; i<valsNum; i++)
				oids[i] = oidVals[i]/skipCells;
		} else {
			for(i=0; i<valsNum; i++)
				oids[i] = oidVals[i]%skipCells;
		}	
	}
	else {
		BUN *oidRes = oidToIdx_bulk(oidVals, valsNum, dimNum, currentDimNum+1, skipCells*array->dims[currentDimNum]->elsNum, array);

		if(currentDimNum == dimNum) {//in the dimension of interest we do not compute the module
			for(i=0; i<valsNum; i++)
				oids[i] = oidRes[i]/skipCells;
		} else {
			for(i=0; i<valsNum; i++)
				oids[i] = oidRes[i]%skipCells;
		}	

		GDKfree(oidRes);
	}

	return oids;
}

#endif

static gdk_dimension* updateDimCandRange(gdk_dimension *dimCand, unsigned int min, unsigned int max) {
	dimCand->min = min > dimCand->min ? min : dimCand->min;
	dimCand->max = max < dimCand->max ? max : dimCand->max;
	dimCand->elsNum = min>max ? 0 : max-min+1; //if 0 then the dimension is empty
	return dimCand;
}

static gdk_dimension* updateDimCandIdxs(gdk_dimension *dimCand, unsigned int min, unsigned int max) {
	unsigned int elsNum = 0; 
	unsigned int *idxs = GDKmalloc(sizeof(unsigned int)*dimCand->elsNum); //at most (quailfyingIdx might alresy be out of the idxs)
	unsigned int i;

	for(i=0 ; i<dimCand->elsNum; i++) {
		if(dimCand->idxs[i] >= min && dimCand->idxs[i] <= max) {
			idxs[elsNum] = dimCand->idxs[i];
			elsNum++;
		}
	}
				
	//release the previous idxs
	GDKfree(dimCand->idxs);
	//store the new idxs
	dimCand->elsNum = elsNum; //if 0 then the dimension is empty
	dimCand->idxs = idxs;
	//upadte the min max if needed
	dimCand->min = min > dimCand->min ? min : dimCand->min;
	dimCand->max = max < dimCand->max ? max : dimCand->max;

	return dimCand;
}

str ALGmaterialise(bat* mbrResult, const ptr *dimsCands, const ptr *dims) {
	gdk_array *dimCands_in = (gdk_array*)*dimsCands;
	gdk_array *array = (gdk_array*)*dims;

	BAT *mbrBAT = projectCells(dimCands_in, array);

	if(!mbrBAT)
		throw(MAL, "algebra.materialise", "project cells failed");
	BBPkeepref(*mbrResult = mbrBAT->batCacheid);

	return MAL_SUCCEED;
}

str ALGdimensionSubselect2(ptr *dimsRes, const ptr *dim, const ptr* dims, const ptr *dimsCands,
							const void *low, const void *high, const bit *li, const bit *hi, const bit *anti) {
	gdk_array *array = (gdk_array*)*dims;
	gdk_analytic_dimension *dimension = (gdk_analytic_dimension*)*dim;
	gdk_dimension *dimCand = NULL;
	gdk_array *dimCands_in = NULL;

	oid qualifyingIdx_min, qualifyingIdx_max;
	int type;
	const void* nil;

	if(dimsCands) 
		dimCands_in = (gdk_array*)*dimsCands;
	else //all dimensions are candidates
		dimCands_in = arrayCopy(array);
	
	*dimsRes = dimCands_in; //the same pointers will be used but the dimension over which the selection will be performed might change 
	if(!dimCands_in) { //empty results
		arrayDelete(array); //I am not gona use it later I can delete it
		return MAL_SUCCEED;
	}
	dimCand = dimCands_in->dims[dimension->dimNum];

    type = dimension->type;
    nil = ATOMnilptr(type);
    type = ATOMbasetype(type);

    if(ATOMcmp(type, low, high) == 0) { //point selection   
		//find the idx of the value
		oid qualifyingIdx = equalIdx(dimension, low); 
		if(*anti) {
			//two ranges qualify for the result [0, quaifyingIdx-1] and [qualifyingIdx+1, max]
			unsigned int elsNum =0;
			unsigned int i=0;
			unsigned int *idxs = NULL;

			if(dimCand->idxs) { 
				idxs = GDKmalloc(sizeof(unsigned int)*dimCand->elsNum); //the qualifyingIdx might be already absent from the idxs
				for(i=0; i<dimCand->elsNum; i++) {
					if(dimCand->idxs[i] != qualifyingIdx) {
						idxs[elsNum] = i;
						elsNum++;
					}
				} 

				//release the previous idxs
				GDKfree(dimCand->idxs);
			} else {
				idxs = GDKmalloc(sizeof(unsigned int)*dimCand->elsNum-1); 
				for(i=dimCand->min ; i<=dimCand->max; i+=dimCand->step) { //we shoud be carefull to consider the limits because they might not be the default ones
					if(i != qualifyingIdx) {
						idxs[elsNum] = i;
						elsNum++;
					}
				}
			}

			//store the new idxs
			dimCand->elsNum = elsNum;
			dimCand->idxs = idxs;
			//upadte the min max if needed
			dimCand->min = qualifyingIdx > dimCand->min ? qualifyingIdx : dimCand->min;
			dimCand->max = qualifyingIdx < dimCand->max ? qualifyingIdx : dimCand->max;

			if(elsNum == 0) { //empty result
				arrayDelete(dimCands_in);
				arrayDelete(array); //I am not gonna use it again I can delete it
				*dimsRes = NULL;
			}

			return MAL_SUCCEED;

		} else { //a single element qualifies for the result
			qualifyingIdx_min = qualifyingIdx; 
			qualifyingIdx_max = qualifyingIdx; 	
		}
	} else if(ATOMcmp(type, low, nil) == 0) { //no lower bound
		qualifyingIdx_min = 0; 
		qualifyingIdx_max = greaterIdx(dimension, high, *hi); 
	} else if(ATOMcmp(type, high, nil) == 0) { //no upper bound
		qualifyingIdx_min = lowerIdx(dimension, low, *li); 
		qualifyingIdx_max = dimension->elsNum-1;
	} else if(ATOMcmp(type, low, nil) != 0 && ATOMcmp(type, high, nil) != 0) {
		qualifyingIdx_min = lowerIdx(dimension, low, *li); 
		qualifyingIdx_max = greaterIdx(dimension, high, *hi); 
	} else {
		arrayDelete(dimCands_in);
		*dimsRes = NULL;
		arrayDelete(array);
		return MAL_SUCCEED;
	}

	//Now that the new range has been found update the results of the dimCand
	if(dimCand->idxs) {
		dimCand = updateDimCandIdxs(dimCand, qualifyingIdx_min, qualifyingIdx_max);
	} else {
		dimCand = updateDimCandRange(dimCand, qualifyingIdx_min, qualifyingIdx_max);
	}	 

	if(dimCand->elsNum == 0) { //empty result
		arrayDelete(dimCands_in);
		arrayDelete(array);
		*dimsRes = NULL;
	}	

	return MAL_SUCCEED;
}

str ALGdimensionSubselect1(ptr *dimsRes, const ptr *dim, const ptr* dims, 
							const void *low, const void *high, const bit *li, const bit *hi, const bit *anti) {
	return ALGdimensionSubselect2(dimsRes, dim, dims, NULL, low, high, li, hi, anti);
}

#if 0
str ALGdimensionSubselect3(ptr *dimsRes, bat *oidsRes, const ptr *array, const bat *vals,
                            const void *low, const void *high, const bit *li, const bit *hi, const bit *anti) {
		/* it is the same with the ALGnonDimensionSubselect1 but with the array and vals argument in different order*/
 		return ALGnonDimensionSubselect1(oidsRes, dimsRes, vals, array, low, high, li, hi, anti);
}
#endif

str ALGdimensionThetasubselect2(ptr *dimsRes, const ptr *dim, const ptr* dims, const ptr *dimsCands, const void *val, const char **opp) {
	bit li = 0;
	bit hi = 0;
	bit anti = 0;
	const char *op = *opp;
	gdk_analytic_dimension *dimension = *dim;
	const void *nil = ATOMnilptr(dimension->type);

	if (op[0] == '=' && ((op[1] == '=' && op[2] == 0) || op[2] == 0)) {
        /* "=" or "==" */
		li = hi = 1;
		anti = 0;
        return ALGdimensionSubselect2(dimsRes, dim, dims, dimsCands, val, nil, &li, &hi, &anti);
    }
    if (op[0] == '!' && op[1] == '=' && op[2] == 0) {
        /* "!=" (equivalent to "<>") */ 
		li = hi = anti = 1;
        return ALGdimensionSubselect2(dimsRes, dim, dims, dimsCands, val, nil, &li, &hi, &anti);
    }
    if (op[0] == '<') { 
        if (op[1] == 0) {
            /* "<" */
			li = hi = anti = 0;
            return ALGdimensionSubselect2(dimsRes, dim, dims, dimsCands, nil, val, &li, &hi, &anti);
        }
        if (op[1] == '=' && op[2] == 0) {
            /* "<=" */
			li = anti = 0;
			hi = 1;
            return ALGdimensionSubselect2(dimsRes, dim, dims, dimsCands, nil, val, &li, &hi, &anti);
        }
        if (op[1] == '>' && op[2] == 0) {
            /* "<>" (equivalent to "!=") */ 
			li = hi = anti = 1;
            return ALGdimensionSubselect2(dimsRes, dim, dims, dimsCands, val, nil, &li, &hi, &anti);
        }
    }
    if (op[0] == '>') { 
        if (op[1] == 0) {
            /* ">" */
			li = hi = anti = 0;
            return ALGdimensionSubselect2(dimsRes, dim, dims, dimsCands, val, nil, &li, &hi, &anti);
        }
        if (op[1] == '=' && op[2] == 0) {
            /* ">=" */
			li = 1;
			hi = anti = 0;
            return ALGdimensionSubselect2(dimsRes, dim, dims, dimsCands, val, nil, &li, &hi, &anti);
        }
    }

    throw(MAL, "algebra.dimensionThetasubselect", "BATdimensionThetasubselect: unknown operator.\n");
}

str ALGdimensionThetasubselect1(ptr *dimsRes, const ptr *dim, const ptr* dims, const void *val, const char **op) {
	return ALGdimensionThetasubselect2(dimsRes, dim, dims, NULL, val, op);
}

str ALGdimensionLeftfetchjoin1(bat *result, const ptr *dimsCands, const ptr *dim, const ptr *dims) {
	gdk_array *array = (gdk_array*)*dims;
	gdk_analytic_dimension *dimension = (gdk_analytic_dimension*)*dim;
	gdk_array *dimCands_in = (gdk_array*)*dimsCands;
	
	BAT *resBAT = NULL;
	BUN resSize = 0;

	if(!dimCands_in) { //empty
		if(!(resBAT = newempty("dimensionLeftfetchjoin")))
			throw(MAL, "algebra.leftfetchjoin", "Problem allocating new BAT");
		BBPkeepref(*result = resBAT->batCacheid);
		return MAL_SUCCEED;
	} else {
		gdk_dimension *dimCand = dimCands_in->dims[dimension->dimNum];

		unsigned short i=0;

		BUN elsR = 1;
		BUN grpR = 1;

		for(i=0; i<dimension->dimNum; i++)
			elsR *= dimCands_in->dims[i]->elsNum;
		for(i=dimension->dimNum+1 ; i<array->dimsNum; i++)
			grpR *= dimCands_in->dims[i]->elsNum;

		resSize = dimCand->elsNum*elsR*grpR;
	
#define computeValues(TPE) \
do { \
	TPE min = *(TPE*)dimension->min; \
	TPE step = *(TPE*)dimension->step; \
	TPE *vals; \
\
	unsigned int i=0, elsRi=0, grpRi = 0; \
\
	if(!(resBAT = BATnew(TYPE_void, TYPE_##TPE, resSize, TRANSIENT))) \
    	    throw(MAL, "algebra.leftfetchjoin", "Problem allocating new BAT"); \
	vals = (TPE*)Tloc(resBAT, BUNfirst(resBAT)); \
\
	if(dimCand->idxs) { \
		/* iterate over the idxs and use each one as many times as defined by elsR */ \
		/* repeat the procedure as many times as defined my grpR */ \
		for(grpRi=0; grpRi<grpR; grpRi++)\
			for(i=0; i<dimCand->elsNum; i++) \
				for(elsRi=0; elsRi<elsR; elsRi++) \
					*vals++ = min + dimCand->idxs[i]*step; \
	} else { \
		/* get the elements in the range and use each one as many times as defined by elsR */ \
		/* repeat the procedure as many times as defined my grpR */ \
		for(grpRi=0; grpRi<grpR; grpRi++)\
			for(i=dimCand->min; i<=dimCand->max; i+=dimCand->step)\
				for(elsRi=0; elsRi<elsR; elsRi++)\
					*vals++ = min + i*step; \
	}\
} while(0)


		/*for each oid in the candsBAT find the real value of the dimension */
 		switch(dimension->type) { \
    	    case TYPE_bte: \
				computeValues(bte);
    	        break;
    	    case TYPE_sht: 
   				computeValues(sht);
    	    	break;
    	    case TYPE_int: {
				int min = *(int*)dimension->min;
				int step = *(int*)dimension->step;
				int *vals;
				unsigned int i=0, elsRi=0, grpRi = 0, idx =0, initialisedIdx=0;

				if(!(resBAT = BATnew(TYPE_void, TYPE_int, resSize, TRANSIENT))) 
    	    		throw(MAL, "algebra.leftfetchjoin", "Problem allocating new BAT"); 
				vals = (int*)Tloc(resBAT, BUNfirst(resBAT));

				if(dimCand->idxs) { 
					/* iterate over the idxs and use each one as many times as defined by elsR */ 
					/* repeat the procedure as many times as defined my grpR */ 
					for(grpRi=0; grpRi<grpR; grpRi++) {
						for(i=0; i<dimCand->elsNum; i++) { 
							int val = min + dimCand->idxs[i]*step; //the value at this position
							for(elsRi=0; elsRi<elsR; elsRi++) { 
								*vals++ = val;
							}
						}
					} 
				} else { 
					/* get the elements in the range and use each one as many times as defined by elsR */ 
					/* repeat the procedure as many times as defined my grpR */ 
		
					/*create the elements for the first group*/
					for(i=dimCand->min; i<=dimCand->max; i+=dimCand->step) {
						int val = min + i*step;
						for(elsRi=0; elsRi<elsR; elsRi++, idx++) {
							vals[idx] = val; 
						}
					}
					/* repeat te elements created during the first group to fill the rests of the groups
 					* (same elemnts no need to re-compute them */ 
					for(grpRi=1; grpRi<grpR; grpRi++) {
						initialisedIdx =0; //for each group we repeat the same values
						for(i=dimCand->min; i<=dimCand->max; i+=dimCand->step) {
							for(elsRi=0; elsRi<elsR; elsRi++, idx++, initialisedIdx++) {
								vals[idx] = vals[initialisedIdx];
							}
						}
					}
				}
   				//computeValues(int);
    	    	} break;
    	    case TYPE_wrd:
   				computeValues(wrd);
    	    	break;
    	    case TYPE_oid:
   				computeValues(oid);
    	    	break;
    	    case TYPE_lng:
   				computeValues(lng);
    		    break;
    	    case TYPE_dbl:
   				computeValues(dbl);
		        break;
    	    case TYPE_flt:
   				computeValues(flt);
   		    	break;
			default:
				throw(MAL, "algebra.dimensionLeftfetchjoin", "Dimension type not supported\n");
	    }
	}

	BATsetcount(resBAT, resSize);
	BATseqbase(resBAT, 0);
    BATderiveProps(resBAT, FALSE);    

	*result = resBAT->batCacheid;
    BBPkeepref(*result);

    return MAL_SUCCEED;

}

str ALGdimensionLeftfetchjoin2(bat *result, const bat *oidsCands, const ptr *dimsCands, const ptr *dim, const ptr *dims) {
	(void)*oidsCands; //We do not use this for dimensions
	return ALGdimensionLeftfetchjoin1(result, dimsCands, dim, dims);
}

str ALGdimensionLeftfetchjoin3(bat *result, const ptr* dimsCands, const ptr *array_in) {
	gdk_array *array = (gdk_array*)*array_in;
	gdk_array *dims_in = (gdk_array*)*dimsCands;
	BAT *resBAT;
	oid *resOids;


	/*count the cells in the output*/
	BUN elsNum = arrayCellsNum(dims_in);
	/*create all oids*/
	if(!(resBAT = BATnew(TYPE_void, TYPE_oid, elsNum, TRANSIENT))) {
		arrayDelete(dims_in);
		throw(MAL, "algebra.dimensionLeftfetchjoin2","Problem allocating new BAT");
	}

	/* create the oids that should be updated based on the dimsCands */
	resOids = (oid*)Tloc(resBAT, BUNfirst(resBAT));
	/* fill the array with the elements in the first dimension */
	computeOids(&resOids, 0, array->dimsNum-1 , array, dims_in, arrayCellsNum(array));

	BATsetcount(resBAT, elsNum);
	BATseqbase(resBAT, 0);
	resBAT->tsorted = 1;
	resBAT->trevsorted = 0;
    resBAT->tkey = 1;
	resBAT->tdense = 0;

	BBPkeepref(*result = resBAT->batCacheid);
	
	return MAL_SUCCEED;
}


str ALGnonDimensionLeftfetchjoin1(bat* result, const bat *mbrOids, const bat *vals, const ptr *dims) {

	/* projecting a non-dimensional column does not differ from projecting any relational column */
	BAT *mbrCandsBAT = NULL, *valsBAT = NULL, *resBAT= NULL;
	(void)*dims;

	if ((valsBAT = BATdescriptor(*vals)) == NULL) {
        throw(MAL, "algebra.leftfetchjoin", RUNTIME_OBJECT_MISSING);
    }

	if ((mbrCandsBAT = BATdescriptor(*mbrOids)) == NULL) {
		BBPunfix(valsBAT->batCacheid);
        throw(MAL, "algebra.leftfetchjoin", RUNTIME_OBJECT_MISSING);
    }

	resBAT = BATproject(mbrCandsBAT, valsBAT);

    BBPunfix(mbrCandsBAT->batCacheid);
	BBPunfix(valsBAT->batCacheid);

    if (resBAT == NULL)
        throw(MAL, "algebra.leftfetchjoin", GDK_EXCEPTION);
    *result = resBAT->batCacheid;
    BBPkeepref(*result);

	return MAL_SUCCEED;
}

str ALGnonDimensionLeftfetchjoin2(bat* result, const ptr *array_in, const bat *vals, const ptr *dims) {
    BAT *materialisedBAT = NULL;
    BAT *nonDimensionalBAT = NULL;
    BUN totalCellsNum, neededCellsNum;

    gdk_array *array = (gdk_array*)*array_in;

    if ((nonDimensionalBAT = BATdescriptor(*vals)) == NULL) {
        throw(MAL, "algebra.leftfecthjoin", RUNTIME_OBJECT_MISSING);
    }
    (void)*dims; //ignore the dims is the same with the array

    totalCellsNum = arrayCellsNum(array);
    neededCellsNum = totalCellsNum - BATcount(nonDimensionalBAT);

    /*TODO: fix this so that I can have the real default value of the column */
    materialisedBAT = materialise_nonDimensional_column(ATOMtype(BATttype(nonDimensionalBAT)), neededCellsNum, NULL);
    if(!materialisedBAT) {
        BBPunfix(nonDimensionalBAT->batCacheid);
        throw(MAL, "algebra.leftfetchjoin", "Problem materialising non-dimensional column");
    }

    /*append the missing values to the BAT */
    BATappend(nonDimensionalBAT, materialisedBAT, TRUE);
    BATsetcount(nonDimensionalBAT, totalCellsNum);

    BBPunfix(materialisedBAT->batCacheid);
    BBPkeepref(*result = nonDimensionalBAT->batCacheid);

    //sent this to the output so that we do not lose it afterwards     
 //   *dimsRes = array;
    return MAL_SUCCEED;
}

#if 0
str ALGmbrproject(bat *result, const bat *bid, const bat *sid, const bat* rid) {
    BAT *b, *s, *r, *bn;

    if ((b = BATdescriptor(*bid)) == NULL) {
        throw(MAL, "algebra.mbrproject", RUNTIME_OBJECT_MISSING);
    }
    if ((s = BATdescriptor(*sid)) == NULL) {
        BBPunfix(b->batCacheid);
        throw(MAL, "algebra.mbrproject", RUNTIME_OBJECT_MISSING);
    }
    if ((r = BATdescriptor(*rid)) == NULL) {
        BBPunfix(b->batCacheid);
        BBPunfix(s->batCacheid);
        throw(MAL, "algebra.mbrproject", RUNTIME_OBJECT_MISSING);
    }
    if(BATmbrproject(&bn, b, s, r) != GDK_SUCCEED)
        bn = NULL;
    BBPunfix(b->batCacheid);
    BBPunfix(s->batCacheid);
    BBPunfix(r->batCacheid);

    if (bn == NULL)
        throw(MAL, "algebra.mbrproject", GDK_EXCEPTION);
    if (!(bn->batDirty&2)) BATsetaccess(bn, BAT_READ);
    *result = bn->batCacheid;
    BBPkeepref(bn->batCacheid);
    return MAL_SUCCEED;

}

str ALGmbrsubselect(bat *result, const ptr *dims, const ptr* dim, const bat *sid, const bat *cid) {
    BAT *b=NULL, *s = NULL, *c = NULL, *bn;

	(void)*dims;
	(void)*dim;

//    if ((b = BATdescriptor(*bid)) == NULL) {
//      throw(MAL, "algebra.mbrsubselect", RUNTIME_OBJECT_MISSING);
//    }
    if (sid && *sid != bat_nil && (s = BATdescriptor(*sid)) == NULL) {
        BBPunfix(b->batCacheid);
        throw(MAL, "algebra.mbrsubselect", RUNTIME_OBJECT_MISSING);
    }
    if (cid && *cid != bat_nil && (c = BATdescriptor(*cid)) == NULL) {
        BBPunfix(b->batCacheid);
        BBPunfix(s->batCacheid);
        throw(MAL, "algebra.mbrsubselect", RUNTIME_OBJECT_MISSING);
    }
    if(BATmbrsubselect(&bn, b, s, c) != GDK_SUCCEED)
        bn = NULL;
    BBPunfix(b->batCacheid);
    BBPunfix(s->batCacheid);
    if (c)
        BBPunfix(c->batCacheid);
    if (bn == NULL)
        throw(MAL, "algebra.mbrsubselect", GDK_EXCEPTION);
    if (!(bn->batDirty&2)) BATsetaccess(bn, BAT_READ);
    *result = bn->batCacheid;
    BBPkeepref(bn->batCacheid);
    return MAL_SUCCEED;
}

str ALGmbrsubselect2(bat *result, const ptr *dims, const ptr* dim, const bat *sid) {
    return ALGmbrsubselect(result, dims, dim, sid, NULL);
}

str ALGproject(bat *result, const ptr* candDims, const bat* candBAT) {	
	gdk_cells *candidatesDimensions = (gdk_cells*)*candDims;
	BAT *candidatesBAT, *resBAT;

	if(!candidatesDimensions) { //empty result
		//create an empy candidates BAT
		 if((resBAT = BATnew(TYPE_void, TYPE_oid, 0, TRANSIENT)) == NULL)
            throw(MAL, "algebra.cellsProject", GDK_EXCEPTION);
		BATsetcount(resBAT, 0);
		BATseqbase(resBAT, 0);
		BATderiveProps(resBAT, FALSE);

    	BBPkeepref((*result= resBAT->batCacheid));

		return MAL_SUCCEED;
	}

	if ((candidatesBAT = BATdescriptor(*candBAT)) == NULL) {
       	throw(MAL, "algebra.cellsProject", RUNTIME_OBJECT_MISSING);
    }

	resBAT = projectCells(candidatesDimensions, candidatesBAT);
	if(!resBAT)
		throw(MAL, "algebra.cellsProject", "Problem allocating new array");
	*result = resBAT->batCacheid;
    BBPkeepref(*result);

	//clean the candidates
	BBPunfix(candidatesBAT->batCacheid);
	freeCells(candidatesDimensions);

	
	return MAL_SUCCEED;
}

#endif

#if 0
str ALGnonDimensionSubselect2(ptr *dimsRes, bat *oidsRes, const bat* values, const ptr *dims, const ptr *dimsCands, const bat* oidsCands,
                            const void *low, const void *high, const bit *li, const bit *hi, const bit *anti) {
	gdk_array *array = (gdk_array*)*dims;
	gdk_array *mbrOut = NULL;
	BAT *valuesBAT = NULL, *mbrCandsBAT = NULL, *oidsCandsBAT = NULL, *oidsResBAT;

	if(dimsCands) { 
		gdk_array *mbrIn = (gdk_array*)*dimsCands;
		mbrCandsBAT = projectCells(mbrIn, array);	
	}		

	mbrOut = arrayCopy(array);

	if ((valuesBAT = BATdescriptor(*values)) == NULL) {
       	throw(MAL, "algebra.nonDimensionSubselect", RUNTIME_OBJECT_MISSING);
    }

	if (oidsCands && (oidsCandsBAT = BATdescriptor(*oidsCands)) == NULL) {
		BBPunfix(valuesBAT->batCacheid);
       	throw(MAL, "algebra.nonDimensionSubselect", RUNTIME_OBJECT_MISSING);
    }

	if(!oidsCands || !BATcount(oidsCandsBAT))
		oidsCandsBAT = NULL;	

	/* merge the oids in the oidsCands and the mbr */
	if(!oidsCandsBAT && mbrCandsBAT) {
		oidsCandsBAT = mbrCandsBAT;
	} else if(oidsCandsBAT && mbrCandsBAT) {	
		BAT *r1p, *r2p;
		if(BATsubjoin(&r1p, &r2p, oidsCandsBAT, mbrCandsBAT, NULL, NULL, 0, BUN_NONE) != GDK_SUCCEED) {
			BBPunfix(valuesBAT->batCacheid);
			BBPunfix(oidsCandsBAT->batCacheid);
			BBPunfix(mbrCandsBAT->batCacheid);

			throw(MAL, "algebra.nonDimensionSubselect2", "Error in BATsubjoin");
		}

		BBPunfix(r1p->batCacheid);
		BBPunfix(oidsCandsBAT->batCacheid);
		BBPunfix(mbrCandsBAT->batCacheid);

		oidsCandsBAT = r2p;
	}

	/*find the values that satisfy the predicate*/
	if(!(oidsResBAT = BATsubselect(valuesBAT, oidsCandsBAT, low, high, *li, *hi, *anti))) {
		BBPunfix(valuesBAT->batCacheid);
		if(oidsCandsBAT)
			BBPunfix(oidsCandsBAT->batCacheid);

		arrayDelete(array);
		*dimsRes = NULL;		

		throw(MAL, "algebra.nonDimensionSubselect", "Problen in BATsubselect");
	}

	if(BATcount(oidsResBAT) == 0) { //empty results
		mbrOut = NULL;
	} else {
		//find the mbr around the qualifying oids
		BATiter oidsIter = bat_iterator(oidsResBAT);
		BUN currBun = BUNfirst(oidsResBAT);
		BUN lastBun = BUNlast(oidsResBAT);
		oid currentOid;
		unsigned short d;
		unsigned int dimIdx;
		BUN totalElsNum = 1, elsNum;

		totalElsNum = arrayCellsNum(array);
	
		//initialise the mbr
		elsNum = totalElsNum;
		currentOid = *(oid*)BUNtail(oidsIter, currBun);

		for(d = array->dimsNum-1; d>0; d--) {
			//get the correct elsNum
			elsNum /= array->dims[d]->elsNum;
			//get the idx for the dimension
			dimIdx = currentOid/elsNum;
			//update the dimension
			mbrOut->dims[d]->min = dimIdx;
			mbrOut->dims[d]->max = dimIdx;
			//update the oid
			currentOid %= elsNum;
		}
		//update the last dimension
		mbrOut->dims[d]->min = currentOid;
		mbrOut->dims[d]->max = currentOid;

		/* continue with the rest of the dimensions */
		for(currBun++; currBun < lastBun; currBun++) {
			elsNum = totalElsNum;
			currentOid = *(oid*)BUNtail(oidsIter, currBun);

			for(d = array->dimsNum-1; d>0; d--) {
				//get the correct elsNum
				elsNum /= array->dims[d]->elsNum;
				//get the idx for the dimension
				dimIdx = currentOid/elsNum;
				//update the dimension
				mbrOut->dims[d]->min = dimIdx < mbrOut->dims[d]->min ? dimIdx : mbrOut->dims[d]->min;
				mbrOut->dims[d]->max = dimIdx > mbrOut->dims[d]->max ? dimIdx : mbrOut->dims[d]->max;
				//update the oid
				currentOid %= elsNum;
			}
			//update the last dimension
			mbrOut->dims[d]->min = currentOid < mbrOut->dims[d]->min ? currentOid : mbrOut->dims[d]->min;
			mbrOut->dims[d]->max = currentOid > mbrOut->dims[d]->max ? currentOid : mbrOut->dims[d]->max;
		}

		for(d=0; d<mbrOut->dimsNum; d++)
			mbrOut->dims[d]->elsNum = floor((mbrOut->dims[d]->max-mbrOut->dims[d]->min)/mbrOut->dims[d]->step) + 1;

	}

	BBPunfix(valuesBAT->batCacheid);
	if(oidsCandsBAT)
		BBPunfix(oidsCandsBAT->batCacheid);
	BBPkeepref((*oidsRes = oidsResBAT->batCacheid));

	*dimsRes = mbrOut;

	return MAL_SUCCEED;
}
#endif

str ALGnonDimensionSubselect2(bat *oidsRes, const bat* values, const ptr *dims, const bat* oidsCands,
                            const void *low, const void *high, const bit *li, const bit *hi, const bit *anti) {
	(void)*dims;

	return ALGsubselect2(oidsRes, values, oidsCands, low, high, li, hi, anti);
}

str ALGnonDimensionSubselect1(bat *oidsRes, const bat *values, const ptr *dims,
                            const void *low, const void *high, const bit *li, const bit *hi, const bit *anti) {
	return ALGnonDimensionSubselect2(oidsRes, values, dims, NULL, low, high, li, hi, anti);
}

str ALGnonDimensionSubselect4(bat *oidsRes, ptr *dimsRes, const bat* values, const ptr *dims, const bat* oidsCands, const ptr *dimsCands,
                            const void *low, const void *high, const bit *li, const bit *hi, const bit *anti) {
	(void)*dims;

	/*normal subselect*/
	*dimsRes = *dimsCands; //the mbr is unaffected at this point

	return ALGsubselect2(oidsRes, values, oidsCands, low, high, li, hi, anti);
}

str ALGnonDimensionSubselect3(bat *oidsRes, ptr *dimsRes, const bat *values, const ptr *dims, const ptr* dimsCands,
                            const void *low, const void *high, const bit *li, const bit *hi, const bit *anti) {
	return ALGnonDimensionSubselect4(oidsRes, dimsRes, values, dims, NULL, dimsCands, low, high, li, hi, anti);
}

str ALGnonDimensionThetasubselect2(bat *oidsRes, const bat *values, const ptr* dims, const bat *oidsCands, const void *val, const char **op) {
	(void)*dims;
	return ALGthetasubselect2(oidsRes, values, oidsCands, val, op);
}

str ALGnonDimensionThetasubselect1(bat *oidsRes, const bat *values, const ptr* dims, const void *val, const char **op) {
	return ALGnonDimensionThetasubselect2(oidsRes, values, dims, NULL, val, op);
}

str ALGnonDimensionThetasubselect4(bat *oidsRes, ptr *dimsRes, const bat *values, const ptr* dims, const bat *oidsCands, const ptr *dimsCands, const void *val, const char **op) {
	(void)*dims;

	*dimsRes = *dimsCands;

	return ALGthetasubselect2(oidsRes, values, oidsCands, val, op);
}

str ALGnonDimensionThetasubselect3(bat *oidsRes, ptr *dimsRes, const bat *values, const ptr* dims, const ptr *dimsCands, const void *val, const char **op) {
	return ALGnonDimensionThetasubselect4(oidsRes, dimsRes, values, dims, NULL, dimsCands, val, op);
}


#if 0
str ALGnonDimensionThetasubselect4(ptr *dimsRes, bat *oidsRes, const bat *values, const ptr* dims, const bat *oidsCands, const ptr *dimsCands, const void *val, const char **opp) {
	bit li = 0;
	bit hi = 0;
	bit anti = 0;
	const char *op = *opp;
	BAT *valuesBAT =  BATdescriptor(*values);
	const void *nil;

	if(!valuesBAT) {
		throw(MAL, "algebra.nonDimensionThetasubselect", RUNTIME_OBJECT_MISSING);
	}

	nil = ATOMnilptr(BATttype(valuesBAT));
	BBPunfix(valuesBAT->batCacheid);

	if (op[0] == '=' && ((op[1] == '=' && op[2] == 0) || op[2] == 0)) {
        /* "=" or "==" */
		li = hi = 1;
		anti = 0;
        return ALGnonDimensionSubselect4(oidsRes, dimsRes, values, dims, oidsCands, dimsCands, val, nil, &li, &hi, &anti);
    }
    if (op[0] == '!' && op[1] == '=' && op[2] == 0) {
        /* "!=" (equivalent to "<>") */ 
		li = hi = anti = 1;
        return ALGnonDimensionSubselect4(oidsRes, dimsRes, values, dims, oidsCands, dimsCands, val, nil, &li, &hi, &anti);
    }
    if (op[0] == '<') { 
        if (op[1] == 0) {
            /* "<" */
			li = hi = anti = 0;
            return ALGnonDimensionSubselect4(oidsRes, dimsRes, values, dims, oidsCands, dimsCands, nil, val, &li, &hi, &anti);
        }
        if (op[1] == '=' && op[2] == 0) {
            /* "<=" */
			li = anti = 0;
			hi = 1;
            return ALGnonDimensionSubselect4(oidsRes, dimsRes, values, dims, oidsCands, dimsCands, nil, val, &li, &hi, &anti);
        }
        if (op[1] == '>' && op[2] == 0) {
            /* "<>" (equivalent to "!=") */ 
			li = hi = anti = 1;
            return ALGnonDimensionSubselect4(oidsRes, dimsRes, values, dims, oidsCands, dimsCands, val, nil, &li, &hi, &anti);
        }
    }
    if (op[0] == '>') { 
        if (op[1] == 0) {
            /* ">" */
			li = hi = anti = 0;
            return ALGnonDimensionSubselect4(oidsRes, dimsRes, values, dims, oidsCands, dimsCands, val, nil, &li, &hi, &anti);
        }
        if (op[1] == '=' && op[2] == 0) {
            /* ">=" */
			li = 1;
			hi = anti = 0;
            return ALGnonDimensionSubselect4(oidsRes, dimsRes, values, dims, oidsCands, dimsCands, val, nil, &li, &hi, &anti);
        }
    }

    throw(MAL, "algebra.dimensionThetasubselect", "BATdimensionThetasubselect: unknown operator.\n");
}

str ALGnonDimensionThetasubselect1(bat *oidsRes, ptr *dimsRes, const bat *values, const ptr* dims, const void *val, const char **op) {
	return ALGnonDimensionThetasubselect4(dimsRes, oidsRes, values, dims, NULL, NULL, val, op);
}

str ALGnonDimensionThetasubselect2(bat *oidsRes, ptr *dimsRes, const bat *values, const ptr* dims, const ptr *dimsCands, const void *val, const char **op) {
	return ALGnonDimensionThetasubselect4(dimsRes, oidsRes, values, dims, NULL, dimsCands, val, op);
}

str ALGnonDimensionThetasubselect3(bat *oidsRes, ptr *dimsRes, const bat *values, const ptr* dims, const bat* oidsCands, const void *val, const char **op) {
	return ALGnonDimensionThetasubselect4(dimsRes, oidsRes, values, dims, oidsCands, NULL, val, op);
}
#endif

str ALGarrayCount(wrd *res, const ptr *array) {
	gdk_array *ar = (gdk_array*)*array;
	*res = arrayCellsNum(ar);

	return MAL_SUCCEED;
}


str ALGprojectDimension(bat* result, const ptr *dim, const ptr *array) {
	const gdk_array *dimsCands = arrayCopy((gdk_array*)*array); //candidates exactly the same to the array
	return ALGdimensionLeftfetchjoin1(result, (void*)&dimsCands, dim, array);
}

str ALGprojectNonDimension(bat *result, const bat *vals, const ptr *array) {
 	/* just send the input vals to the output */
    /* make a copy of vals */
    BAT *resBAT, *inputBAT = BATdescriptor(*vals);
    if(!inputBAT)
        throw(MAL, "algebra.projectArray", RUNTIME_OBJECT_MISSING);
 
    resBAT = BATcopy(inputBAT, TYPE_void, BATttype(inputBAT), FALSE, TRANSIENT);

    (void)*array;

	BBPunfix(inputBAT->batCacheid);
	BBPkeepref(*result = resBAT->batCacheid);
	return MAL_SUCCEED;    

#if 0
    const gdk_array *dimsCands = arrayCopy((gdk_array*)*array); //candidates exactly the same to the array

    //empty cands so that it will project all cells 
    BAT *oidsCandsBAT = newempty("ALGprojectNonDimension");
    bat oidsCands = oidsCandsBAT->batCacheid;
    
    return ALGnonDimensionLeftfetchjoin1(result, (void*)&dimsCands, &oidsCands, vals, array);
#endif
    
}

/* returns the oids that correspond to the MBR */
str ALGmbr1(ptr *mbrRes, const bat *oidsCands, const ptr *dimsCands, const ptr *dims) {
	gdk_array *array = (gdk_array*)*dims;
	gdk_array *mbr = arrayCopy(array);
	BAT *oidsCandsBAT = NULL;

	if (oidsCands && (oidsCandsBAT = BATdescriptor(*oidsCands)) == NULL) {
       	throw(MAL, "algebra.mbr", RUNTIME_OBJECT_MISSING);
    }

	if(dimsCands) { //if there are dimsCands then the MBR is that 
		mbr = (gdk_array*)*dimsCands;
	} else if(oidsCands) {
		/*if the user did not restrict the dimensions then mbr is the one covering 
 		* all qualifying values. Should it be the whole array? What if the user does not 
 		* know exactly what area she is looking for and wants to find that using the values?*/

		//find the mbr around the qualifying oids
		BATiter oidsIter = bat_iterator(oidsCandsBAT);
		BUN currBun = BUNfirst(oidsCandsBAT);
		BUN lastBun = BUNlast(oidsCandsBAT);
		oid currentOid;
		unsigned short d;
		unsigned int dimIdx;
		BUN totalElsNum = 1, elsNum;

		totalElsNum = arrayCellsNum(array);
	
		//initialise the mbr
		elsNum = totalElsNum;
		currentOid = *(oid*)BUNtail(oidsIter, currBun);

		for(d = array->dimsNum-1; d>0; d--) {
			//get the correct elsNum
			elsNum /= array->dims[d]->elsNum;
			//get the idx for the dimension
			dimIdx = currentOid/elsNum;
			//update the dimension
			mbr->dims[d]->min = dimIdx;
			mbr->dims[d]->max = dimIdx;
			//update the oid
			currentOid %= elsNum;
		}
		//update the last dimension
		mbr->dims[d]->min = currentOid;
		mbr->dims[d]->max = currentOid;

		/* continue with the rest of the dimensions */
		for(currBun++; currBun < lastBun; currBun++) {
			elsNum = totalElsNum;
			currentOid = *(oid*)BUNtail(oidsIter, currBun);

			for(d = array->dimsNum-1; d>0; d--) {
				//get the correct elsNum
				elsNum /= array->dims[d]->elsNum;
				//get the idx for the dimension
				dimIdx = currentOid/elsNum;
				//update the dimension
				mbr->dims[d]->min = dimIdx < mbr->dims[d]->min ? dimIdx : mbr->dims[d]->min;
				mbr->dims[d]->max = dimIdx > mbr->dims[d]->max ? dimIdx : mbr->dims[d]->max;
				//update the oid
				currentOid %= elsNum;
			}
			//update the last dimension
			mbr->dims[d]->min = currentOid < mbr->dims[d]->min ? currentOid : mbr->dims[d]->min;
			mbr->dims[d]->max = currentOid > mbr->dims[d]->max ? currentOid : mbr->dims[d]->max;
		}

		for(d=0; d<mbr->dimsNum; d++)
			mbr->dims[d]->elsNum = floor((mbr->dims[d]->max-mbr->dims[d]->min)/mbr->dims[d]->step) + 1;
	}	

	*mbrRes = mbr;
	
	return MAL_SUCCEED;
}

str ALGmbr2(ptr *mbrRes, const bat *oidsCands, const ptr *dims) {
	return ALGmbr1(mbrRes, oidsCands, NULL, dims);
}

str ALGouterjoin(bat *res, const bat *l, const bat *r) {
	BAT *resBAT, *mbrCandsBAT, *oidsCandsBAT;
	BAT *r1p, *r2p;

	if ((mbrCandsBAT = BATdescriptor(*l)) == NULL) {
		throw(MAL, "algebra.outerjoin", RUNTIME_OBJECT_MISSING);
    }

	if ((oidsCandsBAT = BATdescriptor(*r)) == NULL) {
		BBPunfix(mbrCandsBAT->batCacheid);
		throw(MAL, "algebra.outerjoin", RUNTIME_OBJECT_MISSING);
    }


	//left outer join between the mbrCands and the oidsCands
    if(BATsubouterjoin(&r1p, &r2p, mbrCandsBAT, oidsCandsBAT, NULL, NULL, 0 /*null never match*/, BATcount(mbrCandsBAT) /*the size of the resuls. BUN_NONE will cause it to be computed*/) != GDK_SUCCEED) {
		BBPunfix(oidsCandsBAT->batCacheid);
        BBPunfix(mbrCandsBAT->batCacheid);
    }

	resBAT = BATproject(r2p, oidsCandsBAT);

	BBPunfix(r1p->batCacheid);
	BBPunfix(r2p->batCacheid);
	
	BBPunfix(mbrCandsBAT->batCacheid);
	BBPunfix(oidsCandsBAT->batCacheid); 

	BBPkeepref(*res = resBAT->batCacheid);
	return MAL_SUCCEED; 
}

str ALGsubjoin1(ptr *dimsResL, ptr *dimsResR, const ptr *dimensionL, const ptr *dimsL, const ptr *dimensionR, const ptr *dimsR) {
	gdk_analytic_dimension *analytic_dimensionL = (gdk_analytic_dimension*)*dimensionL;	
	gdk_analytic_dimension *analytic_dimensionR = (gdk_analytic_dimension*)*dimensionR;
	gdk_dimension *dimL, *dimR;

	*dimsResL = *dimsL;
	*dimsResR = *dimsR;

	dimL = ((gdk_array*)*dimsResL)->dims[analytic_dimensionL->dimNum];	
	dimR = ((gdk_array*)*dimsResR)->dims[analytic_dimensionR->dimNum];	

#define compareRanges(TPE) \
do { \
	TPE minL = *(TPE*)analytic_dimensionL->min; \
	TPE maxL = *(TPE*)analytic_dimensionL->max; \
	TPE stepL = *(TPE*)analytic_dimensionL->step; \
	TPE minR = *(TPE*)analytic_dimensionR->min; \
	TPE maxR = *(TPE*)analytic_dimensionR->max; \
	TPE stepR = *(TPE*)analytic_dimensionR->step; \
\
	TPE d = gcd_##TPE(stepR, stepL); \
	if(fmod((minL-minR),d)) {\
		dimR->min = dimL->min = dimR->max = dimL->max = 0; \
		dimR->step = dimL->step = 1; \
	} else { \
		/*find the min, max and step of the dimensions*/ \
		TPE l = minL, r=minR; \
		dimR->step = stepL/d; \
		dimL->step = stepR/d;\
		while(l != r) { \
			while(l<r) \
				l+=stepL; \
			while(r<l) \
				r+=stepR; \
		} \
		/*l = r*/ \
		dimL->min = (l-minL)/stepL; \
		dimR->min = (r-minR)/stepR; \
\
		l = maxL, r=maxR; \
		while(l != r) { \
			while(l>r) \
				l-=stepL; \
			while(r>l) \
				r-=stepR; \
		} \
		/*l = r*/ \
		dimL->max = (l-minL)/stepL; \
		dimR->max = (r-minR)/stepR; \
	} \
} while(0)

	switch(analytic_dimensionL->type) {
   	    case TYPE_bte:
   	        compareRanges(bte);
			break;
   	    case TYPE_sht: 
   	    	compareRanges(sht);
			break;
   	    case TYPE_int:
   	    	compareRanges(int);
			break;
   	    case TYPE_wrd:
   	    	compareRanges(wrd);
			break;
   	    case TYPE_oid:
   	    	compareRanges(oid);
			break;
   	    case TYPE_lng:
   		    compareRanges(lng);
			break;
   	    case TYPE_dbl:
	        compareRanges(dbl);
			break;
   	    case TYPE_flt:
	    	compareRanges(flt);
			break;
		default:
			throw(MAL, "algebra.subjoin", "Dimension type not supported\n");
    }

	dimL->elsNum = (dimL->max-dimL->min)/dimL->step+1;
	dimL->idxs = NULL;
	dimR->elsNum = (dimR->max-dimR->min)/dimR->step+1;
	dimR->idxs = NULL;

	return MAL_SUCCEED;
}

str ALGsubjoin2(ptr *dimsResL, ptr *dimsResR, const ptr *dimsCandsL, const ptr *dimL, const ptr *dimsL, const ptr *dimsCandsR, const ptr *dimR, const ptr *dimsR) {
	(void)*dimsL;
	(void)*dimsR;

	return ALGsubjoin1(dimsResL, dimsResR, dimL, dimsCandsL, dimR, dimsCandsR);
}


str ALGnonDimensionQRDecomposition(bat *oidsRes, ptr *dimsRes,  const bat* vals, const ptr *dims)
{
    gdk_array *array = (gdk_array*)*dims;
	gdk_array *array_out = arrayCopy(array); //TODO: Remove this. No need to return array
	double *qarray, *rarray, *els;
	unsigned int rowsNum, colsNum, rowNum, colNum, cellsNum;

	BAT *b;
    dbl *elements = NULL, *new_elements = NULL;

	rowsNum = array->dims[0]->elsNum;
	colsNum = array->dims[1]->elsNum;
	cellsNum = rowsNum*colsNum;

	qarray = (dbl*)GDKmalloc(rowsNum*colsNum*sizeof(double));
	rarray = (dbl*)GDKmalloc(colsNum*colsNum*sizeof(double));
	els = (dbl*)GDKmalloc(rowsNum*colsNum*sizeof(double));

	/* copy the elements in column major to fit the access pattern */
	elements = (dbl*) Tloc(BATdescriptor(*vals), BUNfirst(BATdescriptor(*vals)));
	for(rowNum=0; rowNum<rowsNum; rowNum++) {
		unsigned int skip = rowNum*colsNum;
		for(colNum=0; colNum<colsNum; colNum++) 
			*(els+colNum*rowsNum+rowNum) = elements[colNum+skip];
	}
 
	/* For each column */
	for(colNum =0 ; colNum<colsNum; colNum++) {
		double s=0.0;
		unsigned int colNum_tmp;

		unsigned int skip = colNum*rowsNum;
		unsigned int rSkip = colNum*colNum;

		/*get all rows*/
		for(rowNum=0; rowNum<rowsNum; rowNum++)
			s+=(*(els+skip+rowNum))*(*(els+skip+rowNum));

		*(rarray+rSkip+colNum) = sqrt(s);

		/* get all rows */
		for(rowNum=0; rowNum<rowsNum; rowNum++)
			*(qarray+skip+rowNum) = (*(els+skip+rowNum))/(*(rarray+rSkip+colNum));

		for(colNum_tmp=colNum+1; colNum_tmp<colsNum; colNum_tmp++) {
			unsigned int skip_tmp = colNum_tmp*rowsNum;
			s = 0.0;

			for(rowNum=0; rowNum<rowsNum; rowNum++)
				s+=(*(els+skip_tmp+rowNum))*(*(qarray+skip+rowNum));

			*(rarray+rSkip+colNum_tmp) = s;

			for(rowNum=0; rowNum<rowsNum; rowNum++)
				*(els+skip_tmp+rowNum) -= s*(*(qarray+skip+rowNum));
		}
	}


	if((b = BATnew(TYPE_void, TYPE_dbl, cellsNum, TRANSIENT)) == NULL)
        return createException(MAL, "arrays.QQR", "Problem creating BAT");
	new_elements = (dbl*) Tloc(b, BUNfirst(b));

	for(rowNum=0; rowNum<rowsNum; rowNum++) {
		for(colNum=0; colNum<colsNum; colNum++) {
			new_elements[rowNum+colNum*rowsNum] = *(qarray+colNum*rowsNum+rowNum);
		}
	}

    BATsetcount(b, cellsNum);
    b->tsorted = 0;
    b->trevsorted = b->batCount <= 1;
    b->tkey = 1;
    b->tdense = (b->batCount <= 1 || b->batCount == b->batCount);
    if (b->batCount == 1 || b->batCount == b->batCount)
        b->tseqbase = b->hseqbase;
    b->hsorted = 1;
    b->hdense = 1;
    b->hseqbase = 0;
    b->hkey = 1;
    b->hrevsorted = b->batCount <= 1;

    GDKfree(rarray);
    GDKfree(qarray);
	GDKfree(els);

    BBPkeepref(*oidsRes = b->batCacheid);

	*dimsRes = array_out;

    return MAL_SUCCEED;
}

