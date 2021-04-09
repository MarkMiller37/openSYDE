//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       Align undo command (implementation)

   Align undo command

   \copyright   Copyright 2016 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include "precomp_headers.h"

#include "C_SebUnoAlignCommand.h"
#include "C_GiUnique.h"
#include "C_SebUnoMoveCommand.h"
#include "C_SebScene.h"
#include "C_SebUtil.h"

/* -- Used Namespaces ----------------------------------------------------------------------------------------------- */
using namespace stw_types;
using namespace std;
using namespace stw_opensyde_gui;
using namespace stw_opensyde_gui_logic;

/* -- Module Global Constants --------------------------------------------------------------------------------------- */

/* -- Types --------------------------------------------------------------------------------------------------------- */

/* -- Global Variables ---------------------------------------------------------------------------------------------- */

/* -- Module Global Variables --------------------------------------------------------------------------------------- */

/* -- Module Global Function Prototypes ----------------------------------------------------------------------------- */

/* -- Implementation ------------------------------------------------------------------------------------------------ */

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Default constructor

   \param[in,out] opc_Scene             Pointer to currently active scene
   \param[in]     orc_IDs               Affected unique IDs
   \param[in]     oru64_GuidelineItemID ID of guideline item
   \param[in]     ore_Alignment         Alignment type
   \param[in,out] opc_Parent            Optional pointer to parent
*/
//----------------------------------------------------------------------------------------------------------------------
C_SebUnoAlignCommand::C_SebUnoAlignCommand(QGraphicsScene * const opc_Scene,
                                           const std::vector<stw_types::uint64> & orc_IDs,
                                           const uint64 & oru64_GuidelineItemID, const E_Alignment & ore_Alignment,
                                           QUndoCommand * const opc_Parent) :
   C_SebUnoBaseCommand(opc_Scene, orc_IDs, "Align drawing elements", opc_Parent)
{
   m_Align(oru64_GuidelineItemID, ore_Alignment);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Default destructor
*/
//----------------------------------------------------------------------------------------------------------------------
C_SebUnoAlignCommand::~C_SebUnoAlignCommand(void)
{
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Empty redo

   All actions are done using child commands
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SebUnoAlignCommand::redo(void)
{
   QUndoCommand::redo();
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Empty undo

   All actions are done using child commands
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SebUnoAlignCommand::undo(void)
{
   QUndoCommand::undo();
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Align selected items

   \param[in] oru64_GuidelineItemID ID of guideline item
   \param[in] ore_Alignment         Alignment type
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SebUnoAlignCommand::m_Align(const uint64 & oru64_GuidelineItemID, const E_Alignment & ore_Alignment)
{
   QGraphicsItem * const pc_GuidelineItem = this->m_GetSceneItem(oru64_GuidelineItemID);

   //Is there a guideline object
   if (pc_GuidelineItem != NULL)
   {
      vector<QGraphicsItem *> c_SelectedItems = this->m_GetSceneItems();
      //Are there more than one objects to align
      if (c_SelectedItems.size() > 1)
      {
         //Align objects
         QGraphicsItem * pc_CurItem;
         C_GiUnique * pc_UniqueItem;

         C_SebScene * const pc_Scene = dynamic_cast<C_SebScene * const>(this->mpc_Scene);
         QRectF c_CurRect;
         QPointF c_Difference;
         const QRectF c_GuidelineRect = pc_GuidelineItem->sceneBoundingRect();
         for (uint32 u32_ItItem = 0; u32_ItItem < c_SelectedItems.size(); ++u32_ItItem)
         {
            pc_CurItem = C_SebUtil::h_GetHighestParent(c_SelectedItems[u32_ItItem]);
            if (((pc_CurItem != pc_GuidelineItem) && (pc_Scene != NULL)) && (pc_CurItem != NULL))
            {
               if (pc_Scene->IsAlignmentUsable(pc_CurItem) == true)
               {
                  c_CurRect = pc_CurItem->sceneBoundingRect();
                  switch (ore_Alignment)
                  {
                  case eAL_LEFT:
                     c_Difference = c_GuidelineRect.topLeft() - c_CurRect.topLeft();
                     //No y change
                     c_Difference.setY(0.0);
                     break;
                  case eAL_HORIZONTAL_CENTER:
                     c_Difference = c_GuidelineRect.center() - c_CurRect.center();
                     //No y change
                     c_Difference.setY(0.0);
                     break;
                  case eAL_RIGHT:
                     c_Difference = c_GuidelineRect.topRight() - c_CurRect.topRight();
                     //No y change
                     c_Difference.setY(0.0);
                     break;
                  case eAL_TOP:
                     c_Difference = c_GuidelineRect.topLeft() - c_CurRect.topLeft();
                     //No x change
                     c_Difference.setX(0.0);
                     break;
                  case eAL_VERTICAL_CENTER:
                     c_Difference = c_GuidelineRect.center() - c_CurRect.center();
                     //No x change
                     c_Difference.setX(0.0);
                     break;
                  case eAL_BOTTOM:
                     c_Difference = c_GuidelineRect.bottomLeft() - c_CurRect.bottomLeft();
                     //No x change
                     c_Difference.setX(0.0);
                     break;
                  default:
                     //UNKNOWN
                     break;
                  }

                  pc_UniqueItem = dynamic_cast<C_GiUnique *>(pc_CurItem);
                  if (pc_UniqueItem != NULL)
                  {
                     const std::vector<uint64> c_Vec(1, pc_UniqueItem->GetID());
                     new C_SebUnoMoveCommand(this->mpc_Scene, c_Vec, c_Difference, this);
                  }
               }
            }
         }
      }
   }
}
