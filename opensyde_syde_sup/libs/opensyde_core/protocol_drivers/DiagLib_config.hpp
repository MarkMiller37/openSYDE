//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       DiagLib configuration.

   Contains potentially target specific adaptations for the DiagLib library

   \copyright   Copyright 2009 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------
#ifndef CDIAGLLIB_CONFIG_HPP
#define CDIAGLLIB_CONFIG_HPP

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include "C_SclDynamicArray.hpp"
#include "C_OscUtils.hpp"

/* -- Defines ------------------------------------------------------------------------------------------------------- */
//if the diaglib shall become part of a bigger library a keyword to export classes and functions might be required
#ifdef __BORLANDC__
//for VisLib: export from Borland C++ Builder packages
#define STW_DIAGLIB_EXPORT __declspec(package)
#else
//else leave this blank
#define STW_DIAGLIB_EXPORT
#endif

//Define which dynamicarray type to use for KEFEX variable lists.
//We use the Ansi-C++ DynamicArray
#define DIAGLIB_VAR_DYNAMIC_ARRAY stw_scl::C_SclDynamicArray

//disable logging functionality of KEFEX protocol driver
//so the dependency from the CCMONProtocol classes is resolved:
#define DIAGLIB_KEFEX_PROTOCOL_NO_LOGGING

//DiagLib needs TGL_LoadStr; we have our own implementation:
#define TGL_LoadStr stw::opensyde_core::C_OscUtils::h_LoadString

/* -- Types --------------------------------------------------------------------------------------------------------- */

/* -- Global Variables ---------------------------------------------------------------------------------------------- */

/* -- Function Prototypes ------------------------------------------------------------------------------------------- */

/* -- Implementation ------------------------------------------------------------------------------------------------ */

#endif
