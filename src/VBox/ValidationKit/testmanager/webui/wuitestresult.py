# -*- coding: utf-8 -*-
# $Id$

"""
Test Manager WUI - Test Results.
"""

__copyright__ = \
"""
Copyright (C) 2012-2015 Oracle Corporation

This file is part of VirtualBox Open Source Edition (OSE), as
available from http://www.virtualbox.org. This file is free software;
you can redistribute it and/or modify it under the terms of the GNU
General Public License (GPL) as published by the Free Software
Foundation, in version 2 as it comes in the "COPYING" file of the
VirtualBox OSE distribution. VirtualBox OSE is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL) only, as it comes in the "COPYING.CDDL" file of the
VirtualBox OSE distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.
"""
__version__ = "$Revision$"

# Python imports.

# Validation Kit imports.
from testmanager.webui.wuicontentbase   import WuiContentBase, WuiListContentBase, WuiHtmlBase, WuiTmLink, WuiLinkBase, \
                                               WuiSvnLink, WuiSvnLinkWithTooltip, WuiBuildLogLink, WuiRawHtml;
from testmanager.webui.wuimain          import WuiMain;
from testmanager.core.report            import ReportGraphModel;
from testmanager.core.testbox           import TestBoxData;
from testmanager.core.testcase          import TestCaseData;
from testmanager.core.testset           import TestSetData;
from testmanager.core.testgroup         import TestGroupData;
from testmanager.core.build             import BuildData;
from testmanager.core                   import db;
from testmanager                        import config;
from common                             import webutils, utils;


class WuiTestResult(WuiContentBase):
    """Display test case result"""

    def __init__(self, fnDPrint = None, oDisp = None):
        WuiContentBase.__init__(self, fnDPrint = fnDPrint, oDisp = oDisp);

        # Cyclic import hacks.
        from testmanager.webui.wuiadmin  import WuiAdmin;
        self.oWuiAdmin = WuiAdmin;

    def _toHtml(self, oObject):
        """Translate some object to HTML."""
        if isinstance(oObject, WuiHtmlBase):
            return oObject.toHtml();
        if db.isDbTimestamp(oObject):
            return webutils.escapeElem(self.formatTsShort(oObject));
        if db.isDbInterval(oObject):
            return webutils.escapeElem(self.formatIntervalShort(oObject));
        if utils.isString(oObject):
            return webutils.escapeElem(oObject);
        return webutils.escapeElem(str(oObject));

    def _htmlTable(self, aoTableContent):
        """Generate HTML code for table"""
        sHtml  = u'   <table class="tmtbl-testresult-details" width="100%%">\n';

        for aoSubRows in aoTableContent:
            if len(aoSubRows) == 0:
                continue; # Can happen if there is no testsuit.
            oCaption = aoSubRows[0];
            sHtml += u'    \n' \
                     u'    <tr class="tmtbl-result-details-caption">\n' \
                     u'      <td colspan="2">%s</td>\n' \
                     u'    </tr>\n' \
                % (self._toHtml(oCaption),);

            iRow = 0;
            for aoRow in aoSubRows[1:]:
                iRow  += 1;
                sHtml += u'    <tr class="%s">\n' % ('tmodd' if iRow & 1 else 'tmeven',);
                if len(aoRow) == 1:
                    sHtml += u'      <td class="tmtbl-result-details-subcaption" colspan="2">%s</td>\n' \
                           % (self._toHtml(aoRow[0]),);
                else:
                    sHtml += u'      <th scope="row">%s</th>\n' % (webutils.escapeElem(aoRow[0]),);
                    if len(aoRow) > 2:
                        sHtml += u'     <td>%s</td>\n' % (aoRow[2](aoRow[1]),);
                    else:
                        sHtml += u'     <td>%s</td>\n' % (self._toHtml(aoRow[1]),);
                sHtml += u'    </tr>\n';

        sHtml += u'   </table>\n';

        return sHtml

    def _highlightStatus(self, sStatus):
        """Return sStatus string surrounded by HTML highlight code """
        sTmp = '<font color=%s><b>%s</b></font>' \
            % ('red' if sStatus == 'failure' else 'green', webutils.escapeElem(sStatus.upper()))
        return sTmp

    def _anchorAndAppendBinaries(self, sBinaries, aoRows):
        """ Formats each binary (if any) into a row with a download link. """
        if sBinaries is not None:
            for sBinary in sBinaries.split(','):
                if not webutils.hasSchema(sBinary):
                    sBinary = config.g_ksBuildBinUrlPrefix + sBinary;
                aoRows.append([WuiLinkBase(webutils.getFilename(sBinary), sBinary, fBracketed = False),]);
        return aoRows;


    def _recursivelyGenerateEvents(self, oTestResult, sParentName, sLineage, iRow,
                                   iFailure, oTestSet, iDepth):     # pylint: disable=R0914
        """
        Recursively generate event table rows for the result set.

        oTestResult is an object of the type TestResultDataEx.
        """
        # Hack: Replace empty outer test result name with (pretty) command line.
        if iRow == 1:
            sName = '';
            sDisplayName = sParentName;
        else:
            sName = oTestResult.sName if sParentName == '' else '%s, %s' % (sParentName, oTestResult.sName,);
            sDisplayName = webutils.escapeElem(sName);

        # Format error count.
        sErrCnt = '';
        if oTestResult.cErrors > 0:
            sErrCnt = ' (1 error)' if oTestResult.cErrors == 1 else ' (%d errors)' % oTestResult.cErrors;

        # Format the include in graph checkboxes.
        sLineage += ':%u' % (oTestResult.idStrName,);
        sResultGraph  = '<input type="checkbox" name="%s" value="%s%s" title="Include result in graph."/>' \
                      % (WuiMain.ksParamReportSubjectIds, ReportGraphModel.ksTypeResult, sLineage,);
        sElapsedGraph = '';
        if oTestResult.tsElapsed is not None:
            sElapsedGraph = '<input type="checkbox" name="%s" value="%s%s" title="Include elapsed time in graph."/>' \
                          % ( WuiMain.ksParamReportSubjectIds, ReportGraphModel.ksTypeElapsed, sLineage);


        if    len(oTestResult.aoChildren) == 0 \
          and len(oTestResult.aoValues)   == 0 \
          and len(oTestResult.aoMsgs)     == 0 \
          and len(oTestResult.aoFiles)    == 0:
            # Leaf - single row.
            tsEvent = oTestResult.tsCreated;
            if oTestResult.tsElapsed is not None:
                tsEvent += oTestResult.tsElapsed;
            sHtml  = ' <tr class="%s tmtbl-events-leaf tmtbl-events-lvl%s tmstatusrow-%s">\n' \
                     '  <td>%s</td>\n' \
                     '  <td>%s</td>\n' \
                     '  <td>%s</td>\n' \
                     '  <td>%s</td>\n' \
                     '  <td colspan="2"%s>%s%s</td>\n' \
                     '  <td>%s</td>\n' \
                     ' </tr>\n' \
                   % ( 'tmodd' if iRow & 1 else 'tmeven', iDepth, oTestResult.enmStatus,
                       webutils.escapeElem(self.formatTsShort(tsEvent)),
                       sElapsedGraph,
                       webutils.escapeElem(self.formatIntervalShort(oTestResult.tsElapsed)) if oTestResult.tsElapsed is not None
                                           else '',
                       sDisplayName,
                       ' id="failure-%u"' % (iFailure,) if oTestResult.isFailure() else '',
                       webutils.escapeElem(oTestResult.enmStatus), webutils.escapeElem(sErrCnt),
                       sResultGraph );
            iRow += 1;
        else:
            # Multiple rows.
            sHtml  = ' <tr class="%s tmtbl-events-first tmtbl-events-lvl%s ">\n' \
                     '  <td>%s</td>\n' \
                     '  <td></td>\n' \
                     '  <td></td>\n' \
                     '  <td>%s</td>\n' \
                     '  <td colspan="2">%s</td>\n' \
                     '  <td></td>\n' \
                     ' </tr>\n' \
                   % ( 'tmodd' if iRow & 1 else 'tmeven', iDepth,
                       webutils.escapeElem(self.formatTsShort(oTestResult.tsCreated)), ## @todo more timeline stuff later.
                       sDisplayName,
                       'running' if oTestResult.tsElapsed is None else '', );
            iRow += 1;

            # Depth.
            for oChild in oTestResult.aoChildren:
                (sChildHtml, iRow, iFailure) = self._recursivelyGenerateEvents(oChild, sName, sLineage,
                                                                               iRow, iFailure, oTestSet, iDepth + 1);
                sHtml += sChildHtml;


            # Messages.
            for oMsg in oTestResult.aoMsgs:
                sHtml += ' <tr class="%s tmtbl-events-message tmtbl-events-lvl%s">\n' \
                         '  <td>%s</td>\n' \
                         '  <td></td>\n' \
                         '  <td></td>\n' \
                         '  <td colspan="3">%s: %s</td>\n' \
                         '  <td></td>\n' \
                         ' </tr>\n' \
                       % ( 'tmodd' if iRow & 1 else 'tmeven', iDepth,
                           webutils.escapeElem(self.formatTsShort(oMsg.tsCreated)),
                           webutils.escapeElem(oMsg.enmLevel),
                           webutils.escapeElem(oMsg.sMsg), );
                iRow += 1;

            # Values.
            for oValue in oTestResult.aoValues:
                sHtml += ' <tr class="%s tmtbl-events-value tmtbl-events-lvl%s">\n' \
                         '  <td>%s</td>\n' \
                         '  <td></td>\n' \
                         '  <td></td>\n' \
                         '  <td>%s</td>\n' \
                         '  <td class="tmtbl-events-number">%s</td>\n' \
                         '  <td class="tmtbl-events-unit">%s</td>\n' \
                         '  <td><input type="checkbox" name="%s" value="%s%s:%u" title="Include value in graph."></td>\n' \
                         ' </tr>\n' \
                       % ( 'tmodd' if iRow & 1 else 'tmeven', iDepth,
                           webutils.escapeElem(self.formatTsShort(oValue.tsCreated)),
                           webutils.escapeElem(oValue.sName),
                           utils.formatNumber(oValue.lValue).replace(' ', '&nbsp;'),
                           webutils.escapeElem(oValue.sUnit),
                           WuiMain.ksParamReportSubjectIds, ReportGraphModel.ksTypeValue, sLineage, oValue.idStrName, );
                iRow += 1;

            # Files.
            for oFile in oTestResult.aoFiles:
                if oFile.sMime in [ 'text/plain', ]:
                    aoLinks = [
                        WuiTmLink('%s (%s)' % (oFile.sFile, oFile.sKind), '',
                                  { self._oDisp.ksParamAction:        self._oDisp.ksActionViewLog,
                                    self._oDisp.ksParamLogSetId:      oTestSet.idTestSet,
                                    self._oDisp.ksParamLogFileId:     oFile.idTestResultFile, },
                                  sTitle = oFile.sDescription),
                        WuiTmLink('View Raw', '',
                                  { self._oDisp.ksParamAction:        self._oDisp.ksActionGetFile,
                                    self._oDisp.ksParamGetFileSetId:  oTestSet.idTestSet,
                                    self._oDisp.ksParamGetFileId:     oFile.idTestResultFile,
                                    self._oDisp.ksParamGetFileDownloadIt: False, },
                                  sTitle = oFile.sDescription),
                    ]
                else:
                    aoLinks = [
                        WuiTmLink('%s (%s)' % (oFile.sFile, oFile.sKind), '',
                                  { self._oDisp.ksParamAction:        self._oDisp.ksActionGetFile,
                                    self._oDisp.ksParamGetFileSetId:  oTestSet.idTestSet,
                                    self._oDisp.ksParamGetFileId:     oFile.idTestResultFile,
                                    self._oDisp.ksParamGetFileDownloadIt: False, },
                                  sTitle = oFile.sDescription),
                    ]
                aoLinks.append(WuiTmLink('Download', '',
                                         { self._oDisp.ksParamAction:        self._oDisp.ksActionGetFile,
                                           self._oDisp.ksParamGetFileSetId:  oTestSet.idTestSet,
                                           self._oDisp.ksParamGetFileId:     oFile.idTestResultFile,
                                           self._oDisp.ksParamGetFileDownloadIt: True, },
                                         sTitle = oFile.sDescription));

                sHtml += ' <tr class="%s tmtbl-events-file tmtbl-events-lvl%s">\n' \
                         '  <td></td>\n' \
                         '  <td>%s</td>\n' \
                         '  <td></td>\n' \
                         '  <td>%s</td>\n' \
                         '  <td></td>\n' \
                         '  <td></td>\n' \
                         '  <td></td>\n' \
                         ' </tr>\n' \
                       % ( 'tmodd' if iRow & 1 else 'tmeven', iDepth,
                           webutils.escapeElem(self.formatTsShort(oFile.tsCreated)),
                           '\n'.join(oLink.toHtml() for oLink in aoLinks),);
                iRow += 1;

            # Done?
            if oTestResult.tsElapsed is not None:
                sHtml += ' <tr class="%s tmtbl-events-final tmtbl-events-lvl%s tmstatusrow-%s">\n' \
                         '  <td>%s</td>\n' \
                         '  <td>%s</td>\n' \
                         '  <td>%s</td>\n' \
                         '  <td>%s</td>\n' \
                         '  <td colspan="2"%s>%s%s</td>\n' \
                         '  <td>%s</td>\n' \
                         ' </tr>\n' \
                       % ( 'tmodd' if iRow & 1 else 'tmeven', iDepth, oTestResult.enmStatus,
                           webutils.escapeElem(self.formatTsShort(oTestResult.tsCreated + oTestResult.tsElapsed)),
                           sElapsedGraph,
                           webutils.escapeElem(self.formatIntervalShort(oTestResult.tsElapsed)),
                           sDisplayName,
                           ' id="failure-%u"' % (iFailure,) if oTestResult.isFailure() else '',
                           webutils.escapeElem(oTestResult.enmStatus), webutils.escapeElem(sErrCnt),
                           sResultGraph);
                iRow += 1;

        if oTestResult.isFailure():
            iFailure += 1;

        return (sHtml, iRow, iFailure);

    def showTestCaseResultDetails(self,             # pylint: disable=R0914,R0915
                                  oTestResultTree,
                                  oTestSet,
                                  oBuildEx,
                                  oValidationKitEx,
                                  oTestBox,
                                  oTestGroup,
                                  oTestCaseEx,
                                  oTestVarEx):
        """Show detailed result"""
        def getTcDepsHtmlList(aoTestCaseData):
            """Get HTML <ul> list of Test Case name items"""
            if len(aoTestCaseData) > 0:
                sTmp = '<ul>'
                for oTestCaseData in aoTestCaseData:
                    sTmp += '<li>%s</li>' % (webutils.escapeElem(oTestCaseData.sName),);
                sTmp += '</ul>'
            else:
                sTmp = 'No items'
            return sTmp

        def getGrDepsHtmlList(aoGlobalResourceData):
            """Get HTML <ul> list of Global Resource name items"""
            if len(aoGlobalResourceData) > 0:
                sTmp = '<ul>'
                for oGlobalResourceData in aoGlobalResourceData:
                    sTmp += '<li>%s</li>' % (webutils.escapeElem(oGlobalResourceData.sName),);
                sTmp += '</ul>'
            else:
                sTmp = 'No items'
            return sTmp


        asHtml = []

        # Test result + test set details.
        aoResultRows = [
            WuiTmLink(oTestCaseEx.sName, self.oWuiAdmin.ksScriptName,
                      { self.oWuiAdmin.ksParamAction:         self.oWuiAdmin.ksActionTestCaseDetails,
                        TestCaseData.ksParam_idTestCase:      oTestCaseEx.idTestCase,
                        self.oWuiAdmin.ksParamEffectiveDate:  oTestSet.tsConfig, },
                      fBracketed = False),
        ];
        if oTestCaseEx.sDescription is not None and len(oTestCaseEx.sDescription) > 0:
            aoResultRows.append([oTestCaseEx.sDescription,]);
        aoResultRows.append([ 'Status:', WuiRawHtml('<span class="tmspan-status-%s">%s</span>'
                                                    % (oTestResultTree.enmStatus, oTestResultTree.enmStatus,))]);
        if oTestResultTree.cErrors > 0:
            aoResultRows.append(( 'Errors:',        oTestResultTree.cErrors ));
        aoResultRows.append([ 'Elapsed:',       oTestResultTree.tsElapsed ]);
        cSecCfgTimeout = oTestCaseEx.cSecTimeout if oTestVarEx.cSecTimeout is None else oTestVarEx.cSecTimeout;
        cSecEffTimeout = cSecCfgTimeout * oTestBox.pctScaleTimeout / 100;
        aoResultRows.append([ 'Timeout:',
                              '%s (%s sec)' % (utils.formatIntervalSeconds(cSecEffTimeout), cSecEffTimeout,) ]);
        if cSecEffTimeout != cSecCfgTimeout:
            aoResultRows.append([ 'Cfg Timeout:',
                                  '%s (%s sec)' % (utils.formatIntervalSeconds(cSecCfgTimeout), cSecCfgTimeout,) ]);
        aoResultRows += [
            ( 'Started:',       WuiTmLink(self.formatTsShort(oTestSet.tsCreated), WuiMain.ksScriptName,
                                          { WuiMain.ksParamAction:          WuiMain.ksActionResultsUnGrouped,
                                            WuiMain.ksParamEffectiveDate:   oTestSet.tsCreated,  },
                                          fBracketed = False) ),
        ];
        if oTestSet.tsDone is not None:
            aoResultRows += [ ( 'Done:',
                                WuiTmLink(self.formatTsShort(oTestSet.tsDone), WuiMain.ksScriptName,
                                          { WuiMain.ksParamAction:          WuiMain.ksActionResultsUnGrouped,
                                            WuiMain.ksParamEffectiveDate:   oTestSet.tsDone,  },
                                          fBracketed = False) ) ];
        else:
            aoResultRows += [( 'Done:',      'Still running...')];
        aoResultRows += [( 'Config:',        oTestSet.tsConfig )];
        if oTestVarEx.cGangMembers > 1:
            aoResultRows.append([ 'Member No:',    '#%s (of %s)' % (oTestSet.iGangMemberNo, oTestVarEx.cGangMembers) ]);

        aoResultRows += [
            ( 'Test Group:',    WuiTmLink(oTestGroup.sName, self.oWuiAdmin.ksScriptName,
                                          { self.oWuiAdmin.ksParamAction:         self.oWuiAdmin.ksActionTestGroupDetails,
                                            TestGroupData.ksParam_idTestGroup:    oTestGroup.idTestGroup,
                                            self.oWuiAdmin.ksParamEffectiveDate:  oTestSet.tsConfig,  },
                                          fBracketed = False) ),
        ];
        if oTestVarEx.sTestBoxReqExpr is not None:
            aoResultRows.append([ 'TestBox reqs:', oTestVarEx.sTestBoxReqExpr ]);
        elif oTestCaseEx.sTestBoxReqExpr is not None or oTestVarEx.sTestBoxReqExpr is not None:
            aoResultRows.append([ 'TestBox reqs:', oTestCaseEx.sTestBoxReqExpr ]);
        if oTestVarEx.sBuildReqExpr is not None:
            aoResultRows.append([ 'Build reqs:', oTestVarEx.sBuildReqExpr ]);
        elif oTestCaseEx.sBuildReqExpr is not None or oTestVarEx.sBuildReqExpr is not None:
            aoResultRows.append([ 'Build reqs:', oTestCaseEx.sBuildReqExpr ]);
        if oTestCaseEx.sValidationKitZips is not None and oTestCaseEx.sValidationKitZips != '@VALIDATIONKIT_ZIP@':
            aoResultRows.append([ 'Validation Kit:', oTestCaseEx.sValidationKitZips ]);
        if oTestCaseEx.aoDepTestCases is not None and len(oTestCaseEx.aoDepTestCases) > 0:
            aoResultRows.append([ 'Prereq. Test Cases:', oTestCaseEx.aoDepTestCases, getTcDepsHtmlList ]);
        if oTestCaseEx.aoDepGlobalResources is not None and len(oTestCaseEx.aoDepGlobalResources) > 0:
            aoResultRows.append([ 'Global Resources:', oTestCaseEx.aoDepGlobalResources, getGrDepsHtmlList ]);

        # Builds.
        aoBuildRows = [];
        if oBuildEx is not None:
            aoBuildRows += [
                WuiTmLink('Build', self.oWuiAdmin.ksScriptName,
                          { self.oWuiAdmin.ksParamAction:         self.oWuiAdmin.ksActionBuildDetails,
                            BuildData.ksParam_idBuild:            oBuildEx.idBuild,
                            self.oWuiAdmin.ksParamEffectiveDate:  oTestSet.tsCreated, },
                          fBracketed = False),
            ];
            self._anchorAndAppendBinaries(oBuildEx.sBinaries, aoBuildRows);
            aoBuildRows += [
                ( 'Revision:',                  WuiSvnLinkWithTooltip(oBuildEx.iRevision, oBuildEx.oCat.sRepository,
                                                                      fBracketed = False) ),
                ( 'Product:',                   oBuildEx.oCat.sProduct ),
                ( 'Branch:',                    oBuildEx.oCat.sBranch ),
                ( 'Type:',                      oBuildEx.oCat.sType ),
                ( 'Version:',                   oBuildEx.sVersion ),
                ( 'Created:',                   oBuildEx.tsCreated ),
            ];
            if oBuildEx.uidAuthor is not None:
                aoBuildRows += [ ( 'Author ID:', oBuildEx.uidAuthor ), ];
            if oBuildEx.sLogUrl is not None:
                aoBuildRows += [ ( 'Log:',       WuiBuildLogLink(oBuildEx.sLogUrl, fBracketed = False) ), ];

        aoValidationKitRows = [];
        if oValidationKitEx is not None:
            aoValidationKitRows += [
                WuiTmLink('Validation Kit', self.oWuiAdmin.ksScriptName,
                          { self.oWuiAdmin.ksParamAction:         self.oWuiAdmin.ksActionBuildDetails,
                            BuildData.ksParam_idBuild:            oValidationKitEx.idBuild,
                            self.oWuiAdmin.ksParamEffectiveDate:  oTestSet.tsCreated, },
                          fBracketed = False),
            ];
            self._anchorAndAppendBinaries(oValidationKitEx.sBinaries, aoValidationKitRows);
            aoValidationKitRows += [ ( 'Revision:',    WuiSvnLink(oValidationKitEx.iRevision, fBracketed = False) ) ];
            if oValidationKitEx.oCat.sProduct != 'VBox TestSuite':
                aoValidationKitRows += [ ( 'Product:', oValidationKitEx.oCat.sProduct ), ];
            if oValidationKitEx.oCat.sBranch != 'trunk':
                aoValidationKitRows += [ ( 'Product:', oValidationKitEx.oCat.sBranch ), ];
            if oValidationKitEx.oCat.sType != 'release':
                aoValidationKitRows += [ ( 'Type:',    oValidationKitEx.oCat.sType), ];
            if oValidationKitEx.sVersion != '0.0.0':
                aoValidationKitRows += [ ( 'Version:', oValidationKitEx.sVersion ), ];
            aoValidationKitRows += [
                ( 'Created:',                   oValidationKitEx.tsCreated ),
            ];
            if oValidationKitEx.uidAuthor is not None:
                aoValidationKitRows += [ ( 'Author ID:', oValidationKitEx.uidAuthor ), ];
            if oValidationKitEx.sLogUrl is not None:
                aoValidationKitRows += [ ( 'Log:', WuiBuildLogLink(oValidationKitEx.sLogUrl, fBracketed = False) ), ];

        # TestBox.
        aoTestBoxRows = [
            WuiTmLink(oTestBox.sName, self.oWuiAdmin.ksScriptName,
                      { self.oWuiAdmin.ksParamAction:     self.oWuiAdmin.ksActionTestBoxDetails,
                        TestBoxData.ksParam_idGenTestBox: oTestSet.idGenTestBox, },
                      fBracketed = False),
        ];
        if oTestBox.sDescription is not None and len(oTestBox.sDescription) > 0:
            aoTestBoxRows.append([oTestBox.sDescription, ]);
        aoTestBoxRows += [
            ( 'IP:',                       oTestBox.ip ),
            #( 'UUID:',                     oTestBox.uuidSystem ),
            #( 'Enabled:',                  oTestBox.fEnabled ),
            #( 'Lom Kind:',                 oTestBox.enmLomKind ),
            #( 'Lom IP:',                   oTestBox.ipLom ),
            ( 'OS/Arch:',                  '%s.%s' % (oTestBox.sOs, oTestBox.sCpuArch) ),
            ( 'OS Version:',               oTestBox.sOsVersion ),
            ( 'CPUs:',                     oTestBox.cCpus ),
        ];
        if oTestBox.sCpuName is not None:
            aoTestBoxRows.append(['CPU Name', oTestBox.sCpuName.replace('  ', ' ')]);
        if oTestBox.lCpuRevision is not None:
            # ASSUMING x86+AMD64 versioning scheme here.
            uFamily   = (oTestBox.lCpuRevision >> 24) & 0xff;
            uModel    = (oTestBox.lCpuRevision >>  8) & 0xffff;
            uStepping = oTestBox.lCpuRevision         & 0xff;
            aoTestBoxRows += [
                ( 'CPU Family',   '%u (%#x)' % ( uFamily,   uFamily, ) ),
                ( 'CPU Model',    '%u (%#x)' % ( uModel,    uModel, ) ),
                ( 'CPU Stepping', '%u (%#x)' % ( uStepping, uStepping, ) ),
            ];
        asFeatures = [ oTestBox.sCpuVendor, ];
        if oTestBox.fCpuHwVirt is True:         asFeatures.append(u'HW\u2011Virt');
        if oTestBox.fCpuNestedPaging is True:   asFeatures.append(u'Nested\u2011Paging');
        if oTestBox.fCpu64BitGuest is True:     asFeatures.append(u'64\u2011bit\u2011Guest');
        if oTestBox.fChipsetIoMmu is True:      asFeatures.append(u'I/O\u2011MMU');
        aoTestBoxRows += [
            ( 'Features:',                 u' '.join(asFeatures) ),
            ( 'RAM size:',                 '%s MB' % (oTestBox.cMbMemory,) ),
            ( 'Scratch Size:',             '%s MB' % (oTestBox.cMbScratch,) ),
            ( 'Scale Timeout:',            '%s%%' % (oTestBox.pctScaleTimeout,) ),
            ( 'Script Rev:',               WuiSvnLink(oTestBox.iTestBoxScriptRev, fBracketed = False) ),
            ( 'Python:',                   oTestBox.formatPythonVersion() ),
            ( 'Pending Command:',          oTestBox.enmPendingCmd ),
        ];

        aoRows = [
            aoResultRows,
            aoBuildRows,
            aoValidationKitRows,
            aoTestBoxRows,
        ];

        asHtml.append(self._htmlTable(aoRows));

        #
        # Convert the tree to a list of events, values, message and files.
        #
        sHtmlEvents = '';
        sHtmlEvents += '<table class="tmtbl-events" id="tmtbl-events" width="100%">\n';
        sHtmlEvents += ' <tr class="tmheader">\n' \
                       '  <th>When</th>\n' \
                       '  <th></th>\n' \
                       '  <th>Elapsed</th>\n' \
                       '  <th>Event name</th>\n' \
                       '  <th colspan="2">Value (status)</th>' \
                       '  <th></th>\n' \
                       ' </tr>\n';
        sPrettyCmdLine = '&nbsp;\\<br>&nbsp;&nbsp;&nbsp;&nbsp;\n'.join(webutils.escapeElem(oTestCaseEx.sBaseCmd
                                                                                           + ' '
                                                                                           + oTestVarEx.sArgs).split() );
        (sTmp, _, cFailures) = self._recursivelyGenerateEvents(oTestResultTree, sPrettyCmdLine, '', 1, 0, oTestSet, 0);
        sHtmlEvents += sTmp;

        sHtmlEvents += '</table>\n'

        #
        # Put it all together.
        #
        sHtml  = '<table class="tmtbl-testresult-details-base" width="100%">\n';
        sHtml += ' <tr>\n'
        sHtml += '  <td valign="top" width="20%%">\n%s\n</td>\n' % '   <br>\n'.join(asHtml);

        sHtml += '  <td valign="top" width="80%" style="padding-left:6px">\n';
        sHtml += '   <h2>Events:</h2>\n';
        sHtml += '   <form action="#" method="get" id="graph-form">\n' \
                 '    <input type="hidden" name="%s" value="%s"/>\n' \
                 '    <input type="hidden" name="%s" value="%u"/>\n' \
                 '    <input type="hidden" name="%s" value="%u"/>\n' \
                 '    <input type="hidden" name="%s" value="%u"/>\n' \
                 '    <input type="hidden" name="%s" value="%u"/>\n' \
                 % ( WuiMain.ksParamAction,               WuiMain.ksActionGraphWiz,
                     WuiMain.ksParamGraphWizTestBoxIds,   oTestBox.idTestBox,
                     WuiMain.ksParamGraphWizBuildCatIds,  oBuildEx.idBuildCategory,
                     WuiMain.ksParamGraphWizTestCaseIds,  oTestSet.idTestCase,
                     WuiMain.ksParamGraphWizSrcTestSetId, oTestSet.idTestSet,
                   );
        if oTestSet.tsDone is not None:
            sHtml += '    <input type="hidden" name="%s" value="%s"/>\n' \
                   % ( WuiMain.ksParamEffectiveDate, oTestSet.tsDone, );
        sHtml += '    <p>\n';
        sFormButton = '<button type="submit" onclick="%s">Show graphs</button>' \
                    % ( webutils.escapeAttr('addDynamicGraphInputs("graph-form", "main", "%s", "%s");'
                                            % (WuiMain.ksParamGraphWizWidth, WuiMain.ksParamGraphWizDpi, )) );
        sHtml += '     ' + sFormButton + '\n';
        sHtml += '     %s %s %s\n' \
               % ( WuiTmLink('Log File', '',
                             { WuiMain.ksParamAction:             WuiMain.ksActionViewLog,
                               WuiMain.ksParamLogSetId:           oTestSet.idTestSet,
                             }),
                   WuiTmLink('Raw Log', '',
                             { WuiMain.ksParamAction:             WuiMain.ksActionGetFile,
                               WuiMain.ksParamGetFileSetId:       oTestSet.idTestSet,
                               WuiMain.ksParamGetFileDownloadIt:  False,
                             }),
                   WuiTmLink('Download Log', '',
                             { WuiMain.ksParamAction:             WuiMain.ksActionGetFile,
                               WuiMain.ksParamGetFileSetId:       oTestSet.idTestSet,
                               WuiMain.ksParamGetFileDownloadIt:  True,
                             }),
                  );
        sHtml += '    </p>\n';
        if cFailures == 1:
            sHtml += '    <p>%s</p>\n' % ( WuiTmLink('Jump to failure', '#failure-0'), )
        elif cFailures > 1:
            sHtml += '    <p>Jump to failure: ';
            if cFailures <= 13:
                for iFailure in range(0, cFailures):
                    sHtml += ' ' + WuiTmLink('#%u' % (iFailure,), '#failure-%u' % (iFailure,)).toHtml();
            else:
                for iFailure in range(0, 6):
                    sHtml += ' ' + WuiTmLink('#%u' % (iFailure,), '#failure-%u' % (iFailure,)).toHtml();
                sHtml += ' ... ';
                for iFailure in range(cFailures - 6, cFailures):
                    sHtml += ' ' + WuiTmLink('#%u' % (iFailure,), '#failure-%u' % (iFailure,)).toHtml();
            sHtml += '    </p>\n';

        sHtml += sHtmlEvents;
        sHtml += '   <p>' + sFormButton + '</p>\n';
        sHtml += '   </form>\n';
        sHtml += '  </td>\n';

        sHtml += ' </tr>\n';
        sHtml += '</table>\n';

        return ('Test Case result details', sHtml)


class WuiGroupedResultList(WuiListContentBase):
    """
    WUI results content generator.
    """

    def __init__(self, aoEntries, cEntriesCount, iPage, cItemsPerPage, tsEffective, fnDPrint, oDisp):
        """Override initialization"""
        WuiListContentBase.__init__(self, aoEntries, iPage, cItemsPerPage, tsEffective,
                                    sTitle = 'Ungrouped (%d)' % cEntriesCount, sId = 'results',
                                    fnDPrint = fnDPrint, oDisp = oDisp);

        self._cEntriesCount   = cEntriesCount

        self._asColumnHeaders = [
            'Start',
            'Product Build',
            'Validation Kit',
            'TestBox OS',
            'TestBox Name',
            'Test Case',
            'Elapsed',
            'Result',
        ];
        self._asColumnAttribs = ['align="center"', 'align="center"', 'align="center"',
                                 'align="center"', 'align="center"', 'align="center"',
                                 'align="center"', 'align="center"', 'align="center"',
                                 'align="center"', 'align="center"', 'align="center"' ]


        # Prepare parameter lists.
        self._dTestBoxLinkParams = self._oDisp.getParameters();
        self._dTestBoxLinkParams[WuiMain.ksParamAction]  = WuiMain.ksActionResultsGroupedByTestBox;

        self._dTestCaseLinkParams = self._oDisp.getParameters();
        self._dTestCaseLinkParams[WuiMain.ksParamAction] = WuiMain.ksActionResultsGroupedByTestCase;

        self._dRevLinkParams = self._oDisp.getParameters();
        self._dRevLinkParams[WuiMain.ksParamAction]  = WuiMain.ksActionResultsGroupedByBuildRev;



    def _formatListEntry(self, iEntry):
        """
        Format *show all* table entry
        """
        oEntry = self._aoEntries[iEntry];

        from testmanager.webui.wuiadmin import WuiAdmin;


        oValidationKit = None;
        if oEntry.idBuildTestSuite is not None:
            oValidationKit = WuiTmLink('#%d - r%s' % (oEntry.idBuildTestSuite, oEntry.iRevisionTestSuite),
                                   WuiAdmin.ksScriptName,
                                   { WuiAdmin.ksParamAction:  WuiAdmin.ksActionBuildDetails,
                                     BuildData.ksParam_idBuild: oEntry.idBuildTestSuite },
                                   fBracketed = False);


        aoTestSetLinks = [ WuiTmLink(oEntry.enmStatus,
                                     WuiMain.ksScriptName,
                                     { WuiMain.ksParamAction: WuiMain.ksActionTestResultDetails,
                                       TestSetData.ksParam_idTestSet: oEntry.idTestSet },
                                     fBracketed = False),];
        if oEntry.cErrors > 0:
            aoTestSetLinks.append(WuiTmLink('- %d error(s)' % (oEntry.cErrors, ),
                                            WuiMain.ksScriptName,
                                            { WuiMain.ksParamAction: WuiMain.ksActionTestResultDetails,
                                              TestSetData.ksParam_idTestSet: oEntry.idTestSet },
                                            sFragmentId = 'failure-0', fBracketed = False));


        self._dTestBoxLinkParams[WuiMain.ksParamGroupMemberId]  = oEntry.idTestBox;
        self._dTestCaseLinkParams[WuiMain.ksParamGroupMemberId] = oEntry.idTestCase;
        self._dRevLinkParams[WuiMain.ksParamGroupMemberId]      = oEntry.iRevision;

        sTestBoxTitle = u'';
        if oEntry.sCpuVendor is not None:
            sTestBoxTitle += 'CPU vendor:\t%s\n' % ( oEntry.sCpuVendor, );
        if oEntry.sCpuName is not None:
            sTestBoxTitle += 'CPU name:\t%s\n' % ( ' '.join(oEntry.sCpuName.split()), );
        if oEntry.sOsVersion is not None:
            sTestBoxTitle += 'OS version:\t%s\n' % ( oEntry.sOsVersion, );
        asFeatures = [];
        if oEntry.fCpuHwVirt       is True: asFeatures.append(u'HW\u2011Virt');
        if oEntry.fCpuNestedPaging is True: asFeatures.append(u'Nested\u2011Paging');
        if oEntry.fCpu64BitGuest   is True: asFeatures.append(u'64\u2011bit\u2011Guest');
        #if oEntry.fChipsetIoMmu    is True: asFeatures.append(u'I/O\u2011MMU');
        sTestBoxTitle += u'CPU features:\t' + u', '.join(asFeatures);

        return [
            oEntry.tsCreated,
            [ WuiTmLink('#%d - %s %s (%s)' % (oEntry.idBuild, oEntry.sProduct, oEntry.sVersion, oEntry.sType,),
                        WuiMain.ksScriptName, self._dRevLinkParams, sTitle = '%s' % (oEntry.sBranch,), fBracketed = False),
              WuiSvnLinkWithTooltip(oEntry.iRevision, 'vbox'), ## @todo add sRepository TestResultListingData
              WuiTmLink(self.ksShortDetailsLink, WuiAdmin.ksScriptName,
                        { WuiAdmin.ksParamAction:    WuiAdmin.ksActionBuildDetails,
                          BuildData.ksParam_idBuild: oEntry.idBuild },
                        fBracketed = False),
              ],
            oValidationKit,
            '%s.%s' % (oEntry.sOs, oEntry.sArch),
            [ WuiTmLink(oEntry.sTestBoxName, WuiMain.ksScriptName, self._dTestBoxLinkParams, fBracketed = False,
                        sTitle = sTestBoxTitle),
              WuiTmLink(self.ksShortDetailsLink, WuiAdmin.ksScriptName,
                        { WuiAdmin.ksParamAction:        WuiAdmin.ksActionTestBoxDetails,
                          TestBoxData.ksParam_idTestBox: oEntry.idTestBox },
                        fBracketed = False) ],
            [ WuiTmLink(oEntry.sTestCaseName, WuiMain.ksScriptName, self._dTestCaseLinkParams, fBracketed = False,
                        sTitle = (oEntry.sBaseCmd + ' ' + oEntry.sArgs) if oEntry.sArgs else oEntry.sBaseCmd),
              WuiTmLink(self.ksShortDetailsLink, WuiAdmin.ksScriptName,
                        { WuiAdmin.ksParamAction:          WuiAdmin.ksActionTestCaseDetails,
                          TestCaseData.ksParam_idTestCase: oEntry.idTestCase },
                        fBracketed = False), ],
            oEntry.tsElapsed,
            aoTestSetLinks
        ];
