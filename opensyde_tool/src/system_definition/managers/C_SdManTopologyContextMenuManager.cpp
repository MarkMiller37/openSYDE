//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       Context menu manager of system definition toplogy

   The context menu manager handles all request for context menus with its actions.

   \copyright   Copyright 2016 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include "precomp_headers.hpp"

#include <limits>
#include "C_SdManTopologyContextMenuManager.hpp"
#include "C_SebUtil.hpp"
#include "C_GiLiLineGroup.hpp"
#include "C_GtGetText.hpp"
#include "gitypes.hpp"

/* -- Used Namespaces ----------------------------------------------------------------------------------------------- */
using namespace stw::opensyde_gui;
using namespace stw::opensyde_gui_elements;
using namespace stw::opensyde_gui_logic;

/* -- Module Global Constants --------------------------------------------------------------------------------------- */

/* -- Types --------------------------------------------------------------------------------------------------------- */

/* -- Global Variables ---------------------------------------------------------------------------------------------- */

/* -- Module Global Variables --------------------------------------------------------------------------------------- */

/* -- Module Global Function Prototypes ----------------------------------------------------------------------------- */

/* -- Implementation ------------------------------------------------------------------------------------------------ */

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Default constructor
*/
//----------------------------------------------------------------------------------------------------------------------
C_SdManTopologyContextMenuManager::C_SdManTopologyContextMenuManager() :
   C_SebTopologyBaseContextMenuManager()
{
   //   // insert actions
   this->mpc_ActionEdit = this->mc_ContextMenu.addAction(C_GtGetText::h_GetText("Edit Properties"), this,
                                                         &C_SdManTopologyContextMenuManager::m_Edit);

   this->mpc_ActionEditSeparator = this->mc_ContextMenu.addSeparator();

   //move to right place
   this->mc_ContextMenu.insertAction(this->mpc_ActionCut, this->mpc_ActionEdit);
   this->mc_ContextMenu.insertAction(this->mpc_ActionCut, this->mpc_ActionEditSeparator);

   this->mpc_ActionInterfaceAssignment = this->mc_ContextMenu.addAction(C_GtGetText::h_GetText(
                                                                           "Interface Assignment"),
                                                                        this,
                                                                        &C_SdManTopologyContextMenuManager::m_InterfaceAssignment);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Default destructor

   Clean up.
*/
//----------------------------------------------------------------------------------------------------------------------
//lint -e{1540}  no memory leak because of the parent of all mpc_Action* and the Qt memory management
C_SdManTopologyContextMenuManager::~C_SdManTopologyContextMenuManager()
{
}

//----------------------------------------------------------------------------------------------------------------------
void C_SdManTopologyContextMenuManager::m_SetActionsInvisible(void)
{
   this->mpc_ActionEdit->setVisible(false);
   this->mpc_ActionInterfaceAssignment->setVisible(false);

   C_SebTopologyBaseContextMenuManager::m_SetActionsInvisible();
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Activate single item specific actions

   \return
   true     Specific action found
   false    No specific action found
*/
//----------------------------------------------------------------------------------------------------------------------
bool C_SdManTopologyContextMenuManager::m_ActivateSpecificActions(void)
{
   // specific functionality
   switch (this->mpc_ActiveItem->type())
   {
   case ms32_GRAPHICS_ITEM_NODE:
      this->mpc_ActionEdit->setText(C_GtGetText::h_GetText("Edit Node Properties"));
      this->mpc_ActionEdit->setVisible(true);
      this->mpc_ActionCut->setVisible(true);
      this->mpc_ActionCopy->setVisible(true);
      this->mpc_ActionDelete->setVisible(true);
      this->mpc_ActionBringToFront->setVisible(true);
      this->mpc_ActionSendToBack->setVisible(true);
      break;
   // check for bus
   case ms32_GRAPHICS_ITEM_BUS:         // buses have the same functionality
   case ms32_GRAPHICS_ITEM_CANBUS:      // buses have the same functionality
   case ms32_GRAPHICS_ITEM_ETHERNETBUS: // buses have the same functionality
      this->mpc_ActionCut->setVisible(true);
      this->mpc_ActionCopy->setVisible(true);
      this->mpc_ActionDelete->setVisible(true);
      this->mpc_ActionBringToFront->setVisible(true);
      this->mpc_ActionSendToBack->setVisible(true);
      this->mpc_ActionEdit->setText(C_GtGetText::h_GetText("Edit Bus Properties"));
      this->mpc_ActionEdit->setVisible(true);
      break;
   // check for bus connector
   case ms32_GRAPHICS_ITEM_BUS_CONNECT:
      this->mpc_ActionDelete->setVisible(true);
      this->mpc_ActionInterfaceAssignment->setVisible(true);
      break;
   case ms32_GRAPHICS_ITEM_LINE_ARROW:
      this->mpc_ActionCut->setVisible(true);
      this->mpc_ActionCopy->setVisible(true);
      this->mpc_ActionDelete->setVisible(true);
      this->mpc_ActionBringToFront->setVisible(true);
      this->mpc_ActionSendToBack->setVisible(true);
      break;
   case ms32_GRAPHICS_ITEM_BOUNDARY:    // boundary and text element have the same functionality
   case ms32_GRAPHICS_ITEM_TEXTELEMENT: // boundary and text element have the same functionality
      this->mpc_ActionCut->setVisible(true);
      this->mpc_ActionCopy->setVisible(true);
      this->mpc_ActionDelete->setVisible(true);
      this->mpc_ActionBringToFront->setVisible(true);
      this->mpc_ActionSendToBack->setVisible(true);
      break;
   case ms32_GRAPHICS_ITEM_TEXTELEMENT_BUS:
      this->mpc_ActionBringToFront->setVisible(true);
      this->mpc_ActionSendToBack->setVisible(true);
      this->mpc_ActionEdit->setText(C_GtGetText::h_GetText("Edit Bus Properties"));
      this->mpc_ActionEdit->setVisible(true);
      break;
   default:
      this->mpc_ActionCut->setVisible(true);
      this->mpc_ActionCopy->setVisible(true);
      this->mpc_ActionDelete->setVisible(true);
      this->mpc_ActionBringToFront->setVisible(true);
      this->mpc_ActionSendToBack->setVisible(true);
      break;
   }

   //Setup style
   if (m_ItemTypeHasSetupStyle(this->mpc_ActiveItem->type()))
   {
      this->mpc_ActionSetupStyle->setVisible(true);
   }

   C_SebTopologyBaseContextMenuManager::m_ActivateSpecificActions();

   return true;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Check if the input item type requires a setup style in the context menu

   \param[in] os32_ItemType Item type to check

   \retval   True    Setup style menu is required
   \retval   False   Setup style menu should stay hidden
*/
//----------------------------------------------------------------------------------------------------------------------
bool C_SdManTopologyContextMenuManager::m_ItemTypeHasSetupStyle(const int32_t os32_ItemType)
{
   bool q_Retval;

   switch (os32_ItemType)
   {
   case ms32_GRAPHICS_ITEM_BUS:
   case ms32_GRAPHICS_ITEM_CANBUS:
   case ms32_GRAPHICS_ITEM_ETHERNETBUS:
   case ms32_GRAPHICS_ITEM_LINE_ARROW:
   case ms32_GRAPHICS_ITEM_BOUNDARY:
   case ms32_GRAPHICS_ITEM_TEXTELEMENT:
   case ms32_GRAPHICS_ITEM_TEXTELEMENT_BUS:
      q_Retval = true;
      break;
   default:
      q_Retval = false;
      break;
   }
   return q_Retval;
}

//----------------------------------------------------------------------------------------------------------------------
void C_SdManTopologyContextMenuManager::m_Edit()
{
   Q_EMIT this->SigEdit(this->mpc_ActiveItem);
}

//----------------------------------------------------------------------------------------------------------------------
void C_SdManTopologyContextMenuManager::m_InterfaceAssignment()
{
   Q_EMIT this->SigInterfaceAssignment(this->mpc_ActiveItem);
}
