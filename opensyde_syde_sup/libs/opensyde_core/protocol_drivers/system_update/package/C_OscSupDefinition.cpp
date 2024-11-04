//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       System update package definition

   System update package definition

   \copyright   Copyright 2024 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include "precomp_headers.hpp"

#include "stwtypes.hpp"
#include "C_OscSupDefinition.hpp"

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
C_OscSupDefinition::C_OscSupDefinition(void)
{
   this->Initialize();
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Initialize class content

   Clean up.
*/
//----------------------------------------------------------------------------------------------------------------------
void C_OscSupDefinition::Initialize(void)
{
   u32_ActiveBusIndex = 0UL;
}