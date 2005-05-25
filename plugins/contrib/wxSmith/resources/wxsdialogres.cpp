#include "wxsdialogres.h"

#include "../wxswidgetfactory.h"
#include "../wxswindoweditor.h"
#include <manager.h>


wxsDialogRes::wxsDialogRes(wxsProject* Project,const wxString& Class, const wxString& Xrc, const wxString& Src,const wxString& Head):
    wxsResource(Project),
    ClassName(Class),
    XrcFile(Xrc),
    SrcFile(Src),
    HFile(Head)
{
    Dialog = dynamic_cast<wxsDialog*>(wxsWidgetFactory::Get()->Generate("wxDialog"));
}

wxsDialogRes::~wxsDialogRes()
{
// TODO (SpOoN#1#): Need to kill dialog but this caused crashes
//    wxsWidgetFactory::Get()->Kill(Dialog);
    GetProject()->DeleteDialog(this);
}

wxsEditor* wxsDialogRes::CreateEditor()
{
    wxsWindowEditor* Edit = new wxsWindowEditor(Manager::Get()->GetAppWindow(),XrcFile,this);
    Edit->BuildPreview(Dialog);
    return Edit;
}

void wxsDialogRes::Save()
{
    TiXmlDocument* Doc = GenerateXml();
    
    if ( Doc )
    {
        Doc->SaveFile(XrcFile);
        delete Doc;
    }
}

TiXmlDocument* wxsDialogRes::GenerateXml()
{
    TiXmlDocument* NewDoc = new TiXmlDocument;
    TiXmlElement* Resource = NewDoc->InsertEndChild(TiXmlElement("resource"))->ToElement();
    if ( !Dialog->XmlSave(Resource) )
    {
        delete NewDoc;
        return NULL;
    }
    return NewDoc;
}
