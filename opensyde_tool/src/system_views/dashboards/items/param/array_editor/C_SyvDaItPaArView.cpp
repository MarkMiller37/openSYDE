//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       Data pool list array edit view (implementation)

   Data pool list array edit view

   \copyright   Copyright 2017 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include "precomp_headers.hpp"

#include <QScrollBar>
#include "C_Uti.hpp"
#include "stwtypes.hpp"
#include "C_GtGetText.hpp"
#include "C_SyvDaItPaArView.hpp"
#include "C_SdNdeSingleHeaderView.hpp"
#include "TglUtils.hpp"

/* -- Used Namespaces ----------------------------------------------------------------------------------------------- */
using namespace stw::opensyde_gui;
using namespace stw::opensyde_gui_logic;

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
C_SyvDaItPaArView::C_SyvDaItPaArView(QWidget * const opc_Parent) :
   C_TblViewScroll(opc_Parent),
   mc_Model(),
   mc_Delegate(),
   mpc_LabelCorner(NULL)
{
   //UI Settings
   this->setSortingEnabled(false);
   this->setGridStyle(Qt::NoPen);
   this->setShowGrid(false);
   this->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectColumns);
   this->setSelectionMode(QAbstractItemView::SelectionMode::ExtendedSelection);
   this->setAlternatingRowColors(true);
   this->setDragDropMode(QAbstractItemView::NoDragDrop);
   this->setDefaultDropAction(Qt::DropAction::MoveAction);
   this->setDragEnabled(false);
   this->setLineWidth(0);
   this->setFrameShadow(QAbstractItemView::Shadow::Plain);
   this->setFrameShape(QAbstractItemView::Shape::NoFrame);
   this->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::AnyKeyPressed |
                         QAbstractItemView::EditKeyPressed);
   //Consider all elements for resize
   this->setVerticalHeader(new C_SdNdeSingleHeaderView(Qt::Vertical));
   this->verticalHeader()->setResizeContentsPrecision(-1);
   this->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
   this->verticalHeader()->setFixedHeight(30);
   //Row Height
   this->setHorizontalHeader(new C_SdNdeSingleHeaderView(Qt::Horizontal));
   this->horizontalHeader()->setResizeContentsPrecision(-1);
   this->horizontalHeader()->setDefaultSectionSize(70);
   this->horizontalHeader()->setFixedHeight(30);
   //Corner button
   this->setCornerButtonEnabled(false);

   this->C_SyvDaItPaArView::setModel(&mc_Model);
   this->mc_Delegate.SetModel(&mc_Model);
   this->setItemDelegate(&mc_Delegate);

   //Hover event
   this->setMouseTracking(true);

   //Corner button label
   this->mpc_LabelCorner = new QLabel(this);
   this->mpc_LabelCorner->setAlignment(Qt::AlignCenter);
   this->mpc_LabelCorner->setAttribute(Qt::WA_TransparentForMouseEvents);
   this->mpc_LabelCorner->setText(C_GtGetText::h_GetText("Index"));
   connect(
      this->verticalHeader(), &QHeaderView::geometriesChanged, this,
      &C_SyvDaItPaArView::m_UpdateCornerButton);
   connect(
      this->horizontalHeader(), &QHeaderView::geometriesChanged, this,
      &C_SyvDaItPaArView::m_UpdateCornerButton);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Default destructor

   Clean up.
*/
//----------------------------------------------------------------------------------------------------------------------
C_SyvDaItPaArView::~C_SyvDaItPaArView(void)
{
   mpc_LabelCorner = NULL;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Specify associated list

   \param[in] ou32_ElementIndex  Element index
   \param[in] opc_DataWidget     Data widget
   \param[in] oq_EcuValues       Optional flag if the shown values are ECU values
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvDaItPaArView::SetElement(const uint32_t ou32_ElementIndex,
                                   stw::opensyde_gui_logic::C_PuiSvDbDataElementHandler * const opc_DataWidget,
                                   const bool oq_EcuValues)
{
   this->mc_Model.SetElement(ou32_ElementIndex, opc_DataWidget, oq_EcuValues);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Forward signal
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvDaItPaArView::OnErrorChangePossible(void)
{
   Q_EMIT this->SigErrorChangePossible();
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Get selected indices

   \return
   Selected indices
*/
//----------------------------------------------------------------------------------------------------------------------
std::vector<uint32_t> C_SyvDaItPaArView::m_GetSelectedIndices(void) const
{
   const QModelIndexList c_SelectedItems = this->selectedIndexes();

   std::vector<uint32_t> c_Retval;

   c_Retval.reserve(c_SelectedItems.size());
   for (QModelIndexList::const_iterator c_ItSelectedItem = c_SelectedItems.begin();
        c_ItSelectedItem != c_SelectedItems.end(); ++c_ItSelectedItem)
   {
      const QModelIndex & rc_Item = *c_ItSelectedItem;
      c_Retval.push_back(rc_Item.column());
   }
   C_Uti::h_Uniqueify(c_Retval);
   return c_Retval;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Update corner button size
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvDaItPaArView::m_UpdateCornerButton(void)
{
   tgl_assert(this->mpc_LabelCorner != NULL);
   if (this->mpc_LabelCorner != NULL)
   {
      this->mpc_LabelCorner->setGeometry(0, 0, this->verticalHeader()->width(), this->horizontalHeader()->height());
   }
}
