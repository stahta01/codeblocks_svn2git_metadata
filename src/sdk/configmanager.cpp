/*
* This file is part of Code::Blocks Studio, an open-source cross-platform IDE
* Copyright (C) 2003  Yiannis An. Mandravellos
*
* This program is distributed under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
*
* Author: Thomas Denk
*
* $Id$
* $Date$
*/

#include "sdk_precomp.h"
#include "configmanager.h"
#include "personalitymanager.h"
#include "cbexception.h"
#include "base64.h"
#include "crc32.h"

#include <wx/file.h>
#include <wx/url.h>
#include <wx/stream.h>

#ifdef __WXMSW__
#include <shlobj.h>
#endif

wxString ConfigManager::app_path = wxEmptyString;
wxString ConfigManager::data_path = wxEmptyString;
wxString ConfigManager::config_folder = wxEmptyString;
wxString ConfigManager::home_folder = wxEmptyString;


namespace CfgMgrConsts
{
    const wxString app_path(_T("app_path"));
    const wxString data_path(_T("data_path"));
    const char* rootTag = _T("CodeBlocksConfig");
    const int version = 1;
}


ISerializable::ISerializable()
{}

ISerializable::~ISerializable()
{}




/* ------------------------------------------------------------------------------------------------------------------
*  "Builder pattern" class for ConfigManager
*  Do not use this class  -  Manager::Get()->GetConfigManager() is a lot friendlier
*/

CfgMgrBldr::CfgMgrBldr() : doc(0), volatile_doc(0), r(false)
{
    wxString personality(Manager::Get()->GetPersonalityManager()->GetPersonality());
    if(personality.StartsWith("http://"))
        SwitchToR(personality);
    else
        SwitchTo(ConfigManager::GetConfigFolder() + personality + _T(".conf"));
}

void CfgMgrBldr::SwitchTo(const wxString& absFileName)
{
    Close();
    doc = new TiXmlDocument(absFileName.mb_str());
    if(!doc->LoadFile()
            && !doc->LoadFile((ConfigManager::GetExecutableFolder() + _T("/default.conf")).mb_str())
            && !doc->LoadFile((ConfigManager::GetConfigFolder() + _T("/default.conf")).mb_str()))
    {
        doc->InsertEndChild(TiXmlDeclaration(_T("1.0"), _T("UTF-8"), _T("yes")));
        doc->InsertEndChild(TiXmlElement(CfgMgrConsts::rootTag));
        doc->FirstChildElement(CfgMgrConsts::rootTag)->SetAttribute(_T("version"), CfgMgrConsts::version);
        doc->SetCondenseWhiteSpace(false);
    }

    TiXmlElement* docroot = doc->FirstChildElement(CfgMgrConsts::rootTag);
    if(!docroot)
    {
        wxString s;
        s.sprintf(_("Fatal error parsing configuration. The file  %s is not a valid Code::Blocks config file."), doc->Value());
        cbThrow(s);
    }

    if(doc->ErrorId())
        Manager::Get()->GetMessageManager()->DebugLogError(wxString(_T("TinyXML error:\n")) << doc->ErrorDesc());

    const char *vers = docroot->Attribute(_T("version"));
    if(!vers || atoi(vers) != 1)
        Manager::Get()->GetMessageManager()->DebugLog(_("MessageManager encountered an unknown config file version. Continuing happily."));

    doc->ClearError();
}

void CfgMgrBldr::SwitchToR(const wxString& absFileName)
{
    Close();

    wxURL url(absFileName);
    url.SetProxy(ConfigManager::GetProxy());
    if (url.GetError() == wxURL_NOERR)
    {
        wxInputStream *is = url.GetInputStream();
        if (is && is->IsOk())
        {
            size_t size = is->GetSize();
            wxString str;
            wxChar* c = str.GetWriteBuf(size);
            is->Read(c, size);
            str.UngetWriteBuf(size);

            doc = new TiXmlDocument();

            if(doc->Parse(str.c_str()))
            {
                doc->ClearError();
                delete is;
                return;
            }
            Manager::Get()->GetMessageManager()->DebugLog(_("### Error parsing remote config file"));
            Manager::Get()->GetMessageManager()->DebugLog(doc->ErrorDesc());
        }
        delete is;
    }
    SwitchTo(wxEmptyString); // fall back
}

CfgMgrBldr::~CfgMgrBldr()
{
    NamespaceMap::iterator it;
    for( it = namespaces.begin(); it != namespaces.end(); ++it )
    {
        delete it->second;
    }
    namespaces.clear();
    Close();
}


void CfgMgrBldr::Close()
{
    if(doc && r == false)
    {
        doc->SaveFile();
        delete doc;
        doc = 0;
    }
    r = false;
}


ConfigManager* CfgMgrBldr::Get(const wxString& name_space)
{
    return Instance()->Instantiate(name_space);
}

CfgMgrBldr* CfgMgrBldr::Instance()
{
    static CfgMgrBldr instance;
    return &instance;
}


ConfigManager* CfgMgrBldr::Instantiate(const wxString& name_space)
{
    if(name_space.IsEmpty())
        cbThrow(_("You attempted to get a ConfigManager instance without providing a namespace."));

    wxCriticalSectionLocker locker(cs);
    NamespaceMap::iterator it = namespaces.find(name_space);
    if(it != namespaces.end())
        return it->second;

    TiXmlElement* docroot;

    if(name_space.StartsWith(_T("volatile:")))
    {
        if(!volatile_doc)
        {
            volatile_doc = new TiXmlDocument();
            volatile_doc->InsertEndChild(TiXmlElement(CfgMgrConsts::rootTag));
            volatile_doc->SetCondenseWhiteSpace(false);
        }
        docroot = volatile_doc->FirstChildElement(CfgMgrConsts::rootTag);
    }
    else
    {
        docroot = doc->FirstChildElement(CfgMgrConsts::rootTag);
        if(!docroot)
        {
            wxString err(_("Fatal error parsing supplied configuration file.\nParser error message:\n"));
            err << doc->ErrorDesc();
            cbThrow(err);
        }
    }

    TiXmlElement* root = docroot->FirstChildElement(name_space);

    if(!root) // namespace does not exist
    {
        docroot->InsertEndChild(TiXmlElement(name_space));
        root = docroot->FirstChildElement(name_space);
    }

    if(!root) // now what!
        cbThrow(_("Unable to create namespace in document tree (actually not possible..?)"));

    ConfigManager *c = new ConfigManager(root);
    namespaces[name_space] = c;

    return c;
}




/* ------------------------------------------------------------------------------------------------------------------
*  Functions to retrieve system paths and locate data files in a defined, consistent way.
*  Please note that the application determines app_path and data_path at runtime and passes the results
*  to ConfigManager. GetExecutableFolder() and GetDataFolder() are therefore under normal conditions
*  simply more efficient shortcuts for Read("app_path") and Read("data_path").
*/

wxString ConfigManager::GetProxy()
{
    return Manager::Get()->GetConfigManager(_T("app"))->Read(_T("network_proxy"));
}

wxString ConfigManager::GetExecutableFolder()
{
    if(!ConfigManager::app_path.IsEmpty())
        return ConfigManager::app_path;

#ifdef __WXMSW__

    wxChar name[MAX_PATH];
    GetModuleFileName(0L, name, MAX_PATH);
    wxFileName fname(name);
    ConfigManager::app_path = fname.GetPath(wxPATH_GET_VOLUME);
#else

    ConfigManager::app_path = wxString(SELFPATH,wxConvUTF8);
    ConfigManager::app_path = wxFileName(base).GetPath();
    if (ConfigManager::app_path.IsEmpty())
        ConfigManager::app_path = _T(".");
#endif

    return ConfigManager::app_path;
}

wxString ConfigManager::GetHomeFolder()
{
    if(ConfigManager::home_folder.IsEmpty())
    {
        wxFileName f;
        f.AssignHomeDir();
        ConfigManager::home_folder = f.GetFullPath();
    }
    return ConfigManager::home_folder;
}

#ifdef __WXMSW__

wxString ConfigManager::GetConfigFolder()
{
    if(ConfigManager::config_folder.IsEmpty())
    {
        TCHAR szPath[MAX_PATH];
        SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, szPath);
        ConfigManager::config_folder = wxString(szPath) + _T("/codeblocks");
    }
    if(!wxDirExists(ConfigManager::config_folder))
        wxMkdir(ConfigManager::config_folder);

    return ConfigManager::config_folder;
}

wxString ConfigManager::GetPluginsFolder()
{
    return GetDataFolder() + _T("/plugins");
}

wxString ConfigManager::GetScriptsFolder()
{
    return GetDataFolder() + _T("/scripts");
}

wxString ConfigManager::GetDataFolder()
{
    if(ConfigManager::data_path.IsEmpty())
        ConfigManager::data_path = GetExecutableFolder() + _T("/share/codeblocks");
    return ConfigManager::data_path;
}

wxString ConfigManager::LocateDataFile(const wxString& filename)
{
    wxPathList searchPaths;
    searchPaths.Add(GetDataFolder());
    searchPaths.Add(GetExecutableFolder());
    searchPaths.Add(GetHomeFolder());
    searchPaths.AddEnvList(_T("PATH"));
    searchPaths.Add(_T("C:/"));
    return searchPaths.FindValidPath(filename);
}

#else


wxString ConfigManager::GetConfigFolder()  const
{
    if(ConfigManager::config_folder.IsEmpty())
        ConfigManager::config_folder = GetHomeFolder() + _T("/.codeblocks");
    if(!wxDir::Exists(ConfigManager::config_folder))
        wxMkdir(ConfigManager::config_folder);

    return ConfigManager::config_folder;
}

wxString ConfigManager::GetPluginsFolder()  const
{
    return GetDataFolder() + _T("/plugins");
}

wxString ConfigManager::GetScriptsFolder()
{
    return GetDataFolder() + _T("/scripts");
}

wxString ConfigManager::GetDataFolder()  const
{
    if(ConfigManager::data_path.IsEmpty())
        ConfigManager::data_path(_T("/usr/share/codeblocks"));
    return ConfigManager::data_path;
}

wxString ConfigManager::LocateDataFile(const wxString& filename)  const
{
    wxPathList searchPaths;
    searchPaths.Add(GetDataFolder());
    searchPaths.Add(GetHomeFolder());
    searchPaths.AddEnvList(_T("PATH"));
    searchPaths.Add(_T("/usr/share/"));
    return searchPaths.FindValidPath(filename);
}

#endif




/* ------------------------------------------------------------------------------------------------------------------
*  ConfigManager
*/

ConfigManager::ConfigManager(TiXmlElement* r) : root(r), pathNode(r)
{
    doc = root->GetDocument();
}




/* ------------------------------------------------------------------------------------------------------------------
*  Configuration path handling
*  Note that due to namespaces, you do no longer need to care about saving/restoring the current path in the normal case.
*  Mostly, there will be only one module working with one namespace, and every namespace keeps track of its own path.
*  Also, please note that GetPath() is more expensive than it seems (not a mere accessor to a member variable!), while
*  SetPath() not only sets the current path, but also creates the respective nodes in the XML document if these don't exist.
*/

wxString ConfigManager::GetPath() const
{
    TiXmlElement *e = pathNode;
    wxString ret;
    ret.Alloc(64);

    ret = e->Value();
    while((e = e->Parent()->ToElement()) && e != root)
    {
        ret.Prepend(_T("/"));
        ret.Prepend(e->Value());
    }
    ret.Prepend(_T("/"));
    return ret;
};

void ConfigManager::SetPath(const wxString& path)
{
    wxString p(path + _T("/"));
    pathNode = AssertPath(p);
}


TiXmlElement* ConfigManager::AssertPath(wxString& path)
{
    if(doc->ErrorId())
    {
        Manager::Get()->GetMessageManager()->DebugLogError(wxString(_T("TinyXML error:\n")) << doc->ErrorDesc());
        doc->ClearError();
    }
    path.Replace(_T(" "), _T("_"));
    path.Replace(_T("///"), _T("/"));
    path.Replace(_T("//"),  _T("/"));

    if(!path.Contains(_T("/")))
    {
        path.UpperCase();
        if(path[0] < _T('A') || path[0] > _T('Z'))
        {
            wxString s;
            s.sprintf(_("Warning: The Configuration key %s does not meet the standard for variable naming. Variables names are required to start with a letter."), path.mb_str());
            Manager::Get()->GetMessageManager()->DebugLogError(s);
        }
        return pathNode;
    }

    TiXmlElement* e = pathNode ? pathNode : root;

    path.LowerCase();

    wxString sub;
    do
    {
        sub = path.BeforeFirst(_T('/'));
        path = path.AfterFirst(_T('/'));

        if(sub.IsEmpty())
            e = root;
        else if(sub.IsSameAs(_T(".")))
            ;
        else if(e != root && sub.IsSameAs(_T("..")))
            e = e->Parent()->ToElement();
        else if(sub[0] < _T('a') || sub[0] > _T('z'))
        {
        wxString s;
        s.sprintf(_("Warning: The subpath %s does not meet the standard for path naming. Paths and subpaths are required to start with a letter."), sub.mb_str());
        Manager::Get()->GetMessageManager()->DebugLogError(s);
        }
        else
        {
            TiXmlElement* n = e->FirstChildElement(sub);
            if(n)
                e = n;
            else
                e = (TiXmlElement*) e->InsertEndChild(TiXmlElement(sub));
        }
        if(doc->Error())
        {
            Manager::Get()->GetMessageManager()->DebugLog(wxString(_T("Error accessing config path: ")) + doc->ErrorDesc());
            doc->ClearError();
        }
    }
    while(path.Contains(_T("/")));

    path.UpperCase();
    if(!path.IsEmpty() && (path[0] < _T('A') || path[0] > _T('Z')))
    {
        wxString s;
        s.sprintf(_("Warning: The Configuration key %s does not meet the standard for variable naming. Variables names are required to start with a letter."), path.mb_str());
        Manager::Get()->GetMessageManager()->DebugLogError(s);
    }
    return e;
}


/* -----------------------------------------------------------------------------------------------------
*  Clear all nodes from your namespace or delete the namespace alltogether (removing it from the config file).
*  After Delete() returns, the pointer to your instance is invalid.
*/

void ConfigManager::Clear()
{
    root->Clear();
}

void ConfigManager::Delete()
{
    CfgMgrBldr * bld = CfgMgrBldr::Instance();
    wxString ns(root->Value());

    root->Clear();
    doc->RootElement()->RemoveChild(root);

    wxCriticalSectionLocker(bld->cs);
    NamespaceMap::iterator it = bld->namespaces.find(ns);
    if(it != bld->namespaces.end())
        bld->namespaces.erase(it);

    delete this;
}

void ConfigManager::DeleteAll()
{
    CfgMgrBldr * bld = CfgMgrBldr::Instance();
    wxString ns(root->Value());

    if(!ns.IsSameAs(_T("app")))
        cbThrow(_T("Illegal attempt to invoke DeleteAll()."));

    wxCriticalSectionLocker(bld->cs);
    doc->RootElement()->Clear();
    for(NamespaceMap::iterator it = bld->namespaces.begin(); it != bld->namespaces.end(); ++it)
    {
        delete it->second;
        bld->namespaces.erase(it);
    }
}


/* ------------------------------------------------------------------------------------------------------------------
*  Utility functions for writing nodes
*/

TiXmlElement* ConfigManager::GetUniqElement(TiXmlElement* p, const wxString& q)
{
    TiXmlElement* r;
    if(r = p->FirstChildElement(q.mb_str()))
        return r;

    return (TiXmlElement*)(p->InsertEndChild(TiXmlElement(q.mb_str())));
}

void ConfigManager::SetNodeText(TiXmlElement* n, const TiXmlText& t)
{
    TiXmlNode *c = n->FirstChild();
    if(c)
        n->ReplaceChild(c, t);
    else
        n->InsertEndChild(t);
}



/* ------------------------------------------------------------------------------------------------------------------
*  Write and read values
*  Regardless of namespaces, the string keys app_path and data_path always refer to the location of the application's executable
*  and the data path, respectively. These values are never saved to the configuration, but kept in static variables.
*  The application makes use of this by "writing" to the configuration file after determining these values at runtime.
*/
void ConfigManager::Write(const wxString& name,  const wxString& value, bool ignoreEmpty)
{
    if(name.IsSameAs(CfgMgrConsts::app_path))
    {
        ConfigManager::app_path = value;
        return;
    }
    else if(name.IsSameAs(CfgMgrConsts::data_path))
    {
        ConfigManager::data_path = value;
        return;
    }
    if(ignoreEmpty && value.IsEmpty())
        return;

    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlElement *str = GetUniqElement(e, key);

    TiXmlElement *s = GetUniqElement(str, _T("str"));
    SetNodeText(s, TiXmlText(value.mb_str()));
}

void ConfigManager::Write(const wxString& key, const char* str)
{
    Write(key, wxString(str));
};

wxString ConfigManager::Read(const wxString& name, const wxString& defaultVal)
{
    if(name.IsSameAs(CfgMgrConsts::app_path))
        return ConfigManager::app_path;
    else if(name.IsSameAs(CfgMgrConsts::data_path))
        return ConfigManager::data_path;

    wxString ret;

    if(Read(name, &ret))
        return ret;
    else
        return defaultVal;
}

bool ConfigManager::Read(const wxString& name, wxString* str)
{
    if(name.IsSameAs(CfgMgrConsts::app_path))
    {
        str->assign(ConfigManager::app_path);
        return true;
    }
    else if(name.IsSameAs(CfgMgrConsts::data_path))
    {
        str->assign(ConfigManager::data_path);
        return true;
    }

    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlText *t = (TiXmlText *) parentHandle.FirstChild(key).FirstChild(_T("str")).FirstChild().Node();

    if(t)
    {
        str->assign(t->Value());
        return true;
    }
    return false;
}

void ConfigManager::Write(const wxString& name,  const wxColour& c)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlElement *leaf = GetUniqElement(e, key);

    TiXmlElement *s = GetUniqElement(leaf, _T("colour"));
    s->SetAttribute("r", c.Red());
    s->SetAttribute("g", c.Green());
    s->SetAttribute("b", c.Blue());
}

wxColour ConfigManager::ReadColour(const wxString& name, const wxColour& defaultVal)
{
    wxColour ret;

    if(Read(name, &ret))
        return ret;
    else
        return defaultVal;
}

bool ConfigManager::Read(const wxString& name, wxColour* ret)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlElement *c = (TiXmlElement *) parentHandle.FirstChild(key).FirstChild(_T("colour")).Element();

    if(c)
    {
        int r, g, b;
        if(c->QueryIntAttribute(_T("r"), &r) == TIXML_SUCCESS
                && c->QueryIntAttribute(_T("g"), &g) == TIXML_SUCCESS
                && c->QueryIntAttribute(_T("b"), &b) == TIXML_SUCCESS)
            ret->Set(r, b, g);
        return true;
    }
    return false;
}

void ConfigManager::Write(const wxString& name,  int value)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);
    TiXmlElement *leaf = GetUniqElement(e, key);

    leaf->SetAttribute(_T("int"), value);
}

int  ConfigManager::ReadInt(const wxString& name,  int defaultVal)
{
    int ret;

    if(Read(name, &ret))
        return ret;
    else
        return defaultVal;
}

bool ConfigManager::Read(const wxString& name,  int* value)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlElement *leaf = parentHandle.FirstChild(key).Element();

    if(leaf)
        return leaf->QueryIntAttribute(_T("int"), value) == TIXML_SUCCESS;
    return false;
}


void ConfigManager::Write(const wxString& name,  bool value)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);
    TiXmlElement *leaf = GetUniqElement(e, key);

    leaf->SetAttribute(_T("bool"), value ? _T("1") : _T("0"));
}

bool  ConfigManager::ReadBool(const wxString& name,  bool defaultVal)
{
    bool ret;

    if(Read(name, &ret))
        return ret;
    else
        return defaultVal;
}

bool ConfigManager::Read(const wxString& name,  bool* value)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlElement *leaf = parentHandle.FirstChild(key).Element();

    if(leaf && leaf->Attribute("bool"))
    {
        *value = leaf->Attribute("bool")[0] == _T('1');
        return true;
    }
    return false;
}


void ConfigManager::Write(const wxString& name,  double value)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);
    TiXmlElement *leaf = GetUniqElement(e, key);

    leaf->SetDoubleAttribute(_T("double"), value);
}

double  ConfigManager::ReadDouble(const wxString& name,  double defaultVal)
{
    double ret;

    if(Read(name, &ret))
        return ret;
    else
        return defaultVal;
}

bool ConfigManager::Read(const wxString& name,  double* value)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlElement *leaf = parentHandle.FirstChild(key).Element();

    if(leaf)
        return leaf->QueryDoubleAttribute(_T("double"), value) == TIXML_SUCCESS;
    return false;
}


void ConfigManager::Set(const wxString& name)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);
    GetUniqElement(e, key);
}

void ConfigManager::UnSet(const wxString& name)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlNode *leaf = GetUniqElement(e, key);
    e->RemoveChild(leaf);
}

bool ConfigManager::Exists(const wxString& name)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlElement *leaf = parentHandle.FirstChild(key).Element();

    return leaf;
}



void ConfigManager::Write(const wxString& name,  const wxArrayString& arrayString)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlElement *leaf = GetUniqElement(e, key);

    TiXmlElement *as;
    as = GetUniqElement(leaf, _T("astr"));
    leaf->RemoveChild(as);
    as = GetUniqElement(leaf, _T("astr"));

    for(unsigned int i = 0; i < arrayString.GetCount(); ++i)
    {
        TiXmlElement s("s");
        s.InsertEndChild(TiXmlText(arrayString[i].mb_str()));
        as->InsertEndChild(s);
    }
}

void ConfigManager::Read(const wxString& name, wxArrayString *arrayString)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlNode *asNode = parentHandle.FirstChild(key).FirstChild(_T("astr")).Node();

    TiXmlNode *curr = 0;
    if(asNode)
    {
        while(curr = asNode->IterateChildren(_T("s"), curr)->ToElement())
            arrayString->Add(curr->FirstChild()->ToText()->Value());
    }
}

wxArrayString ConfigManager::ReadArrayString(const wxString& name)
{
    wxArrayString as;
    Read(name, &as);
    return as;
}

void ConfigManager::WriteBinary(const wxString& name,  const wxString& source)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlElement *str = GetUniqElement(e, key);

    TiXmlElement *s = GetUniqElement(str, _T("bin"));
    s->SetAttribute(_T("crc"), wxCrc32::FromString(source));
    SetNodeText(s, TiXmlText(wxBase64::Encode(source).mb_str()));
}

void ConfigManager::WriteBinary(const wxString& name,  void* ptr, size_t len)
{
    wxString s((const char*)ptr, len);
    WriteBinary(name,  s);
}

wxString ConfigManager::ReadBinary(const wxString& name)
{
    wxString str;
    wxString key(name);
    TiXmlElement* e = AssertPath(key);
    unsigned int crc;

    TiXmlHandle parentHandle(e);
    TiXmlElement* bin = parentHandle.FirstChild(key).FirstChild(_T("bin")).Element();

    if(!bin)
        return wxEmptyString;

    if(bin->QueryIntAttribute(_T("crc"), (int*)&crc) != TIXML_SUCCESS)
        return wxEmptyString;

    TiXmlText *t = bin->FirstChild()->ToText();
    if (t)
    {
        str.assign(t->Value());
        str = wxBase64::Decode(str);
        if(crc ==  wxCrc32::FromString(str))
            return str;
    }
    return wxEmptyString;
}


wxArrayString ConfigManager::EnumerateSubPaths(const wxString& path)
{
    wxString key(path + _T("/")); // the trailing slash hack is required because AssertPath expects a key name
    TiXmlNode* e = AssertPath(key);
    wxArrayString ret;

    TiXmlElement *curr = 0;
    if(e)
    {
        while(curr = e->IterateChildren(curr)->ToElement())
        {
            wxChar c = *(curr->Value());
            if(c < _T('A') || c > _T('Z')) // first char must be a letter, uppercase letters are key names
                ret.Add(curr->Value());
        }
    }
    return ret;
}


void ConfigManager::DeleteSubPath(const wxString& thePath)
{
    if(thePath.IsSameAs(_T("/"))) // does not remove root!
        return;

    wxString path(thePath.Lower());

    if(path.Last() == _T('/'))
        path.RemoveLast();

    TiXmlElement* parent = pathNode ? pathNode : root;

    if(path.Contains(_T("/")))
    {
        path.Replace(_T("///"), _T("/"));
        path.Replace(_T("//"),  _T("/"));

        wxString sub;
        do
        {
            sub = path.BeforeFirst(_T('/'));
            path = path.AfterFirst(_T('/'));

            if(sub.IsEmpty())
                parent = root;
            else if(sub.IsSameAs(_T(".")))
                ;
            else if(parent != root && sub.IsSameAs(_T("..")))
                parent = parent->Parent()->ToElement();
            else
            {
                TiXmlElement* n = parent->FirstChildElement(sub);
                if(n)
                    parent = n;
                else
                    return;
            }
        }
        while(path.Contains(_T("/")));
    }

    if(!path.IsEmpty())
    {
        if(TiXmlNode *toRemove = parent->FirstChild(path))
        {
            toRemove->Clear();
            parent->RemoveChild(toRemove);
        }
    }
}


wxArrayString ConfigManager::EnumerateKeys(const wxString& path)
{
    wxString key(path + _T("/")); // the trailing slash hack is required because AssertPath expects a key name
    TiXmlNode* e = AssertPath(key);
    wxArrayString ret;

    TiXmlElement *curr = 0;
    if(e)
    {
        while(curr = e->IterateChildren(curr)->ToElement())
        {
            wxChar c = *(curr->Value());
            if(c >= _T('A') && c <= _T('Z')) // opposite of the above
                ret.Add(curr->Value());
        }
    }
    return ret;
}

void ConfigManager::Write(const wxString& name, const ISerializable& object)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlElement *obj = GetUniqElement(e, key);

    TiXmlElement *s = GetUniqElement(obj, _T("obj"));
    SetNodeText(s, TiXmlText(wxBase64Encode(object.SerializeOut()).mb_str()));
}

bool ConfigManager::Read(const wxString& name, ISerializable* object)
{
    wxString str;
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlText *t = (TiXmlText *) parentHandle.FirstChild(key).FirstChild(_T("obj")).FirstChild().Node();

    if(t)
    {
        str.assign(t->Value());
        object->SerializeIn(wxBase64Decode(str));
    }
    return wxEmptyString;
}

void ConfigManager::Write(const wxString& name, const ConfigManagerContainer::StringToStringMap& map)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlElement *leaf = GetUniqElement(e, key);

    TiXmlElement *mNode;
    mNode = GetUniqElement(leaf, _T("ssmap"));
    leaf->RemoveChild(mNode);
    mNode = GetUniqElement(leaf, _T("ssmap"));

    for(ConfigManagerContainer::StringToStringMap::const_iterator it = map.begin(); it != map.end(); ++it)
    {
        TiXmlElement s(it->first);
        s.InsertEndChild(TiXmlText(it->second));
        mNode->InsertEndChild(s);
    }
}

void ConfigManager::Read(const wxString& name, ConfigManagerContainer::StringToStringMap* map)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlNode *mNode = parentHandle.FirstChild(key).FirstChild(_T("ssmap")).Node();

    TiXmlNode *curr = 0;
    if(mNode)
    {
        while(curr = mNode->IterateChildren(curr)->ToElement())
            (*map)[curr->Value()] = curr->FirstChild()->ToText()->Value();
    }
}

ConfigManagerContainer::StringToStringMap ConfigManager::ReadSSMap(const wxString& name)
{
    ConfigManagerContainer::StringToStringMap ret;
    Read(name, &ret);
    return ret;
}

void ConfigManager::Write(const wxString& name, const ConfigManagerContainer::IntToStringMap& map)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlElement *leaf = GetUniqElement(e, key);

    TiXmlElement *mNode;
    mNode = GetUniqElement(leaf, _T("ismap"));
    leaf->RemoveChild(mNode);
    mNode = GetUniqElement(leaf, _T("ismap"));

    wxString tmp;
    for(ConfigManagerContainer::IntToStringMap::const_iterator it = map.begin(); it != map.end(); ++it)
    {
        tmp.sprintf("x%d", (int) it->first);
        TiXmlElement s(tmp);
        s.InsertEndChild(TiXmlText(it->second));
        mNode->InsertEndChild(s);
    }
}

void ConfigManager::Read(const wxString& name, ConfigManagerContainer::IntToStringMap* map)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlNode *mNode = parentHandle.FirstChild(key).FirstChild(_T("ismap")).Node();

    TiXmlNode *curr = 0;
    long tmp;
    if(mNode)
    {
        while(curr = mNode->IterateChildren(curr)->ToElement())
        {
            wxString(curr->Value()).Mid(1).ToLong(&tmp);
            (*map)[tmp] = curr->FirstChild()->ToText()->Value();
        }
    }
}

ConfigManagerContainer::IntToStringMap ConfigManager::ReadISMap(const wxString& name)
{
    ConfigManagerContainer::IntToStringMap ret;
    Read(name, &ret);
    return ret;
}






void ConfigManager::Write(const wxString& name, const ConfigManagerContainer::StringSet& set)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlElement *leaf = GetUniqElement(e, key);

    TiXmlElement *mNode;
    mNode = GetUniqElement(leaf, _T("sset"));
    leaf->RemoveChild(mNode);
    mNode = GetUniqElement(leaf, _T("sset"));

    for(ConfigManagerContainer::StringSet::const_iterator it = set.begin(); it != set.end(); ++it)
    {
        TiXmlElement s(_T("s"));
        s.InsertEndChild(TiXmlText(*it));
        mNode->InsertEndChild(s);
    }
}


void ConfigManager::Read(const wxString& name, ConfigManagerContainer::StringSet* set)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlHandle parentHandle(e);
    TiXmlNode *mNode = parentHandle.FirstChild(key).FirstChild(_T("sset")).Node();

    TiXmlNode *curr = 0;
    if(mNode)
    {
        while(curr = mNode->IterateChildren(curr)->ToElement())
            set->insert(curr->FirstChild()->ToText()->Value())
            ;
    }
}

ConfigManagerContainer::StringSet ConfigManager::ReadSSet(const wxString& name)
{
    ConfigManagerContainer::StringSet ret;
    Read(name, &ret);
    return ret;
}


void ConfigManager::Write(const wxString& name, const ConfigManagerContainer::SerializableObjectMap* map)
{
    wxString key(name);
    TiXmlElement* e = AssertPath(key);

    TiXmlElement *leaf = GetUniqElement(e, key);

    TiXmlElement *mNode;
    mNode = GetUniqElement(leaf, _T("objmap"));
    leaf->RemoveChild(mNode);
    mNode = GetUniqElement(leaf, _T("objmap"));

    for(ConfigManagerContainer::SerializableObjectMap::const_iterator it = map->begin(); it != map->end(); ++it)
    {
        TiXmlElement s(it->first);
        s.InsertEndChild(TiXmlText(wxBase64Encode(it->second->SerializeOut())));
        mNode->InsertEndChild(s);
    }
}





