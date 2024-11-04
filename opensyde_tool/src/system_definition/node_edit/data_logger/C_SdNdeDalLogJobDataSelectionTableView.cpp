//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       Table view for log job data selection

   \copyright   Copyright 2024 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include <QScrollBar>
#include <QStandardItemModel>
#include <QHeaderView>

#include "precomp_headers.hpp"
#include "stwtypes.hpp"

#include "C_SdNdeDpUtil.hpp"
#include "C_Uti.hpp"
#include "C_OgeWiUtil.hpp"
#include "C_SdNdeDalLogJobDataSelectionTableView.hpp"

/* -- Used Namespaces ----------------------------------------------------------------------------------------------- */
using namespace stw::opensyde_gui;
using namespace stw::opensyde_gui_logic;
using namespace stw::opensyde_core;

/* -- Module Global Constants --------------------------------------------------------------------------------------- */

/* -- Types --------------------------------------------------------------------------------------------------------- */

/* -- Global Variables ---------------------------------------------------------------------------------------------- */

/* -- Module Global Variables --------------------------------------------------------------------------------------- */

/* -- Module Global Function Prototypes ----------------------------------------------------------------------------- */

/* -- Implementation ------------------------------------------------------------------------------------------------ */

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Default constructor

 *   Set up GUI with all elements.

   \param[in,out]  opc_Parent    Optional pointer to parent
*/
//----------------------------------------------------------------------------------------------------------------------
C_SdNdeDalLogJobDataSelectionTableView::C_SdNdeDalLogJobDataSelectionTableView(QWidget * const opc_Parent) :
   C_TblViewScroll(opc_Parent)
{
   QItemSelectionModel * const pc_LastSelectionModel = this->selectionModel();

   this->mc_SortProxyModel.setSourceModel(&mc_Model);
   this->mc_SortProxyModel.setSortRole(static_cast<int32_t>(Qt::DisplayRole));
   this->C_SdNdeDalLogJobDataSelectionTableView::setModel(&mc_SortProxyModel);
   //Delete last selection model, see Qt documentation for setModel
   delete pc_LastSelectionModel;

   this->mc_SortProxyModel.setFilterCaseSensitivity(Qt::CaseInsensitive);

   this->m_InitColumns();

   this->setSortingEnabled(true);
   this->setGridStyle(Qt::NoPen);
   this->setShowGrid(false);
   this->setCornerButtonEnabled(false);
   this->setDragEnabled(false);
   this->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
   this->setSelectionMode(QAbstractItemView::SelectionMode::ExtendedSelection);

   // configure the scrollbar to stop resizing the widget when showing or hiding the scrollbar
   this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
   this->verticalScrollBar()->hide();
   this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
   this->horizontalScrollBar()->hide();

   // Deactivate custom context menu of scroll bar
   this->verticalScrollBar()->setContextMenuPolicy(Qt::NoContextMenu);
   this->horizontalScrollBar()->setContextMenuPolicy(Qt::NoContextMenu);

   //Hide vertical header
   this->verticalHeader()->hide();

   //Avoid styling table inside
   C_OgeWiUtil::h_ApplyStylesheetProperty(this->verticalScrollBar(), "C_SdNdeDalLogJobDataSelectionTableView", true);
   C_OgeWiUtil::h_ApplyStylesheetProperty(this->horizontalScrollBar(), "C_SdNdeDalLogJobDataSelectionTableView", true);

   connect(this->verticalScrollBar(), &QScrollBar::rangeChanged, this,
           &C_SdNdeDalLogJobDataSelectionTableView::m_ShowHideVerticalScrollBar);
   connect(this->horizontalScrollBar(), &QScrollBar::rangeChanged, this,
           &C_SdNdeDalLogJobDataSelectionTableView::m_ShowHideHorizontalScrollBar);
   connect(&this->mc_Model, &C_SdNdeDalLogJobDataSelectionTableModel::SigDataChanged, this,
           &C_SdNdeDalLogJobDataSelectionTableView::SigDataChanged);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Default destructor

   Clean up.
*/
//----------------------------------------------------------------------------------------------------------------------
C_SdNdeDalLogJobDataSelectionTableView::~C_SdNdeDalLogJobDataSelectionTableView(void)
{
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Load user settings

   \param[in]  orc_Values  Values
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SdNdeDalLogJobDataSelectionTableView::LoadUserSettings(const std::vector<int32_t> & orc_Values)
{
   if (this->m_SetColumnWidths(orc_Values) == false)
   {
      m_InitColumns();
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Save user settings

   \param[in,out]  orc_Values    Values
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SdNdeDalLogJobDataSelectionTableView::SaveUserSettings(std::vector<int32_t> & orc_Values) const
{
   const std::map<C_SdNdeDalLogJobDataSelectionTableModel::E_Columns,
                  uint32_t> c_DefaultColumnWidths = C_SdNdeDalLogJobDataSelectionTableView::mh_GetDefaultColumnWidths();

   orc_Values = this->m_GetColumnWidths();
   for (uint32_t u32_It = 0UL; u32_It < orc_Values.size(); ++u32_It)
   {
      if (orc_Values[u32_It] == 0L)
      {
         const std::map<C_SdNdeDalLogJobDataSelectionTableModel::E_Columns,
                        uint32_t>::const_iterator c_It = c_DefaultColumnWidths.find(C_SdNdeDalLogJobDataSelectionTableModel::h_ColumnToEnum(
                                                                                       u32_It));
         if (c_It != c_DefaultColumnWidths.cend())
         {
            //Column hidden, use default instead
            orc_Values[u32_It] = static_cast<int32_t>(c_It->second);
         }
      }
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Check if any messages present

   \return
   True  Empty
   False Not empty
*/
//----------------------------------------------------------------------------------------------------------------------
bool C_SdNdeDalLogJobDataSelectionTableView::IsEmpty() const
{
   return (this->mc_Model.rowCount() == 0);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Filter for string

   \param[in]  orc_Text    String
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SdNdeDalLogJobDataSelectionTableView::Search(const QString & orc_Text)
{
   this->mc_SortProxyModel.setFilterFixedString(orc_Text);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Convey data changes to model
 *
 *  \param[in]  orc_DataElements    Selected data elements to be added
 *  \param[in]  ou32_NodeIndex      Current node index

*/
//----------------------------------------------------------------------------------------------------------------------
void C_SdNdeDalLogJobDataSelectionTableView::UpdateData(
   const std::vector<stw::opensyde_core::C_OscDataLoggerDataElementReference> & orc_DataElements,
   const uint32_t ou32_NodeIndex)
{
   //   this->sortByColumn(C_SdNdeDalLogJobDataSelectionTableModel::h_EnumToColumn(
   //                         C_SdNdeDalLogJobDataSelectionTableModel::eDATA_ELEMENT),
   //                      Qt::AscendingOrder);

   this->mc_Model.UpdateData(orc_DataElements, ou32_NodeIndex);

   // Resize the "Comment" column (Since it may have longer text)
   this->resizeColumnToContents(C_SdNdeDalLogJobDataSelectionTableModel::h_EnumToColumn(
                                   C_SdNdeDalLogJobDataSelectionTableModel::eCOMMENT));
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Overwritten selection changed event slot

   Here: Emit signal with new number of selected items

   \param[in]  orc_Selected      Selected
   \param[in]  orc_Deselected    Deselected
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SdNdeDalLogJobDataSelectionTableView::selectionChanged(const QItemSelection & orc_Selected,
                                                              const QItemSelection & orc_Deselected)
{
   // This method call ensures correct item row selection
   C_TblViewScroll::selectionChanged(orc_Selected, orc_Deselected);

   std::vector<uint32_t> c_SelectedIndices;
   if (orc_Selected.size() >= 0)
   {
      c_SelectedIndices = C_SdNdeDpUtil::h_ConvertVector(this->selectedIndexes());
      C_Uti::h_Uniqueify(c_SelectedIndices);
      Q_EMIT this->SigSelectionChanged(c_SelectedIndices.size());
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Initialize default columns
 *
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SdNdeDalLogJobDataSelectionTableView::m_InitColumns()
{
   const
   std::map<C_SdNdeDalLogJobDataSelectionTableModel::E_Columns,
            uint32_t> c_ColumnWidths = C_SdNdeDalLogJobDataSelectionTableView::mh_GetDefaultColumnWidths();

   for (std::map<C_SdNdeDalLogJobDataSelectionTableModel::E_Columns,
                 uint32_t>::const_iterator c_ItEntry = c_ColumnWidths.cbegin(); c_ItEntry != c_ColumnWidths.cend();
        ++c_ItEntry)
   {
      this->setColumnWidth(C_SdNdeDalLogJobDataSelectionTableModel::h_EnumToColumn(
                              c_ItEntry->first), c_ItEntry->second);
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Show hide vertical scroll bar

   \param[in]  os32_Min  Minimum range
   \param[in]  os32_Max  Maximum range
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SdNdeDalLogJobDataSelectionTableView::m_ShowHideVerticalScrollBar(const int32_t os32_Min,
                                                                         const int32_t os32_Max) const
{
   // manual showing and hiding of the scrollbar to stop resizing the parent widget when showing or hiding the scrollbar
   if ((os32_Min == 0) && (os32_Max == 0))
   {
      this->verticalScrollBar()->hide();
   }
   else
   {
      this->verticalScrollBar()->show();
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Show hide horizontal scroll bar

   \param[in]  os32_Min  Minimum range
   \param[in]  os32_Max  Maximum range
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SdNdeDalLogJobDataSelectionTableView::m_ShowHideHorizontalScrollBar(const int32_t os32_Min,
                                                                           const int32_t os32_Max) const
{
   // manual showing and hiding of the scrollbar to stop resizing the parent widget when showing or hiding the scrollbar
   if ((os32_Min == 0) && (os32_Max == 0))
   {
      this->horizontalScrollBar()->hide();
   }
   else
   {
      this->horizontalScrollBar()->show();
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Get default column widths

   \return
   Default column widths
*/
//----------------------------------------------------------------------------------------------------------------------
std::map<stw::opensyde_gui_logic::C_SdNdeDalLogJobDataSelectionTableModel::E_Columns,
         uint32_t> C_SdNdeDalLogJobDataSelectionTableView::mh_GetDefaultColumnWidths()
{
   std::map<C_SdNdeDalLogJobDataSelectionTableModel::E_Columns, uint32_t> c_ColumnWidths;
   c_ColumnWidths[C_SdNdeDalLogJobDataSelectionTableModel::eDATA_ELEMENT] = 250;
   c_ColumnWidths[C_SdNdeDalLogJobDataSelectionTableModel::eLOCATION] = 120;
   c_ColumnWidths[C_SdNdeDalLogJobDataSelectionTableModel::eNAMESPACE] = 250;
   c_ColumnWidths[C_SdNdeDalLogJobDataSelectionTableModel::eLOGGING_NAME] = 150;
   c_ColumnWidths[C_SdNdeDalLogJobDataSelectionTableModel::eCOMMENT] = 300;

   return c_ColumnWidths;
}