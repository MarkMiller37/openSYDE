//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       Custom group box (implementation)

   Custom group box.
   This class does not contain any functionality,
   but needs to exist, to have a unique group,
   to apply a specific stylesheet for.

   \copyright   Copyright 2016 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include "precomp_headers.hpp"

#include <QPainter>
#include "C_OgeGbx.hpp"

/* -- Used Namespaces ----------------------------------------------------------------------------------------------- */

using namespace stw::opensyde_gui_elements;

/* -- Module Global Constants --------------------------------------------------------------------------------------- */

/* -- Types --------------------------------------------------------------------------------------------------------- */

/* -- Global Variables ---------------------------------------------------------------------------------------------- */

/* -- Module Global Variables --------------------------------------------------------------------------------------- */

/* -- Module Global Function Prototypes ----------------------------------------------------------------------------- */

/* -- Implementation ------------------------------------------------------------------------------------------------ */

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Default constructor

   Set up GUI with all elements.

   \param[in,out] opc_Parent Optional pointer to parent
*/
//----------------------------------------------------------------------------------------------------------------------

C_OgeGbx::C_OgeGbx(QWidget * const opc_Parent) :
   QGroupBox(opc_Parent)
{
}
