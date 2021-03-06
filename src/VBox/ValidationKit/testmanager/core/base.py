# -*- coding: utf-8 -*-
# $Id$
# pylint: disable=C0302

"""
Test Manager Core - Base Class(es).
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


# Standard python imports.
import copy;
import re;
import socket;
import sys;
import uuid;
import unittest;

# Validation Kit imports.
from common import utils;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int      # pylint: disable=W0622,C0103


class TMExceptionBase(Exception):
    """
    For exceptions raised by any TestManager component.
    """
    pass;

class TMTooManyRows(TMExceptionBase):
    """
    Too many rows in the result.
    Used by ModelLogicBase decendants.
    """
    pass;


class ModelBase(object): # pylint: disable=R0903
    """
    Something all classes in the logical model inherits from.

    Not sure if 'logical model' is the right term here.
    Will see if it has any purpose later on...
    """

    def __init__(self):
        pass;


class ModelDataBase(ModelBase): # pylint: disable=R0903
    """
    Something all classes in the data classes in the logical model inherits from.
    """

    ## Child classes can use this to list array attributes which should use
    # an empty array ([]) instead of None as database NULL value.
    kasAltArrayNull = [];


    def __init__(self):
        ModelBase.__init__(self);


    #
    # Standard methods implemented by combining python magic and hungarian prefixes.
    #

    def getDataAttributes(self):
        """
        Returns a list of data attributes.
        """
        asRet   = [];
        asAttrs = dir(self);
        for sAttr in asAttrs:
            if sAttr[0] == '_' or sAttr[0] == 'k':
                continue;
            oValue = getattr(self, sAttr);
            if callable(oValue):
                continue;
            asRet.append(sAttr);
        return asRet;

    def initFromOther(self, oOther):
        """
        Initialize this object with the values from another instance (child
        class instance is accepted).

        This serves as a kind of copy constructor.

        Returns self.  May raise exception if the type of other object differs
        or is damaged.
        """
        for sAttr in self.getDataAttributes():
            setattr(self, sAttr, getattr(oOther, sAttr));
        return self;

    @staticmethod
    def getHungarianPrefix(sName):
        """
        Returns the hungarian prefix of the given name.
        """
        for i in range(len(sName)):
            if sName[i] not in ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
                                'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z']:
                assert re.search('^[A-Z][a-zA-Z0-9]*$', sName[i:]) is not None;
                return sName[:i];
        return sName;

    def getAttributeParamNullValues(self, sAttr):
        """
        Returns a list of parameter NULL values, with the preferred one being
        the first element.

        Child classes can override this to handle one or more attributes specially.
        """
        sPrefix = self.getHungarianPrefix(sAttr);
        if sPrefix in ['id', 'uid', 'i', 'off', 'pct']:
            return [-1, '', '-1',];
        elif sPrefix in ['l', 'c',]:
            return [long(-1), '', '-1',];
        elif sPrefix == 'f':
            return ['',];
        elif sPrefix in ['enm', 'ip', 's', 'ts', 'uuid']:
            return ['',];
        elif sPrefix in ['ai', 'aid', 'al', 'as']:
            return [[], '', None]; ## @todo ??
        elif sPrefix == 'bm':
            return ['', [],]; ## @todo bitmaps.
        raise TMExceptionBase('Unable to classify "%s" (prefix %s)' % (sAttr, sPrefix));

    def isAttributeNull(self, sAttr, oValue):
        """
        Checks if the specified attribute value indicates NULL.
        Return True/False.

        Note! This isn't entirely kosher actually.
        """
        if oValue is None:
            return True;
        aoNilValues = self.getAttributeParamNullValues(sAttr);
        return oValue in aoNilValues;

    def _convertAttributeFromParamNull(self, sAttr, oValue):
        """
        Converts an attribute from parameter NULL to database NULL value.
        Returns the new attribute value.
        """
        aoNullValues = self.getAttributeParamNullValues(sAttr);
        if oValue in aoNullValues:
            oValue = None if sAttr not in self.kasAltArrayNull else [];
        #
        # Perform deep conversion on ModelDataBase object and lists of them.
        #
        elif isinstance(oValue, list) and len(oValue) > 0 and isinstance(oValue[0], ModelDataBase):
            oValue = copy.copy(oValue);
            for i in range(len(oValue)):
                assert isinstance(oValue[i], ModelDataBase);
                oValue[i] = copy.copy(oValue[i]);
                oValue[i].convertFromParamNull();

        elif isinstance(oValue, ModelDataBase):
            oValue = copy.copy(oValue);
            oValue.convertFromParamNull();

        return oValue;

    def convertFromParamNull(self):
        """
        Converts from parameter NULL values to database NULL values (None).
        Returns self.
        """
        for sAttr in self.getDataAttributes():
            oValue = getattr(self, sAttr);
            oNewValue = self._convertAttributeFromParamNull(sAttr, oValue);
            if oValue != oNewValue:
                setattr(self, sAttr, oNewValue);
        return self;

    def _convertAttributeToParamNull(self, sAttr, oValue):
        """
        Converts an attribute from database NULL to a sepcial value we can pass
        thru parameter list.
        Returns the new attribute value.
        """
        if oValue is None:
            oValue = self.getAttributeParamNullValues(sAttr)[0];
        #
        # Perform deep conversion on ModelDataBase object and lists of them.
        #
        elif isinstance(oValue, list) and len(oValue) > 0 and isinstance(oValue[0], ModelDataBase):
            oValue = copy.copy(oValue);
            for i in range(len(oValue)):
                assert isinstance(oValue[i], ModelDataBase);
                oValue[i] = copy.copy(oValue[i]);
                oValue[i].convertToParamNull();

        elif isinstance(oValue, ModelDataBase):
            oValue = copy.copy(oValue);
            oValue.convertToParamNull();

        return oValue;

    def convertToParamNull(self):
        """
        Converts from database NULL values (None) to special values we can
        pass thru parameters list.
        Returns self.
        """
        for sAttr in self.getDataAttributes():
            oValue = getattr(self, sAttr);
            oNewValue = self._convertAttributeToParamNull(sAttr, oValue);
            if oValue != oNewValue:
                setattr(self, sAttr, oNewValue);
        return self;

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        """
        Validates and convert one attribute.
        Returns the converted value.

        Child classes can override this to handle one or more attributes specially.
        Note! oDb can be None.
        """
        sPrefix = self.getHungarianPrefix(sAttr);

        if sPrefix in ['id', 'uid']:
            (oNewValue, sError) = self.validateInt( oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix in ['i', 'off', 'pct']:
            (oNewValue, sError) = self.validateInt( oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                    iMin = getattr(self, 'kiMin_' + sAttr, 0),
                                                    iMax = getattr(self, 'kiMax_' + sAttr, 0x7ffffffe));
        elif sPrefix in ['l', 'c']:
            (oNewValue, sError) = self.validateLong(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                    lMin = getattr(self, 'klMin_' + sAttr, 0),
                                                    lMax = getattr(self, 'klMax_' + sAttr, None));
        elif sPrefix == 'f':
            if oValue is '' and not fAllowNull: oValue = '0'; # HACK ALERT! Checkboxes are only added when checked.
            (oNewValue, sError) = self.validateBool(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix == 'ts':
            (oNewValue, sError) = self.validateTs(  oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix == 'ip':
            (oNewValue, sError) = self.validateIp(  oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix == 'uuid':
            (oNewValue, sError) = self.validateUuid(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix == 'enm':
            (oNewValue, sError) = self.validateWord(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                    asValid = getattr(self, 'kasValidValues_' + sAttr)); # The list is required.
        elif sPrefix == 's':
            (oNewValue, sError) = self.validateStr( oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                    cchMin = getattr(self, 'kcchMin_' + sAttr, 0),
                                                    cchMax = getattr(self, 'kcchMax_' + sAttr, 4096));
        ## @todo al.
        elif sPrefix == 'aid':
            (oNewValue, sError) = self.validateListOfInts(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                          iMin = 1, iMax = 0x7ffffffe);
        elif sPrefix == 'as':
            (oNewValue, sError) = self.validateListOfStr(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                         asValidValues = getattr(self, 'kasValidValues_' + sAttr, None),
                                                         cchMin = getattr(self, 'kcchMin_' + sAttr, 0 if fAllowNull else 1),
                                                         cchMax = getattr(self, 'kcchMax_' + sAttr, 4096));

        elif sPrefix == 'bm':
            ## @todo figure out bitfields.
            (oNewValue, sError) = self.validateListOfStr(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        else:
            raise TMExceptionBase('Unable to classify "%s" (prefix %s)' % (sAttr, sPrefix));

        _ = sParam; _ = oDb;
        return (oNewValue, sError);

    def _validateAndConvertWorker(self, asAllowNullAttributes, oDb):
        """
        Worker for implementing validateAndConvert().
        """
        dErrors = dict();
        for sAttr in self.getDataAttributes():
            oValue      = getattr(self, sAttr);
            sParam      = getattr(self, 'ksParam_' + sAttr);
            aoNilValues = self.getAttributeParamNullValues(sAttr);
            aoNilValues.append(None);

            (oNewValue, sError) = self._validateAndConvertAttribute(sAttr, sParam, oValue, aoNilValues,
                                                                    sAttr in asAllowNullAttributes, oDb);
            if oValue != oNewValue:
                setattr(self, sAttr, oNewValue);
            if sError is not None:
                dErrors[sParam] = sError;
        return dErrors;

    def validateAndConvert(self, oDb):
        """
        Validates the input and converts valid fields to their right type.
        Returns a dictionary with per field reports, only invalid fields will
        be returned, so an empty dictionary means that the data is valid.

        The dictionary keys are ksParam_*.

        Child classes can override _validateAndConvertAttribute to handle
        selected fields specially.  There are also a few class variables that
        can be used to advice the validation: kcchMin_sAttr, kcchMax_sAttr,
            kiMin_iAttr, kiMax_iAttr, klMin_lAttr, klMax_lAttr,
            kasValidValues_enmAttr, and kasAllowNullAttributes.
        """
        return self._validateAndConvertWorker(getattr(self, 'kasAllowNullAttributes', list()), oDb);

    def convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict):
        """
        Calculate the attribute value when initialized from a parameter.

        Returns the new value, with parameter NULL values. Raises exception on
        invalid parameter value.

        Child classes can override to do special parameter conversion jobs.
        """
        sPrefix = self.getHungarianPrefix(sAttr);
        asValidValues = getattr(self, 'kasValidValues_' + sAttr, None);
        if fStrict:
            if sPrefix == 'f':
                # HACK ALERT! Checkboxes are only present when checked, so we always have to provide a default.
                oNewValue = oDisp.getStringParam(sParam, asValidValues, '0');
            elif sPrefix[0] == 'a':
                # HACK ALERT! List are not present if empty.
                oNewValue = oDisp.getListOfStrParams(sParam, []);
            else:
                oNewValue = oDisp.getStringParam(sParam, asValidValues, None);
        else:
            if sPrefix[0] == 'a':
                oNewValue = oDisp.getListOfStrParams(sParam, []);
            else:
                assert oValue is not None, 'sAttr=%s' % (sAttr,);
                oNewValue = oDisp.getStringParam(sParam, asValidValues, oValue);
        return oNewValue;

    def initFromParams(self, oDisp, fStrict = True):
        """
        Initialize the object from parameters.
        The input is not validated at all, except that all parameters must be
        present when fStrict is True.

        Returns self. Raises exception on invalid parameter value.

        Note! The returned object has parameter NULL values, not database ones!
        """

        self.convertToParamNull()
        for sAttr in self.getDataAttributes():
            oValue    = getattr(self, sAttr);
            oNewValue = self.convertParamToAttribute(sAttr, getattr(self, 'ksParam_' + sAttr), oValue, oDisp, fStrict);
            if oNewValue != oValue:
                setattr(self, sAttr, oNewValue);
        return self;

    def areAttributeValuesEqual(self, sAttr, sPrefix, oValue1, oValue2):
        """
        Called to compare two attribute values and python thinks differs.

        Returns True/False.

        Child classes can override this to do special compares of things like arrays.
        """
        # Just in case someone uses it directly.
        if oValue1 == oValue2:
            return True;

        #
        # Timestamps can be both string (param) and object (db)
        # depending on the data source.  Compare string values to make
        # sure we're doing the right thing here.
        #
        if sPrefix == 'ts':
            return str(oValue1) == str(oValue2);

        #
        # Some generic code handling ModelDataBase children.
        #
        if isinstance(oValue1, list) and isinstance(oValue2, list):
            if len(oValue1) == len(oValue2):
                for i in range(len(oValue1)):
                    if   not isinstance(oValue1[i], ModelDataBase) \
                      or type(oValue1) != type(oValue2):
                        return False;
                    if not oValue1[i].isEqual(oValue2[i]):
                        return False;
                return True;

        elif  isinstance(oValue1, ModelDataBase) \
          and type(oValue1) == type(oValue2):
            return oValue1[i].isEqual(oValue2[i]);

        _ = sAttr;
        return False;

    def isEqual(self, oOther):
        """ Compares two instances. """
        for sAttr in self.getDataAttributes():
            if getattr(self, sAttr) != getattr(oOther, sAttr):
                # Delegate the final decision to an overridable method.
                if not self.areAttributeValuesEqual(sAttr, self.getHungarianPrefix(sAttr),
                                                    getattr(self, sAttr), getattr(oOther, sAttr)):
                    return False;
        return True;

    def isEqualEx(self, oOther, asExcludeAttrs):
        """ Compares two instances, omitting the given attributes. """
        for sAttr in self.getDataAttributes():
            if    sAttr not in asExcludeAttrs \
              and getattr(self, sAttr) != getattr(oOther, sAttr):
                # Delegate the final decision to an overridable method.
                if not self.areAttributeValuesEqual(sAttr, self.getHungarianPrefix(sAttr),
                                                    getattr(self, sAttr), getattr(oOther, sAttr)):
                    return False;
        return True;

    def reinitToNull(self):
        """
        Reinitializes the object to (database) NULL values.
        Returns self.
        """
        for sAttr in self.getDataAttributes():
            setattr(self, sAttr, None);
        return self;

    def toString(self):
        """
        Stringifies the object.
        Returns string representation.
        """

        sMembers = '';
        for sAttr in self.getDataAttributes():
            oValue = getattr(self, sAttr);
            sMembers += ', %s=%s' % (sAttr, oValue);

        oClass = type(self);
        if sMembers == '':
            return '<%s>' % (oClass.__name__);
        return '<%s: %s>' % (oClass.__name__, sMembers[2:]);

    def __str__(self):
        return self.toString();



    #
    # New validation helpers.
    #
    # These all return (oValue, sError), where sError is None when the value
    # is valid and an error message when not.  On success and in case of
    # range errors, oValue is converted into the requested type.
    #

    @staticmethod
    def validateInt(sValue, iMin = 0, iMax = 0x7ffffffe, aoNilValues = tuple([-1, None, '']), fAllowNull = True):
        """ Validates an integer field. """
        if sValue in aoNilValues:
            if fAllowNull:
                return (None if sValue is None else aoNilValues[0], None);
            return (sValue, 'Mandatory.');

        try:
            if utils.isString(sValue):
                iValue = int(sValue, 0);
            else:
                iValue = int(sValue);
        except:
            return (sValue, 'Not an integer');

        if iValue in aoNilValues:
            return (aoNilValues[0], None if fAllowNull else 'Mandatory.');

        if iValue < iMin:
            return (iValue, 'Value too small (min %d)' % (iMin,));
        elif iValue > iMax:
            return (iValue, 'Value too high (max %d)' % (iMax,));
        return (iValue, None);

    @staticmethod
    def validateLong(sValue, lMin = 0, lMax = None, aoNilValues = tuple([long(-1), None, '']), fAllowNull = True):
        """ Validates an long integer field. """
        if sValue in aoNilValues:
            if fAllowNull:
                return (None if sValue is None else aoNilValues[0], None);
            return (sValue, 'Mandatory.');
        try:
            if utils.isString(sValue):
                lValue = long(sValue, 0);
            else:
                lValue = long(sValue);
        except:
            return (sValue, 'Not a long integer');

        if lValue in aoNilValues:
            return (aoNilValues[0], None if fAllowNull else 'Mandatory.');

        if lMin is not None and lValue < lMin:
            return (lValue, 'Value too small (min %d)' % (lMin,));
        elif lMax is not None and lValue > lMax:
            return (lValue, 'Value too high (max %d)' % (lMax,));
        return (lValue, None);

    @staticmethod
    def validateTs(sValue, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates a timestamp field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');
        if not utils.isString(sValue):
            return (sValue, None);

        sError = None;
        if len(sValue) == len('2012-10-08 01:54:06.364207+02:00'):
            oRes = re.match(r'(\d{4})-([01]\d)-([0123])\d ([012]\d):[0-5]\d:([0-6]\d).\d{6}[+-](\d\d):(\d\d)', sValue);
            if    oRes is not None \
              and (   int(oRes.group(6)) >  12 \
                   or int(oRes.group(7)) >= 60):
                sError = 'Invalid timezone offset.';
        elif len(sValue) == len('2012-10-08 01:54:06.00'):
            oRes = re.match(r'(\d{4})-([01]\d)-([0123])\d ([012]\d):[0-5]\d:([0-6]\d).\d{2}', sValue);
        elif len(sValue) == len('9999-12-31 23:59:59.999999'):
            oRes = re.match(r'(\d{4})-([01]\d)-([0123])\d ([012]\d):[0-5]\d:([0-6]\d).\d{6}', sValue);
        elif len(sValue) == len('999999-12-31 00:00:00.00'):
            oRes = re.match(r'(\d{6})-([01]\d)-([0123])\d ([012]\d):[0-5]\d:([0-6]\d).\d{2}', sValue);
        elif len(sValue) == len('9999-12-31T23:59:59.999999Z'):
            oRes = re.match(r'(\d{4})-([01]\d)-([0123])\d[Tt]([012]\d):[0-5]\d:([0-6]\d).\d{6}[Zz]', sValue);
        elif len(sValue) == len('9999-12-31T23:59:59.999999999Z'):
            oRes = re.match(r'(\d{4})-([01]\d)-([0123])\d[Tt]([012]\d):[0-5]\d:([0-6]\d).\d{9}[Zz]', sValue);
        else:
            return (sValue, 'Invalid timestamp length.');

        if oRes is None:
            sError = 'Invalid timestamp (format: 2012-10-08 01:54:06.364207+02:00).';
        else:
            iYear  = int(oRes.group(1));
            if iYear % 4 == 0 and (iYear % 100 != 0  or iYear % 400 == 0):
                acDaysOfMonth = [31, 29, 31,  30, 31, 30,  31, 31, 30,  31, 30, 31];
            else:
                acDaysOfMonth = [31, 28, 31,  30, 31, 30,  31, 31, 30,  31, 30, 31];
            iMonth = int(oRes.group(2));
            iDay   = int(oRes.group(3));
            iHour  = int(oRes.group(4));
            iSec   = int(oRes.group(5));
            if iMonth > 12:
                sError = 'Invalid timestamp month.';
            elif iDay > acDaysOfMonth[iMonth - 1]:
                sError = 'Invalid timestamp day-of-month (%02d has %d days).' % (iMonth, acDaysOfMonth[iMonth - 1]);
            elif iHour > 23:
                sError = 'Invalid timestamp hour.'
            elif iSec >= 61:
                sError = 'Invalid timestamp second.'
            elif iSec >= 60:
                sError = 'Invalid timestamp: no leap seconds, please.'
        return (sValue, sError);

    @staticmethod
    def validateIp(sValue, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates an IP address field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if sValue == '::1':
            return (sValue, None);

        try:
            socket.inet_pton(socket.AF_INET, sValue); # pylint: disable=E1101
        except:
            try:
                socket.inet_pton(socket.AF_INET6, sValue); # pylint: disable=E1101
            except:
                return (sValue, 'Not a valid IP address.');

        return (sValue, None);

    @staticmethod
    def validateBool(sValue, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates a boolean field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if sValue in ('True', 'true', '1', True):
            return (True, None);
        if sValue in ('False', 'false', '0', False):
            return (False, None);
        return (sValue, 'Invalid boolean value.');

    @staticmethod
    def validateUuid(sValue, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates an UUID field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        try:
            sValue = str(uuid.UUID(sValue));
        except:
            return (sValue, 'Invalid UUID value.');
        return (sValue, None);

    @staticmethod
    def validateWord(sValue, cchMin = 1, cchMax = 64, asValid = None, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates a word field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if re.search('[^a-zA-Z0-9_-]', sValue) is not None:
            sError = 'Single word ([a-zA-Z0-9_-]), please.';
        elif cchMin is not None and len(sValue) < cchMin:
            sError = 'Too short, min %s chars' % (cchMin,);
        elif cchMax is not None and len(sValue) > cchMax:
            sError = 'Too long, max %s chars' % (cchMax,);
        elif asValid is not None and sValue not in asValid:
            sError = 'Invalid value "%s", must be one of: %s' % (sValue, asValid);
        else:
            sError = None;
        return (sValue, sError);

    @staticmethod
    def validateStr(sValue, cchMin = 0, cchMax = 4096, aoNilValues = tuple([None, '']), fAllowNull = True,
                    fAllowUnicodeSymbols = False):
        """ Validates a string field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if cchMin is not None and len(sValue) < cchMin:
            sError = 'Too short, min %s chars' % (cchMin,);
        elif cchMax is not None and len(sValue) > cchMax:
            sError = 'Too long, max %s chars' % (cchMax,);
        elif fAllowUnicodeSymbols is False and utils.hasNonAsciiCharacters(sValue):
            sError = 'Non-ascii characters not allowed'
        else:
            sError = None;
        return (sValue, sError);

    @staticmethod
    def validateEmail(sValue, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates a email field."""
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if re.match(r'.+@.+\..+', sValue) is None:
            return (sValue,'Invalid e-mail format.');
        return (sValue, None);

    @staticmethod
    def validateListOfSomething(asValues, aoNilValues = tuple([[], None]), fAllowNull = True):
        """ Validate a list of some uniform values. Returns a copy of the list (if list it is). """
        if asValues in aoNilValues  or  (len(asValues) == 0 and not fAllowNull):
            return (asValues, None if fAllowNull else 'Mandatory.')

        if not isinstance(asValues, list):
            return (asValues, 'Invalid data type (%s).' % (type(asValues),));

        asValues = list(asValues); # copy the list.
        if len(asValues) > 0:
            oType = type(asValues[0]);
            for i in range(1, len(asValues)):
                if type(asValues[i]) is not oType:
                    return (asValues, 'Invalid entry data type ([0]=%s vs [%d]=%s).' % (oType, i, type(asValues[i])) );

        return (asValues, None);

    @staticmethod
    def validateListOfStr(asValues, cchMin = None, cchMax = None, asValidValues = None,
                          aoNilValues = tuple([[], None]), fAllowNull = True):
        """ Validates a list of text items."""
        (asValues, sError) = ModelDataBase.validateListOfSomething(asValues, aoNilValues, fAllowNull);

        if sError is None  and asValues not in aoNilValues  and  len(asValues) > 0:
            if not utils.isString(asValues[0]):
                return (asValues, 'Invalid item data type.');

            if not fAllowNull and cchMin is None:
                cchMin = 1;

            for sValue in asValues:
                if asValidValues is not None  and  sValue not in asValidValues:
                    sThisErr = 'Invalid value "%s".' % (sValue,);
                elif cchMin is not None  and  len(sValue) < cchMin:
                    sThisErr = 'Value "%s" is too short, min length is %u chars.' % (sValue, cchMin);
                elif cchMax is not None  and  len(sValue) > cchMax:
                    sThisErr = 'Value "%s" is too long, max length is %u chars.' % (sValue, cchMax);
                else:
                    continue;

                if sError is None:
                    sError = sThisErr;
                else:
                    sError += ' ' + sThisErr;

        return (asValues, sError);

    @staticmethod
    def validateListOfInts(asValues, iMin = 0, iMax = 0x7ffffffe, aoNilValues = tuple([[], None]), fAllowNull = True):
        """ Validates a list of integer items."""
        (asValues, sError) = ModelDataBase.validateListOfSomething(asValues, aoNilValues, fAllowNull);

        if sError is None  and asValues not in aoNilValues  and  len(asValues) > 0:
            for i in range(len(asValues)):
                sValue = asValues[i];

                sThisErr = '';
                try:
                    iValue = int(sValue);
                except:
                    sThisErr = 'Invalid integer value "%s".' % (sValue,);
                else:
                    asValues[i] = iValue;
                    if iValue < iMin:
                        sThisErr = 'Value %d is too small (min %d)' % (iValue, iMin,);
                    elif iValue > iMax:
                        sThisErr = 'Value %d is too high (max %d)' % (iValue, iMax,);
                    else:
                        continue;

                if sError is None:
                    sError = sThisErr;
                else:
                    sError += ' ' + sThisErr;

        return (asValues, sError);



    #
    # Old validation helpers.
    #

    @staticmethod
    def _validateInt(dErrors, sName, sValue, iMin = 0, iMax = 0x7ffffffe, aoNilValues = tuple([-1, None, ''])):
        """ Validates an integer field. """
        (sValue, sError) = ModelDataBase.validateInt(sValue, iMin, iMax, aoNilValues, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateIntNN(dErrors, sName, sValue, iMin = 0, iMax = 0x7ffffffe, aoNilValues = tuple([-1, None, ''])):
        """ Validates an integer field, not null. """
        (sValue, sError) = ModelDataBase.validateInt(sValue, iMin, iMax, aoNilValues, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateLong(dErrors, sName, sValue, lMin = 0, lMax = None, aoNilValues = tuple([long(-1), None, ''])):
        """ Validates an long integer field. """
        (sValue, sError) = ModelDataBase.validateLong(sValue, lMin, lMax, aoNilValues, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateLongNN(dErrors, sName, sValue, lMin = 0, lMax = None, aoNilValues = tuple([long(-1), None, ''])):
        """ Validates an long integer field, not null. """
        (sValue, sError) = ModelDataBase.validateLong(sValue, lMin, lMax, aoNilValues, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateTs(dErrors, sName, sValue):
        """ Validates a timestamp field. """
        (sValue, sError) = ModelDataBase.validateTs(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateTsNN(dErrors, sName, sValue):
        """ Validates a timestamp field, not null. """
        (sValue, sError) = ModelDataBase.validateTs(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateIp(dErrors, sName, sValue):
        """ Validates an IP address field. """
        (sValue, sError) = ModelDataBase.validateIp(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateIpNN(dErrors, sName, sValue):
        """ Validates an IP address field, not null. """
        (sValue, sError) = ModelDataBase.validateIp(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateBool(dErrors, sName, sValue):
        """ Validates a boolean field. """
        (sValue, sError) = ModelDataBase.validateBool(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateBoolNN(dErrors, sName, sValue):
        """ Validates a boolean field, not null. """
        (sValue, sError) = ModelDataBase.validateBool(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateUuid(dErrors, sName, sValue):
        """ Validates an UUID field. """
        (sValue, sError) = ModelDataBase.validateUuid(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateUuidNN(dErrors, sName, sValue):
        """ Validates an UUID field, not null. """
        (sValue, sError) = ModelDataBase.validateUuid(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateWord(dErrors, sName, sValue, cchMin = 1, cchMax = 64, asValid = None):
        """ Validates a word field. """
        (sValue, sError) = ModelDataBase.validateWord(sValue, cchMin, cchMax, asValid, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateWordNN(dErrors, sName, sValue, cchMin = 1, cchMax = 64, asValid = None):
        """ Validates a boolean field, not null. """
        (sValue, sError) = ModelDataBase.validateWord(sValue, cchMin, cchMax, asValid, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateStr(dErrors, sName, sValue, cchMin = 0, cchMax = 4096):
        """ Validates a string field. """
        (sValue, sError) = ModelDataBase.validateStr(sValue, cchMin, cchMax, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateStrNN(dErrors, sName, sValue, cchMin = 0, cchMax = 4096):
        """ Validates a string field, not null. """
        (sValue, sError) = ModelDataBase.validateStr(sValue, cchMin, cchMax, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateEmail(dErrors, sName, sValue):
        """ Validates a email field."""
        (sValue, sError) = ModelDataBase.validateEmail(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateEmailNN(dErrors, sName, sValue):
        """ Validates a email field."""
        (sValue, sError) = ModelDataBase.validateEmail(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateListOfStr(dErrors, sName, asValues, asValidValues = None):
        """ Validates a list of text items."""
        (sValue, sError) = ModelDataBase.validateListOfStr(asValues, asValidValues = asValidValues, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateListOfStrNN(dErrors, sName, asValues, asValidValues = None):
        """ Validates a list of text items, not null and len >= 1."""
        (sValue, sError) = ModelDataBase.validateListOfStr(asValues, asValidValues = asValidValues, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    #
    # Various helpers.
    #

    @staticmethod
    def formatSimpleNowAndPeriod(oDb, tsNow = None, sPeriodBack = None,
                                 sTablePrefix = '', sExpCol = 'tsExpire', sEffCol = 'tsEffective'):
        """
        Formats a set of tsNow and sPeriodBack arguments for a standard testmanager
        table.

        If sPeriodBack is given, the query is effective for the period
        (tsNow - sPeriodBack) thru (tsNow).

        If tsNow isn't given, it defaults to current time.

        Returns the final portion of a WHERE query (start with AND) and maybe an
        ORDER BY and LIMIT bit if sPeriodBack is given.
        """
        if tsNow is not None:
            if sPeriodBack is not None:
                sRet = oDb.formatBindArgs('  AND  ' + sTablePrefix + sExpCol + ' > (%s::timestamp - %s::interval)\n'
                                          '  AND  tsEffective <= %s\n'
                                          'ORDER BY ' + sTablePrefix + sExpCol + ' DESC\n'
                                          'LIMIT 1\n'
                                          , ( tsNow, sPeriodBack, tsNow));
            else:
                sRet = oDb.formatBindArgs('  AND  ' + sTablePrefix + sExpCol + '     > %s\n'
                                          '  AND  ' + sTablePrefix + sEffCol + ' <= %s\n'
                                          , ( tsNow, tsNow, ));
        else:
            if sPeriodBack is not None:
                sRet = oDb.formatBindArgs('  AND  ' + sTablePrefix + sExpCol + '     > (CURRENT_TIMESTAMP - %s::interval)\n'
                                          '  AND  ' + sTablePrefix + sEffCol + ' <= CURRENT_TIMESTAMP\n'
                                          'ORDER BY ' + sTablePrefix + sExpCol + ' DESC\n'
                                          'LIMIT 1\n'
                                          , ( sPeriodBack, ));
            else:
                sRet = '  AND  ' + sTablePrefix + sExpCol + '     = \'infinity\'::timestamp\n';
        return sRet;

    @staticmethod
    def formatSimpleNowAndPeriodQuery(oDb, sQuery, aBindArgs, tsNow = None, sPeriodBack = None,
                                      sTablePrefix = '', sExpCol = 'tsExpire', sEffCol = 'tsEffective'):
        """
        Formats a simple query for a standard testmanager table with optional
        tsNow and sPeriodBack arguments.

        The sQuery and sBindArgs are passed along to oDb.formatBindArgs to form
        the first part of the query.  Must end with an open WHERE statement as
        we'll be adding the time part starting with 'AND something...'.

        See formatSimpleNowAndPeriod for tsNow and sPeriodBack description.

        Returns the final portion of a WHERE query (start with AND) and maybe an
        ORDER BY and LIMIT bit if sPeriodBack is given.

        """
        return oDb.formatBindArgs(sQuery, aBindArgs) \
             + ModelDataBase.formatSimpleNowAndPeriod(oDb, tsNow, sPeriodBack, sTablePrefix, sExpCol, sEffCol);

    #
    # Sub-classes.
    #

    class DispWrapper(object):
        """Proxy object."""
        def __init__(self, oDisp, sAttrFmt):
            self.oDisp    = oDisp;
            self.sAttrFmt = sAttrFmt;
        def getStringParam(self, sName, asValidValues = None, sDefault = None):
            """See WuiDispatcherBase.getStringParam."""
            return self.oDisp.getStringParam(self.sAttrFmt % (sName,), asValidValues, sDefault);
        def getListOfStrParams(self, sName, asDefaults = None):
            """See WuiDispatcherBase.getListOfStrParams."""
            return self.oDisp.getListOfStrParams(self.sAttrFmt % (sName,), asDefaults);
        def getListOfIntParams(self, sName, iMin = None, iMax = None, aiDefaults = None):
            """See WuiDispatcherBase.getListOfIntParams."""
            return self.oDisp.getListOfIntParams(self.sAttrFmt % (sName,), iMin, iMax, aiDefaults);




# pylint: disable=E1101,C0111,R0903
class ModelDataBaseTestCase(unittest.TestCase):
    """
    Base testcase for ModelDataBase decendants.
    Derive from this and override setUp.
    """

    def setUp(self):
        """
        Override this! Don't call super!
        The subclasses are expected to set aoSamples to an array of instance
        samples.  The first entry must be a default object, the subsequent ones
        are optional and their contents freely choosen.
        """
        self.aoSamples = [ModelDataBase(),];

    def testEquality(self):
        for oSample in self.aoSamples:
            self.assertEqual(oSample.isEqual(copy.copy(oSample)), True);
            self.assertIsNotNone(oSample.isEqual(self.aoSamples[0]));

    def testNullConversion(self):
        if len(self.aoSamples[0].getDataAttributes()) == 0:
            return;
        for oSample in self.aoSamples:
            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.convertToParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oSample), False);
            self.assertEqual(oCopy.convertFromParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oSample), True, '\ngot     : %s\nexpected: %s' % (oCopy, oSample,));

            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.convertToParamNull(), oCopy);
            oCopy2 = copy.copy(oCopy);
            self.assertEqual(oCopy.convertToParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oCopy2), True);
            self.assertEqual(oCopy.convertToParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oCopy2), True);

            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.convertFromParamNull(), oCopy);
            oCopy2 = copy.copy(oCopy);
            self.assertEqual(oCopy.convertFromParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oCopy2), True);
            self.assertEqual(oCopy.convertFromParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oCopy2), True);

    def testReinitToNull(self):
        oFirst = copy.copy(self.aoSamples[0]);
        self.assertEqual(oFirst.reinitToNull(), oFirst);
        for oSample in self.aoSamples:
            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.reinitToNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oFirst), True);

    def testValidateAndConvert(self):
        for oSample in self.aoSamples:
            oCopy = copy.copy(oSample);
            oCopy.convertToParamNull();
            dError1 = oCopy.validateAndConvert(None);

            oCopy2  = copy.copy(oCopy);
            self.assertEqual(oCopy.validateAndConvert(None), dError1);
            self.assertEqual(oCopy.isEqual(oCopy2), True);

    def testInitFromParams(self):
        class DummyDisp(object):
            def getStringParam(self, sName, asValidValues = None, sDefault = None):
                _ = sName; _ = asValidValues;
                return sDefault;
            def getListOfStrParams(self, sName, asDefaults = None):
                _ = sName;
                return asDefaults;
            def getListOfIntParams(self, sName, iMin = None, iMax = None, aiDefaults = None):
                _ = sName; _ = iMin; _ = iMax;
                return aiDefaults;

        for oSample in self.aoSamples:
            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.initFromParams(DummyDisp(), fStrict = False), oCopy);

    def testToString(self):
        for oSample in self.aoSamples:
            self.assertIsNotNone(oSample.toString());

# pylint: enable=E1101,C0111,R0903


class ModelLogicBase(ModelBase): # pylint: disable=R0903
    """
    Something all classes in the logic classes the logical model inherits from.
    """

    def __init__(self, oDb):
        ModelBase.__init__(self);

        #
        # Note! Do not create a connection here if None, we need to DB share
        #       connection with all other logic objects so we can perform half
        #       complex transactions involving several logic objects.
        #
        self._oDb = oDb;

    def getDbConnection(self):
        """
        Gets the database connection.
        This should only be used for instantiating other ModelLogicBase children.
        """
        return self._oDb;


class AttributeChangeEntry(object): # pylint: disable=R0903
    """
    Data class representing the changes made to one attribute.
    """

    def __init__(self, sAttr, oNewRaw, oOldRaw, sNewText, sOldText):
        self.sAttr          = sAttr;
        self.oNewRaw        = oNewRaw;
        self.oOldRaw        = oOldRaw;
        self.sNewText       = sNewText;
        self.sOldText       = sOldText;

class ChangeLogEntry(object): # pylint: disable=R0903
    """
    A change log entry returned by the fetchChangeLog method typically
    implemented by ModelLogicBase child classes.
    """

    def __init__(self, uidAuthor, sAuthor, tsEffective, tsExpire, oNewRaw, oOldRaw, aoChanges):
        self.uidAuthor      = uidAuthor;
        self.sAuthor        = sAuthor;
        self.tsEffective    = tsEffective;
        self.tsExpire       = tsExpire;
        self.oNewRaw        = oNewRaw;
        self.oOldRaw        = oOldRaw;      # Note! NULL for the last entry.
        self.aoChanges      = aoChanges;

