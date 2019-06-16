/**CFile****************************************************************

  FileName    [sclUtil.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Standard-cell library representation.]

  Synopsis    [Various utilities.]

  Author      [Alan Mishchenko, Niklas Een]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - August 24, 2012.]

  Revision    [$Id: sclUtil.c,v 1.0 2012/08/24 00:00:00 alanmi Exp $]

  Modified By [Soheil Hashemi, Marina Neseem]

  Affiliation [Brown University]

  Date        [Started February 2019.]


***********************************************************************/

#include "sclSize.h"
#include "map/mio/mio.h"
#include "base/main/main.h"
#include <sys/stat.h>
#include <tcl.h>

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Converts pNode->pData gates into array of SC_Lit gate IDs and back.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_SclMioGates2SclGates( SC_Lib * pLib, Abc_Ntk_t * p )
{
    Abc_Obj_t * pObj;
    int i, gateId, bufferId;
    // find buffer
    if ( Mio_LibraryReadBuf((Mio_Library_t *)p->pManFunc) == NULL )
    {
        printf( "Cannot find buffer in the current library. Quitting.\n" );
        return;
    }
    bufferId = Abc_SclCellFind( pLib, Mio_GateReadName(Mio_LibraryReadBuf((Mio_Library_t *)p->pManFunc)) );
    assert( bufferId >= 0 );
    // remap cells
    assert( p->vGates == NULL );
    p->vGates = Vec_IntStartFull( Abc_NtkObjNumMax(p) );
    Abc_NtkForEachNodeNotBarBuf1( p, pObj, i )
    {
        gateId = Abc_SclCellFind( pLib, Mio_GateReadName((Mio_Gate_t *)pObj->pData) );
        assert( gateId >= 0 );
        Vec_IntWriteEntry( p->vGates, i, gateId );
    }
    p->pSCLib = pLib;
}
void Abc_SclSclGates2MioGates( SC_Lib * pLib, Abc_Ntk_t * p )
{
    Abc_Obj_t * pObj;
    SC_Cell * pCell;
    int i, Counter = 0, CounterAll = 0;
    assert( p->vGates != NULL );
    Abc_NtkForEachNodeNotBarBuf1( p, pObj, i )
    {
        pCell = Abc_SclObjCell(pObj);
        assert( pCell->n_inputs == Abc_ObjFaninNum(pObj) );
        pObj->pData = Mio_LibraryReadGateByName( (Mio_Library_t *)p->pManFunc, pCell->pName, NULL );
        Counter += (pObj->pData == NULL);
        assert( pObj->fMarkA == 0 && pObj->fMarkB == 0 );
        CounterAll++;
    }
    if ( Counter )
        printf( "Could not find %d (out of %d) gates in the current library.\n", Counter, CounterAll );
    Vec_IntFreeP( &p->vGates );
    p->pSCLib = NULL;
}

/**Function*************************************************************

  Synopsis    [Transfer gate sizes from AIG without barbufs.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_SclTransferGates( Abc_Ntk_t * pOld, Abc_Ntk_t * pNew )
{
    Abc_Obj_t * pObj; int i;
    assert( pOld->nBarBufs2 > 0 );
    assert( pNew->nBarBufs2 == 0 );
    Abc_NtkForEachNodeNotBarBuf( pOld, pObj, i )
    {
        if ( pObj->pCopy == NULL )
            continue;
        assert( Abc_ObjNtk(pObj->pCopy) == pNew );
        pObj->pData = pObj->pCopy->pData;
    }
}

/**Function*************************************************************

  Synopsis    [Reports percentage of gates of each size.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
#define ABC_SCL_MAX_SIZE 64
void Abc_SclManPrintGateSizes( SC_Lib * pLib, Abc_Ntk_t * p, Vec_Int_t * vGates )
{
    Abc_Obj_t * pObj;
    SC_Cell * pCell;
    int i, nGates = 0, Counters[ABC_SCL_MAX_SIZE] = {0};
    double TotArea = 0, Areas[ABC_SCL_MAX_SIZE] = {0};
    Abc_NtkForEachNodeNotBarBuf1( p, pObj, i )
    {
        pCell = SC_LibCell( pLib, Vec_IntEntry(vGates, Abc_ObjId(pObj)) );
        assert( pCell->Order < ABC_SCL_MAX_SIZE );
        Counters[pCell->Order]++;
        Areas[pCell->Order] += pCell->area;
        TotArea += pCell->area;
        nGates++;
    }
    printf( "Total gates = %d.  Total area = %.1f\n", nGates, TotArea );
    for ( i = 0; i < ABC_SCL_MAX_SIZE; i++ )
    {
        if ( Counters[i] == 0 )
            continue;
        printf( "Cell size = %d.  ", i );
        printf( "Count = %6d  ",     Counters[i] );
        printf( "(%5.1f %%)   ",     100.0 * Counters[i] / nGates );
        printf( "Area = %12.1f  ",   Areas[i] );
        printf( "(%5.1f %%)  ",      100.0 * Areas[i] / TotArea );
        printf( "\n" );
    }
}
void Abc_SclPrintGateSizes( SC_Lib * pLib, Abc_Ntk_t * p )
{
    Abc_SclMioGates2SclGates( pLib, p );
    Abc_SclManPrintGateSizes( pLib, p, p->vGates );
    Abc_SclSclGates2MioGates( pLib, p );
    Vec_IntFreeP( &p->vGates );
    p->pSCLib = NULL;
}

/**Function*************************************************************

  Synopsis    [Downsizes each gate to its minimium size.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
SC_Cell * Abc_SclFindMaxAreaCell( SC_Cell * pRepr )
{
    SC_Cell * pCell, * pBest = pRepr;
    float AreaBest = pRepr->area;
    int i;
    SC_RingForEachCell( pRepr, pCell, i )
        if ( AreaBest < pCell->area )
        {
            AreaBest = pCell->area;
            pBest = pCell;
        }
    return pBest;
}
Vec_Int_t * Abc_SclFindMinAreas( SC_Lib * pLib, int fUseMax )
{
    Vec_Int_t * vMinCells;
    SC_Cell * pCell, * pRepr = NULL, * pBest = NULL;
    int i, k;
    // map each gate in the library into its min/max-size prototype
    vMinCells = Vec_IntStartFull( Vec_PtrSize(&pLib->vCells) );
    SC_LibForEachCellClass( pLib, pRepr, i )
    {
        pBest = fUseMax ? Abc_SclFindMaxAreaCell(pRepr) : pRepr;
        SC_RingForEachCell( pRepr, pCell, k )
            Vec_IntWriteEntry( vMinCells, pCell->Id, pBest->Id );
    }
    return vMinCells;
}
void Abc_SclMinsizePerform( SC_Lib * pLib, Abc_Ntk_t * p, int fUseMax, int fVerbose )
{
    Vec_Int_t * vMinCells;
    Abc_Obj_t * pObj;
    int i, gateId;
    vMinCells = Abc_SclFindMinAreas( pLib, fUseMax );
    Abc_SclMioGates2SclGates( pLib, p );
    Abc_NtkForEachNodeNotBarBuf1( p, pObj, i )
    {
        gateId = Vec_IntEntry( p->vGates, i );
        assert( gateId >= 0 && gateId < Vec_PtrSize(&pLib->vCells) );
        gateId = Vec_IntEntry( vMinCells, gateId );
        assert( gateId >= 0 && gateId < Vec_PtrSize(&pLib->vCells) );
        Vec_IntWriteEntry( p->vGates, i, gateId );
    }
    Abc_SclSclGates2MioGates( pLib, p );
    Vec_IntFree( vMinCells );
}
int Abc_SclCountMinSize( SC_Lib * pLib, Abc_Ntk_t * p, int fUseMax )
{
    Vec_Int_t * vMinCells;
    Abc_Obj_t * pObj;
    int i, gateId, Counter = 0;
    vMinCells = Abc_SclFindMinAreas( pLib, fUseMax );
    Abc_NtkForEachNodeNotBarBuf1( p, pObj, i )
    {
        gateId = Vec_IntEntry( p->vGates, i );
        Counter += ( gateId == Vec_IntEntry(vMinCells, gateId) );
    }
    Vec_IntFree( vMinCells );
    return Counter;
}

/**Function*************************************************************

  Synopsis    [Reads timing constraints using SDC parser from Synopsys.]

  Description []
               
  SideEffects []

  SeeAlso     []

 ***********************************************************************/
// Defining Callbacks...
int parsedCreateClockCallback(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]) {
    char* period = argv[1];
    char* portPin = argv[2];
    char* name = argv[3];

    // TODO: Currently only period is assigned... Missing support for the rest!
    Abc_FrameSetClockPeriod( atof(period) );

    return TCL_OK;
}

int parsedSetInputDelayCallback(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]) {
    char* portPin = argv[1];
    char* clk = argv[2];
    char* delayValue = argv[3];
    
    // TODO: Use these variables...
    return TCL_OK;
}

int parsedSetMaxFanoutCallback(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]) {
    char* maxFanoutValue = argv[1];
    char* ObjectList = argv[2];

    // TODO: OnjectList not Supported!
    Abc_FrameSetMaxFanout( atof(maxFanoutValue) );
    return TCL_OK;
}

int parsedSetMaxTransitionCallback(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]) {
    char* maxTransValue = argv[1];
    char* ObjectList = argv[2];
    
    // TODO: OnjectList not Supported!
    Abc_FrameSetMaxTrans( atof(maxTransValue) ); 
    return TCL_OK;
}

/* HELPER Template: define another callback function for another command we will support */
int parseCOMMAND_NAME_CALLBACK(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[]) {
    /* Don't change the order of the following argvs */
    char* period = argv[1];
    char* portPin = argv[2];
    char* name = argv[3];
    /* Don't change the order of the above argvs */

    // TODO: replace the below code with setting the actual constraints in abc
    // for example:
    // printf("Port Pin: %s\n", portPin);

    return TCL_OK;
}

// Defining the Abc_SclReadSDC function:
void Abc_SclReadSDC( Abc_Frame_t * pAbc, char * pFileName, int fVerbose )
{
    char parseCommand[100];
    strcpy(parseCommand, "sdc::parse_file ");

    strcat(parseCommand, pFileName);

    // initialize the tcl interpreter
    Tcl_Interp *interp;
    interp = Tcl_CreateInterp();
    if(interp == NULL){
        fprintf(stderr, "Error in Tcl_CreateInterp, aborting\n");
    }
    if(Tcl_Init(interp) == TCL_ERROR){
        fprintf(stderr, "Error in Tcl_Init: %s\n", Tcl_GetStringResult(interp));
    }
    // register the defined function with the tcl interpreter
    Tcl_CreateCommand(interp, "parsedSetInputDelayCallback", parsedSetInputDelayCallback, (ClientData) NULL, (ClientData) NULL);
    Tcl_CreateCommand(interp, "parsedCreateClockCallback", parsedCreateClockCallback, (ClientData) NULL, (ClientData) NULL);
    Tcl_CreateCommand(interp, "parsedSetMaxFanoutCallback", parsedSetMaxFanoutCallback, (ClientData) NULL, (ClientData) NULL);
    Tcl_CreateCommand(interp, "parsedSetMaxTransitionCallback", parsedSetMaxTransitionCallback, (ClientData) NULL, (ClientData) NULL);

    // TODO: register other defined functions here
    // example: Tcl_CreateCommand(interp, "parseCOMMAND_NAME_CALLBACK", parseCOMMAND_NAME_CALLBACK, (ClientData) NULL, (ClientData) NULL);

    // No need to change any of the below code
    //int code = Tcl_EvalFile(interp, "./brown_parser.tcl");
    
    //printf("%s\n", __FILE__);

    // FIXME: We are using reletive address to tcl file as of now... this needs fixing for systemwide use...
    int code = Tcl_EvalFile(interp, "src/map/scl/brown_parser.tcl");
    if(code == TCL_OK){
        Tcl_Eval(interp, parseCommand);
    } else
    {
        printf("SDC File Parsing Failed!\n");
    }
    //printf("SDC File Parsing Done!\n");

    //if ( fVerbose ){
        //printf( "Setting global clock period to be \"%f\".\n", Abc_FrameReadClockPeriod() );
    //}
}
/**Function*************************************************************

  Synopsis    [Reads timing constraints.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_SclReadTimingConstr( Abc_Frame_t * pAbc, char * pFileName, int fVerbose )
{
    char Buffer[1000], * pToken;
    FILE * pFile = fopen( pFileName, "rb" );
    while ( fgets( Buffer, 1000, pFile ) )
    {
        pToken = strtok( Buffer, " \t\r\n" );
        if ( pToken == NULL )
            continue;
        if ( !strcmp(pToken, "set_driving_cell") )
//        if ( !strcmp(pToken, "default_input_cell") )
        {
            Abc_FrameSetDrivingCell( Abc_UtilStrsav(strtok(NULL, " \t\r\n")) );
            if ( fVerbose ) 
                printf( "Setting driving cell to be \"%s\".\n", Abc_FrameReadDrivingCell() );
        }
        else if ( !strcmp(pToken, "set_load") )
//        else if ( !strcmp(pToken, "default_output_load") )
        {
            Abc_FrameSetMaxLoad( atof(strtok(NULL, " \t\r\n")) );
            if ( fVerbose ) 
                printf( "Setting output load to be %f.\n", Abc_FrameReadMaxLoad() );
        }
        else printf( "Unrecognized token \"%s\".\n", pToken );
    }
    fclose( pFile );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Int_t * Abc_SclExtractBarBufs( Abc_Ntk_t * pNtk )
{
    Vec_Int_t * vBufs;
    Mio_Gate_t * pBuffer;
    Abc_Obj_t * pObj; int i;
    pBuffer = Mio_LibraryReadBuf( (Mio_Library_t *)pNtk->pManFunc );
    if ( pBuffer == NULL )
    {
        printf( "Cannot find buffer in the current library. Quitting.\n" );
        return NULL;
    }
    vBufs = Vec_IntAlloc( 100 );
    Abc_NtkForEachBarBuf( pNtk, pObj, i )
    {
        assert( pObj->pData == NULL );
        pObj->pData = pBuffer;
        Vec_IntPush( vBufs, i );
    }
    return vBufs;
}
void Abc_SclInsertBarBufs( Abc_Ntk_t * pNtk, Vec_Int_t * vBufs )
{
    Abc_Obj_t * pObj; int i;
    Abc_NtkForEachObjVec( vBufs, pNtk, pObj, i )
        pObj->pData = NULL;
}
st__table * Abc_SclGetSpefNameCapTable(Abc_Ntk_t * pNtk, int capUnit)
{
    printf("Using Physical-aware timing analysis.\n");

    char pSpefFile[1000];
    sprintf(pSpefFile, "spef_output/netlist.spef");

    st__table * tNameToCap = 0;
    printf("-- Parsing SPEF.\n");
    tNameToCap = st__init_table(strcmp, st__strhash);
    if (Abc_SclParseSpef(pSpefFile, tNameToCap, capUnit) != 1){
        printf("Abc_SclParseSpef failed spectacularly!\n");
    }
    return tNameToCap;
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////

ABC_NAMESPACE_IMPL_END

