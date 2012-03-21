// Created on: 2006-05-25
// Created by: Alexander GRIGORIEV
// Copyright (c) 2006-2012 OPEN CASCADE SAS
//
// The content of this file is subject to the Open CASCADE Technology Public
// License Version 6.5 (the "License"). You may not use the content of this file
// except in compliance with the License. Please obtain a copy of the License
// at http://www.opencascade.org and read it completely before using this file.
//
// The Initial Developer of the Original Code is Open CASCADE S.A.S., having its
// main offices at: 1, place des Freres Montgolfier, 78280 Guyancourt, France.
//
// The Original Code and all software distributed under the License is
// distributed on an "AS IS" basis, without warranty of any kind, and the
// Initial Developer hereby disclaims all such warranties, including without
// limitation, any warranties of merchantability, fitness for a particular
// purpose or non-infringement. Please see the License for the specific terms
// and conditions governing the rights and limitations under the License.


#include <VrmlData_Scene.hxx>
#include <VrmlData_InBuffer.hxx>
#include <VrmlData_Appearance.hxx>
#include <VrmlData_Box.hxx>
#include <VrmlData_Color.hxx>
#include <VrmlData_Cone.hxx>
#include <VrmlData_Coordinate.hxx>
#include <VrmlData_Cylinder.hxx>
#include <VrmlData_DataMapOfShapeAppearance.hxx>
#include <VrmlData_Group.hxx>
#include <VrmlData_ImageTexture.hxx>
#include <VrmlData_InBuffer.hxx>
#include <VrmlData_IndexedFaceSet.hxx>
#include <VrmlData_IndexedLineSet.hxx>
#include <VrmlData_Material.hxx>
#include <VrmlData_Normal.hxx>
#include <VrmlData_Scene.hxx>
#include <VrmlData_ShapeNode.hxx>
#include <VrmlData_Sphere.hxx>
#include <VrmlData_TextureCoordinate.hxx>
#include <VrmlData_UnknownNode.hxx>
//#include <VrmlData_WorldInfo.hxx>
#include <NCollection_Vector.hxx>
#include <TopoDS_TFace.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Builder.hxx>
#include <Precision.hxx>

#ifdef WNT
#define _CRT_SECURE_NO_DEPRECATE
#pragma warning (disable:4996)
#endif

static void     dumpNode        (Standard_OStream&              theStream,
                                 const Handle(VrmlData_Node)&   theNode,
                                 const TCollection_AsciiString& theIndent);

static void     dumpNodeHeader  (Standard_OStream&              theStream,
                                 const TCollection_AsciiString& theIndent,
                                 const char *                   theType,
                                 const char *                   theName);

//=======================================================================
//function : VrmlData_Scene
//purpose  : Constructor
//=======================================================================

VrmlData_Scene::VrmlData_Scene
        (const Handle(NCollection_IncAllocator)& theAlloc)
  : myLinearScale     (1.),
    myStatus          (VrmlData_StatusOK),
    myAllocator       (theAlloc.IsNull() ?
                       new NCollection_IncAllocator : theAlloc.operator->()),
    myLineError       (0),
    myOutput          (0L),
    myIndent          (2),
    myCurrentIndent   (0),
    myAutoNameCounter (0)
{
  myWorldInfo = new VrmlData_WorldInfo (* this);
  myWorldInfo->AddInfo ("Created by OPEN CASCADE (tm) VrmlData API");
  myLstNodes.Append (myWorldInfo);
  myAllNodes.Append (myWorldInfo);
}

//=======================================================================
//function : AddNode
//purpose  : 
//=======================================================================

const Handle(VrmlData_Node)& VrmlData_Scene::AddNode
                                (const Handle(VrmlData_Node)& theN,
                                 const Standard_Boolean       isTopLevel)
{
  if (theN.IsNull() == Standard_False)
    if (theN->IsKind (STANDARD_TYPE(VrmlData_WorldInfo)) == Standard_False) {
      myMutex.Lock();
      const Handle(VrmlData_Node)& aNode =
        myAllNodes.Append ((&theN->Scene()== this) ? theN : theN->Clone (NULL));
      // Name is checked for uniqueness. If not, letter 'D' is appended until
      // the name proves to be unique.
      if (aNode->Name()[0] != '\0')
        while (myNamedNodes.Add (aNode) == Standard_False)
          aNode->setName (aNode->Name(), "D");
      if (isTopLevel)
        myLstNodes.Append (aNode);
      myMutex.Unlock();
      return aNode;
    }
  static Handle(VrmlData_Node) aNullNode;
  aNullNode.Nullify();
  return aNullNode;
}

//=======================================================================
//function : operator <<
//purpose  : Export to text stream (file or else)
//=======================================================================

Standard_OStream& operator << (Standard_OStream&     theOutput,
                               const VrmlData_Scene& theScene)
{
  VrmlData_Scene& aScene = const_cast <VrmlData_Scene&> (theScene);
  aScene.myMutex.Lock();
  aScene.myCurrentIndent = 0;
  aScene.myLineError = 0;
  aScene.myOutput = 0L;
  aScene.myNamedNodesOut.Clear();
  aScene.myUnnamedNodesOut.Clear();
  aScene.myAutoNameCounter = 0;

  // Dummy write

  VrmlData_Scene::Iterator anIterD(aScene.myLstNodes);
  for (; anIterD.More(); anIterD.Next()) {
    const Handle(VrmlData_Node)& aNode = anIterD.Value();
    if (aNode.IsNull() == Standard_False) {
      const VrmlData_ErrorStatus aStatus = aScene.WriteNode (0L, aNode);
      if (aStatus != VrmlData_StatusOK &&
          aStatus != VrmlData_NotImplemented)
        break;
    }
  }

  aScene.myOutput = &theOutput;
  aScene.myNamedNodesOut.Clear();
  theOutput << "#VRML V2.0 utf8" << endl << endl;

  // Real write

  VrmlData_Scene::Iterator anIter(aScene.myLstNodes);
  for (; anIter.More(); anIter.Next()) {
    const Handle(VrmlData_Node)& aNode = anIter.Value();
    if (aNode.IsNull() == Standard_False) {
      const VrmlData_ErrorStatus aStatus = aScene.WriteNode (0L, aNode);
      if (aStatus != VrmlData_StatusOK &&
          aStatus != VrmlData_NotImplemented)
        break;
    }
  }
  aScene.myOutput = 0L;
  aScene.myNamedNodesOut.Clear();
  aScene.myUnnamedNodesOut.Clear();
  aScene.myMutex.Unlock();
  return theOutput;
}

//=======================================================================
//function : SetVrmlDir
//purpose  : 
//=======================================================================

void VrmlData_Scene::SetVrmlDir (const TCollection_ExtendedString& theDir)
{
  TCollection_ExtendedString& aDir = myVrmlDir.Append (theDir);
  const Standard_ExtCharacter aTerminator = aDir.Value(aDir.Length());
  if (aTerminator != Standard_ExtCharacter('\\') &&
      aTerminator != Standard_ExtCharacter('/'))
#ifdef WNT
    aDir += TCollection_ExtendedString ("\\");
#else
    aDir += TCollection_ExtendedString ("/");
#endif
}

//=======================================================================
//function : WorldInfo
//purpose  : 
//=======================================================================

const Handle_VrmlData_WorldInfo& VrmlData_Scene::WorldInfo() const
{
  return myWorldInfo;
}

//=======================================================================
//function : readLine
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::readLine (VrmlData_InBuffer& theBuffer)
{
  VrmlData_ErrorStatus aStatus = VrmlData_StatusOK;
  if (theBuffer.Input.eof())
    aStatus = VrmlData_EndOfFile;
  else {
    theBuffer.Input.getline (theBuffer.Line, sizeof(theBuffer.Line));
    theBuffer.LineCount++;
    const int stat = theBuffer.Input.rdstate();
    if (stat & ios::badbit)
      aStatus = VrmlData_UnrecoverableError;
    else if (stat & ios::failbit)
      if (stat & ios::eofbit)
        aStatus = VrmlData_EndOfFile;
      else
        aStatus = VrmlData_GeneralError;
    theBuffer.LinePtr = &theBuffer.Line[0];
    theBuffer.IsProcessed = Standard_False;
  }
  return aStatus;
}

//=======================================================================
//function : ReadLine
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::ReadLine (VrmlData_InBuffer& theBuffer)
{
  VrmlData_ErrorStatus aStatus (VrmlData_StatusOK); 

  while (aStatus == VrmlData_StatusOK) {
    // Find the first significant character of the line
    for (; * theBuffer.LinePtr != '\0'; theBuffer.LinePtr++) {
      if (* theBuffer.LinePtr != ' ' && * theBuffer.LinePtr != '\t'
          && * theBuffer.LinePtr != ',')
      {
        if (* theBuffer.LinePtr == '\n' || * theBuffer.LinePtr == '\r' ||
            * theBuffer.LinePtr == '#')
          // go requesting the next line
          break;
        goto nonempty_line;
      }
    }
    // the line is empty here (no significant characters). Read the next one.
    aStatus = readLine (theBuffer);
  }

  // error or EOF detected
  return aStatus;

 nonempty_line:
  // Try to detect comment
  if (theBuffer.IsProcessed == Standard_False) {
    Standard_Boolean isQuoted (Standard_False);
    Standard_Integer anOffset (0);
    char * ptr = theBuffer.LinePtr;
    for (; * ptr != '\0'; ptr++) {
      if (anOffset)
        * ptr = ptr[anOffset];
      if (* ptr == '\n' || * ptr == '\r' || * ptr == '#') {
        if (isQuoted == Standard_False) {
          * ptr = '\0';
          break;
        }
      } else if (* ptr == '\\' && isQuoted)
        ptr[0] = ptr[++anOffset];
      else if (* ptr == '\"')
        isQuoted = !isQuoted;
    }
    theBuffer.IsProcessed = Standard_True;
  }
  return aStatus;
}

//=======================================================================
//function : readHeader
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::readHeader (VrmlData_InBuffer& theBuffer)
{
  VrmlData_ErrorStatus aStat = readLine (theBuffer);
  if (aStat == VrmlData_StatusOK &&
      !VRMLDATA_LCOMPARE(theBuffer.LinePtr, "#VRML V2.0"))
    aStat = VrmlData_NotVrmlFile;
  else 
    aStat = readLine(theBuffer);
  return aStat;
}

//=======================================================================
//function : operator <<
//purpose  : Import from text stream (file or else)
//=======================================================================

VrmlData_Scene& VrmlData_Scene::operator << (Standard_IStream& theInput)
{
  VrmlData_InBuffer aBuffer (theInput);
  myMutex.Lock();
  // Read the VRML header
  myStatus = readHeader (aBuffer);
  const Handle(VrmlData_UnknownNode) aNullNode= new VrmlData_UnknownNode(*this);
//   if (myStatus == StatusOK)
//     myStatus = ReadLine (aBuffer);
  // Read VRML data by nodes
  while (~0) {
    if (!VrmlData_Node::OK(myStatus, ReadLine(aBuffer))) {
      if (myStatus == VrmlData_EndOfFile)
        myStatus = VrmlData_StatusOK;
      break;
    }
    // this line provides the method ReadNode in the present context
    Handle(VrmlData_Node) aNode;
    myStatus = aNullNode->ReadNode (aBuffer, aNode);
    // Unknown nodes are not stored however they do not generate error
    if (myStatus != VrmlData_StatusOK)
      break;
    if (aNode.IsNull() == Standard_False /*&&
        !aNode->IsKind (STANDARD_TYPE(VrmlData_UnknownNode))*/)
    {
      if (aNode->IsKind (STANDARD_TYPE(VrmlData_WorldInfo)) == Standard_False)
        myLstNodes.Append (aNode);
      else if (aNode->IsDefault() == Standard_False) {
        const Handle(VrmlData_WorldInfo) aInfo =
          Handle(VrmlData_WorldInfo)::DownCast (aNode);
        myWorldInfo->SetTitle (aInfo->Title());
        NCollection_List <const char *>::Iterator anIterInfo =
          aInfo->InfoIterator();
        for (; anIterInfo.More(); anIterInfo.Next())
          myWorldInfo->AddInfo (anIterInfo.Value());
      }
    }
  }
  if (myStatus != VrmlData_StatusOK)
    myLineError = aBuffer.LineCount;
  myMutex.Unlock();
  return * this;
}

//=======================================================================
//function : FindNode
//purpose  : 
//=======================================================================

Handle(VrmlData_Node) VrmlData_Scene::FindNode
                                (const char                   * theName,
                                 const Handle(Standard_Type)& theType) const
{
  Handle(VrmlData_Node) aResult;
#ifdef USE_LIST_API
  Iterator anIter (myAllNodes);
  for (; anIter.More(); anIter.Next())
    if (!strcmp (anIter.Value()->Name(), theName)) {
      aResult = anIter.Value();
      if (theType.IsNull())
        break;
      if (aResult->IsKind(theType))
        break;
      aResult.Nullify();
    }
#else
  const Handle(VrmlData_UnknownNode) aDummyNode = new VrmlData_UnknownNode;
  aDummyNode->myName = theName;
  if (myNamedNodes.Contains (aDummyNode))
    aResult = const_cast<VrmlData_MapOfNode&>(myNamedNodes).Added(aDummyNode);
#endif
  return aResult;
}

//=======================================================================
//function : FindNode
//purpose  : 
//=======================================================================

Handle(VrmlData_Node) VrmlData_Scene::FindNode
                                        (const char *   theName,
                                         gp_Trsf&       theLocation) const
{
  gp_Trsf aLoc;
  Handle(VrmlData_Node) aResult;
  Iterator anIter (myLstNodes);
  for (; anIter.More(); anIter.Next()) {
    const Handle(VrmlData_Node)& aNode = anIter.Value();
    if (aNode.IsNull())
      continue;
    // Match a top-level node name
    if (strcmp(aNode->Name(), theName) == 0) {
      aResult = aNode;
      theLocation = aLoc;
      break;
    }
    // Try a Group type of node
    if (aNode->IsKind(STANDARD_TYPE(VrmlData_Group)))
    {
      const Handle(VrmlData_Group) aGroup =
        Handle(VrmlData_Group)::DownCast (aNode);
      if (aGroup.IsNull() == Standard_False) {
        aResult = aGroup->FindNode(theName, theLocation);
        if (aResult.IsNull() == Standard_False)
          break;
      }
    }
  }
  return aResult;
}

//=======================================================================
//function : ReadWord
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::ReadWord
                                      (VrmlData_InBuffer&           theBuffer,
                                       TCollection_AsciiString&     theWord)
{
  VrmlData_ErrorStatus aStatus = ReadLine(theBuffer);
  if (aStatus == VrmlData_StatusOK) {
    char * ptr = theBuffer.LinePtr;
    while (* ptr != '\0' && * ptr != '\n' && * ptr != '\r' &&
           * ptr != ' '  && * ptr != '\t' && * ptr != '{' && * ptr != '}' &&
           * ptr != ','  && * ptr != '['  && * ptr != ']')
      ptr++;
    const Standard_Integer aLen = Standard_Integer(ptr - theBuffer.LinePtr);
    if (aLen <= 0)
      aStatus = VrmlData_StringInputError;
    else {
      theWord = TCollection_AsciiString ((Standard_CString)theBuffer.LinePtr,
                                         aLen);
      theBuffer.LinePtr = ptr;
    }
  }
  return aStatus;
}

//=======================================================================
//function : createNode
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::createNode
                                      (VrmlData_InBuffer&           theBuffer,
                                       Handle(VrmlData_Node)&       theNode,
                                       const Handle(Standard_Type)& theType)
{
  VrmlData_ErrorStatus    aStatus;
  Handle(VrmlData_Node)   aNode;
  TCollection_AsciiString aName;

  // Read the DEF token to assign the node name
  if (VrmlData_Node::OK(aStatus, ReadLine(theBuffer)))
    if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "DEF")) {
      if (VrmlData_Node::OK(aStatus, ReadWord (theBuffer, aName)))
        aStatus = ReadLine(theBuffer);
    } else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "NULL")) {
      theNode.Nullify();
      return aStatus;
    }

  const char * strName = aName.ToCString();
  if (aStatus == VrmlData_StatusOK) {
    // create the new node
    if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Appearance"))
      aNode = new VrmlData_Appearance     (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Shape"))
      aNode = new VrmlData_ShapeNode      (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Box"))
      aNode = new VrmlData_Box            (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Color"))
      aNode = new VrmlData_Color          (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Cone"))
      aNode = new VrmlData_Cone           (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Coordinate")) {
      aNode = new VrmlData_Coordinate     (* this, strName);
      
      // Check for "Coordinate3"
      if (VRMLDATA_LCOMPARE (theBuffer.LinePtr, "3"))
        theBuffer.LinePtr++;
    }
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Cylinder"))
      aNode = new VrmlData_Cylinder       (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Group"))
      aNode = new VrmlData_Group          (* this, strName,
                                           Standard_False);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Transform"))
      aNode = new VrmlData_Group          (* this, strName,
                                           Standard_True);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Inline"))
      aNode = new VrmlData_Group          (* this, strName,
                                           Standard_False);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Separator"))
      aNode = new VrmlData_Group          (* this, strName,
                                           Standard_False);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Switch"))
      aNode = new VrmlData_Group          (* this, strName,
                                           Standard_False);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "ImageTexture"))
      aNode = new VrmlData_ImageTexture   (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "IndexedFaceSet"))
      aNode = new VrmlData_IndexedFaceSet (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "IndexedLineSet"))
      aNode = new VrmlData_IndexedLineSet (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Material"))
      aNode = new VrmlData_Material       (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Normal"))
      aNode = new VrmlData_Normal         (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "Sphere"))
      aNode = new VrmlData_Sphere         (* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "TextureCoordinate"))
      aNode = new VrmlData_TextureCoordinate(* this, strName);
    else if (VRMLDATA_LCOMPARE(theBuffer.LinePtr, "WorldInfo"))
      aNode = new VrmlData_WorldInfo      (* this, strName);
    else {
      void * isProto = VRMLDATA_LCOMPARE(theBuffer.LinePtr, "PROTO");
      TCollection_AsciiString aTitle;
      aStatus = ReadWord (theBuffer, aTitle);
      if (isProto) {
        aStatus = ReadLine(theBuffer);
        if (aStatus == VrmlData_StatusOK)
          if (theBuffer.LinePtr[0] != '[')
            aStatus = VrmlData_VrmlFormatError;
          else {
            theBuffer.LinePtr++;
            Standard_Integer aLevelCounter(0);
   // This loop searches for any opening bracket '['.
   // Such bracket increments the level counter. A closing bracket decrements
   // the counter. The loop terminates when the counter becomes negative.
            while (aLevelCounter >= 0 &&
                   (aStatus = ReadLine(theBuffer)) == VrmlData_StatusOK) {
              int aChar;
              while ((aChar = theBuffer.LinePtr[0]) != '\0') {
                theBuffer.LinePtr++;
                if        (aChar == '[') {
                  aLevelCounter++;
                  break;
                } else if (aChar == ']') {
                  aLevelCounter--;
                  break;
                }
              }
            }
          }
      }
      if (aStatus == VrmlData_StatusOK)
        aNode = new VrmlData_UnknownNode(* this,
                                         strName,
                                         aTitle.ToCString());
    }
  }
  aStatus = ReadLine(theBuffer);
  if (aNode.IsNull() == Standard_False) {
    if (aNode->Name()[0] != '\0')
      myNamedNodes.Add (aNode);
    if (theType.IsNull() == Standard_False)
      if (aNode->IsKind(theType) == Standard_False)
        aStatus = VrmlData_VrmlFormatError;
  }
  if (aStatus == VrmlData_StatusOK)
    if (theBuffer.LinePtr[0] == '{') {
      theBuffer.LinePtr++;
      theNode = aNode;
      myAllNodes.Append(aNode);
    } else
      aStatus = VrmlData_VrmlFormatError;
  return aStatus;
}

//=======================================================================
//function : operator TopoDS_Shape
//purpose  : 
//=======================================================================

VrmlData_Scene::operator TopoDS_Shape () const
{
  TopoDS_Shape aShape;
  VrmlData_Scene::createShape (aShape, myLstNodes, 0L);
  return aShape;
}

//=======================================================================
//function : GetShape
//purpose  : 
//=======================================================================

TopoDS_Shape VrmlData_Scene::GetShape (VrmlData_DataMapOfShapeAppearance& aMap)
{
  TopoDS_Shape aShape;
  VrmlData_Scene::createShape (aShape, myLstNodes, &aMap);
  return aShape;
}

//=======================================================================
//function : createShape
//purpose  : 
//=======================================================================

void VrmlData_Scene::createShape
                (TopoDS_Shape&                      outShape,
                 const VrmlData_ListOfNode&         lstNodes,
                 VrmlData_DataMapOfShapeAppearance* pMapShapeApp)
{
  TopoDS_Shape aSingleShape;  // used when there is a single ShapeNode
  Standard_Boolean isSingleShape (Standard_True);
  BRep_Builder aBuilder;
  outShape.Nullify();
  aBuilder.MakeCompound(TopoDS::Compound(outShape));
  aSingleShape.Orientation(TopAbs_FORWARD);

  Iterator anIter (lstNodes);
  for (; anIter.More(); anIter.Next()) {
    // Try a Shape type of node
    const Handle(VrmlData_ShapeNode) aNodeShape =
      Handle(VrmlData_ShapeNode)::DownCast (anIter.Value());
    if (aNodeShape.IsNull() == Standard_False) {
      const Handle(VrmlData_Geometry) aNodeGeom =
        Handle(VrmlData_Geometry)::DownCast(aNodeShape->Geometry());
      if (aNodeGeom.IsNull() == Standard_False) {
        if (aSingleShape.IsNull() == Standard_False)
          isSingleShape = Standard_False;
        const Handle(TopoDS_TShape) aTShape = aNodeGeom->TShape();
        aSingleShape.TShape(aTShape);
        if (aSingleShape.IsNull() == Standard_False) {
          aBuilder.Add (outShape, aSingleShape);
          if (pMapShapeApp != 0L) {
            const Handle(VrmlData_Appearance)& anAppearance =
              aNodeShape->Appearance();
            if (anAppearance.IsNull() == Standard_False) {
              // Check if the current topology is a single face
              if (aTShape->IsKind(STANDARD_TYPE(TopoDS_TFace)))
                pMapShapeApp->Bind(aTShape, anAppearance);
              else {
                // This is not a face, explode it in faces and bind each face
                TopoDS_Shape aCurShape;
                aCurShape.TShape(aTShape);
                TopExp_Explorer anExp(aCurShape, TopAbs_FACE);
                for (; anExp.More(); anExp.Next()) {
                  const TopoDS_Face& aFace = TopoDS::Face(anExp.Current());
                  pMapShapeApp->Bind(aFace.TShape(), anAppearance);
                }
              }
            }
          }
        }
      }
      continue;
    }
    // Try a Group type of node
    const Handle(VrmlData_Group) aNodeGroup =
      Handle(VrmlData_Group)::DownCast (anIter.Value());
    if (aNodeGroup.IsNull() == Standard_False) {
      TopoDS_Shape aShape;
      aNodeGroup->Shape(aShape, pMapShapeApp);
      if (aShape.IsNull() == Standard_False) {
        aBuilder.Add (outShape, aShape);
        isSingleShape = Standard_False;
      }
    }
  }
  if (isSingleShape)
    outShape = aSingleShape;
}

//=======================================================================
//function : ReadReal
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::ReadReal
                                (VrmlData_InBuffer& theBuffer,
                                 Standard_Real&     theResult,
                                 Standard_Boolean   isScale,
                                 Standard_Boolean   isOnlyPositive) const
{
  Standard_Real aResult(0.);
  VrmlData_ErrorStatus aStatus;
  if (VrmlData_Node::OK(aStatus, VrmlData_Scene::ReadLine(theBuffer))) {
    char * endptr;
    aResult = strtod (theBuffer.LinePtr, &endptr);
    if (endptr == theBuffer.LinePtr)
      aStatus = VrmlData_NumericInputError;
    else if (isOnlyPositive && aResult < 0.001*Precision::Confusion())
      aStatus = VrmlData_IrrelevantNumber;
    else {
      theResult = isScale ? (aResult * myLinearScale) : aResult;
      theBuffer.LinePtr = endptr;
    }
  }
  return aStatus;
}

//=======================================================================
//function : ReadXYZ
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::ReadXYZ
                                (VrmlData_InBuffer&     theBuffer,
                                 gp_XYZ&                theXYZ,
                                 Standard_Boolean       isScale,
                                 Standard_Boolean       isOnlyPos) const
{
  Standard_Real aVal[3] = {0., 0., 0.};
  VrmlData_ErrorStatus aStatus;
  for (Standard_Integer i = 0; i < 3; i++) {
    if (!VrmlData_Node::OK(aStatus, VrmlData_Scene::ReadLine(theBuffer)))
      break;
    char * endptr;
    aVal[i] = strtod (theBuffer.LinePtr, &endptr);
    if (endptr == theBuffer.LinePtr) {
      aStatus = VrmlData_NumericInputError;
      break;
    } else {
      if (isOnlyPos && aVal[i] < 0.001*Precision::Confusion()) {
        aStatus = VrmlData_IrrelevantNumber;
        break;
      }
      theBuffer.LinePtr = endptr;
    }
  }
  if (aStatus == VrmlData_StatusOK)
    if (isScale)
      theXYZ.SetCoord (aVal[0] * myLinearScale,
                       aVal[1] * myLinearScale,
                       aVal[2] * myLinearScale);
    else
      theXYZ.SetCoord (aVal[0], aVal[1], aVal[2]);
  return aStatus;
}

//=======================================================================
//function : ReadXY
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::ReadXY
                                (VrmlData_InBuffer&     theBuffer,
                                 gp_XY&                 theXY,
                                 Standard_Boolean       isScale,
                                 Standard_Boolean       isOnlyPos) const
{
  Standard_Real aVal[2] = {0., 0.};
  VrmlData_ErrorStatus aStatus;
  for (Standard_Integer i = 0; i < 2; i++) {
    if (!VrmlData_Node::OK(aStatus, VrmlData_Scene::ReadLine(theBuffer)))
      break;
    char * endptr;
    aVal[i] = strtod (theBuffer.LinePtr, &endptr);
    if (endptr == theBuffer.LinePtr) {
      aStatus = VrmlData_NumericInputError;
      break;
    } else {
      if (isOnlyPos && aVal[i] < 0.001*Precision::Confusion()) {
        aStatus = VrmlData_IrrelevantNumber;
        break;
      }
      theBuffer.LinePtr = endptr;
    }
  }
  if (aStatus == VrmlData_StatusOK)
    if (isScale)
      theXY.SetCoord (aVal[0] * myLinearScale, aVal[1] * myLinearScale);
    else
      theXY.SetCoord (aVal[0], aVal[1]);
  return aStatus;
}

//=======================================================================
//function : ReadArrIndex
//purpose  : Read the body of the data node (comma-separated list of int
//           multiplets)
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::ReadArrIndex
                                  (VrmlData_InBuffer&         theBuffer,
                                   const Standard_Integer **& theArray,
                                   Standard_Size&             theNBlocks) const
{
  VrmlData_ErrorStatus aStatus;
  theNBlocks = 0;
  if (VrmlData_Node::OK(aStatus, ReadLine(theBuffer)))
    if (theBuffer.LinePtr[0] != '[')  // opening bracket
      aStatus = VrmlData_VrmlFormatError;
    else {
      theBuffer.LinePtr++;
      NCollection_Vector<const Standard_Integer *> vecIndice;
      NCollection_Vector<Standard_Integer>         vecInt;
      Standard_Boolean isMore (Standard_True);
      long             anIntValue;

      // Loop reading integers from the stream
      while (isMore && VrmlData_Node::OK(aStatus, ReadLine(theBuffer)))
      {
        // closing bracket, in case that it follows a comma
        if (theBuffer.LinePtr[0] == ']') {
          theBuffer.LinePtr++;
          break;
        }
        if (!VrmlData_Node::OK(aStatus, VrmlData_Node::ReadInteger(theBuffer,
                                                                   anIntValue)))
          break;
        // Check for valid delimiter (']' or ',') 
        if (!VrmlData_Node::OK(aStatus, ReadLine(theBuffer)))
          break;
        if (theBuffer.LinePtr[0] == ']') {
          theBuffer.LinePtr++;
          isMore = Standard_False;
        }
        if (anIntValue >= 0)
          // The input value is a node index, store it in the buffer vector
          vecInt.Append (static_cast<Standard_Integer> (anIntValue));
        if ((anIntValue < 0 || isMore == Standard_False)
            && vecInt.Length() > 0)
        {
          const Standard_Integer aLen = vecInt.Length();
          // The input is the end-of-face, store and close this face
          Standard_Integer * bufFace = static_cast <Standard_Integer *>
            (myAllocator->Allocate((aLen+1) * sizeof(Standard_Integer)));
          if (bufFace == 0L) {
            aStatus = VrmlData_UnrecoverableError;
            break;
          }
          bufFace[0] = aLen;
          for (Standard_Integer i = 0; i < aLen; i++)
            bufFace[i+1] = vecInt(i);
          vecInt.Clear();
          vecIndice.Append(bufFace);
        }
      }
      if (aStatus == VrmlData_StatusOK) {
        const Standard_Size aNbBlocks =
          static_cast <Standard_Size> (vecIndice.Length());
        if (aNbBlocks) {
          const Standard_Integer ** anArray =
            static_cast <const Standard_Integer **>
            (myAllocator->Allocate (aNbBlocks * sizeof(Standard_Integer *)));
          if (anArray == 0L)
            aStatus = VrmlData_UnrecoverableError;
          else {
            for (size_t i = 0; i < aNbBlocks; i++)
              anArray[i] = vecIndice(i);
            theNBlocks = aNbBlocks;
            theArray = anArray;
          } 
        }
      }
    }
  return aStatus;
}

//=======================================================================
//function : writeArrIndex
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::WriteArrIndex
                                (const char *              thePrefix,
                                 const Standard_Integer ** theArrIndex,
                                 const Standard_Size       theNbBlocks) const
{
  VrmlData_ErrorStatus aStatus (VrmlData_StatusOK);
  if (theNbBlocks && (IsDummyWrite() == Standard_False)) {
    if (VrmlData_Node::OK (aStatus,
                           WriteLine (thePrefix, "[", 1)))
    {
      const size_t aLineLimit = (myCurrentIndent < 41) ? 36 : 100;
      char buf[256];
      for (Standard_Size iBlock = 0; iBlock < theNbBlocks; iBlock++) {
        const Standard_Integer nVal (* theArrIndex[iBlock]);
        const Standard_Integer * arrVal = theArrIndex[iBlock]+1;
        switch (nVal) {
        case 1:
          sprintf (buf, "%d,", arrVal[0]);
          break;
        case 2:
          sprintf (buf, "%d,%d,", arrVal[0], arrVal[1]);
          break;
        case 3:
          sprintf (buf, "%d,%d,%d,", arrVal[0], arrVal[1], arrVal[2]);
          break;
        case 4:
          sprintf (buf, "%d,%d,%d,%d,",
                   arrVal[0], arrVal[1], arrVal[2], arrVal[3]);
          break;
        default:
          if (nVal > 0) {
            char * ptr = &buf[0];
            for (Standard_Integer i = 0; i < nVal; i++) {
              sprintf (ptr, "%d,", arrVal[i]);
              ptr = strchr (ptr, ',') + 1;
              if ((ptr - &buf[0]) > (ptrdiff_t)aLineLimit) {
                WriteLine(buf);
                ptr = &buf[0];
              }
            }
          }
        }
        WriteLine (buf, iBlock < theNbBlocks-1 ? "-1," : "-1");
      }
      if (aStatus == VrmlData_StatusOK)
        aStatus = WriteLine ("]", 0L, -1);
    }
  }
  return aStatus;
}

//=======================================================================
//function : WriteXYZ
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::WriteXYZ
                                (const gp_XYZ&          theXYZ,
                                 const Standard_Boolean isApplyScale,
                                 const char             * thePostfix) const
{
  char buf[240];
  if (IsDummyWrite() == Standard_False)
    if (isApplyScale && myLinearScale > Precision::Confusion())
      sprintf (buf, "%.12g %.12g %.12g%s", theXYZ.X() / myLinearScale,
               theXYZ.Y() / myLinearScale, theXYZ.Z() / myLinearScale,
               thePostfix ? thePostfix : "");
    else
      sprintf (buf, "%.12g %.12g %.12g%s", theXYZ.X(), theXYZ.Y(), theXYZ.Z(),
               thePostfix ? thePostfix : "");
  return WriteLine (buf);
}

//=======================================================================
//function : WriteLine
//purpose  : write the given string prepending the current indentation
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::WriteLine
                                        (const char             * theLin0,
                                         const char             * theLin1,
                                         const Standard_Integer theIndent) const
{
  static const char spaces[] = "                                        "
                               "                                        ";
  VrmlData_ErrorStatus& aStatus =
    const_cast <VrmlData_ErrorStatus&> (myStatus);
  if (IsDummyWrite())
    aStatus = VrmlData_StatusOK;
  else {
    Standard_Integer& aCurrentIndent =
      const_cast <Standard_Integer&> (myCurrentIndent);
    if (theIndent < 0)
      aCurrentIndent -= myIndent;
    if (aCurrentIndent < 0)
      aCurrentIndent = 0;
    if (theLin0 == 0L && theLin1 == 0L)
      (* myOutput) << endl;
    else {
      const Standard_Integer nSpaces = Min (aCurrentIndent, sizeof(spaces)-1);
      (* myOutput) << &spaces[sizeof(spaces)-1 - nSpaces];
      if (theLin0) {
        (* myOutput) << theLin0;
        if (theLin1)
          (* myOutput) << ' ' << theLin1;
      } else
        (* myOutput) << theLin1;
      (* myOutput) << endl;
    }
    const int stat = myOutput->rdstate();
    if (stat & ios::badbit)
      aStatus = VrmlData_UnrecoverableError;
    else if (stat & ios::failbit)
//       if (stat & ios::eofbit)
//         aStatus = VrmlData_EndOfFile;
//       else
      aStatus = VrmlData_GeneralError;
    if (theIndent > 0)
      aCurrentIndent += myIndent;
  }
  return myStatus;
}

//=======================================================================
//function : WriteNode
//purpose  : 
//=======================================================================

VrmlData_ErrorStatus VrmlData_Scene::WriteNode
                                (const char *                 thePrefix,
                                 const Handle(VrmlData_Node)& theNode) const
{
  VrmlData_ErrorStatus aStatus (VrmlData_StatusOK);
  Standard_Boolean isNoName (Standard_False);
  if (theNode->Name() == 0L)
    isNoName = Standard_True;
  else if (theNode->Name()[0] == '\0')
    isNoName = Standard_True;

  if (theNode.IsNull() == Standard_False)
    if (theNode->IsDefault() == Standard_False) {
      if (isNoName && IsDummyWrite()) {
        // We are in a tentative 'write' session (nothing is written).
        // The goal is to identify multiply referred nodes.
        Standard_Address addrNode = theNode.operator->();
        if (!const_cast<NCollection_Map<Standard_Address>&>(myUnnamedNodesOut)
            .Add (addrNode))
        {
          Handle(VrmlData_UnknownNode) bidNode = new VrmlData_UnknownNode;
          char buf[32];
          do {
            sprintf (buf, "_%d",
                     ++const_cast<Standard_Integer&>(myAutoNameCounter));
            bidNode->myName = &buf[0];
          } while (myNamedNodes.Contains (bidNode));
          // We found the vacant automatic name, let us assign to it.
          theNode->setName (&buf[0]);
          const_cast<VrmlData_MapOfNode&>(myNamedNodes).Add (theNode);
          return aStatus; // do not search under already duplicated node
        }
      }
      if (isNoName)
        aStatus = theNode->Write (thePrefix);
      else {
        // If the node name consists of blank characters, we do not write it
        const char * nptr = theNode->Name();
        for (; * nptr != '\0'; nptr++)
          if (* nptr != ' ' && * nptr != '\t')
            break;
        if (* nptr == '\0')
          aStatus = theNode->Write (thePrefix);
        else {
          // Name is written under DEF clause
          TCollection_AsciiString buf;
          if (myNamedNodesOut.Contains (theNode))
          {
            buf += "USE ";
            buf += theNode->Name();
            aStatus = WriteLine (thePrefix, buf.ToCString());
          } 
          else 
          {
            if (thePrefix)
            {
              buf += thePrefix;
              buf += ' ';
            }
            buf += "DEF ";
            buf += theNode->Name();
            aStatus = theNode->Write (buf.ToCString());
            const_cast<VrmlData_MapOfNode&>(myNamedNodesOut).Add (theNode);
          }
        }
      }
    }
  return aStatus;
}

//=======================================================================
//function : Dump
//purpose  : 
//=======================================================================

void VrmlData_Scene::Dump (Standard_OStream& theStream) const
{
  theStream << " ===== Diagnostic Dump of a Scene (" << myAllNodes.Extent()
            << " nodes)" << endl;

  /*
  Iterator anIterA(myAllNodes);
  for (; anIterA.More(); anIterA.Next())
    dumpNode(theStream, anIterA.Value(), "");
  */
  Iterator anIter(myLstNodes);
  for (; anIter.More(); anIter.Next())
    dumpNode(theStream, anIter.Value(), "  ");
}

//=======================================================================
//function : dumpNode
//purpose  : static (local) function
//=======================================================================

void dumpNode (Standard_OStream&                theStream,
               const Handle(VrmlData_Node)&     theNode,
               const TCollection_AsciiString&   theIndent)
{
  if (theNode.IsNull())
    return;
  TCollection_AsciiString aNewIndent = 
    theIndent.IsEmpty() ? theIndent : theIndent + "  "; 
  if (theNode->IsKind(STANDARD_TYPE(VrmlData_Appearance))) {
    const Handle(VrmlData_Appearance) anAppearance = 
      Handle(VrmlData_Appearance)::DownCast (theNode);
    dumpNodeHeader (theStream, theIndent, "Appearance", theNode->Name());
    if (theIndent.IsEmpty() == Standard_False) {
      dumpNode (theStream, anAppearance->Material(), aNewIndent);
      dumpNode (theStream, anAppearance->Texture(), aNewIndent);
      dumpNode (theStream, anAppearance->TextureTransform(), aNewIndent);
    }
  } else if (theNode->IsKind(STANDARD_TYPE(VrmlData_ShapeNode))) {
    const Handle(VrmlData_ShapeNode) aShape = 
      Handle(VrmlData_ShapeNode)::DownCast (theNode);
    dumpNodeHeader (theStream, theIndent, "Shape", theNode->Name());
    if (theIndent.IsEmpty() == Standard_False) {
      dumpNode (theStream, aShape->Appearance(), aNewIndent);
      dumpNode (theStream, aShape->Geometry(), aNewIndent);
    }
  } else if (theNode->IsKind(STANDARD_TYPE(VrmlData_Box)))
    dumpNodeHeader (theStream, theIndent, "Box", theNode->Name());
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_Cylinder)))
    dumpNodeHeader (theStream, theIndent, "Cylinder", theNode->Name());
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_Sphere)))
    dumpNodeHeader (theStream, theIndent, "Sphere", theNode->Name());
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_Cone)))
    dumpNodeHeader (theStream, theIndent, "Cone", theNode->Name());
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_Coordinate)))
    dumpNodeHeader (theStream, theIndent, "Coordinate", theNode->Name());
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_Group))) {
    const Handle(VrmlData_Group) aGroup = 
      Handle(VrmlData_Group)::DownCast (theNode);
    char buf[64];
    sprintf (buf, "Group (%s)",
             aGroup->IsTransform() ? "Transform" : "Group");
    dumpNodeHeader (theStream, theIndent, buf, theNode->Name());
    if (theIndent.IsEmpty() == Standard_False) {
      VrmlData_ListOfNode::Iterator anIter = aGroup->NodeIterator();
      for (; anIter.More(); anIter.Next())
        dumpNode (theStream, anIter.Value(), aNewIndent);
    }
  } else if (theNode->IsKind(STANDARD_TYPE(VrmlData_ImageTexture)))
    dumpNodeHeader (theStream, theIndent, "ImageTexture", theNode->Name());
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_IndexedFaceSet))) {
    const Handle(VrmlData_IndexedFaceSet) aNode =
      Handle(VrmlData_IndexedFaceSet)::DownCast(theNode);
    const Standard_Integer ** ppDummy; 
    const Standard_Size nCoord = aNode->Coordinates()->Length();
    const Standard_Size nPoly  = aNode->Polygons (ppDummy);
    char buf[64];
    sprintf (buf, "IndexedFaceSet (%d vertices, %d polygons)", nCoord, nPoly);
    dumpNodeHeader (theStream, theIndent, buf, theNode->Name());
  } else if (theNode->IsKind(STANDARD_TYPE(VrmlData_IndexedLineSet))) {
    const Handle(VrmlData_IndexedLineSet) aNode =
      Handle(VrmlData_IndexedLineSet)::DownCast(theNode);
    const Standard_Integer ** ppDummy; 
    const Standard_Size nCoord = aNode->Coordinates()->Length();
    const Standard_Size nPoly  = aNode->Polygons (ppDummy);
    char buf[64];
    sprintf (buf, "IndexedLineSet (%d vertices, %d polygons)", nCoord, nPoly);
    dumpNodeHeader (theStream, theIndent, buf, theNode->Name());
  } else if (theNode->IsKind(STANDARD_TYPE(VrmlData_Material))) {
//     const Handle(VrmlData_Material) aMaterial = 
//       Handle(VrmlData_Material)::DownCast (theNode);
    dumpNodeHeader (theStream, theIndent, "Material", theNode->Name());
  }
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_Normal)))
    dumpNodeHeader (theStream, theIndent, "Normal", theNode->Name());
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_TextureCoordinate)))
    dumpNodeHeader (theStream, theIndent, "TextureCoordinate", theNode->Name());
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_WorldInfo)))
    dumpNodeHeader (theStream, theIndent, "WorldInfo", theNode->Name());
  else if (theNode->IsKind(STANDARD_TYPE(VrmlData_UnknownNode))) {
    const Handle(VrmlData_UnknownNode) anUnknown = 
      Handle(VrmlData_UnknownNode)::DownCast (theNode);
    char buf[64];
    sprintf (buf, "Unknown (%s)", anUnknown->GetTitle().ToCString());
    dumpNodeHeader (theStream, theIndent, buf, theNode->Name());
  }
}

//=======================================================================
//function : dumpNodeHeader
//purpose  : 
//=======================================================================

void dumpNodeHeader (Standard_OStream&                  theStream,
                     const TCollection_AsciiString&     theIndent,
                     const char *                       theType,
                     const char *                       theName)
{
  theStream << theIndent << theType <<" node";
  if (theName[0] == '\0')
    theStream << endl;
  else
    theStream << ": \"" << theName << '\"' << endl;
}
