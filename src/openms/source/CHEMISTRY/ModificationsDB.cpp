// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2016.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Timo Sachsenberg $
// $Authors: Andreas Bertsch $
// --------------------------------------------------------------------------
//

#include <OpenMS/CHEMISTRY/ModificationsDB.h>

#include <OpenMS/FORMAT/UnimodXMLFile.h>
#include <OpenMS/SYSTEM/File.h>
#include <OpenMS/CHEMISTRY/ResidueDB.h>
#include <OpenMS/CHEMISTRY/Residue.h>
#include <OpenMS/CONCEPT/LogStream.h>

#include <vector>
#include <algorithm>
#include <fstream>
#include <cmath>

using namespace std;

namespace OpenMS
{
  ModificationsDB::ModificationsDB()
  {
    readFromUnimodXMLFile("CHEMISTRY/unimod.xml");
    readFromOBOFile("CHEMISTRY/PSI-MOD.obo");
  }


  ModificationsDB::~ModificationsDB()
  {
    modification_names_.clear();
    for (vector<ResidueModification*>::iterator it = mods_.begin(); it != mods_.end(); ++it)
    {
      delete *it;
    }
  }


  Size ModificationsDB::getNumberOfModifications() const
  {
    return mods_.size();
  }


  const ResidueModification& ModificationsDB::getModification(Size index) const
  {
    if (index >= mods_.size())
    {
      throw Exception::IndexOverflow(__FILE__, __LINE__, __PRETTY_FUNCTION__, index, mods_.size());
    }
    return *mods_[index];
  }


  void ModificationsDB::searchModifications(set<const ResidueModification*>& mods, const String& mod_name, const String& residue, ResidueModification::TermSpecificity term_spec) const
  {
    mods.clear();

    if (!modification_names_.has(mod_name))
    {
      throw Exception::ElementNotFound(__FILE__, __LINE__, __PRETTY_FUNCTION__,
                                       mod_name);
    }

    const set<const ResidueModification*>& temp = modification_names_[mod_name];
    for (set<const ResidueModification*>::const_iterator it = temp.begin();
         it != temp.end(); ++it)
    {
      if (residuesMatch_(residue, (*it)->getOrigin()) &&
          (term_spec == ResidueModification::NUMBER_OF_TERM_SPECIFICITY ||
           (term_spec == (*it)->getTermSpecificity())))
      {
        mods.insert(*it);
      }
    }
  }


  const ResidueModification& ModificationsDB::getModification(const String& mod_name, const String& residue, ResidueModification::TermSpecificity term_spec) const
  {
    set<const ResidueModification*> mods;
    searchModifications(mods, mod_name, residue, term_spec);

    if (mods.empty())
    {
      throw Exception::InvalidValue(__FILE__, __LINE__, __PRETTY_FUNCTION__, "Retrieving the modification failed. It is not available for the residue '" + String(residue) + "' and term specificity " + String(Int(term_spec)) + ".", mod_name);
    }
    if (mods.size() > 1)
    {
      LOG_WARN << "Warning (ModificationsDB::getModification): more than one modification with name '" + mod_name + "', residue '" + residue + "', specificity '" + String(Int(term_spec)) << "' found, picking the first one of:";
      for (set<const ResidueModification*>::const_iterator it = mods.begin();
           it != mods.end(); ++it)
      {
        LOG_WARN << " " << (*it)->getFullId();
      }
      LOG_WARN << "\n";
    }
    return **mods.begin();
  }


  bool ModificationsDB::has(String modification) const
  {
    return modification_names_.has(modification);
  }


  Size ModificationsDB::findModificationIndex(const String& mod_name) const
  {
    Size idx(0);
    if (modification_names_.has(mod_name))
    {
      if (modification_names_[mod_name].size() > 1)
      {
        throw Exception::ElementNotFound(__FILE__, __LINE__, __PRETTY_FUNCTION__, "more than one element of name '" + mod_name + "' found!");
      }
    }
    else
    {
      throw Exception::ElementNotFound(__FILE__, __LINE__, __PRETTY_FUNCTION__, mod_name);
    }

    const ResidueModification* mod = *modification_names_[mod_name].begin();
    for (Size i = 0; i != mods_.size(); ++i)
    {
      if (mods_[i] == mod)
      {
        idx = i;
        break;
      }
    }
    return idx;
  }


  void ModificationsDB::searchModificationsByDiffMonoMass(vector<String>& mods, double mass, double max_error, const String& residue, ResidueModification::TermSpecificity term_spec)
  {
    mods.clear();
    for (vector<ResidueModification*>::const_iterator it = mods_.begin();
         it != mods_.end(); ++it)
    {
      if ((fabs((*it)->getDiffMonoMass() - mass) <= max_error) &&
          residuesMatch_(residue, (*it)->getOrigin()) &&
          (term_spec == ResidueModification::NUMBER_OF_TERM_SPECIFICITY ||
           (term_spec == (*it)->getTermSpecificity())))
      {
        mods.push_back((*it)->getFullId());
      }
    }
  }


  const ResidueModification* ModificationsDB::getBestModificationByMonoMass(double mass, double max_error, const String& residue, ResidueModification::TermSpecificity term_spec)
  {
    double min_error = max_error;
    const ResidueModification* mod = 0;
    for (vector<ResidueModification*>::const_iterator it = mods_.begin();
         it != mods_.end(); ++it)
    {
      double mono_mass = (*it)->getMonoMass();
      if ((mono_mass <= 0) && !residue.empty())
      {
        // Since not all modifications have a monoisotopic mass stored (they may
        // map to multiple residues), we calculate a monoisotopic mass from the
        // delta mass.
        // First the internal (inside an AA chain) weight of the residue:
        const Residue* residue_ = ResidueDB::getInstance()->getResidue(residue);
        if (residue_ == 0) continue; // @TODO: throw an exception here?
        double internal_weight = residue_->getMonoWeight() -
          residue_->getInternalToFull().getMonoWeight();
        mono_mass = (*it)->getDiffMonoMass() + internal_weight;
      }
      // using less instead of less-or-equal will pick the first matching
      // modification of equally heavy modifications (in our case this is the
      // first matching UniMod entry)
      double mass_error = fabs(mono_mass - mass);
      if ((mass_error < min_error) &&
          residuesMatch_(residue, (*it)->getOrigin()) &&
          (term_spec == ResidueModification::NUMBER_OF_TERM_SPECIFICITY ||
           (term_spec == (*it)->getTermSpecificity())))
      {
        min_error = mass_error;
        mod = *it;
      }
    }
    return mod;
  }


  const ResidueModification* ModificationsDB::getBestModificationByDiffMonoMass(double mass, double max_error, const String& residue, ResidueModification::TermSpecificity term_spec)
  {
    double min_error = max_error;
    const ResidueModification* mod = 0;
    for (vector<ResidueModification*>::const_iterator it = mods_.begin();
         it != mods_.end(); ++it)
    {
      // using less instead of less-or-equal will pick the first matching
      // modification of equally heavy modifications (in our case this is the
      // first matching UniMod entry)
      double mass_error = fabs((*it)->getDiffMonoMass() - mass);
      if ((mass_error < min_error) &&
          residuesMatch_(residue, (*it)->getOrigin()) &&
          (term_spec == ResidueModification::NUMBER_OF_TERM_SPECIFICITY ||
           (term_spec == (*it)->getTermSpecificity())))
      {
        min_error = mass_error;
        mod = *it;
      }
    }
    return mod;
  }

  void ModificationsDB::readFromUnimodXMLFile(const String& filename)
  {
    vector<ResidueModification*> new_mods;
    UnimodXMLFile().load(filename, new_mods);

    for (vector<ResidueModification*>::iterator it = new_mods.begin(); it != new_mods.end(); ++it)
    {
      if ((*it)->getTermSpecificity() != ResidueModification::ANYWHERE && // Terminal specificity
          (*it)->getOrigin().size() == 1) // single amino acids letter
      {
        String term_spec;
        if ((*it)->getTermSpecificity() == ResidueModification::C_TERM)
        {
          term_spec = "C-term";
        }
        else if ((*it)->getTermSpecificity() == ResidueModification::N_TERM)
        {
          term_spec = "N-term";
        }
        else
        {
          // TODO log message
        }
        (*it)->setFullId((*it)->getId() + " (" + term_spec + " " + (*it)->getOrigin() + ")");
      }
      else
      {
        (*it)->setFullId((*it)->getId() + " (" + (*it)->getOrigin() + ")");
      }
      // e.g. Oxidation (M)
      modification_names_[(*it)->getFullId()].insert(*it);
      // e.g. Oxidation
      modification_names_[(*it)->getId()].insert(*it);
      // e.g. Oxidized
      modification_names_[(*it)->getFullName()].insert(*it);
      // e.g. UniMod:312
      modification_names_[(*it)->getUniModAccession()].insert(*it);
      mods_.push_back(*it);

      //cerr << (*it)->getFullId() << " " << (*it)->getTermSpecificity() << endl;
    }
  }

  void ModificationsDB::readFromOBOFile(const String& filename)
  {
    ResidueModification mod;
    Map<String, ResidueModification> all_mods;

    ifstream is(File::find(filename).c_str());
    String line, line_wo_spaces, id;

    //parse file
    while (getline(is, line, '\n'))
    {
      line.trim();
      line_wo_spaces = line;
      line_wo_spaces.removeWhitespaces();

      if (line == "" || line[0] == '!') //skip empty lines and comments
      {
        continue;
      }

      if (line_wo_spaces == "[Term]")       //new term
      {
        if (id != "") //store last term
        {
          all_mods[id] = mod;
          id = "";
          mod = ResidueModification();
        }
      }
      //new id line
      else if (line_wo_spaces.hasPrefix("id:"))
      {
        id = line.substr(line.find(':') + 1).trim();
        mod.setId(id);
        mod.setPSIMODAccession(id);
      }
      else if (line_wo_spaces.hasPrefix("name:"))
      {
        mod.setFullName(line.substr(line.find(':') + 1).trim());
      }
      else if (line_wo_spaces.hasPrefix("is_a:"))
      {
        // TODO
      }
      else if (line_wo_spaces.hasPrefix("def:"))
      {
        line.remove('[');
        line.remove(']');
        line.remove(',');
        vector<String> split;
        line.split(' ', split);
        for (Size i = 0; i != split.size(); ++i)
        {
          if (split[i].hasPrefix("UniMod:"))
          {
            mod.setUniModAccession(split[i].trim());
          }
        }
      }
      else if (line_wo_spaces.hasPrefix("comment:"))
      {
        // TODO
      }
      else if (line_wo_spaces.hasPrefix("synonym:"))
      {
        vector<String> val_split;
        line.split('"', val_split);
        if (val_split.size() < 3)
        {
          throw Exception::ParseError(__FILE__, __LINE__, __PRETTY_FUNCTION__, line, "missing \" characters to enclose argument!");
        }
        mod.addSynonym(val_split[1]);

        if (line_wo_spaces.hasSubstring("PSI-MOD-label"))
        {
          mod.setName(val_split[1]);
        }
      }
      else if (line_wo_spaces.hasPrefix("property_value:"))
      {
        String val = line_wo_spaces.substr(15, line_wo_spaces.size() - 15);
        val.trim();

        if (val.hasSubstring("\"none\""))
        {
          continue;
        }

        vector<String> val_split;
        val.split('"', val_split);
        if (val_split.size() != 3)
        {
          throw Exception::ParseError(__FILE__, __LINE__, __PRETTY_FUNCTION__, line, "missing \" characters to enclose argument!");
        }
        if (val.hasPrefix("DiffAvg:"))
        {
          mod.setDiffAverageMass(val_split[1].toDouble());
        }
        else if (val.hasPrefix("DiffFormula:"))
        {
          vector<String> tmp_split;
          line.split('"', tmp_split);
          tmp_split[1].removeWhitespaces();
          mod.setDiffFormula(EmpiricalFormula(tmp_split[1]));
        }
        else if (val.hasPrefix("DiffMono:"))
        {
          mod.setDiffMonoMass(val_split[1].toDouble());
        }
        else if (val.hasPrefix("Formula:"))
        {
          mod.setFormula(val_split[1]);
        }
        else if (val.hasPrefix("MassAvg:"))
        {
          mod.setAverageMass(val_split[1].toDouble());
        }
        else if (val.hasPrefix("MassMono:"))
        {
          mod.setMonoMass(val_split[1].toDouble());
        }
        else if (val.hasPrefix("Origin:"))
        {
          mod.setOrigin(val_split[1]);
        }
        else if (val.hasPrefix("Source:"))
        {
          mod.setSourceClassification(val_split[1]);
        }
        else if (val.hasPrefix("TermSpec:"))
        {
          mod.setTermSpecificity(val_split[1]);
        }
      }
    }

    if (id != "") //store last term
    {
      all_mods[id] = mod;
    }

    // now use the term and all synonyms to build the database
    for (Map<String, ResidueModification>::ConstIterator it = all_mods.begin(); it != all_mods.end(); ++it)
    {

      // check whether a unimod definition already exists, then simply add synonyms to it
      if (it->second.getUniModAccession() != "")
      {
        //cerr << "Found UniMod PSI-MOD mapping: " << it->second.getPSIMODAccession() << " " << it->second.getUniModAccession() << endl;
        set<const ResidueModification*> mods = modification_names_[it->second.getUniModAccession()];
        for (set<const ResidueModification*>::const_iterator mit = mods.begin(); mit != mods.end(); ++mit)
        {
          //cerr << "Adding PSIMOD accession: " << it->second.getPSIMODAccession() << " " << it->second.getUniModAccession() << endl;
          modification_names_[it->second.getPSIMODAccession()].insert(*mit);
        }
      }
      else
      {
        // the mod has so far not been mapped to a unimod mod
        // first check whether the mod is specific
        if (it->second.getOrigin().size() == 1 && it->second.getOrigin() != "X")
        {
          mods_.push_back(new ResidueModification(it->second));

          set<String> synonyms = it->second.getSynonyms();
          synonyms.insert(it->first);
          synonyms.insert(it->second.getFullName());
          //synonyms.insert(it->second.getUniModAccession());
          synonyms.insert(it->second.getPSIMODAccession());
          mods_.back()->setFullId(it->second.getFullName() + " (" + it->second.getOrigin() + ")");
          synonyms.insert(mods_.back()->getFullId());

          // now check each of the names and link it to the residue modification
          for (set<String>::const_iterator nit = synonyms.begin(); nit != synonyms.end(); ++nit)
          {
            modification_names_[*nit].insert(mods_.back());
          }
        }
      }
    }
  }

  void ModificationsDB::getAllSearchModifications(vector<String>& modifications) const
  {
    modifications.clear();

    for (vector<ResidueModification*>::const_iterator it = mods_.begin(); it != mods_.end(); ++it)
    {
      if ((*it)->getUniModAccession() != "")
      {
        modifications.push_back((*it)->getFullId());
      }
    }
    sort(modifications.begin(), modifications.end());
  }


  bool ModificationsDB::residuesMatch_(const String& residue, const String& origin) const
  {
    // maybe @TODO: translate residue to one letter code, if necessary
    return (residue.empty() || (origin == residue) ||
            (origin == "N-term") || (origin == "C-term") ||
            // are the following cases actually possible?:
            origin.empty() || (origin == "X") || (origin == "."));
  }

} // namespace OpenMS
