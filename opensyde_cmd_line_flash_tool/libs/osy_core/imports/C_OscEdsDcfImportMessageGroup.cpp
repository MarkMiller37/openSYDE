//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       Data class to group multiple imported EDS/Dcf messages

   Data class to group multiple imported EDS/Dcf messages

   \copyright   Copyright 2023 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include "precomp_headers.hpp"

#include "C_OscEdsDcfImportMessageGroup.hpp"

/* -- Used Namespaces ----------------------------------------------------------------------------------------------- */
using namespace stw::opensyde_core;

/* -- Module Global Constants --------------------------------------------------------------------------------------- */

/* -- Types --------------------------------------------------------------------------------------------------------- */

/* -- Global Variables ---------------------------------------------------------------------------------------------- */

/* -- Module Global Variables --------------------------------------------------------------------------------------- */

/* -- Module Global Function Prototypes ----------------------------------------------------------------------------- */

/* -- Implementation ------------------------------------------------------------------------------------------------ */

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Default constructor
*/
//----------------------------------------------------------------------------------------------------------------------
C_OscEdsDcfImportMessageGroup::C_OscEdsDcfImportMessageGroup()
{
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Clear
*/
//----------------------------------------------------------------------------------------------------------------------
void C_OscEdsDcfImportMessageGroup::Clear()
{
   this->c_OscMessageData.clear();
   this->c_OscSignalData.clear();
   this->c_SignalDefaultMinMaxValuesUsed.clear();
}