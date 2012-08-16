//
// Newint - Parallel electron correlation program.
// Filename: molden.cc
// Copyright (C) 2012 Shane Parker
//
// Author: Shane Parker <shane.parker@u.northwestern.edu>
// Maintainer: NU theory
//
// This file is part of the Newint package (to be renamed).
//
// The Newint package is free software; you can redistribute it and\/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// The Newint package is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the Newint package; see COPYING.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <array>
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <tuple>
#include <algorithm>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <map>

#include <src/molden/molden.h>
#include <src/scf/geometry.h>
#include <src/scf/atommap.h>
#include <src/scf/atom.h>
#include <src/util/constants.h>

using namespace std;

Molden::Molden(bool is_spherical) : is_spherical_(is_spherical) {
   /************************************************************
   * These are temporary fixes to account for different        *
   *  normalizations of the mixed d functions. I stress        *
   *  temporary. As written, this should be applied after      *
   *  everything is in Newint order                            *
   ************************************************************/
   {
      double s_scale_array[] = { 1.0 };
      double p_scale_array[] = { 1.0, 1.0, 1.0 };
      double d_scale_array[] = { 1.0, ::sqrt(3.0), 1.0, ::sqrt(3.0), ::sqrt(3.0), 1.0 };
      // The f functions are definitely not right, but I'll get to that later
      double f_scale_array[] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };

      vector<double> s_scale(s_scale_array, s_scale_array+sizeof(s_scale_array)/sizeof(double));
      vector<double> p_scale(p_scale_array, p_scale_array+sizeof(p_scale_array)/sizeof(double));
      vector<double> d_scale(d_scale_array, d_scale_array+sizeof(d_scale_array)/sizeof(double));
      vector<double> f_scale(f_scale_array, f_scale_array+sizeof(f_scale_array)/sizeof(double));

      scaling_.push_back(s_scale);
      scaling_.push_back(p_scale);
      scaling_.push_back(d_scale);
      scaling_.push_back(f_scale);
   }

   /************************************************************
   * Unfortunately Molden seems to order things in a really    *
   * silly way, so I have no choice but to hardcode the        *
   * conversions                                               *
   ************************************************************/
   /************************************************************
   * Build maps from Molden ordering to Newint ordering.       *
   ************************************************************/
   {
      vector<int> cart_s_order = { 0 };
      vector<int> cart_p_order = { 0, 1, 2 };
      vector<int> cart_d_order = { 0, 3, 1, 4, 5, 2 };
      vector<int> cart_f_order = { 0, 4, 3, 1, 5, 9, 8, 6, 7, 2 };

      vector<vector<int> > m2n_cart = {cart_s_order, cart_p_order, cart_d_order, cart_f_order};
      m2n_cart_.insert(m2n_cart_.end(), m2n_cart.begin(), m2n_cart.end());

      vector<int> sph_s_order = { 0 };
      vector<int> sph_p_order = { 0, 1, 2 };
      vector<int> sph_d_order = { 3, 4, 1, 2, 0 };
      vector<int> sph_f_order = { 5, 6, 3, 4, 1, 2, 0 };

      vector<vector<int> > m2n_sph = {sph_s_order, sph_p_order, sph_d_order, sph_f_order};
      m2n_sph_.insert(m2n_sph_.end(), m2n_sph.begin(), m2n_sph.end());
   }


   /************************************************************
   * Build maps from Newint ordering to Molden ordering.       *
   ************************************************************/
   {
      vector<int> cart_s_order = { 0 };
      vector<int> cart_p_order = { 0, 1, 2 };
      vector<int> cart_d_order = { 0, 2, 5, 1, 3, 4 };
      vector<int> cart_f_order = { 0, 3, 9, 2, 1, 4, 7, 8, 6, 5 };

      vector<vector<int> > n2m_cart = {cart_s_order, cart_p_order, cart_d_order, cart_f_order};
      n2m_cart_.insert(n2m_cart_.end(), n2m_cart.begin(), n2m_cart.end());

      vector<int> sph_s_order = { 0 };
      vector<int> sph_p_order = { 0, 1, 2 };
      vector<int> sph_d_order = { 4, 2, 3, 0, 1 };
      vector<int> sph_f_order = { 6, 4, 5, 2, 3, 0, 1 };

      vector<vector<int> > n2m_sph = {sph_s_order, sph_p_order, sph_d_order, sph_f_order};
      n2m_sph_.insert(n2m_sph_.end(), n2m_sph.begin(), n2m_sph.end());
   }

   /************************************************************
   * Build transformations from cartesian to sphericals        *
   ************************************************************/
   compute_transforms();
}


/************************************************************************************
*  read_geo( const string molden_file )                                             *
*                                                                                   *
*  Reads a molden file and creates a bunch of shared_ptr's to Atoms                 *
*                                                                                   *
*  TODO: Lots of clean up. There are some unused things in here that may be used    *
*     in future functions.                                                          *
************************************************************************************/
vector<shared_ptr<const Atom> > Molden::read_geo(const string molden_file) {
   /************************************************************
   *  Set up variables that will contain the organized info    *
   ************************************************************/
   int num_atoms = 0;

   /* Atom positions */
   vector< array<double,3> > positions;
   /* Atom names */
   vector< string > names;
   /* Map atom number to basis info */
   map<int, vector<tuple<string, vector<double>, vector<double> > > > basis_info;

   /************************************************************
   *  Set up "global" regular expressions                      *
   ************************************************************/
   boost::regex gto_re("\\[GTO\\]");
   boost::regex atoms_re("\\[Atoms\\]");
   boost::regex mo_re("\\[MO\\]");
   boost::regex other_re("\\[\\w+\\]");

   boost::cmatch matches;

   /************************************************************
   *  Booleans to check and make sure each important section   *
   *  was found                                                *
   ************************************************************/
   bool found_atoms = false;
   bool found_gto = false;
   bool found_mo = false;

   double scale;

   /************************************************************
   *  At the moment, I'm not really sure where the [5D]        *
   *  keyword will be found, so I'm just going to look all     *
   *  the way through the file and then close it               *
   ************************************************************/

   string line; // Contains the current line of the file

   /************************************************************
   *  Open input stream                                        *
   ************************************************************/

   ifstream ifs;
   ifs.open(molden_file.c_str());
   if(!ifs.is_open()){
      throw runtime_error("Molden input file not found");
   }
   else {
      getline(ifs, line);

      while (!ifs.eof()){
         if (boost::regex_search(line,atoms_re)) {
            boost::regex ang_re("Angs");
            boost::regex atoms_line("(\\w{1,2})\\s+\\d+\\s+\\d+\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)");

            scale = boost::regex_search(line, ang_re) ? ang2bohr__ : 1.0;

            getline(ifs, line);
            while(!boost::regex_search(line, other_re)){
               if (ifs.eof()) { break; }

               if(boost::regex_search(line.c_str(), matches, atoms_line)) {
                  ++num_atoms;

                  string nm(matches[1].first, matches[1].second);
                  names.push_back(nm);
                  
                  string x_str(matches[2].first, matches[2].second);
                  string y_str(matches[3].first, matches[3].second);
                  string z_str(matches[4].first, matches[4].second);

                  array<double,3> pos;

                  pos[0] = boost::lexical_cast<double>(x_str)*scale;
                  pos[1] = boost::lexical_cast<double>(y_str)*scale;
                  pos[2] = boost::lexical_cast<double>(z_str)*scale;
                  
                  positions.push_back(pos);

                  getline(ifs,line);
               }

               else { getline(ifs,line); }
            }

            found_atoms = true;
         }
         else if (boost::regex_search(line,gto_re)){
            getline(ifs, line);

            boost::regex atom_line("(\\d+)\\s*\\S*");
            boost::regex shell_line("([spdf])\\s+(\\d+)\\s*\\S*");
            boost::regex exp_line("(\\S+)\\s+(\\S+)");
            boost::regex Dd("[Dd]");

            while(!boost::regex_search(line,other_re)){
               if (ifs.eof()) { break; }

               /* This line should be a new atom */
               if(!boost::regex_search(line.c_str(), matches, atom_line)) {
                  getline(ifs,line); continue;
               }
               vector<tuple<string,vector<double>,vector<double> > > atom_basis_info;

               string atom_no_str(matches[1].first, matches[1].second);
               int atom_no = boost::lexical_cast<int>(atom_no_str.c_str());

               string ang_l;

               getline(ifs, line);

               while(boost::regex_search(line.c_str(), matches, shell_line)) {
                  /* Now it should be a new angular shell */
                  string ang_l(matches[1].first, matches[1].second);

                  string num_exp_string(matches[2].first, matches[2].second);
                  int num_exponents = boost::lexical_cast<int>(num_exp_string);

                  vector<double> exponents;
                  vector<double> coefficients;

                  for (int i = 0; i < num_exponents; ++i) {
                     getline(ifs,line);

                     boost::regex_search(line.c_str(), matches, exp_line);

                     string exp_string(matches[1].first, matches[1].second);
                     string coeff_string(matches[2].first, matches[2].second);

                     exp_string = boost::regex_replace(exp_string, Dd, "E");
                     coeff_string = boost::regex_replace(coeff_string, Dd, "E");

                     double exponent = boost::lexical_cast<double>(exp_string);
                     double coeff = boost::lexical_cast<double>(coeff_string);

                     exponents.push_back(exponent);
                     coefficients.push_back(coeff);
                  }

                  /************************************************************
                  *  Right now I have evec and cvec which have the newly      *
                  *  exponents and coefficients                               *
                  ************************************************************/
                  atom_basis_info.push_back(make_tuple(ang_l, exponents, coefficients));

                  getline(ifs, line); 
               }
               //atom_basis_info to basis_info
               basis_info.insert(pair<int,vector<tuple<string,vector<double>,vector<double> > > >(atom_no, atom_basis_info));
            }

            found_gto = true;
         }
         else {
            getline(ifs,line);
         }
      }
   }
   ifs.close();

   /************************************************************
   *  Check to make sure all the necessary information was     *
   *  found.                                                   *
   ************************************************************/
   if( !(found_atoms && found_gto) ){
      string message("Section not found in Molden file: ");
      if (!found_atoms){ message += "atoms "; }
      if (!found_gto)  { message += "GTO"; }
      throw runtime_error(message);
   }

   /************************************************************
   *  Now all the information is collected, it just has to be  *
   *  organized                                                *
   ************************************************************/

   vector<shared_ptr<const Atom> > all_atoms;

   /* Assuming the names and positions vectors are in the right order */
   vector<string>::iterator niter = names.begin();
   vector<array<double,3> >::iterator piter = positions.begin();
   for(int i = 0; i < num_atoms; ++i, ++niter, ++piter){
      vector<tuple<string, vector<double>, vector<double> > > binfo = basis_info.find(i+1)->second;
      if (i == num_atoms) {
         throw runtime_error("It appears an atom was missing in the GTO section. Check your file");
      }

      transform(niter->begin(), niter->end(), niter->begin(), ::tolower);
      /* For each atom, I need to make an atom object and stick it into a vector */
      shared_ptr<const Atom> this_atom(new Atom(is_spherical_, *niter, *piter, binfo));

      all_atoms.push_back(this_atom);
   }

   return all_atoms;
}

/************************************************************************************
*  read_mos                                                                         *
*                                                                                   *
*  Read the MO data and form a Coeff object                                         *
************************************************************************************/
shared_ptr<const Coeff> Molden::read_mos(shared_ptr<const Geometry> geom, string molden_file) {
   shared_ptr<const Coeff> out(new const Coeff(geom));
   double* cdata = out->data();
   int num_basis = geom->nbasis();
   int num_atoms = geom->natom();
   is_spherical_ = geom->spherical();
   bool cartesian = true;

   string line; // Contains current line of file

   /* Check to see whether the file itself is cartesian */
   ifstream sph_input;
   sph_input.open(molden_file.c_str());
   if(!sph_input.is_open()){
      throw runtime_error("Molden input file not found");
   }   
   else {
      boost::regex _5d_re("\\[5[Dd]\\]");
      boost::regex _5d7f_re("\\[5[Dd]7[Ff]\\]");
      while (!sph_input.eof()) {
         getline(sph_input, line);
         if(boost::regex_search(line,_5d_re)){
            cartesian = false;
         }
         else if(boost::regex_search(line,_5d7f_re)) {
            cartesian = false;
         }
      }
   }
   sph_input.close();

   /************************************************************
   *  Set up offsets and order that I'm interested in here     *
   ************************************************************/
   vector<int> atom_offsets;
   {
      vector<vector<int> > offsets = geom->offsets();
      for(vector<vector<int> >::const_iterator ioff = offsets.begin(); ioff != offsets.end(); ++ioff) {
         atom_offsets.push_back(ioff->front());
      }
   }
   vector<int> gto_order;
   vector<vector<int> > shell_orders;
   vector<vector<double> > mo_coefficients;

   /************************************************************
   *  Set up "global" regular expressions                      *
   ************************************************************/
   boost::regex mo_re("\\[MO\\]");
   boost::regex gto_re("\\[GTO\\]");
   boost::regex other_re("\\[\\w+\\]");

   boost::cmatch matches;

   /************************************************************
   *  Booleans to check and make sure each important section   *
   *  was found                                                *
   ************************************************************/
   bool found_gto = false;
   bool found_mo = false;

   /************************************************************
   *  Open input stream                                        *
   ************************************************************/

   ifstream ifs;
   ifs.open(molden_file.c_str());
   if(!ifs.is_open()){
      throw runtime_error("Molden input file not found");
   }
   else {
      getline(ifs, line);

      while (!ifs.eof()){
         if (boost::regex_search(line,gto_re)){
            getline(ifs, line);

            boost::regex atom_line("(\\d+)\\s*\\d*");
            boost::regex shell_line("([spd])\\s+(\\d+)\\s*\\S*");

            while(!boost::regex_search(line,other_re)){
               if (ifs.eof()) { break; }

               /* This line should be a new atom */
               if(!boost::regex_search(line.c_str(), matches, atom_line)) {
                  getline(ifs,line); continue;
               }
               string atom_no_str(matches[1].first, matches[1].second);
               int atom_no = boost::lexical_cast<int>(atom_no_str.c_str());
               if(atom_no > num_atoms) {
                  throw runtime_error("Atom # found is > num_atoms. Check molden file");
               }
               else {
                  gto_order.push_back(atom_no);
               }

               vector<int> atomic_shell_order;
               AtomMap atommap;

               getline(ifs, line);

               while(boost::regex_search(line.c_str(), matches, shell_line)) {
                  /* Now it should be a new angular shell */
                  string ang_l(matches[1].first, matches[1].second);
                  atomic_shell_order.push_back(atommap.angular_number(ang_l));

                  string num_exp_string(matches[2].first, matches[2].second);
                  int num_exponents = boost::lexical_cast<int>(num_exp_string);

                  for (int i = 0; i < num_exponents; ++i) {
                     getline(ifs,line);
                  }

                  getline(ifs, line); 
               }
               shell_orders.push_back(atomic_shell_order);
            }

            found_gto = true;
         }
         else if (boost::regex_search(line,mo_re)) {
            if(!found_gto) {
               throw runtime_error("MO section found before GTO section. Check Molden file.");
            }

            /* Not used at the moment. Maybe later.
            boost::regex sym_re("Sym=\\s+(\\S+)");
            boost::regex ene_re("Ene=\\s+(\\S+)");
            boost::regex spin_re("Spin=\\s+(\\w+)");
            boost::regex occup_re("Occup=\\s+(\\S+)");
            */
            boost::regex coeff_re("\\d+\\s+(\\S+)");

            getline(ifs,line);
            while(!boost::regex_search(line,other_re)) {
               if (ifs.eof()) { break; }

               vector<double> movec;

               getline(ifs,line);
               while(!boost::regex_search(line.c_str(),coeff_re)) {
                  /* For now, throwing away excess data until I get to MO coefficients */
                  getline(ifs,line);
               }

               while(boost::regex_search(line.c_str(),matches,coeff_re)){
                  string mo_string(matches[1].first, matches[1].second);
                  double coeff = boost::lexical_cast<double>(mo_string);
                  
                  movec.push_back(coeff);
                  getline(ifs,line);
               }

               mo_coefficients.push_back(movec);
            }
         }
         else {
            getline(ifs,line);
         }
      }
   found_mo = true;
   }
   ifs.close();

   /************************************************************
   *  Now to organize all the info                             *
   ************************************************************/
   if(!(found_gto && found_mo)) {
      throw runtime_error("GTO or MO section not found in molden file.");
   }
   else{
      double *idata = cdata;
      for(auto imo = mo_coefficients.begin(); imo != mo_coefficients.end(); ++imo) {
         auto icoeff = imo->begin();
         int ii = 0;
         for(auto iatom = shell_orders.begin(); iatom != shell_orders.end(); ++iatom, ++ii) {
            double* tmp_idata = idata + atom_offsets[gto_order[ii]-1];
            for(auto ishell = iatom->begin(); ishell != iatom->end(); ++ishell) {
               if (cartesian) {
                  vector<int> corder = m2n_cart_.at(*ishell);
                  vector<double> scales = scaling_.at(*ishell);
                     int jj = 0;
                  if (is_spherical_) {
                     vector<double> in;
                     for(auto iorder = corder.begin(); iorder != corder.end(); ++iorder) {
                        in.push_back(*(icoeff + *iorder) * scales.at(jj++));
                     }
                     vector<double> new_in = transform_cart(in, *ishell);
                     tmp_idata = copy(new_in.begin(), new_in.end(), tmp_idata);
                  }
                  else {
                     for(auto iorder = corder.begin(); iorder != corder.end(); ++iorder) {
                        *tmp_idata++ = *(icoeff + *iorder) * scales.at(jj++);
                     }
                  }
                  icoeff += corder.size();
               }
               else {
                  vector<int> corder = m2n_sph_.at(*ishell);
                  for(auto iorder = corder.begin(); iorder != corder.end(); ++iorder) {
                     *tmp_idata++ = *(icoeff + *iorder);
                  }
                  tmp_idata += corder.size();
               }
            }
         }
         idata += num_basis;
      }
   }

   return out;
}

/************************************************************************************
*  write_geo( shared_ptr<Geometry> geo, const string molden_file )                  *
*                                                                                   *
*  Writes a molden file. Just the geometry though (Atoms section)                   *
*                                                                                   *
************************************************************************************/
void Molden::write_geo(const shared_ptr<const Geometry> geo, const string molden_file) {
   const int num_atoms = geo->natom();

   ofstream m_out;
   m_out.open(molden_file);

   if (!m_out.is_open()) {
      throw runtime_error("Could not open molden file for writing");
   }

   m_out << "[Molden Format]" << endl;

   m_out << "[Atoms] Angs" << endl;

   for(int i = 0; i < num_atoms; ++i) {
      shared_ptr<const Atom> cur_atom = geo->atoms(i);
      
      const string cur_name = cur_atom->name();
      const int cur_number = cur_atom->atom_number();
      const array<double,3> cur_pos = cur_atom->position();

      m_out << setw(2) << cur_name << setw(8)  << i+1 
                                   << setw(8)  << cur_number << setiosflags(ios_base::scientific)
                                   << setw(20) << setprecision(12) << cur_pos[0]/ang2bohr__
                                   << setw(20) << setprecision(12) << cur_pos[1]/ang2bohr__
                                   << setw(20) << setprecision(12) << cur_pos[2]/ang2bohr__ << endl;
   }

   m_out.close();
}

/************************************************************************************
*  write_mos( shared_ptr<Reference> ref, const string molden_file )                 *
*                                                                                   *
*  Writes the GTO and MO sections of a molden file. write_mos does not check to     *
*  see if the molden_file already exists. Only opens to append.                     *
*                                                                                   *
*  These basis functions are being written as they already are, cartesian or not.   *
*  There will be no transforming from spherical to cartesian just to print!         *
*                                                                                   *
*  TODO: Check whether this actually works                                          *
************************************************************************************/
void Molden::write_mos(const shared_ptr<const Reference> ref, const string molden_file) {
   ofstream ofs;
   ofs.open(molden_file, ios::app);

   if(!ofs.is_open()){
      throw runtime_error("MOs could not be written to molden file: file couldn't be opened");
   }

   shared_ptr<const Geometry> geom = ref->geom();
   vector<shared_ptr<const Atom> > atoms = geom->atoms();

   is_spherical_ = geom->spherical();

   {
      int num_atoms = geom->natom();

      /************************************************************
      *  Print GTO section                                        *
      ************************************************************/
      ofs << "[GTO]" << endl;
      
      auto iatom = atoms.begin();
      for(int ii = 0; ii != num_atoms; ++iatom, ++ii) {
         ofs << ii+1 << endl;

         AtomMap am;

         vector<shared_ptr<const Shell> > shells = (*iatom)->shells();
         for(auto ishell = shells.begin(); ishell != shells.end(); ++ishell) {
            string ang_l = am.angular_string((*ishell)->angular_number());
            vector<double> exponents = (*ishell)->exponents();

            int num_contracted = (*ishell)->contractions().size();
            for(int jj = 0; jj < num_contracted; ++jj) {
               pair<int,int> range = (*ishell)->contraction_ranges(jj);
               
               ofs << setw(2) << ang_l << setw(8) << range.second - range.first << endl;
               for(int kk = range.first; kk < range.second; ++kk) {
                  ofs << setiosflags(ios_base::scientific)
                      << setw(20) << setprecision(8) << exponents[kk]
                      << setw(20) << setprecision(8) 
                      << (*ishell)->contractions(jj)[kk]*denormalize((*ishell)->angular_number(), exponents[kk]) << endl;
               }
            }
         }
         ofs << endl;
      }
      ofs << endl;
      string cart_string = geom->spherical() ? "[5D]" : "";
      ofs << cart_string << endl;
   }

   {
      ofs << "[MO]" << endl;

      shared_ptr<const Coeff> coeff = ref->coeff();
      int nbasis = coeff->nbasis();
      vector<double> eigvec = ref->eig();
      double* modata = coeff->data();

      int nocc = ref->nclosed();

      vector<double>::iterator ieig;
      vector<double> tmp_eigvec(nbasis,0.0);
      if(eigvec.empty()) {
         ieig = tmp_eigvec.begin();
      }
      else{
         ieig = eigvec.begin();
      }
      int num_mos = coeff->mdim();
      for(int i = 0; i < num_mos; ++i, ++ieig){
         ofs << " Ene=" << setw(12) << setprecision(6) << fixed << *ieig << endl;

         /* At the moment only thinking about RHF, so assume spin is Alpha */
         ofs << " Spin=" << "  Alpha" << endl;

         /* At the moment, assuming occupation can be 2 or 0. Should be fine for RHF */
         string occ_string = nocc-- > 0 ? "  2.000" : "  0.000";
         ofs << " Occup=" << occ_string << endl;

         int j = 1;

         for (auto iatom = atoms.begin(); iatom != atoms.end(); ++iatom) {
            vector<shared_ptr<const Shell> > shells = (*iatom)->shells();
            for (auto ishell = shells.begin(); ishell != shells.end(); ++ishell) {
               for (int icont = 0; icont != (*ishell)->num_contracted(); ++icont) {
                  vector<int> corder = (is_spherical_ ? n2m_sph_.at((*ishell)->angular_number()) : n2m_cart_.at((*ishell)->angular_number()));
                  /* This below is probably temporary... I may need to scale the spherical components */
                  vector<double> scales = (is_spherical_ ? vector<double>(corder.size(), 1.0) : scaling_.at((*ishell)->angular_number())) ;
                  for(auto iorder = corder.begin(); iorder != corder.end(); ++iorder) {
                     ofs << fixed << setw(4) << j++ << setw(22) << setprecision(16) << modata[*iorder] / scales.at(*iorder) << endl;
                  }
                  modata += corder.size();
               }
            }
         }
      }
   }
}

double Molden::denormalize(int l, double alpha) {
   double denom = 1.0;
   for (int ii = 2; ii <= l; ++ii) denom *= 2 * ii - 1;
   double value = ::pow(2.0 * alpha / pi__, 0.75) * ::pow(::sqrt(4.0 * alpha), static_cast<double>(l)) / ::sqrt(denom);

   return (1.0 / value);
}

vector<double> Molden::transform_cart(vector<double> carts, int ang_l) {
   vector<vector<pair<int,double> > > mtuv = lmtuv_.at(ang_l);

   vector<double> out;
   for( auto im = mtuv.begin(); im != mtuv.end(); ++im ) {
      double value = 0.0;

      for ( auto ituv = im->begin(); ituv != im->end(); ++ituv ) {
         value += (ituv->second) * carts.at(ituv->first);
      }

      out.push_back(value);
   }

   return out;
}
