//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       Screen for parameter set file information (implementation)

   Screen for parameter set file information

   \copyright   Copyright 2019 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include "precomp_headers.hpp"

#include <QFileInfo>

#include "stwtypes.hpp"
#include "stwerrors.hpp"
#include "C_GtGetText.hpp"
#include "C_SyvUpPacParamSetFileInfoPopUp.hpp"
#include "ui_C_SyvUpPacParamSetFileInfoPopUp.h"

/* -- Used Namespaces ----------------------------------------------------------------------------------------------- */
using namespace stw::errors;
using namespace stw::opensyde_gui;
using namespace stw::opensyde_core;
using namespace stw::opensyde_gui_logic;
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

   \param[in,out] orc_Parent     Reference to parent
   \param[in]     orc_Path       Path for parameter set
   \param[in]     ou32_NodeIndex Node index for comparison
*/
//----------------------------------------------------------------------------------------------------------------------
C_SyvUpPacParamSetFileInfoPopUp::C_SyvUpPacParamSetFileInfoPopUp(
   stw::opensyde_gui_elements::C_OgePopUpDialog & orc_Parent, const QString & orc_Path, const uint32_t ou32_NodeIndex) :
   QWidget(&orc_Parent),
   mpc_Ui(new Ui::C_SyvUpPacParamSetFileInfoPopUp),
   mrc_ParentDialog(orc_Parent),
   mc_FileInfo(orc_Path, orc_Path, ou32_NodeIndex)
{
   this->mpc_Ui->setupUi(this);

   InitStaticNames();

   // register the widget for showing
   this->mrc_ParentDialog.SetWidget(this);

   this->m_ReadFile();

   connect(this->mpc_Ui->pc_PushButtonOk, &QPushButton::clicked, this, &C_SyvUpPacParamSetFileInfoPopUp::m_OkClicked);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Default destructor
*/
//----------------------------------------------------------------------------------------------------------------------
C_SyvUpPacParamSetFileInfoPopUp::~C_SyvUpPacParamSetFileInfoPopUp(void) noexcept
{
   delete this->mpc_Ui;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Initialize all displayed static names
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvUpPacParamSetFileInfoPopUp::InitStaticNames(void) const
{
   const QFileInfo c_Info(this->mc_FileInfo.GetPath());

   this->mrc_ParentDialog.SetTitle(C_GtGetText::h_GetText("Parameter Set Image File"));
   this->mrc_ParentDialog.SetSubTitle(c_Info.fileName());
   this->mpc_Ui->pc_LabelHeadingPreview->setText(C_GtGetText::h_GetText("File Information"));
   this->mpc_Ui->pc_PushButtonOk->setText(C_GtGetText::h_GetText("OK"));
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Overwritten key press event slot

   Here: Handle specific enter key cases

   \param[in,out] opc_KeyEvent Event identification and information
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvUpPacParamSetFileInfoPopUp::keyPressEvent(QKeyEvent * const opc_KeyEvent)
{
   bool q_CallOrg = true;

   //Handle all enter key cases manually
   if ((opc_KeyEvent->key() == static_cast<int32_t>(Qt::Key_Enter)) ||
       (opc_KeyEvent->key() == static_cast<int32_t>(Qt::Key_Return)))
   {
      if (((opc_KeyEvent->modifiers().testFlag(Qt::ControlModifier) == true) &&
           (opc_KeyEvent->modifiers().testFlag(Qt::AltModifier) == false)) &&
          (opc_KeyEvent->modifiers().testFlag(Qt::ShiftModifier) == false))
      {
         this->mrc_ParentDialog.accept();
      }
      else
      {
         q_CallOrg = false;
      }
   }
   if (q_CallOrg == true)
   {
      QWidget::keyPressEvent(opc_KeyEvent);
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Handle file read step

   \return
   C_NO_ERR   data read
   C_RD_WR    specified file does not exist
              specified file is present but structure is invalid (e.g. invalid XML file; not checksum found)
   C_CHECKSUM specified file is present but checksum is invalid
   C_CONFIG   file does not contain essential information
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvUpPacParamSetFileInfoPopUp::m_ReadFile(void)
{
   const int32_t s32_Result = this->mc_FileInfo.ReadFile();
   const QString & rc_Text = this->mc_FileInfo.GetComparisonResultsHtml();
   QString c_Html;

   c_Html += "<html>";
   c_Html += "<body>";
   if (s32_Result == C_NO_ERR)
   {
      c_Html += rc_Text;
   }
   else
   {
      c_Html += C_GtGetText::h_GetText("Could not read ");
      c_Html += this->mc_FileInfo.GetPath();
      c_Html += ".<br>";
      c_Html += C_GtGetText::h_GetText("Please make sure it is an existing and valid parameter set image file.");
   }
   c_Html += "</body>";
   c_Html += "</html>";

   this->mpc_Ui->pc_TextEditCompare->setHtml(c_Html);
   return s32_Result;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Slot of Ok button click
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvUpPacParamSetFileInfoPopUp::m_OkClicked(void)
{
   this->mrc_ParentDialog.accept();
}
