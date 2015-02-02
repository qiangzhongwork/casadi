/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2014 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            K.U. Leuven. All rights reserved.
 *    Copyright (C) 2011-2014 Greg Horn
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


#ifndef CASADI_SUBREF_HPP
#define CASADI_SUBREF_HPP

#include "mx_node.hpp"
#include <map>
#include <stack>

/// \cond INTERNAL

namespace casadi {
  /** \brief Reference to a submatrix
      \author Joel Andersson
      \date 2013
  */
  class CASADI_EXPORT SubRef : public MXNode {
  public:

    /// Constructor
    SubRef(const MX& x, const Slice& i, const Slice& j);

    /// Clone function
    virtual SubRef* clone() const;

    /// Destructor
    virtual ~SubRef() {}

    /// Evaluate the function (template)
    template<typename T, typename Mat>
    void evaluateGen(const Mat** input, Mat** output, int* itmp, T* rtmp);

    /// Evaluate the function numerically
    virtual void evaluateD(const DMatrix** input, DMatrix** output, int* itmp, double* rtmp);

    /// Evaluate the function symbolically (SX)
    virtual void evaluateSX(const SX** input, SX** output, int* itmp, SXElement* rtmp);

    /// Evaluate the function symbolically (MX)
    virtual void evaluateMX(const MXPtrV& input, MXPtrV& output, const MXPtrVV& fwdSeed,
                            MXPtrVV& fwdSens, const MXPtrVV& adjSeed, MXPtrVV& adjSens,
                            bool output_given);

    /// Propagate sparsity
    virtual void propagateSparsity(DMatrix** input, DMatrix** output, bool fwd);

    /// Print a part of the expression */
    virtual void printPart(std::ostream &stream, int part) const;

    /** \brief Generate code for the operation */
    virtual void generateOperation(std::ostream &stream, const std::vector<std::string>& arg,
                                   const std::vector<std::string>& res, CodeGenerator& gen) const;

    /** \brief Get the operation */
    virtual int getOp() const { return OP_SUBREF;}

    /// Data members
    Slice i_, j_;
  };

} // namespace casadi

/// \endcond

#endif // CASADI_SUBREF_HPP
