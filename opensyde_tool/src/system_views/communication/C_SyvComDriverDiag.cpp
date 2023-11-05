//----------------------------------------------------------------------------------------------------------------------
/*!
   \file
   \brief       GUI communication driver for diagnostics (implementation)

   Add functionality for diagnostics to the base class:
   * drivers for accessing data pool elements ("DataDealers")
   * diagnostic protocols via openSYDE or KEFEX protocols

   \copyright   Copyright 2017 Sensor-Technik Wiedemann GmbH. All rights reserved.
*/
//----------------------------------------------------------------------------------------------------------------------

/* -- Includes ------------------------------------------------------------------------------------------------------ */
#include "precomp_headers.hpp"

#include "stwerrors.hpp"

#include "C_SyvComDriverDiag.hpp"
#include "C_PuiSvHandler.hpp"
#include "C_PuiSdHandler.hpp"
#include "C_PuiSvData.hpp"
#include "TglUtils.hpp"
#include "C_OscLoggingHandler.hpp"
#include "C_Uti.hpp"
#include "C_GtGetText.hpp"
#include "C_SyvComDriverUtil.hpp"
#include "C_OscCanUtil.hpp"

/* -- Used Namespaces ----------------------------------------------------------------------------------------------- */
using namespace stw::errors;
using namespace stw::scl;
using namespace stw::can;
using namespace stw::opensyde_gui;
using namespace stw::opensyde_core;
using namespace stw::opensyde_gui_logic;

/* -- Module Global Constants --------------------------------------------------------------------------------------- */

/* -- Types --------------------------------------------------------------------------------------------------------- */

/* -- Global Variables ---------------------------------------------------------------------------------------------- */

/* -- Module Global Variables --------------------------------------------------------------------------------------- */

/* -- Module Global Function Prototypes ----------------------------------------------------------------------------- */

/* -- Implementation ------------------------------------------------------------------------------------------------ */

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Default constructor

   Initialize all members based on view

   \param[in]  ou32_ViewIndex    View index
*/
//----------------------------------------------------------------------------------------------------------------------
C_SyvComDriverDiag::C_SyvComDriverDiag(const uint32_t ou32_ViewIndex) :
   C_OscComDriverProtocol(),
   mu32_ViewIndex(ou32_ViewIndex),
   mpc_CanDllDispatcher(NULL),
   mpc_EthernetDispatcher(NULL)
{
   mpc_AsyncThread = new C_SyvComDriverThread(&C_SyvComDriverDiag::mh_ThreadFunc, this);

   connect(&this->mc_PollingThread, &C_SyvComPollingThreadDiag::finished, this,
           &C_SyvComDriverDiag::m_HandlePollingFinished);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Default destructor

   Clean up.
*/
//----------------------------------------------------------------------------------------------------------------------
C_SyvComDriverDiag::~C_SyvComDriverDiag(void)
{
   if (mpc_AsyncThread->isRunning() == true)
   {
      this->StopCycling();
   }

   delete mpc_AsyncThread;
   mpc_AsyncThread = NULL;

   if (this->mc_PollingThread.isRunning() == true)
   {
      this->mc_PollingThread.requestInterruption();

      if (this->mc_PollingThread.wait(2000U) == false)
      {
         // Not finished yet
         osc_write_log_warning("Closing diagnostic driver", "Waiting time for stopping polling thread was not enough");
      }
   }

   //let base class know we are about to die:
   C_OscComDriverProtocol::PrepareForDestruction();

   for (uint32_t u32_ItDiagProtocol = 0; u32_ItDiagProtocol < this->mc_DiagProtocols.size(); ++u32_ItDiagProtocol)
   {
      delete (this->mc_DiagProtocols[u32_ItDiagProtocol]);
      this->mc_DiagProtocols[u32_ItDiagProtocol] = NULL;
   }

   for (uint32_t u32_DealerIndex = 0U; u32_DealerIndex < this->mc_DataDealers.size(); u32_DealerIndex++)
   {
      delete this->mc_DataDealers[u32_DealerIndex];
      this->mc_DataDealers[u32_DealerIndex] = NULL;
   }

   if (mpc_CanDllDispatcher != NULL)
   {
      this->mpc_CanDllDispatcher->CAN_Exit();
#ifdef _WIN32
      this->mpc_CanDllDispatcher->DLL_Close();
#endif

      delete mpc_CanDllDispatcher;
      mpc_CanDllDispatcher = NULL;
   }

   delete this->mpc_EthernetDispatcher;
   //lint -e{1579}  no memory leak because mpc_AsyncThread is deleted here which is not detected by lint
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Initialize all members

   \return
   C_NO_ERR      Operation success
   C_NOACT       No active nodes
   C_CONFIG      Invalid system definition/view configuration
   C_RD_WR       Configured communication DLL does not exist
   C_OVERFLOW    Unknown transport protocol or unknown diagnostic server for at least one node
   C_BUSY        System view error detected
   C_COM         CAN initialization failed or no route found for at least one node
   C_CHECKSUM    Internal buffer overflow detected
   C_RANGE       Routing configuration failed
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::InitDiag(void)
{
   int32_t s32_Return;
   uint32_t u32_ActiveBusIndex;
   uint32_t u32_ActiveNodeCounter;
   bool q_NodeDiagRoutingError = false;

   std::vector<uint8_t> c_ActiveNodes;

   s32_Return = C_SyvComDriverUtil::h_GetOscComDriverParamFromView(this->mu32_ViewIndex, u32_ActiveBusIndex,
                                                                   c_ActiveNodes, &this->mpc_CanDllDispatcher,
                                                                   &this->mpc_EthernetDispatcher, true, true,
                                                                   &q_NodeDiagRoutingError);

   if (s32_Return == C_NO_ERR)
   {
      s32_Return = this->m_InitDiagNodes();
   }

   if (s32_Return == C_NO_ERR)
   {
      // pem folder is optional -> no error handling
      mc_PemDatabase.ParseFolder(C_Uti::h_GetPemDbPath().toStdString());

      s32_Return = C_OscComDriverProtocol::Init(C_PuiSdHandler::h_GetInstance()->GetOscSystemDefinitionConst(),
                                                u32_ActiveBusIndex, c_ActiveNodes, this->mpc_CanDllDispatcher,
                                                this->mpc_EthernetDispatcher, &this->mc_PemDatabase);
   }

   // Get active diag nodes
   if (q_NodeDiagRoutingError == true)
   {
      std::set<uint32_t> c_NodeDashboardErrors;
      std::set<uint32_t> c_RelevantNodes;
      // Special case: Dashboard specific routing error detected and this nodes must be deactivated
      C_PuiSvHandler::h_GetInstance()->GetViewNodeDashboardRoutingErrors(this->mu32_ViewIndex,
                                                                         c_NodeDashboardErrors);
      C_PuiSvHandler::h_GetInstance()->GetViewRelevantNodesForDashboardRouting(this->mu32_ViewIndex,
                                                                               c_RelevantNodes);

      // In this case errors are nodes which has deactivated diagnostic functions, but can be used for
      // routing for example
      for (u32_ActiveNodeCounter = 0U; u32_ActiveNodeCounter < this->mc_ActiveNodesIndexes.size();
           ++u32_ActiveNodeCounter)
      {
         bool q_IsDiagNode = true;
         if (c_NodeDashboardErrors.find(this->mc_ActiveNodesIndexes[u32_ActiveNodeCounter]) !=
             c_NodeDashboardErrors.end())
         {
            // Deactivate the node for diagnostic
            q_IsDiagNode = false;
         }

         if (q_IsDiagNode == true)
         {
            this->mc_ActiveDiagNodes.push_back(u32_ActiveNodeCounter);
         }

         // Register all nodes which are relevant for communication (dashboard itself or routing)
         if (c_RelevantNodes.find(this->mc_ActiveNodesIndexes[u32_ActiveNodeCounter]) !=
             c_RelevantNodes.end())
         {
            // Deactivate the node for diagnostic
            this->mc_ActiveCommunicatingNodes.push_back(u32_ActiveNodeCounter);
         }
      }
   }
   else
   {
      // All active nodes are capable for diagnostic
      // Assign all indexes for mc_ActiveNodesIndexes of the active nodes to mc_ActiveDiagNodes and
      // mc_ActiveCommunicatingNodes
      this->mc_ActiveDiagNodes.resize(this->mc_ActiveNodesIndexes.size());
      for (u32_ActiveNodeCounter = 0U; u32_ActiveNodeCounter < mc_ActiveNodesIndexes.size(); ++u32_ActiveNodeCounter)
      {
         this->mc_ActiveDiagNodes[u32_ActiveNodeCounter] = u32_ActiveNodeCounter;
      }

      this->mc_ActiveCommunicatingNodes = this->mc_ActiveDiagNodes;
   }

   if (s32_Return == C_NO_ERR)
   {
      s32_Return = m_InitDiagProtocol();
      if (s32_Return == C_NO_ERR)
      {
         s32_Return = m_InitDataDealer();

         if (s32_Return == C_NO_ERR)
         {
            this->mq_Initialized = true;
         }
      }
   }
   else if (s32_Return == C_NOACT)
   {
      this->mq_Initialized = true;
   }
   else
   {
      // Nothing to do
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Sets all node into diagnostic mode with necessary security access

   Steps:
   * set up required routing
   * bring server nodes into required sessions
   * activate security access level required for diagnostics

   \param[in]  orc_ErrorDetails  Details for current error

   \return
   C_NO_ERR    All nodes set to session successfully
   C_CONFIG    InitDiag function was not called or not successful or protocol was not initialized properly.
   C_COM       Error of service
   C_DEFAULT   Checksum of a datapool does not match
               Datapool with the name orc_DatapoolName does not exist on the server
   C_CHECKSUM  Security related error (something went wrong while handshaking with the server)
   C_TIMEOUT   Expected response not received within timeout
   C_RD_WR     malformed protocol response
   C_WARN      error response
   C_BUSY      Connection to at least one server failed
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::SetDiagnosticMode(QString & orc_ErrorDetails)
{
   int32_t s32_Return;

   this->mc_DefectNodeIndices.clear();

   s32_Return = this->m_StartRoutingDiag(orc_ErrorDetails, this->mc_DefectNodeIndices);

   // In case of a timeout, lets check all other nodes too, to get a complete list of not available nodes
   if ((s32_Return == C_NO_ERR) ||
       (s32_Return == C_TIMEOUT))
   {
      // Reset the previous error details in case of a timeout. It will be refilled with the next retries.
      orc_ErrorDetails = "";

      // Bring all nodes to the same session and security level
      // But check if the server is already in the correct session. The routing init has set some servers
      // to the session already
      s32_Return = this->m_SetNodesSessionId(this->mc_ActiveDiagNodes,
                                             C_OscProtocolDriverOsy::hu8_DIAGNOSTIC_SESSION_EXTENDED_DIAGNOSIS, true,
                                             this->mc_DefectNodeIndices);
      if (s32_Return == C_NO_ERR)
      {
         s32_Return = this->m_SetNodesSecurityAccess(this->mc_ActiveDiagNodes, 1U, this->mc_DefectNodeIndices);
         if (s32_Return != C_NO_ERR)
         {
            osc_write_log_error("Initializing diagnostic protocol", "Could not get security access");
         }
      }
      else
      {
         osc_write_log_error("Initializing diagnostic protocol", "Could not activate extended diagnostic session");
      }
      if (s32_Return != C_NO_ERR)
      {
         std::set<uint32_t>::const_iterator c_ItDefectNode;
         for (c_ItDefectNode = this->mc_DefectNodeIndices.begin(); c_ItDefectNode != this->mc_DefectNodeIndices.end();
              ++c_ItDefectNode)
         {
            orc_ErrorDetails += "- " + static_cast<QString>(this->m_GetActiveNodeName(*c_ItDefectNode).c_str()) + "\n";
         }
      }
   }

   // Start all diagnosis server
   if (s32_Return == C_NO_ERR)
   {
      s32_Return = this->m_StartDiagServers(orc_ErrorDetails);
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Request all cyclic transmissions based on the view configuration

   Steps:
   * configure rails for all nodes
   * request all configured cyclic and change driven transmissions

   The function will abort on the first communication problem.

   \param[in,out]  orc_ErrorDetails                Error details (if any)
   \param[in,out]  orc_FailedIdRegisters           Element IDs which failed registration (if any)
   \param[in,out]  orc_FailedIdErrorDetails        Error details for element IDs which failed registration (if any)
   \param[out]     orc_FailedNodesElementNumber    Map with all nodes as key with the number (not the index) of the
                                                   element which caused the error OSY_UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED
                                                   (0x70: To many transmissions already registered)
   \param[out]     orc_NodesElementNumber          Map with all nodes as key with the number (not the index) of the
                                                   element which should be registered (With and without error)

   \return
   C_CONFIG   configured view does not exist
              InitDiag() was not performed
   C_COM      Communication error
   C_NO_ERR   transmissions initialized
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::SetUpCyclicTransmissions(QString & orc_ErrorDetails,
                                                     std::vector<C_OscNodeDataPoolListElementId> & orc_FailedIdRegisters, std::vector<QString> & orc_FailedIdErrorDetails, std::map<uint32_t,
                                                                                                                                                                                    uint32_t> & orc_FailedNodesElementNumber, std::map<uint32_t,
                                                                                                                                                                                                                                       uint32_t> & orc_NodesElementNumber)
{
   int32_t s32_Return = C_NO_ERR;
   const C_PuiSvData * const pc_View = C_PuiSvHandler::h_GetInstance()->GetView(this->mu32_ViewIndex);

   orc_FailedNodesElementNumber.clear();
   orc_NodesElementNumber.clear();

   if ((pc_View == NULL) || (this->mq_Initialized == false))
   {
      s32_Return = C_CONFIG;
   }
   else
   {
      uint32_t u32_DiagNodeCounter;
      //set up rails:
      for (u32_DiagNodeCounter = 0U; u32_DiagNodeCounter < this->mc_ActiveDiagNodes.size(); u32_DiagNodeCounter++)
      {
         // Get the original active node index
         const uint32_t u32_ActiveNode = this->mc_ActiveDiagNodes[u32_DiagNodeCounter];
         uint16_t u16_RateMs;
         u16_RateMs = pc_View->GetUpdateRateFast();
         s32_Return = this->mc_DiagProtocols[u32_ActiveNode]->DataPoolSetEventDataRate(0, u16_RateMs);
         if (s32_Return == C_NO_ERR)
         {
            u16_RateMs = pc_View->GetUpdateRateMedium();
            s32_Return = this->mc_DiagProtocols[u32_ActiveNode]->DataPoolSetEventDataRate(1, u16_RateMs);
         }

         if (s32_Return == C_NO_ERR)
         {
            u16_RateMs = pc_View->GetUpdateRateSlow();
            s32_Return = this->mc_DiagProtocols[u32_ActiveNode]->DataPoolSetEventDataRate(2, u16_RateMs);
         }

         if (s32_Return != C_NO_ERR)
         {
            osc_write_log_warning(
               "Asynchronous communication",
               static_cast<QString>("Node \"%1\" - DataPoolSetEventDataRate - error: %2\n"
                                    "C_RANGE    parameter out of range (checked by client-side function)\n"
                                    "C_TIMEOUT  expected response not received within timeout\n"
                                    "C_NOACT    could not send request (e.g. Tx buffer full)\n"
                                    "C_CONFIG   pre-requisites not correct; e.g. driver not initialized\n"
                                    "C_WARN     error response\n"
                                    "C_RD_WR    malformed protocol response\n").arg(static_cast<QString>(
                                                                                       m_GetActiveNodeName(
                                                                                          u32_ActiveNode)
                                                                                       .c_str())).arg(
                  C_Uti::h_StwError(s32_Return)).toStdString().c_str());
            s32_Return = C_COM;
            orc_ErrorDetails += m_GetActiveNodeName(u32_ActiveNode).c_str();
            break;
         }
      }
   }

   if ((pc_View != NULL) &&
       (s32_Return == C_NO_ERR))
   {
      //request all transmissions that are configured for the current view
      const QMap<C_OscNodeDataPoolListElementId, C_PuiSvReadDataConfiguration> & rc_Transmissions =
         pc_View->GetReadRailAssignments();

      for (QMap<C_OscNodeDataPoolListElementId, C_PuiSvReadDataConfiguration>::const_iterator c_It =
              rc_Transmissions.begin(); c_It != rc_Transmissions.end(); ++c_It)
      {
         bool q_Found;
         //we need the node index within the list of active nodes:
         const uint32_t u32_ActiveDiagNodeIndex = this->m_GetActiveDiagIndex(c_It.key().u32_NodeIndex, &q_Found);
         //Skip inactive nodes
         if (q_Found == true)
         {
            const uint32_t u32_ActiveNodeIndex = this->mc_ActiveDiagNodes[u32_ActiveDiagNodeIndex];
            uint8_t u8_NegResponseCode = 0;
            //check for valid value ranges (node index is checked in "GetActiveIndex" function)
            tgl_assert(c_It.key().u32_DataPoolIndex <= 0xFFU);
            tgl_assert(c_It.key().u32_ListIndex <= 0xFFFFU);
            tgl_assert(c_It.key().u32_ElementIndex <= 0xFFFFU);

            if ((c_It.value().e_TransmissionMode == C_PuiSvReadDataConfiguration::eTM_CYCLIC) ||
                (c_It.value().e_TransmissionMode == C_PuiSvReadDataConfiguration::eTM_ON_CHANGE))
            {
               const std::map<uint32_t, uint32_t>::iterator c_ItNodesElementNumber = orc_NodesElementNumber.find(
                  c_It.key().u32_NodeIndex);
               if (c_ItNodesElementNumber == orc_NodesElementNumber.end())
               {
                  orc_NodesElementNumber[c_It.key().u32_NodeIndex] = 1U;
               }
               else
               {
                  c_ItNodesElementNumber->second = c_ItNodesElementNumber->second + 1;
               }
            }

            if (c_It.value().e_TransmissionMode == C_PuiSvReadDataConfiguration::eTM_CYCLIC)
            {
               s32_Return = this->mc_DiagProtocols[u32_ActiveNodeIndex]->DataPoolReadCyclic(
                  static_cast<uint8_t>(c_It.key().u32_DataPoolIndex), static_cast<uint16_t>(c_It.key().u32_ListIndex),
                  static_cast<uint16_t>(c_It.key().u32_ElementIndex), c_It.value().u8_RailIndex, &u8_NegResponseCode);
            }
            else if (c_It.value().e_TransmissionMode == C_PuiSvReadDataConfiguration::eTM_ON_CHANGE)
            {
               //convert the type dependent threshold to a uint32_t representation
               std::vector<uint8_t> c_Threshold;
               uint32_t u32_Threshold;
               c_It.value().c_ChangeThreshold.GetValueAsLittleEndianBlob(c_Threshold);
               //defensive measure: as element may only be up to 32bit the threshold may also not be > 32bit
               tgl_assert(c_Threshold.size() <= 4);
               //fill up to 4 bytes with zeroes
               c_Threshold.resize(4, 0U);
               //finally compose the uint32_t:
               u32_Threshold = c_Threshold[0] +
                               (static_cast<uint32_t>(c_Threshold[1]) << 8U) +
                               (static_cast<uint32_t>(c_Threshold[2]) << 16U) +
                               (static_cast<uint32_t>(c_Threshold[3]) << 24U);

               s32_Return = this->mc_DiagProtocols[u32_ActiveNodeIndex]->DataPoolReadChangeDriven(
                  static_cast<uint8_t>(c_It.key().u32_DataPoolIndex), static_cast<uint16_t>(c_It.key().u32_ListIndex),
                  static_cast<uint16_t>(c_It.key().u32_ElementIndex),
                  c_It.value().u8_RailIndex, u32_Threshold, &u8_NegResponseCode);
            }
            else
            {
               // No registration necessary
               s32_Return = C_NO_ERR;
            }
            //Both services map to the same error
            if (s32_Return != C_NO_ERR)
            {
               QString c_AdditionalInfo;
               QString c_Details;
               std::map<uint32_t, uint32_t>::iterator c_ItFailedNodesElementNumber;

               switch (s32_Return)
               {
               case C_RANGE:
                  c_Details = C_GtGetText::h_GetText("Parameter out of range (checked by client-side function)");
                  break;
               case C_NOACT:
                  c_Details = C_GtGetText::h_GetText("Could not send request (e.g. Tx buffer full)");
                  break;
               case C_CONFIG:
                  c_Details = C_GtGetText::h_GetText("Pre-requisites not correct; e.g. driver not initialized");
                  break;
               case C_WARN:
                  switch (u8_NegResponseCode)
                  {
                  case 0x13:
                     c_AdditionalInfo = C_GtGetText::h_GetText("Incorrect length of request");
                     break;
                  case 0x22:
                     c_AdditionalInfo = C_GtGetText::h_GetText(
                        "Datapool element specified by data identifier cannot be transferred event driven (invalid data type)");
                     break;
                  case 0x70:
                     c_AdditionalInfo = C_GtGetText::h_GetText("Too many transmissions already registered");

                     c_ItFailedNodesElementNumber = orc_FailedNodesElementNumber.find(c_It.key().u32_NodeIndex);

                     if (c_ItFailedNodesElementNumber == orc_FailedNodesElementNumber.end())
                     {
                        // Save the information about the number of the first element which failed
                        orc_FailedNodesElementNumber[c_It.key().u32_NodeIndex] =
                           orc_NodesElementNumber[c_It.key().u32_NodeIndex];
                     }
                     break;
                  case 0x31:
                     c_AdditionalInfo = C_GtGetText::h_GetText("Invalid transmission mode.\n"
                                                               "\n"
                                                               "When initiating transmission:\n"
                                                               "- Datapool element specified by data identifier is not available\n"
                                                               "- changeDrivenThreshold is zero\n"
                                                               "\n"
                                                               "When stopping transmission:\n"
                                                               "- Datapool element specified by data identifier is currently not transferred event driven");
                     break;
                  case 0x33:
                     c_AdditionalInfo = C_GtGetText::h_GetText("Required security level was not unlocked");
                     break;
                  case 0x14:
                     c_AdditionalInfo = C_GtGetText::h_GetText(
                        "The total length of the event driven response messages would exceed the available buffer size");
                     break;
                  case 0x7F:
                     c_AdditionalInfo = C_GtGetText::h_GetText(
                        "The requested service is not available in the session currently active");
                     break;
                  default:
                     c_AdditionalInfo =
                        static_cast<QString>(C_GtGetText::h_GetText("Unknown NRC: 0x%1")).arg(QString::number(
                                                                                                 u8_NegResponseCode,
                                                                                                 16));
                     break;
                  }
                  c_Details = static_cast<QString>(C_GtGetText::h_GetText("Error response (%1)")).arg(c_AdditionalInfo);
                  break;
               case C_RD_WR:
                  c_Details = C_GtGetText::h_GetText("Malformed protocol response");
                  break;
               default:
                  c_Details = C_GtGetText::h_GetText("Unknown error");
                  break;
               }
               orc_FailedIdErrorDetails.push_back(c_Details);
               orc_FailedIdRegisters.push_back(c_It.key());
               //Error can be ignored, user feedback is different
               s32_Return = C_NO_ERR;
            }
         }
      }
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Stop cyclic transmissions for all nodes

    Even if one of the nodes reports an error this function will continue and try to stop
     communication for the rest.

   \return
   C_CONFIG   configured view does not exist
              InitDiag() was not performed
   C_COM      Communication error (at least one of the nodes did not confirm the stop)
   C_NO_ERR   requested to stop transmissions
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::StopCyclicTransmissions(void)
{
   int32_t s32_Return = C_NO_ERR;
   const C_PuiSvData * const pc_View = C_PuiSvHandler::h_GetInstance()->GetView(this->mu32_ViewIndex);

   if ((pc_View == NULL) || (this->mq_Initialized == false))
   {
      s32_Return = C_CONFIG;
   }
   else
   {
      uint32_t u32_DiagNode;
      //stop all transmissions
      for (u32_DiagNode = 0U; u32_DiagNode < this->mc_ActiveDiagNodes.size(); u32_DiagNode++)
      {
         // Get the original active node index
         const uint32_t u32_ActiveNode = this->mc_ActiveDiagNodes[u32_DiagNode];
         const int32_t s32_Return2 = this->mc_DiagProtocols[u32_ActiveNode]->DataPoolStopEventDriven();
         if (s32_Return2 != C_NO_ERR)
         {
            osc_write_log_warning("Asynchronous communication",
                                  static_cast<QString>("Node \"%1\" - DataPoolStopEventDriven - warning: %2\n").
                                  arg(static_cast<QString>(m_GetActiveNodeName(u32_ActiveNode).c_str())).
                                  arg(C_Uti::h_StwError(s32_Return2)).toStdString().c_str());
            s32_Return = C_COM;
         }
      }
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Close the com driver

   All KEFEX servers will be logged off and if used the routing to KEFEX servers will be closed and deactivated.
   The openSYDE protocol server will not be closed. The session timeout is used to close all connections.

   \return
   C_NO_ERR   request sent, positive response received
   C_TIMEOUT  expected response not received within timeout
   C_NOACT    could not send protocol request
   C_WARN     error response
   C_CONFIG   CAN dispatcher not installed
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::StopDiagnosisServer(void)
{
   int32_t s32_Return = C_NO_ERR;

   if (this->mq_Initialized == true)
   {
      uint32_t u32_Counter;

      for (u32_Counter = 0U; u32_Counter < this->mc_DiagProtocols.size(); ++u32_Counter)
      {
         C_OscDiagProtocolKfx * const pc_DiagProtocolKfx =
            dynamic_cast<C_OscDiagProtocolKfx *>(this->mc_DiagProtocols[u32_Counter]);

         if (pc_DiagProtocolKfx != NULL)
         {
            int32_t s32_ReturnLogoff;
            s32_ReturnLogoff = pc_DiagProtocolKfx->Logoff(false);

            if (s32_ReturnLogoff != C_NO_ERR)
            {
               // We must logoff all nodes, but want to know an error. No break.
               s32_Return = s32_ReturnLogoff;
            }
         }
      }

      // Stop cyclic transmissions due to problems when closing TCP sockets.
      // If a socket was closed by client and a cyclic transmission was sent by server before the close was processed
      // a socket reset will occur
      this->StopCyclicTransmissions();

      this->m_StopRoutingOfActiveNodes();

      this->mq_Initialized = false;
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Starts the thread for async communication

   \return
   C_NO_ERR    Thread started with cyclic communication
   C_CONFIG    InitDiag function was not called or not successful or protocol was not initialized properly.
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::StartCycling(void)
{
   int32_t s32_Return = C_CONFIG;

   if ((this->mpc_AsyncThread != NULL) &&
       (this->mq_Initialized == true))
   {
      this->mpc_AsyncThread->start();
      s32_Return = C_NO_ERR;
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Stops the thread for async communication
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvComDriverDiag::StopCycling(void)
{
   tgl_assert(this->mpc_AsyncThread != NULL);
   if (this->mpc_AsyncThread != NULL)
   {
      this->mpc_AsyncThread->requestInterruption();
      if (this->mpc_AsyncThread->wait(2000U) == false)
      {
         // Not finished yet
         osc_write_log_warning("Stopping diagnostic cycling", "Waiting time for stopping thread was not enough");
      }
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Sends the tester present message to all active and reached nodes

   \return
   C_NO_ERR    All nodes set to session successfully
   C_CONFIG    Init function was not called or not successful or protocol was not initialized properly.
   C_COM       Error of service
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::SendTesterPresentToActiveNodes(void)
{
   return this->SendTesterPresent(this->mc_ActiveCommunicatingNodes);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled reading from data pool

   \param[in]  ou32_NodeIndex       node index to read from
   \param[in]  ou8_DataPoolIndex    data pool to read from
   \param[in]  ou16_ListIndex       list index to read from
   \param[in]  ou16_ElementIndex    element index to read from

   \return
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollDataPoolRead(const uint32_t ou32_NodeIndex, const uint8_t ou8_DataPoolIndex,
                                             const uint16_t ou16_ListIndex, const uint16_t ou16_ElementIndex)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartDataPoolRead((*mc_DataDealers[u32_ActiveIndex]), ou8_DataPoolIndex,
                                                      ou16_ListIndex, ou16_ElementIndex);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled writing to data pool

   \param[in]  ou32_NodeIndex       node index to write from
   \param[in]  ou8_DataPoolIndex    data pool to write from
   \param[in]  ou16_ListIndex       list index to write from
   \param[in]  ou16_ElementIndex    element index to write from

   \return
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollDataPoolWrite(const uint32_t ou32_NodeIndex, const uint8_t ou8_DataPoolIndex,
                                              const uint16_t ou16_ListIndex, const uint16_t ou16_ElementIndex)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartDataPoolWrite((*mc_DataDealers[u32_ActiveIndex]), ou8_DataPoolIndex,
                                                       ou16_ListIndex, ou16_ElementIndex);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled reading from data pool

   \param[in]  ou32_NodeIndex       node index to read from
   \param[in]  ou8_DataPoolIndex    data pool to read from
   \param[in]  ou16_ListIndex       list index to read from
   \param[in]  ou16_ElementIndex    element index to read from

   \return
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollNvmRead(const uint32_t ou32_NodeIndex, const uint8_t ou8_DataPoolIndex,
                                        const uint16_t ou16_ListIndex, const uint16_t ou16_ElementIndex)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartNvmRead((*mc_DataDealers[u32_ActiveIndex]), ou8_DataPoolIndex,
                                                 ou16_ListIndex, ou16_ElementIndex);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled writing to data pool

   \param[in]  ou32_NodeIndex       node index to write from
   \param[in]  ou8_DataPoolIndex    data pool to write from
   \param[in]  ou16_ListIndex       list index to write from
   \param[in]  ou16_ElementIndex    element index to write from

   \return
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollNvmWrite(const uint32_t ou32_NodeIndex, const uint8_t ou8_DataPoolIndex,
                                         const uint16_t ou16_ListIndex, const uint16_t ou16_ElementIndex)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartNvmWrite((*mc_DataDealers[u32_ActiveIndex]), ou8_DataPoolIndex,
                                                  ou16_ListIndex, ou16_ElementIndex);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled reading a list from data pool

   \param[in]  ou32_NodeIndex       node index to read from
   \param[in]  ou8_DataPoolIndex    data pool to read from
   \param[in]  ou16_ListIndex       list index to read from

   \return
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollNvmReadList(const uint32_t ou32_NodeIndex, const uint8_t ou8_DataPoolIndex,
                                            const uint16_t ou16_ListIndex)
{
   int32_t s32_Return;
   bool q_Found;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex, &q_Found);

   if ((u32_ActiveIndex >= mc_DataDealers.size()) || (q_Found == false))
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartNvmReadList((*mc_DataDealers[u32_ActiveIndex]), ou8_DataPoolIndex,
                                                     ou16_ListIndex);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled writing changed NVM elements to data pool

   \param[in]  ou32_NodeIndex    Node index to write to
   \param[in]  orc_ListIds       Lists to update CRC only

   \return
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollSafeNvmWriteChangedElements(const uint32_t ou32_NodeIndex,
                                                            const std::vector<C_OscNodeDataPoolListId> & orc_ListIds)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartNvmSafeWriteChangedValues((*mc_DataDealers[u32_ActiveIndex]), orc_ListIds);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Gets the output of PollSafeNvmWriteChangedElements

   \param[out]  orc_ChangedElements    All changed elements

   \return
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::GetPollSafeNvmWriteChangedElementsOutput(
   std::vector<C_OscNodeDataPoolListElementId> & orc_ChangedElements) const
{
   return this->mc_PollingThread.GetNvmSafeWriteChangedValuesOutput(orc_ChangedElements);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled reading of NVM values

   PollSafeNvmWriteChangedElements must be called before calling PollSafeNvmReadValues

   \param[in]  ou32_NodeIndex    node index to read from

   \return
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollSafeNvmReadValues(const uint32_t ou32_NodeIndex)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartNvmSafeReadValues(*mc_DataDealers[u32_ActiveIndex]);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Returns the output of the function NvmSafeReadValues

   Must be called after the thread was finished after calling NvmSafeReadValues

   \param[out]  orpc_ParamNodeValues   Pointer to node with read values

   \return
   C_NO_ERR   result returned
   C_BUSY     previously started polled communication still going on
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::GetPollNvmSafeReadValuesOutput(const C_OscNode * (&orpc_ParamNodeValues)) const
{
   return this->mc_PollingThread.GetNvmSafeReadValuesOutput(orpc_ParamNodeValues);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled reading of NVM values

   PollSafeNvmWriteChangedElements must be called before calling PollSafeNvmReadValues

   \param[in]  ou32_NodeIndex    node index to read from

   \return
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollSafeNvmSafeWriteCrcs(const uint32_t ou32_NodeIndex)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartNvmSafeWriteCrcs(*mc_DataDealers[u32_ActiveIndex]);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled reading of NVM values

   PollSafeNvmWriteChangedElements must be called before calling PollSafeNvmReadValues

   \param[in]  ou32_NodeIndex       node index to read from
   \param[in]  ou8_DataPoolIndex    data pool to read from
   \param[in]  ou16_ListIndex       list index to read from

   \return
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollNvmNotifyOfChanges(const uint32_t ou32_NodeIndex, const uint8_t ou8_DataPoolIndex,
                                                   const uint16_t ou16_ListIndex)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartNvmNotifyOfChanges((*mc_DataDealers[u32_ActiveIndex]), ou8_DataPoolIndex,
                                                            ou16_ListIndex);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Gets the output of PollSafeNvmWriteChangedElements

   \param[out]  orq_ApplicationAcknowledge   true: positive acknowledge from server
                                             false: negative acknowledge from server

   \return
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::GetPollNvmNotifyOfChangesOutput(bool & orq_ApplicationAcknowledge) const
{
   return this->mc_PollingThread.GetNvmNotifyOfChangesOutput(orq_ApplicationAcknowledge);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Wrap polling results

   Can be used to extract the results of one service execution after it has finished.

   \param[out]  ors32_Result  result code of executed service function
                              for possible values see the DataDealer's function documentation

   \return
   C_NO_ERR   result code read
   C_BUSY     previously started polled communication still going on
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::GetPollResults(int32_t & ors32_Result) const
{
   return this->mc_PollingThread.GetResults(ors32_Result);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Get result of previously started service execution

   Can be used to extract the results of one service execution after it has finished.

   \param[out]  oru8_Nrc   negative response code of executed service function
                           for possible values see the DataDealer's function documentation

   \return
   C_NO_ERR       result code read
   C_BUSY         previously started polled communication still going on
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::GetPollResultNrc(uint8_t & oru8_Nrc) const
{
   return this->mc_PollingThread.GetNegativeResponseCode(oru8_Nrc);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Wrapping call of h_NvmSafeClearInternalContent

   \param[in]  ou32_NodeIndex    Index of node

   \return
   C_NO_ERR  started polling
   C_RANGE   node index out of range
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::NvmSafeClearInternalContent(const uint32_t ou32_NodeIndex) const
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      C_SyvComDataDealer * const pc_DataDealer = mc_DataDealers[u32_ActiveIndex];
      if (pc_DataDealer != NULL)
      {
         s32_Return = C_NO_ERR;
         pc_DataDealer->NvmSafeClearInternalContent();
      }
      else
      {
         s32_Return = C_RANGE;
      }
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Start thread for polled reading of NVM values for creating parameter set file

   \param[in]  ou32_NodeIndex    Index of node
   \param[in]  orc_ListIds       Container will relevant list IDs

   \return
   C_NO_ERR  started polling
   C_RANGE   node index out of range
   C_BUSY    polling thread already busy (only one polled function possible in parallel)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::PollNvmSafeReadParameterValues(const uint32_t ou32_NodeIndex,
                                                           const std::vector<C_OscNodeDataPoolListId> & orc_ListIds)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      s32_Return = mc_PollingThread.StartNvmSafeReadParameterValues((*mc_DataDealers[u32_ActiveIndex]),
                                                                    orc_ListIds);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Wrapping call of h_NvmSafeCreateCleanFileWithoutCrc

   \param[in]  ou32_NodeIndex    Node index to work with
   \param[in]  orc_Path          Parameter file path
   \param[in]  orc_FileInfo      Optional general file information

   \return
   C_NO_ERR   data saved
   C_RANGE    Node index out of range
              File already exists
   C_OVERFLOW Wrong sequence of function calls
   C_CONFIG   Internal data invalid
   C_BUSY     file already exists
   C_RD_WR    could not write to file (e.g. missing write permissions; missing folder)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::NvmSafeCreateCleanFileWithoutCrc(const uint32_t ou32_NodeIndex, const QString & orc_Path,
                                                             const C_OscParamSetInterpretedFileInfoData & orc_FileInfo)
const
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      const stw::scl::C_SclString c_Path = orc_Path.toStdString().c_str();
      C_SyvComDataDealer * const pc_DataDealer = mc_DataDealers[u32_ActiveIndex];
      if (pc_DataDealer != NULL)
      {
         s32_Return = pc_DataDealer->NvmSafeCreateCleanFileWithoutCrc(c_Path, orc_FileInfo);
      }
      else
      {
         s32_Return = C_RANGE;
      }
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Wrapping call of h_NvmSafeReadFileWithoutCrc

   Warning: CRC is not checked

   \param[in]  ou32_NodeIndex    Node index to work with
   \param[in]  orc_Path          Parameter file path

   \return
   C_NO_ERR   data read
   C_OVERFLOW Wrong sequence of function calls
   C_RANGE    Path does not match the path of the preceding function calls
              Node index out of range
   C_RD_WR    specified file does not exist
              specified file is present but structure is invalid (e.g. invalid XML file)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::NvmSafeReadFileWithoutCrc(const uint32_t ou32_NodeIndex, const QString & orc_Path) const
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      const stw::scl::C_SclString c_Path = orc_Path.toStdString().c_str();
      C_SyvComDataDealer * const pc_DataDealer = mc_DataDealers[u32_ActiveIndex];
      if (pc_DataDealer != NULL)
      {
         s32_Return = pc_DataDealer->NvmSafeReadFileWithoutCrc(c_Path);
      }
      else
      {
         s32_Return = C_RANGE;
      }
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Wrapping call of NvmSafeCheckParameterFileContents

   \param[in]   ou32_NodeIndex      node index to read from
   \param[in]   orc_Path            File path
   \param[out]  orc_DataPoolLists   Loaded data pool lists (Always cleared at start)

   \return
   C_NO_ERR   Lists valid
   C_OVERFLOW Wrong sequence of function calls
   C_RANGE    Path does not match the path of the preceding function calls
              Node index out of range
   C_CONFIG   Mismatch of data with current node
               or no valid pointer to the original instance of "C_OscNode" is set in "C_OscDataDealer"
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::NvmSafeCheckParameterFileContents(const uint32_t ou32_NodeIndex, const QString & orc_Path,
                                                              std::vector<C_OscNodeDataPoolListId> & orc_DataPoolLists)
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      const stw::scl::C_SclString c_Path = orc_Path.toStdString().c_str();
      s32_Return = this->mc_DataDealers[u32_ActiveIndex]->NvmSafeCheckParameterFileContents(
         c_Path, orc_DataPoolLists);
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Wrapping call of h_NvmSafeUpdateCrcForFile

   \param[in]  ou32_NodeIndex    Node index to work with
   \param[in]  orc_Path          Parameter file path

   \return
   C_NO_ERR   CRC updated
   C_OVERFLOW Wrong sequence of function calls
   C_RANGE    Path does not match the path of the preceding function calls
              Node index out of range
   C_RD_WR    specified file does not exist
              specified file is present but structure is invalid (e.g. invalid XML file)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::NvmSafeUpdateCrcForFile(const uint32_t ou32_NodeIndex, const QString & orc_Path) const
{
   int32_t s32_Return;
   const uint32_t u32_ActiveIndex = this->m_GetActiveDiagIndex(ou32_NodeIndex);

   if (u32_ActiveIndex >= mc_DataDealers.size())
   {
      s32_Return = C_RANGE;
   }
   else
   {
      const stw::scl::C_SclString c_Path = orc_Path.toStdString().c_str();
      C_SyvComDataDealer * const pc_DataDealer = mc_DataDealers[u32_ActiveIndex];
      if (pc_DataDealer != NULL)
      {
         s32_Return = pc_DataDealer->NvmSafeUpdateCrcForFile(c_Path);
      }
      else
      {
         s32_Return = C_RANGE;
      }
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Returns a reference to all data dealer

   \return
   Reference to vector with all data dealer
*/
//----------------------------------------------------------------------------------------------------------------------
const std::vector<C_SyvComDataDealer *> & C_SyvComDriverDiag::GetAllDataDealer(void) const
{
   return this->mc_DataDealers;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Adds a widget to inform about new datapool com signal events

   \param[in]  opc_Widget  Pointer to dashboard widget base
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvComDriverDiag::RegisterWidget(C_PuiSvDbDataElementHandler * const opc_Widget)
{
   if (opc_Widget != NULL)
   {
      uint32_t u32_Counter;

      for (u32_Counter = 0U; u32_Counter < opc_Widget->GetWidgetDataPoolElementCount(); ++u32_Counter)
      {
         C_PuiSvDbNodeDataPoolListElementId c_DpElementId;
         if (opc_Widget->GetDataPoolElementIndex(u32_Counter, c_DpElementId) == C_NO_ERR)
         {
            // Is it relevant for this data dealer (No handling of bus signals here)?
            if (c_DpElementId.GetType() == C_PuiSvDbNodeDataPoolListElementId::eBUS_SIGNAL)
            {
               C_OscCanMessageIdentificationIndices c_MsgId;
               uint32_t u32_SignalIndex;
               const C_OscCanMessage * pc_CanMsg;
               const C_OscCanSignal * pc_Signal;
               const C_OscNodeDataPoolListElement * const pc_Element =
                  C_PuiSdHandler::h_GetInstance()->GetOscDataPoolListElement(c_DpElementId);

               // Get the signal information out of the CAN message of this datapool element
               C_PuiSdHandler::h_GetInstance()->ConvertElementIndexToSignalIndex(c_DpElementId,
                                                                                 c_MsgId, u32_SignalIndex);
               pc_CanMsg = C_PuiSdHandler::h_GetInstance()->GetCanMessage(c_MsgId);
               pc_Signal = C_PuiSdHandler::h_GetInstance()->GetCanSignal(c_MsgId, u32_SignalIndex);

               if ((pc_CanMsg != NULL) &&
                   (pc_Signal != NULL) &&
                   (pc_Element != NULL))
               {
                  const C_OscCanMessageUniqueId c_MsgCanId(pc_CanMsg->u32_CanId, pc_CanMsg->q_IsExtended);
                  C_SyvComDriverDiagWidgetRegistration c_WidgetRegistration;
                  QMap<C_OscCanMessageUniqueId, QList<C_SyvComDriverDiagWidgetRegistration> >::iterator c_ItElement;

                  c_WidgetRegistration.c_Signal = *pc_Signal;

                  if (c_WidgetRegistration.c_Signal.e_MultiplexerType == C_OscCanSignal::eMUX_MULTIPLEXED_SIGNAL)
                  {
                     uint32_t u32_SignalCounter;
                     bool q_MultiplexerSignalFound = false;

                     // Special case: This signal is multiplexed. It is necessary to know the multiplexer signal
                     for (u32_SignalCounter = 0U; u32_SignalCounter < pc_CanMsg->c_Signals.size(); ++u32_SignalCounter)
                     {
                        if (pc_CanMsg->c_Signals[u32_SignalCounter].e_MultiplexerType ==
                            C_OscCanSignal::eMUX_MULTIPLEXER_SIGNAL)
                        {
                           // Save the multiplexer signal
                           c_WidgetRegistration.c_MultiplexerSignal = pc_CanMsg->c_Signals[u32_SignalCounter];
                           q_MultiplexerSignalFound = true;
                           break;
                        }
                     }

                     // A multiplexer signal must exist if at least one multiplexed signal is present
                     tgl_assert(q_MultiplexerSignalFound == true);
                  }

                  c_WidgetRegistration.pc_Handler = opc_Widget;
                  c_WidgetRegistration.q_IsExtended = pc_CanMsg->q_IsExtended;
                  c_WidgetRegistration.u16_Dlc = pc_CanMsg->u16_Dlc;
                  c_WidgetRegistration.c_ElementId = c_DpElementId;
                  // Save the value content to have the content instance with the correct type as template for
                  // the new values
                  c_WidgetRegistration.c_ElementContent = pc_Element->c_Value;

                  c_ItElement = this->mc_AllWidgets.find(c_MsgCanId);

                  // Add the widget to the map
                  if (c_ItElement != this->mc_AllWidgets.end())
                  {
                     //Check if not already contained
                     if (c_ItElement.value().contains(c_WidgetRegistration) == false)
                     {
                        // There is a list for this datapool element already
                        c_ItElement.value().push_back(c_WidgetRegistration);
                     }
                  }
                  else
                  {
                     // The map has no entry for this datapool element. Add a new list with this widget
                     QList<C_SyvComDriverDiagWidgetRegistration> c_List;
                     c_List.push_back(c_WidgetRegistration);
                     this->mc_AllWidgets.insert(c_MsgCanId, c_List);
                  }
               }
               else
               {
                  // TODO: Error handling
               }
            }
         }
      }
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Returns the information about the routing configuration

   \param[out]  ore_Mode   Needed routing mode

   \return
   true     Routing is necessary
   false    Routing is not necessary
*/
//----------------------------------------------------------------------------------------------------------------------
bool C_SyvComDriverDiag::m_GetRoutingMode(C_OscRoutingCalculation::E_Mode & ore_Mode) const
{
   ore_Mode = C_OscRoutingCalculation::eROUTING_CHECK;

   return true;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Returns needed session ID for the current routing mode

   \return
   Session ID for flashloader
*/
//----------------------------------------------------------------------------------------------------------------------
uint8_t C_SyvComDriverDiag::m_GetRoutingSessionId(void) const
{
   return C_OscProtocolDriverOsy::hu8_DIAGNOSTIC_SESSION_EXTENDED_DIAGNOSIS;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Checks if the routing for a not openSYDE server is necessary

   \param[in]  orc_Node    Current node

   \return
   true    Specific server and legacy routing necessary
   false   No specific server and legacy routing necessary
*/
//----------------------------------------------------------------------------------------------------------------------
bool C_SyvComDriverDiag::m_IsRoutingSpecificNecessary(const C_OscNode & orc_Node) const
{
   bool q_Return = false;

   if (orc_Node.c_Properties.e_DiagnosticServer == C_OscNodeProperties::eDS_KEFEX)
   {
      q_Return = true;
   }

   return q_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Prepares the routing for a KEFEX server

   \param[in]   ou32_ActiveNode                       Active node index of vector mc_ActiveNodes
   \param[in]   opc_Node                              Pointer to current node
   \param[in]   orc_LastNodeOfRouting                 The last node in the routing chain before the final target server
   \param[in]   opc_ProtocolOsyOfLastNodeOfRouting    The protocol of the last node
   \param[out]  oppc_RoutingDispatcher                The legacy routing dispatcher

   \return
   C_NO_ERR    Specific server necessary and legacy routing dispatcher created
   C_NOACT     No specific server necessary
   C_CONFIG    opc_ProtocolOsyOfLastNodeOfRouting is NULL
               Diagnose protocol is NULL
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_StartRoutingSpecific(const uint32_t ou32_ActiveNode, const C_OscNode * const opc_Node,
                                                   const C_OscRoutingRoutePoint & orc_LastNodeOfRouting,
                                                   C_OscProtocolDriverOsy * const opc_ProtocolOsyOfLastNodeOfRouting,
                                                   C_OscCanDispatcherOsyRouter ** const oppc_RoutingDispatcher)
{
   int32_t s32_Return = C_NOACT;

   if (opc_Node->c_Properties.e_DiagnosticServer == C_OscNodeProperties::eDS_KEFEX)
   {
      C_OscDiagProtocolKfx * const pc_DiagProtocolKfx =
         dynamic_cast<C_OscDiagProtocolKfx * const>(this->mc_DiagProtocols[ou32_ActiveNode]);

      if ((pc_DiagProtocolKfx != NULL) &&
          (opc_ProtocolOsyOfLastNodeOfRouting != NULL))
      {
         (*oppc_RoutingDispatcher) = new C_OscCanDispatcherOsyRouter(*opc_ProtocolOsyOfLastNodeOfRouting);
         // TODO Filter settings?
         (*oppc_RoutingDispatcher)->SetFilterParameters(orc_LastNodeOfRouting.u8_OutInterfaceNumber, 0x00000000,
                                                        0x00000000);

         this->mc_LegacyRouterDispatchers[ou32_ActiveNode] = (*oppc_RoutingDispatcher);
         // Set the new dispatcher
         pc_DiagProtocolKfx->SetDispatcher(*oppc_RoutingDispatcher);

         s32_Return = C_NO_ERR;
      }
      else
      {
         s32_Return = C_CONFIG;
      }
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Stops the specific routing configuration for one specific node

   \param[in]  ou32_ActiveNode   Active node index of vector mc_ActiveNodes
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvComDriverDiag::m_StopRoutingSpecific(const uint32_t ou32_ActiveNode)
{
   if ((this->mc_ActiveNodesIndexes[ou32_ActiveNode] < this->mpc_SysDef->c_Nodes.size()) &&
       (this->mpc_SysDef->c_Nodes[this->mc_ActiveNodesIndexes[ou32_ActiveNode]].c_Properties.e_DiagnosticServer ==
        C_OscNodeProperties::eDS_KEFEX))
   {
      C_OscDiagProtocolKfx * const pc_DiagProtocolKfx =
         dynamic_cast<C_OscDiagProtocolKfx * const>(this->mc_DiagProtocols[ou32_ActiveNode]);

      if (pc_DiagProtocolKfx != NULL)
      {
         // Remove dispatcher
         pc_DiagProtocolKfx->SetDispatcher(NULL);
      }
   }

   C_OscComDriverProtocol::m_StopRoutingSpecific(ou32_ActiveNode);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Checks if the interface has relevant functions activated

   In this case diagnostic and update functionality

   \param[in]  orc_ComItfSettings   Interface configuration

   \return
   true     Interface has relevant functions activated and is connected
   false    Interface has no relevant functions activated or is not connected
*/
//----------------------------------------------------------------------------------------------------------------------
bool C_SyvComDriverDiag::m_CheckInterfaceForFunctions(const C_OscNodeComInterfaceSettings & orc_ComItfSettings) const
{
   bool q_Return = false;

   if ((orc_ComItfSettings.GetBusConnected() == true) &&
       ((orc_ComItfSettings.q_IsRoutingEnabled == true) ||
        (orc_ComItfSettings.q_IsDiagnosisEnabled == true)))
   {
      q_Return = true;
   }

   return q_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Distributes the CAN message to all registered C_OscDataDealer for all relevant Datapool comm signals

   \param[in]  orc_Msg  Current CAN message
   \param[in]  oq_IsTx  Message was sent by C_OscComDriverBase itself
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvComDriverDiag::m_HandleCanMessage(const T_STWCAN_Msg_RX & orc_Msg, const bool oq_IsTx)
{
   const bool q_IsExtended = orc_Msg.u8_XTD == 1;

   C_OscComDriverBase::m_HandleCanMessage(orc_Msg, oq_IsTx);
   QMap<C_OscCanMessageUniqueId, QList<C_SyvComDriverDiagWidgetRegistration> >::const_iterator c_ItElement;

   // Check if this CAN message id is relevant
   c_ItElement = this->mc_AllWidgets.find(C_OscCanMessageUniqueId(orc_Msg.u32_ID, q_IsExtended));

   if (c_ItElement != this->mc_AllWidgets.end())
   {
      const QList<C_SyvComDriverDiagWidgetRegistration> & rc_Registrations = c_ItElement.value();
      QList<C_SyvComDriverDiagWidgetRegistration>::const_iterator c_ItRegistration;

      // Iterate through all widget registrations
      for (c_ItRegistration = rc_Registrations.begin(); c_ItRegistration != rc_Registrations.end(); ++c_ItRegistration)
      {
         const C_SyvComDriverDiagWidgetRegistration & rc_WidgetRegistration = *c_ItRegistration;

         // Is the CAN message as expected
         if (((orc_Msg.u8_XTD == 1U) == rc_WidgetRegistration.q_IsExtended) &&
             (rc_WidgetRegistration.pc_Handler != NULL))
         {
            bool q_SignalFits;
            bool q_DlcErrorPossible = true;

            if (rc_WidgetRegistration.c_Signal.e_MultiplexerType != C_OscCanSignal::eMUX_MULTIPLEXED_SIGNAL)
            {
               // No multiplexed signal, no dependency of a multiplexer value
               q_SignalFits = C_OscCanUtil::h_IsSignalInMessage(orc_Msg.u8_DLC, rc_WidgetRegistration.c_Signal);
            }
            else
            {
               // Multiplexed signal. Checking the multiplexer signal first
               q_SignalFits = C_OscCanUtil::h_IsSignalInMessage(orc_Msg.u8_DLC,
                                                                rc_WidgetRegistration.c_MultiplexerSignal);

               if (q_SignalFits == true)
               {
                  // Multiplexer fits into the message. Get the multiplexer value
                  C_PuiSvDbDataElementContent c_MultiplexerContent;
                  C_OscCanUtil::h_GetSignalValue(orc_Msg.au8_Data, rc_WidgetRegistration.c_MultiplexerSignal,
                                                 c_MultiplexerContent);
                  const C_OscNodeDataPoolContent::E_Type e_Type = c_MultiplexerContent.GetType();
                  uint16_t u16_MultiplexerValue;

                  // Multiplexer can be maximum 16 bit
                  if (e_Type == C_OscNodeDataPoolContent::eUINT8)
                  {
                     u16_MultiplexerValue = c_MultiplexerContent.GetValueU8();
                  }
                  else if (e_Type == C_OscNodeDataPoolContent::eUINT16)
                  {
                     u16_MultiplexerValue = c_MultiplexerContent.GetValueU16();
                  }
                  else
                  {
                     // May not happen
                     tgl_assert(false);
                     u16_MultiplexerValue = 0;
                  }

                  if (rc_WidgetRegistration.c_Signal.u16_MultiplexValue == u16_MultiplexerValue)
                  {
                     // The multiplexer value is matching. The signal is in the message.
                     q_SignalFits = C_OscCanUtil::h_IsSignalInMessage(orc_Msg.u8_DLC, rc_WidgetRegistration.c_Signal);
                  }
                  else
                  {
                     // The multiplexer value is not matching. The signal is not in the message, but it is no DLC error.
                     q_SignalFits = false;
                     q_DlcErrorPossible = false;
                  }
               }
            }

            if (q_SignalFits == true)
            {
               C_PuiSvDbDataElementContent c_Content;
               const uint64_t u64_TimeStamp = orc_Msg.u64_TimeStamp / 1000U;

               // Get the content
               c_Content = rc_WidgetRegistration.c_ElementContent;
               C_OscCanUtil::h_GetSignalValue(orc_Msg.au8_Data, rc_WidgetRegistration.c_Signal, c_Content);
               c_Content.SetTimeStamp(static_cast<uint32_t>(u64_TimeStamp));

               rc_WidgetRegistration.pc_Handler->InsertNewValueIntoQueue(rc_WidgetRegistration.c_ElementId,
                                                                         c_Content);
            }
            else if (q_DlcErrorPossible == true)
            {
               //Error message for widget
               rc_WidgetRegistration.pc_Handler->SetErrorForInvalidDlc(rc_WidgetRegistration.c_ElementId,
                                                                       orc_Msg.u8_DLC);
            }
            else
            {
               // Nothing to do
            }
         }
      }
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Default constructor

   Initialize all members based on view
*/
//----------------------------------------------------------------------------------------------------------------------
C_SyvComDriverDiag::C_SyvComDriverDiagWidgetRegistration::C_SyvComDriverDiagWidgetRegistration(void) :
   pc_Handler(NULL),
   u16_Dlc(0),
   q_IsExtended(false)
{
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Check if current equal to orc_Cmp

   \param[in]  orc_Cmp  Compared instance

   \return
   Current equal to orc_Cmp
   Else false
*/
//----------------------------------------------------------------------------------------------------------------------
bool C_SyvComDriverDiag::C_SyvComDriverDiagWidgetRegistration::operator ==(
   const C_SyvComDriverDiagWidgetRegistration & orc_Cmp) const
{
   bool q_Return = true;

   if ((this->q_IsExtended != orc_Cmp.q_IsExtended) ||
       (this->u16_Dlc != orc_Cmp.u16_Dlc) ||
       (this->pc_Handler != orc_Cmp.pc_Handler) ||
       (this->c_Signal != orc_Cmp.c_Signal) ||
       (this->c_MultiplexerSignal != orc_Cmp.c_MultiplexerSignal))
   {
      q_Return = false;
   }

   return q_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Detects all nodes which are used in current dashboard

   The nodes which which has used datapool elements will be saved in an addition vector

   \return
   C_NO_ERR      No error
   C_CONFIG      Invalid system definition/view configuration
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_InitDiagNodes(void)
{
   int32_t s32_Return = C_CONFIG;
   const C_PuiSvData * const pc_View = C_PuiSvHandler::h_GetInstance()->GetView(this->mu32_ViewIndex);

   if (pc_View != NULL)
   {
      s32_Return = C_NO_ERR;

      //request all transmissions that are configured for the current view
      const QMap<C_OscNodeDataPoolListElementId, C_PuiSvReadDataConfiguration> & rc_Transmissions =
         pc_View->GetReadRailAssignments();
      const std::set<C_OscNodeDataPoolListElementId> c_WriteElements = pc_View->GetWriteAssignments();

      // Get all nodes which has used datapool elements on this dashboard
      for (QMap<C_OscNodeDataPoolListElementId, C_PuiSvReadDataConfiguration>::const_iterator c_ItElement =
              rc_Transmissions.begin(); c_ItElement != rc_Transmissions.end(); ++c_ItElement)
      {
         this->mc_DiagNodesWithElements.insert(c_ItElement.key().u32_NodeIndex);
      }
      for (std::set<C_OscNodeDataPoolListElementId>::const_iterator c_ItWriteElement =
              c_WriteElements.begin(); c_ItWriteElement != c_WriteElements.end(); ++c_ItWriteElement)
      {
         this->mc_DiagNodesWithElements.insert((*c_ItWriteElement).u32_NodeIndex);
      }
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
void C_SyvComDriverDiag::m_InitDiagProtocolKfx(C_OscDiagProtocolKfx * const opc_DiagProtocolKefex) const
{
   stw::diag_lib::C_KFXCommConfiguration c_CommConfig;

   // TODO Init KEFEX protocol dynamically
   c_CommConfig.SetBaseID(6);
   c_CommConfig.SetClientAddress(100);
   c_CommConfig.SetServerAddress(0);
   c_CommConfig.SetBSMax(20);
   c_CommConfig.SetSTMin(0);
   c_CommConfig.SetTimeout(200);

   opc_DiagProtocolKefex->SetNvmValidFlagUsed(false);
   opc_DiagProtocolKefex->SetCommunicationParameters(c_CommConfig);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Initialize diagnostic protocols

   The functions fills the vector mc_OsyProtocols of the base class too.

   \return
   C_NO_ERR   Operation success
   C_CONFIG   Invalid initialization
   C_OVERFLOW Unknown diagnostic server for at least one node or invalid node identifier was set in diagnostic protocol
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_InitDiagProtocol(void)
{
   int32_t s32_Retval = C_NO_ERR;

   if ((this->mc_TransportProtocols.size() >= this->m_GetActiveNodeCount()) &&
       (this->mc_ServerIds.size() == this->m_GetActiveNodeCount()))
   {
      //Initialize protocol driver
      this->mc_DiagProtocols.resize(this->m_GetActiveNodeCount(), NULL);
      this->mc_OsyProtocols.resize(this->m_GetActiveNodeCount(), NULL);

      for (uint32_t u32_ItActiveNode = 0;
           (u32_ItActiveNode < this->mc_ActiveNodesIndexes.size()) && (s32_Retval == C_NO_ERR);
           ++u32_ItActiveNode)
      {
         const C_OscNode * const pc_Node =
            C_PuiSdHandler::h_GetInstance()->GetOscNodeConst(this->mc_ActiveNodesIndexes[u32_ItActiveNode]);
         if (pc_Node != NULL)
         {
            //Diagnostic protocol initialization
            C_OscDiagProtocolOsy * pc_DiagProtocolOsy = NULL;
            C_OscDiagProtocolKfx * pc_DiagProtocolKefex = NULL;
            C_OscDiagProtocolBase * pc_DiagProtocol = NULL;

            switch (pc_Node->c_Properties.e_DiagnosticServer)
            {
            case C_OscNodeProperties::eDS_OPEN_SYDE:
               pc_DiagProtocolOsy = new C_OscDiagProtocolOsy();
               s32_Retval = pc_DiagProtocolOsy->SetTransportProtocol(this->mc_TransportProtocols[u32_ItActiveNode]);
               if (s32_Retval == C_NO_ERR)
               {
                  s32_Retval =
                     pc_DiagProtocolOsy->SetNodeIdentifiers(this->GetClientId(),
                                                            this->mc_ServerIds[u32_ItActiveNode]);
                  if (s32_Retval != C_NO_ERR)
                  {
                     //Invalid configuration = programming error
                     osc_write_log_error("Initializing diagnostic protocol", "Could not set node identifiers");
                     s32_Retval = C_OVERFLOW;
                  }
               }
               else
               {
                  //Invalid configuration = programming error
                  s32_Retval = C_OVERFLOW;
               }
               pc_DiagProtocol = pc_DiagProtocolOsy;
               this->mc_OsyProtocols[u32_ItActiveNode] = pc_DiagProtocolOsy;
               break;
            case C_OscNodeProperties::eDS_KEFEX:
               pc_DiagProtocolKefex = new C_OscDiagProtocolKfx();
               pc_DiagProtocolKefex->SetDispatcher(this->m_GetCanDispatcher());
               pc_DiagProtocol = pc_DiagProtocolKefex;
               this->m_InitDiagProtocolKfx(pc_DiagProtocolKefex);
               break;
            case C_OscNodeProperties::eDS_NONE:
            default:
               osc_write_log_error("Initializing diagnostic protocol", "Unknown diagnostic protocol");
               s32_Retval = C_OVERFLOW;
               break;
            }
            this->mc_DiagProtocols[u32_ItActiveNode] = pc_DiagProtocol;
         }
         else
         {
            osc_write_log_error("Initializing diagnostic protocol", "Node not found");
            s32_Retval = C_CONFIG;
         }

         if (s32_Retval != C_NO_ERR)
         {
            break;
         }
      }
   }
   else
   {
      osc_write_log_error("Initializing diagnostic protocol",
                          "Inconsistent number of diagnostic protocols or node IDs installed");
      s32_Retval = C_CONFIG;
   }
   return s32_Retval;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Initialize data dealers

   \return
   C_NO_ERR Operation success
   C_CONFIG Invalid initialization
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_InitDataDealer(void)
{
   int32_t s32_Retval = C_NO_ERR;

   if (this->mc_DiagProtocols.size() == this->m_GetActiveNodeCount())
   {
      const C_PuiSvData * const pc_View = C_PuiSvHandler::h_GetInstance()->GetView(this->mu32_ViewIndex);

      if (pc_View != NULL)
      {
         this->mc_DataDealers.resize(this->mc_ActiveDiagNodes.size());
         for (uint32_t u32_DiagNodeCounter = 0U; u32_DiagNodeCounter  < this->mc_ActiveDiagNodes.size();
              ++u32_DiagNodeCounter)
         {
            // Get the original active node index
            const uint32_t u32_ActiveNode = this->mc_ActiveDiagNodes[u32_DiagNodeCounter];
            C_OscNode * const pc_Node =
               C_PuiSdHandler::h_GetInstance()->GetOscNode(this->mc_ActiveNodesIndexes[u32_ActiveNode]);
            if (pc_Node != NULL)
            {
               //Data dealer init
               this->mc_DataDealers[u32_DiagNodeCounter] =
                  new C_SyvComDataDealer(pc_Node, this->mc_ActiveNodesIndexes[u32_ActiveNode],
                                         this->mc_DiagProtocols[u32_ActiveNode]);
            }
            else
            {
               osc_write_log_error("Initializing data dealer", "Node not found");
               s32_Retval = C_CONFIG;
            }
         }
      }
      else
      {
         osc_write_log_error("Initializing data dealer", "Configured view invalid");
         s32_Retval = C_CONFIG;
      }
   }
   else
   {
      osc_write_log_error("Initializing data dealer", "Inconsistent number of diagnostic protocols installed");
      s32_Retval = C_CONFIG;
   }
   return s32_Retval;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Initialize the necessary routing configuration to start the routing for diagnosis

   Prepares all active nodes with its routing configurations if necessary
   Three different types of routing:
   - openSYDE routing for a openSYDE server
   - legacy routing for a KEFEX server
   - legacy routing for a KEFEX server after openSYDE routing to a openSYDE server

   \param[in]   orc_ErrorDetails       Details for current error
   \param[out]  orc_ErrorActiveNodes   All active node indexes of nodes which can not be reached

   \return
   C_NO_ERR Operation success
   C_CONFIG Invalid initialization
   C_WARN      Error response
   C_BUSY      Connection to at least one server failed
   C_COM       Communication problem
   C_TIMEOUT   Expected response not received within timeout
   C_RD_WR     Unexpected content in response
   C_NOACT     At least one node does not support Ethernet to Ethernet routing
   C_CHECKSUM  Security related error (something went wrong while handshaking with the server)
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_StartRoutingDiag(QString & orc_ErrorDetails, std::set<uint32_t> & orc_ErrorActiveNodes)
{
   int32_t s32_Return = C_NO_ERR;
   uint32_t u32_DiagNodeCounter;
   uint32_t u32_ErrorActiveNodeIndex;

   // Start IP to IP routing for all nodes which need it
   for (u32_DiagNodeCounter = 0U; u32_DiagNodeCounter < this->mc_ActiveDiagNodes.size(); ++u32_DiagNodeCounter)
   {
      // Get the original active node index
      const uint32_t u32_ActiveNode = this->mc_ActiveDiagNodes[u32_DiagNodeCounter];
      s32_Return = this->m_StartRoutingIp2Ip(u32_ActiveNode, &u32_ErrorActiveNodeIndex);

      if (s32_Return != C_NO_ERR)
      {
         m_GetRoutingErrorDetails(orc_ErrorDetails, orc_ErrorActiveNodes,
                                  u32_ActiveNode, u32_ErrorActiveNodeIndex);

         break;
      }
   }

   if (s32_Return == C_NO_ERR)
   {
      // Search nodes which needs routing
      for (u32_DiagNodeCounter = 0U; u32_DiagNodeCounter < this->mc_ActiveDiagNodes.size();
           ++u32_DiagNodeCounter)
      {
         // Get the original active node index
         const uint32_t u32_ActiveNode = this->mc_ActiveDiagNodes[u32_DiagNodeCounter];
         const C_OscNode * const pc_Node =
            C_PuiSdHandler::h_GetInstance()->GetOscNodeConst(this->mc_ActiveNodesIndexes[u32_ActiveNode]);

         tgl_assert(pc_Node != NULL);
         if (pc_Node != NULL)
         {
            s32_Return = this->m_StartRouting(u32_ActiveNode, &u32_ErrorActiveNodeIndex);

            tgl_assert(pc_Node->pc_DeviceDefinition != NULL);
            // Reconnect is only supported by openSYDE nodes
            if ((pc_Node->c_Properties.e_DiagnosticServer == C_OscNodeProperties::eDS_OPEN_SYDE) &&
                (s32_Return == C_NO_ERR) &&
                (this->GetClientId().u8_BusIdentifier == this->mc_ServerIds[u32_ActiveNode].u8_BusIdentifier))
            {
               s32_Return = this->ReConnectNode(this->mc_ServerIds[u32_ActiveNode]);
            }

            if (s32_Return != C_NO_ERR)
            {
               m_GetRoutingErrorDetails(orc_ErrorDetails, orc_ErrorActiveNodes,
                                        u32_ActiveNode, u32_ErrorActiveNodeIndex);
               break;
            }
         }
      }
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Starts the diagnose servers

   Calling the verify function to start all diag servers.

   \param[in,out]  orc_ErrorDetails    Details for current error

   \return
   C_NO_ERR   request sent, positive response received
   C_TIMEOUT  expected response not received within timeout
   C_NOACT    could not send request (e.g. Tx buffer full)
   C_CONFIG   pre-requisites not correct; e.g. driver not initialized
   C_WARN     error response
   C_RD_WR    malformed protocol response
   C_DEFAULT  checksum of datapool does not match
              Datapool with the name orc_DatapoolName does not exist on the server
   C_COM      communication driver reported error
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_StartDiagServers(QString & orc_ErrorDetails)
{
   int32_t s32_Retval = C_NO_ERR;

   this->mc_ReadDatapoolMetadata.clear();

   if ((this->mq_Initialized == true) && (this->mc_DiagProtocols.size() > 0UL))
   {
      uint32_t u32_DiagNodeCounter;

      this->mc_ReadDatapoolMetadata.resize(this->mc_ActiveDiagNodes.size());

      for (u32_DiagNodeCounter = 0U; u32_DiagNodeCounter < this->mc_ActiveDiagNodes.size(); ++u32_DiagNodeCounter)
      {
         // Get the original active node index
         const uint32_t u32_ActiveNode = this->mc_ActiveDiagNodes[u32_DiagNodeCounter];
         // Check only if Datapool of node is really used
         if (this->mc_DiagNodesWithElements.find(this->mc_ActiveNodesIndexes[u32_ActiveNode]) !=
             this->mc_DiagNodesWithElements.end())
         {
            const C_OscNode * const pc_Node =
               C_PuiSdHandler::h_GetInstance()->GetOscNodeConst(this->mc_ActiveNodesIndexes[u32_ActiveNode]);

            if (pc_Node != NULL)
            {
               if (pc_Node->c_Properties.e_DiagnosticServer == C_OscNodeProperties::eDS_KEFEX)
               {
                  // Activate when supporting Kefex on dashboard
                  /*
                  uint8_t u8_DataPoolIndex;
                  uint16_t u16_NumberOfDataPoolElements;
                  uint16_t u16_DataPoolVersion;

                  u8_DataPoolIndex = 0U;
                  u16_NumberOfDataPoolElements = 0x0049U;
                  u16_DataPoolVersion = 0x0000U;
                  u32_DataPoolChecksum = 0xC4CBU;
                  s32_Return = this->mc_DiagProtocols[u32_Counter]->DataPoolVerify(u8_DataPoolIndex,
                                                                                   u16_NumberOfDataPoolElements,
                                                                                   u16_DataPoolVersion,
                                                                                   u32_DataPoolChecksum,
                                                                                   q_Match);

                  if ((s32_Return != C_NO_ERR) || (q_Match == false))
                  {
                     if ((s32_Return == C_NO_ERR) && (q_Match == false))
                     {
                        stw::scl::C_SclString c_Error;
                        c_Error.PrintFormatted("Datapool verify failed between client and server. Node: %s " \
                                               "Datapool: %s", pc_Node->c_Properties.c_Name.c_str(),
                                               pc_Node->c_DataPools[u8_DataPoolIndex].c_Name.c_str());
                        osc_write_log_error("Starting diagnostics", c_Error);
                        // Datapool checksum does not match
                        s32_Retval = C_DEFAULT;
                     }
                     else
                     {
                        s32_Retval = s32_Return;
                     }
                     if (s32_Retval != C_NO_ERR)
                     {
                        orc_ErrorDetails += static_cast<QString>("- ") + pc_Node->c_Properties.c_Name.c_str() +
                                            ", Datapool: \"" +
                                            pc_Node->c_DataPools[u8_DataPoolIndex].c_Name.c_str() + "\"\n";
                     }
                  }
                  */
               }
               else
               {
                  int32_t s32_Return;

                  // Get all Datapool names on node to create the mapping
                  s32_Return = this->m_GetAllDatapoolMetadata(u32_DiagNodeCounter, orc_ErrorDetails);

                  if (s32_Return == C_NO_ERR)
                  {
                     // Verify all used Datapools for checksum and version
                     s32_Return = this->m_CheckOsyDatapoolsAndCreateMapping(u32_DiagNodeCounter, orc_ErrorDetails);
                  }

                  if (s32_Return != C_NO_ERR)
                  {
                     // Do not overwrite previous errors with C_NO_ERR
                     s32_Retval = s32_Return;
                  }
               }
            }
            else
            {
               s32_Retval = C_CONFIG;
            }
         }
      }
   }
   else if ((this->mq_Initialized == true) &&
            (this->mc_DiagProtocols.size() == 0U) &&
            (this->mc_ActiveDiagNodes.size() == 0U))
   {
      // Special case: No error. No connectable nodes, but nodes third party nodes could be active
      s32_Retval = C_NO_ERR;
   }
   else
   {
      s32_Retval = C_CONFIG;
   }

   return s32_Retval;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Get all Datapool meta data for specific openSYDE node

   All Datapool names will be read from the node and tried

   \param[in]      ou32_ActiveDiagNodeIndex   Active diag node index (mc_ActiveDiagNodes)
   \param[in,out]  orc_ErrorDetails           Details for current error

   \return
   C_NO_ERR   Datapool metadata were read successfully
   C_TIMEOUT  expected response not received within timeout
   C_NOACT    could not send protocol request
   C_CONFIG   CAN dispatcher not installed
   C_WARN     error response
   C_COM      communication driver reported error
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_GetAllDatapoolMetadata(const uint32_t ou32_ActiveDiagNodeIndex,
                                                     QString & orc_ErrorDetails)
{
   uint32_t u32_ItDataPool;
   int32_t s32_Return = C_NO_ERR;
   // Get the original active node index
   const uint32_t u32_ActiveNode = this->mc_ActiveDiagNodes[ou32_ActiveDiagNodeIndex];

   for (u32_ItDataPool = 0U; u32_ItDataPool < C_OscNode::hu32_MAX_NUMBER_OF_DATA_POOLS_PER_NODE; ++u32_ItDataPool)
   {
      uint8_t u8_ErrorCode;
      C_OscProtocolDriverOsy::C_DataPoolMetaData c_Metadata;

      // Get meta data
      s32_Return = this->mc_DiagProtocols[u32_ActiveNode]->DataPoolReadMetaData(
         static_cast<uint8_t>(u32_ItDataPool),
         c_Metadata.au8_Version, c_Metadata.c_Name, &u8_ErrorCode);

      if (s32_Return == C_NO_ERR)
      {
         // Datapool exists and metadata are available
         this->mc_ReadDatapoolMetadata[ou32_ActiveDiagNodeIndex].push_back(c_Metadata);
      }
      else
      {
         QString c_ErrorReason;
         if (s32_Return == C_WARN)
         {
            // Error response
            if (u8_ErrorCode == C_OscProtocolDriverOsy::hu8_NR_CODE_REQUEST_OUT_OF_RANGE)
            {
               // Range reached. No error, no further Datapools available on node
               s32_Return = C_NO_ERR;
            }
            else
            {
               c_ErrorReason = "The read of the Datapool meta data"
                               " failed with error " +
                               static_cast<QString>(C_OscLoggingHandler::h_StwError(s32_Return).c_str()) +
                               " and negative response code: " + QString::number(u8_ErrorCode);
            }
         }
         else
         {
            // Service error
            c_ErrorReason = "The read of the Datapool meta data"
                            " failed with error " +
                            static_cast<QString>(C_OscLoggingHandler::h_StwError(s32_Return).c_str());
         }

         if (c_ErrorReason != "")
         {
            const uint32_t u32_NodeIndex = this->mc_ActiveNodesIndexes[u32_ActiveNode];
            const C_OscNode * const pc_Node =
               C_PuiSdHandler::h_GetInstance()->GetOscNodeConst(u32_NodeIndex);
            if (pc_Node != NULL)
            {
               stw::scl::C_SclString c_Error;
               c_Error.PrintFormatted("Datapool verify failed between client and node %s. " \
                                      "Reason: %s",
                                      pc_Node->c_Properties.c_Name.c_str(),
                                      c_ErrorReason.toStdString().c_str());
               osc_write_log_error("Starting diagnostics", c_Error);

               //Translation: 1=Node name, 2=List of Datapool names
               orc_ErrorDetails += static_cast<QString>(C_GtGetText::h_GetText("- %1: %2\n")).arg(
                  pc_Node->c_Properties.c_Name.c_str()).arg("\n   " + c_ErrorReason);
            }
         }

         break;
      }
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Check of openSYDE Datapools

   Each Datapool will be checked for matching name, version and CRC

   m_GetAllDatapoolMetadata must be called before calling m_CheckDatapoolsAndCreateMapping

   \param[in]      ou32_ActiveDiagNodeIndex  Active diag node index (mc_ActiveDiagNodes)
   \param[in,out]  orc_ErrorDetails          Details for current error

   \return
   C_NO_ERR   Datapools are as expected
   C_TIMEOUT  expected response not received within timeout
   C_NOACT    could not send request (e.g. Tx buffer full)
   C_CONFIG   pre-requisites not correct; e.g. driver not initialized, node or view invalid
   C_RD_WR    malformed protocol response
   C_DEFAULT  Checksum or version of Datapool does not match
              Datapool with the name orc_DatapoolName does not exist on the server
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_CheckOsyDatapoolsAndCreateMapping(const uint32_t ou32_ActiveDiagNodeIndex,
                                                                QString & orc_ErrorDetails)
{
   int32_t s32_Retval = C_CONFIG;
   QString c_DataPoolErrorString = "";
   // Get the original active node index
   const uint32_t u32_ActiveNode = this->mc_ActiveDiagNodes[ou32_ActiveDiagNodeIndex];

   stw::opensyde_core::C_OscDiagProtocolOsy * const pc_Protocol =
      dynamic_cast<C_OscDiagProtocolOsy *>(this->mc_DiagProtocols[u32_ActiveNode]);
   const uint32_t u32_NodeIndex = this->mc_ActiveNodesIndexes[u32_ActiveNode];
   const C_PuiSvData * const pc_View = C_PuiSvHandler::h_GetInstance()->GetView(this->mu32_ViewIndex);
   const C_OscNode * const pc_Node =
      C_PuiSdHandler::h_GetInstance()->GetOscNodeConst(u32_NodeIndex);

   if ((pc_View != NULL) &&
       (pc_Node != NULL) &&
       (pc_Protocol != NULL))
   {
      // Get all registered Datapool elements for comparing. Check only used Datapools
      std::set<C_OscNodeDataPoolListElementId> c_RegisteredElementIds;
      pc_View->GetAllRegisteredDashboardElements(c_RegisteredElementIds);
      std::map<uint8_t, uint8_t> c_DatapoolMapping;

      s32_Retval = C_NO_ERR;

      for (uint32_t u32_ItDataPool = 0; u32_ItDataPool < pc_Node->c_DataPools.size(); ++u32_ItDataPool)
      {
         std::set<C_OscNodeDataPoolListElementId>::const_iterator c_ItElement;
         bool q_DatapoolRelevant = false;

         // Search for a usage of the Datapool
         for (c_ItElement = c_RegisteredElementIds.begin(); c_ItElement != c_RegisteredElementIds.end(); ++c_ItElement)
         {
            if (((*c_ItElement).u32_NodeIndex == u32_NodeIndex) &&
                ((*c_ItElement).u32_DataPoolIndex == u32_ItDataPool))
            {
               q_DatapoolRelevant = true;
               break;
            }
         }

         if (q_DatapoolRelevant == true)
         {
            // Datapool is used on dashboard
            const C_OscNodeDataPool & rc_Datapool = pc_Node->c_DataPools[u32_ItDataPool];
            int32_t s32_Return;
            QString c_ErrorReason = "";
            uint32_t u32_ServerDatapoolIndex = 0U;
            C_OscProtocolDriverOsy::C_DataPoolMetaData c_ServerMetadata;

            // Get metadata
            s32_Return = this->m_GetReadDatapoolMetadata(ou32_ActiveDiagNodeIndex,
                                                         rc_Datapool.c_Name,
                                                         u32_ServerDatapoolIndex,
                                                         c_ServerMetadata);

            // Compare metadata with already read metadata
            if (s32_Return == C_NO_ERR)
            {
               // Check name if string is not empty. Empty string in case of not supported data by protocol
               if ((c_ServerMetadata.c_Name != "") &&
                   (c_ServerMetadata.c_Name != rc_Datapool.c_Name))
               {
                  // Name does not match
                  c_ErrorReason = "The name of Datapool does not match (Client: " +
                                  static_cast<QString>(rc_Datapool.c_Name.c_str()) +
                                  ", Server: " + static_cast<QString>(c_ServerMetadata.c_Name.c_str()) + ").";

                  s32_Return = C_DEFAULT;
               }
               // Check version
               else if (memcmp(rc_Datapool.au8_Version, c_ServerMetadata.au8_Version,
                               sizeof(c_ServerMetadata.au8_Version)) != 0)
               {
                  const QString c_VersionServer = static_cast<QString>("v%1.%2r%3").
                                                  arg(c_ServerMetadata.au8_Version[0], 2, 10, QChar('0')).
                                                  arg(c_ServerMetadata.au8_Version[1], 2, 10, QChar('0')).
                                                  arg(c_ServerMetadata.au8_Version[2], 2, 10, QChar('0'));
                  const QString c_VersionClient = static_cast<QString>("v%1.%2r%3").
                                                  arg(rc_Datapool.au8_Version[0], 2, 10, QChar('0')).
                                                  arg(rc_Datapool.au8_Version[1], 2, 10, QChar('0')).
                                                  arg(rc_Datapool.au8_Version[2], 2, 10, QChar('0'));

                  // Version does not match
                  c_ErrorReason = "The version of Datapool " + static_cast<QString>(rc_Datapool.c_Name.c_str()) +
                                  " does not match (Client: " + c_VersionClient +
                                  ", Server: " + c_VersionServer + ").";

                  s32_Return = C_DEFAULT;
               }
               else
               {
                  bool q_Match = false;
                  s32_Return = C_SyvComDriverDiag::mh_HandleDatapoolCrcVerification(rc_Datapool, *pc_Protocol,
                                                                                    u32_ServerDatapoolIndex,
                                                                                    q_Match, c_ErrorReason);

                  if (s32_Return == C_NO_ERR)
                  {
                     if (q_Match == false)
                     {
                        // Checksum does not match
                        c_ErrorReason = "The checksum of Datapool " + static_cast<QString>(rc_Datapool.c_Name.c_str()) +
                                        " does not match.";
                        s32_Return = C_DEFAULT;
                     }
                     else
                     {
                        // Datapool is fine. Add to mapping
                        c_DatapoolMapping[static_cast<uint8_t>(u32_ItDataPool)] =
                           static_cast<uint8_t>(u32_ServerDatapoolIndex);

                        // Log the registered mapping
                        if (u32_ItDataPool == u32_ServerDatapoolIndex)
                        {
                           osc_write_log_info("Starting diagnostics",
                                              "No mapping for Datapool \"" + rc_Datapool.c_Name +
                                              "\" necessary"
                                              " (Datapool index: " + C_SclString::IntToStr(u32_ItDataPool) + ").");
                        }
                        else
                        {
                           osc_write_log_info("Starting diagnostics",
                                              "A mapping for Datapool \"" + rc_Datapool.c_Name +
                                              "\" is necessary"
                                              " (Datapool index on client: " + C_SclString::IntToStr(u32_ItDataPool) +
                                              " ;Datapool index on server: " +
                                              C_SclString::IntToStr(u32_ServerDatapoolIndex) + ").");
                        }
                     }
                  }
               }
            }
            else
            {
               // Special case: Datapool with this name does not exist
               c_ErrorReason = "The Datapool " + static_cast<QString>(rc_Datapool.c_Name.c_str()) +
                               " does not exist on the server.";
               s32_Return = C_DEFAULT;
            }

            if (s32_Return != C_NO_ERR)
            {
               // Verify failed
               stw::scl::C_SclString c_Error;
               c_Error.PrintFormatted("Datapool verify failed between client and node %s. " \
                                      "Reason: %s",
                                      pc_Node->c_Properties.c_Name.c_str(),
                                      c_ErrorReason.toStdString().c_str());
               osc_write_log_error("Starting diagnostics", c_Error);

               c_DataPoolErrorString += "\n   ";
               //Translation: 1=Datapool name
               c_DataPoolErrorString += c_ErrorReason;
               s32_Retval = s32_Return;
            }
            else
            {
               stw::scl::C_SclString c_Text;
               c_Text.PrintFormatted("Datapool verified. Node: %s " \
                                     "Datapool: %s", pc_Node->c_Properties.c_Name.c_str(), rc_Datapool.c_Name.c_str());
               osc_write_log_info("Starting diagnostics", c_Text);
            }
         }
      }

      if (s32_Retval == C_NO_ERR)
      {
         // Register the mapping
         pc_Protocol->RegisterDataPoolMapping(c_DatapoolMapping);
      }
      else
      {
         //Translation: 1=Node name, 2=List of Datapool names
         orc_ErrorDetails += static_cast<QString>(C_GtGetText::h_GetText("- %1: %2\n")).arg(
            pc_Node->c_Properties.c_Name.c_str()).arg(c_DataPoolErrorString);
      }
   }
   else
   {
      osc_write_log_error("Starting diagnostics", "Error on starting: Node or view invalid.");
   }

   return s32_Retval;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Gets the Datapool metadata and its index on the server with a specific Datapool name

   \param[in]   ou32_ActiveDiagNodeIndex     Active diag node index (mc_ActiveDiagNodes)
   \param[in]   orc_DatapoolName             Searched Datapool name
   \param[out]  oru32_ServerDatapoolIndex    Index of Datapool on server (only valid when return value is C_NO_ERR)
   \param[out]  orc_Metadata                 Metadata of Datapool on server (only valid when return value is C_NO_ERR)

   \retval   C_NO_ERR   Datapool was found
   \retval   C_RANGE    Datapool with the name orc_DatapoolName does not exist on the server
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_GetReadDatapoolMetadata(const uint32_t ou32_ActiveDiagNodeIndex,
                                                      const C_SclString & orc_DatapoolName,
                                                      uint32_t & oru32_ServerDatapoolIndex,
                                                      C_OscProtocolDriverOsy::C_DataPoolMetaData & orc_Metadata) const
{
   int32_t s32_Return = C_RANGE;
   const std::list<C_OscProtocolDriverOsy::C_DataPoolMetaData> & rc_NodeDatapoolsMetadata =
      this->mc_ReadDatapoolMetadata[ou32_ActiveDiagNodeIndex];

   std::list<C_OscProtocolDriverOsy::C_DataPoolMetaData>::const_iterator c_ItMetadata;

   oru32_ServerDatapoolIndex = 0U;

   for (c_ItMetadata = rc_NodeDatapoolsMetadata.begin(); c_ItMetadata != rc_NodeDatapoolsMetadata.end();
        ++c_ItMetadata)
   {
      const C_OscProtocolDriverOsy::C_DataPoolMetaData & rc_Metadata = *c_ItMetadata;

      if (orc_DatapoolName == rc_Metadata.c_Name)
      {
         // Datapool found
         orc_Metadata = rc_Metadata;
         s32_Return = C_NO_ERR;
         break;
      }

      // Next one...
      ++oru32_ServerDatapoolIndex;
   }

   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Get index of node in list of diag active nodes

   If no active diag node is found that matches the passed absolute index the function will fail with an assertion.

   \param[in]     ou32_NodeIndex    absolute index of node within system description
   \param[out]    opq_Found         Optional flag if server id was found

   \return   index of node within list of active diag nodes
*/
//----------------------------------------------------------------------------------------------------------------------
uint32_t C_SyvComDriverDiag::m_GetActiveDiagIndex(const uint32_t ou32_NodeIndex, bool * const opq_Found) const
{
   uint32_t u32_DiagNodeIndex = 0U;
   bool q_Found = false;
   const uint32_t u32_ActiveIndex = this->m_GetActiveIndex(ou32_NodeIndex, &q_Found);

   if (q_Found == true)
   {
      // Original node active index found, now searching the active diag node index
      q_Found = false;

      for (u32_DiagNodeIndex = 0U; u32_DiagNodeIndex < this->mc_ActiveDiagNodes.size(); ++u32_DiagNodeIndex)
      {
         if (this->mc_ActiveDiagNodes[u32_DiagNodeIndex] == u32_ActiveIndex)
         {
            q_Found = true;
            break;
         }
      }
   }

   if (opq_Found != NULL)
   {
      *opq_Found = q_Found;
   }
   else
   {
      tgl_assert(q_Found == true);
   }

   return u32_DiagNodeIndex;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Handle datapool crc verification

   \param[in]      orc_Datapool              Datapool
   \param[in,out]  orc_Protocol              Protocol
   \param[in]      ou32_ServerDatapoolIndex  Server datapool index
   \param[out]     orq_Match                 Match
   \param[in,out]  orc_ErrorReason           Error reason

   \return
   C_NO_ERR   request sent, positive response received
   C_TIMEOUT  expected response not received within timeout
   C_NOACT    could not put request in Tx queue ...
   C_CONFIG   no transport protocol installed
   C_WARN     error response
   C_RD_WR    unexpected content in response (here: wrong data pool index)
   C_COM      communication driver reported error
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::mh_HandleDatapoolCrcVerification(const C_OscNodeDataPool & orc_Datapool,
                                                             stw::opensyde_core::C_OscDiagProtocolOsy & orc_Protocol,
                                                             const uint32_t ou32_ServerDatapoolIndex, bool & orq_Match,
                                                             QString & orc_ErrorReason)
{
   int32_t s32_Return = C_SyvComDriverDiag::mh_DoDatapoolCrcVerification(orc_Datapool, orc_Protocol,
                                                                         ou32_ServerDatapoolIndex, true,
                                                                         orq_Match, orc_ErrorReason);

   if (s32_Return == C_NO_ERR)
   {
      if (orq_Match == false)
      {
         if ((orc_Datapool.e_Type != C_OscNodeDataPool::eNVM) &&
             (orc_Datapool.e_Type != C_OscNodeDataPool::eHALC_NVM))
         {
            osc_write_log_info("connect", "CRC mismatch, trying V1 compatibility CRC with adapted default values ...");
            s32_Return = C_SyvComDriverDiag::mh_DoDatapoolCrcVerification(orc_Datapool, orc_Protocol,
                                                                          ou32_ServerDatapoolIndex, false,
                                                                          orq_Match, orc_ErrorReason);
         }
      }
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief  Do datapool crc verification

   \param[in]      orc_Datapool              Datapool
   \param[in,out]  orc_Protocol              Protocol
   \param[in]      ou32_ServerDatapoolIndex  Server datapool index
   \param[in]      oq_UseGeneratedVariant    Use generated variant
   \param[out]     orq_Match                 Match
   \param[in,out]  orc_ErrorReason           Error reason

   \return
   C_NO_ERR   request sent, positive response received
   C_TIMEOUT  expected response not received within timeout
   C_NOACT    could not put request in Tx queue ...
   C_CONFIG   no transport protocol installed
   C_WARN     error response
   C_RD_WR    unexpected content in response (here: wrong data pool index)
   C_COM      communication driver reported error
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::mh_DoDatapoolCrcVerification(const C_OscNodeDataPool & orc_Datapool,
                                                         C_OscDiagProtocolOsy & orc_Protocol,
                                                         const uint32_t ou32_ServerDatapoolIndex,
                                                         const bool oq_UseGeneratedVariant, bool & orq_Match,
                                                         QString & orc_ErrorReason)
{
   int32_t s32_Return;
   uint32_t u32_DataPoolChecksum = 0U;

   // Check checksum
   if (oq_UseGeneratedVariant)
   {
      orc_Datapool.CalcGeneratedDefinitionHash(u32_DataPoolChecksum);
   }
   else
   {
      orc_Datapool.CalcDefinitionHash(u32_DataPoolChecksum,
                                      C_OscNodeDataPool::eCT_NON_NVM_DEFAULT_COMPAT_V1);
   }

   s32_Return = orc_Protocol.DataPoolVerify(
      static_cast<uint8_t>(ou32_ServerDatapoolIndex),
      0U, //N/A for openSYDE protocol
      0U, //N/A for openSYDE protocol
      u32_DataPoolChecksum,
      orq_Match);
   if (s32_Return != C_NO_ERR)
   {
      // Service error
      orc_ErrorReason = "The verify of the Datapool " + static_cast<QString>(orc_Datapool.c_Name.c_str()) +
                        " failed with error " +
                        static_cast<QString>(C_OscLoggingHandler::h_StwError(s32_Return).c_str());
   }
   return s32_Return;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Calls the cycle functions of all protocols

   \return
   C_NO_ERR    no problems
*/
//----------------------------------------------------------------------------------------------------------------------
int32_t C_SyvComDriverDiag::m_Cycle(void)
{
   for (uint32_t u32_Counter = 0U; u32_Counter < this->mc_DiagProtocols.size(); ++u32_Counter)
   {
      const int32_t s32_Return = this->mc_DiagProtocols[u32_Counter]->Cycle();
      if (s32_Return != C_NO_ERR)
      {
         // TODO Errorhandling
         break;
      }
   }

   return C_NO_ERR;
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Function for continuous calling by thread.
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvComDriverDiag::mh_ThreadFunc(void * const opv_Instance)
{
   //lint -e{9079}  This class is the only one which registers itself at the caller of this function. It must match.
   C_SyvComDriverDiag * const pc_ComDriver = reinterpret_cast<C_SyvComDriverDiag *>(opv_Instance);

   tgl_assert(pc_ComDriver != NULL);
   if (pc_ComDriver != NULL)
   {
      pc_ComDriver->m_ThreadFunc();
   }
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Function for continuous calling by thread.
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvComDriverDiag::m_ThreadFunc(void)
{
   static uint32_t hu32_LastSentTesterPresent = 0U;
   static uint32_t hu32_LastSentDebugTest = 0U;
   uint32_t u32_CurrentTime;

   if (hu32_LastSentTesterPresent == 0U)
   {
      // Initialize the time scheduling
      hu32_LastSentTesterPresent = stw::tgl::TglGetTickCount();
      hu32_LastSentDebugTest = hu32_LastSentTesterPresent;
   }

   u32_CurrentTime = stw::tgl::TglGetTickCount();

   if (u32_CurrentTime > (hu32_LastSentTesterPresent + 1000U))
   {
      hu32_LastSentTesterPresent = u32_CurrentTime;
      this->SendTesterPresent(this->mc_ActiveCommunicatingNodes);
   }
   else if (u32_CurrentTime > (hu32_LastSentDebugTest + 200U))
   {
      // For testing
      hu32_LastSentDebugTest = u32_CurrentTime;
   }
   else
   {
      // nothing to do
   }

   // Handle datapool events
   this->m_Cycle();

   // Handle CAN message / com signal input
   this->DistributeMessages();

   //rescind CPU time to other threads ...
   stw::tgl::TglSleep(1);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Handle polling finished event
*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvComDriverDiag::m_HandlePollingFinished(void)
{
   int32_t s32_Result;
   uint8_t u8_Nrc;

   tgl_assert(this->mc_PollingThread.GetResults(s32_Result) == C_NO_ERR);
   tgl_assert(this->mc_PollingThread.GetNegativeResponseCode(u8_Nrc) == C_NO_ERR);
   //Start with next one
   this->mc_PollingThread.AcceptNextRequest();
   Q_EMIT this->SigPollingFinished(s32_Result, u8_Nrc);
}

//----------------------------------------------------------------------------------------------------------------------
/*! \brief   Reports error details in case of an routing error with check for duplicate entries

   \param[in,out]   orc_ErrorDetails          Details for current error
   \param[out]      orc_ErrorActiveNodes      All active node indexes of nodes which can not be reached
   \param[in]     ou32_ActiveNode             active node index of vector mc_ActiveNodesIndexes which was the target
   \param[in]     ou32_ErrorActiveNodeIndex   active node index which caused the error on starting routing

*/
//----------------------------------------------------------------------------------------------------------------------
void C_SyvComDriverDiag::m_GetRoutingErrorDetails(QString & orc_ErrorDetails, std::set<uint32_t> & orc_ErrorActiveNodes,
                                                  const uint32_t ou32_ActiveNode,
                                                  const uint32_t ou32_ErrorActiveNodeIndex) const
{
   // Check if both nodes are already marked as error to avoid duplicates in the orc_ErrorDetails string
   // Duplicates could occur dependent of the routing order
   if (orc_ErrorActiveNodes.find(ou32_ActiveNode) == orc_ErrorActiveNodes.end())
   {
      // Add the "target" node as error target
      const C_OscNode * const pc_Node =
         C_PuiSdHandler::h_GetInstance()->GetOscNodeConst(this->mc_ActiveNodesIndexes[ou32_ActiveNode]);

      orc_ErrorActiveNodes.insert(ou32_ActiveNode);
      tgl_assert(pc_Node != NULL);
      if (pc_Node != NULL)
      {
         orc_ErrorDetails += static_cast<QString>("\"") + pc_Node->c_Properties.c_Name.c_str() + "\"\n";
      }
   }
   if (orc_ErrorActiveNodes.find(ou32_ErrorActiveNodeIndex) == orc_ErrorActiveNodes.end())
   {
      // Add the "routing" node as error target
      const C_OscNode * const pc_Node =
         C_PuiSdHandler::h_GetInstance()->GetOscNodeConst(this->mc_ActiveNodesIndexes[ou32_ErrorActiveNodeIndex]);

      orc_ErrorActiveNodes.insert(ou32_ErrorActiveNodeIndex);
      tgl_assert(pc_Node != NULL);
      if (pc_Node != NULL)
      {
         orc_ErrorDetails += static_cast<QString>("\"") + pc_Node->c_Properties.c_Name.c_str() + "\"\n";
      }
   }
}
