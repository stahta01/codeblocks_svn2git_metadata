/*
* This file is part of Code::Blocks Studio, an open-source cross-platform IDE
* Copyright (C) 2003  Yiannis An. Mandravellos
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
* Contact e-mail: Yiannis An. Mandravellos <mandrav@codeblocks.org>
* Program URL   : http://www.codeblocks.org
*
* $Id$
* $Date$
*/

#include "customvars.h"
#include "compilergcc.h"
#include <configmanager.h>
#include <wx/log.h>
#include <wx/arrimpl.cpp>
WX_DEFINE_OBJARRAY(VarsArray);

#define CONF_GROUP "/compiler_gcc/custom_variables"

CustomVars::CustomVars(CompilerGCC* compiler)
    : m_pCompiler(compiler)
{
	//ctor
	Load();
	DoAddDefaults();
}

CustomVars::~CustomVars()
{
	//dtor
}

void CustomVars::Load()
{
	m_Vars.Clear();
	long cookie;
	wxString entry;
	wxConfigBase* conf = ConfigManager::Get();
	wxString oldPath = conf->GetPath();
	conf->SetPath(CONF_GROUP);
	bool cont = conf->GetFirstEntry(entry, cookie);
	while (cont)
	{
		DoAdd(entry, conf->Read(entry), false);
		cont = conf->GetNextEntry(entry, cookie);
	}
	conf->SetPath(oldPath);
}

void CustomVars::Save()
{
	wxConfigBase* conf = ConfigManager::Get();
	conf->DeleteGroup(CONF_GROUP);
	wxString oldPath = conf->GetPath();
	conf->SetPath(CONF_GROUP);
	for (unsigned int i = 0; i < m_Vars.GetCount(); ++i)
	{
		conf->Write(m_Vars[i].name, m_Vars[i].value);
	}
	conf->SetPath(oldPath);
}

void CustomVars::Clear()
{
	m_Vars.Clear();
	DoAddDefaults();
}

void CustomVars::DoAddDefaults()
{
	DoAdd("RM", "rm -f", false);			// delete command (for clean target mostly)
	DoAdd("ZIP", "zip.exe -a", false);		// zip (compression program)
	DoAdd("ZIP_EXT", "zip", false);			// .zip (compressed program extension)
}

void CustomVars::Add(const wxString& name, const wxString& value)
{
	DoAdd(name, value, false);
}

void CustomVars::DoAdd(const wxString& name, const wxString& value, bool builtin)
{
	Var newvar;
	newvar.name = name;
	newvar.value = value;
	newvar.builtin = builtin;
	DoAdd(newvar);
}

void CustomVars::DoAdd(const Var& newvar)
{
	Var* v = VarExists(newvar.name);
	if (v)
	{
		// the var exists
		// perform one check only:
		// if the var being added, is declared "builtin"
		// and the existing var is not,
		// promote the existing var to "builtin"
		if (newvar.builtin && !v->builtin)
			v->builtin = true;
        // actually we 'll perform another check:
        // if the new var is declared "builtin"
        // we 'll replace the old value, builtin or not ;)
        else if (newvar.builtin)
        {
            DoDeleteVar(v, true);
            m_Vars.Add(Var(newvar));
        }
		return;
	}
	m_Vars.Add(Var(newvar));
}

bool CustomVars::DoDeleteVar(Var* var, bool deleteIfBuiltin)
{
	if (var && (!var->builtin || deleteIfBuiltin))
	{
		m_Vars.Remove(var);
		return true;
	}
    return false;
}

bool CustomVars::DeleteVar(const wxString& name)
{
	return DeleteVar(GetVarByName(name));
}

bool CustomVars::DeleteVar(Var* var)
{
    DoDeleteVar(var, false);
	return false;
}

Var* CustomVars::VarExists(const wxString& name)
{
	return GetVarByName(name);
}

Var* CustomVars::GetVarByName(const wxString& name)
{
	for (unsigned int i = 0; i < m_Vars.GetCount(); ++i)
	{
		if (name.Matches(m_Vars[i].name))
			return &m_Vars[i];
	}
	return 0L;
}
